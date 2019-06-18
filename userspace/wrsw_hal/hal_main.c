/* Main HAL file */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <time.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <libwr/wrs-msg.h>
#include <libwr/switch_hw.h>
#include <libwr/shw_io.h>
#include <libwr/sfp_lib.h>
#include <libwr/config.h>
#include <libwr/shmem.h>
#include <libwr/hal_shmem.h>
#include <libwr/util.h>
#include <libwr/timeout.h>

#include "wrsw_hal.h"
#include <rt_ipc.h>

#define MAX_CLEANUP_CALLBACKS 16
#define UPDATE_FAN_PERIOD 500
#define UPDATE_ALL_PERIOD 100

static int daemon_mode = 0;
static hal_cleanup_callback_t cleanup_cb[MAX_CLEANUP_CALLBACKS];
static char *logfilename;
static char *dotconfigname = "/wr/etc/dot-config";

struct hal_shmem_header *hal_shmem;
struct wrs_shm_head *hal_shmem_hdr;

/* Adds a function to be called during the HAL shutdown. */
int hal_add_cleanup_callback(hal_cleanup_callback_t cb)
{
	int i;
	for (i = 0; i < MAX_CLEANUP_CALLBACKS; i++)
		if (!cleanup_cb[i]) {
			cleanup_cb[i] = cb;
			return 0;
		}

	return -1;
}

/* Calls all cleanup callbacks */
static void call_cleanup_cbs(void)
{
	int i;

	pr_debug("Cleaning up...\n");
	for (i = 0; i < MAX_CLEANUP_CALLBACKS; i++)
		if (cleanup_cb[i])
			cleanup_cb[i] ();
}

static void sighandler(int sig)
{
	pr_error("signal caught (%d)!\n", sig);

	//Set state led to orange
	shw_io_write(shw_io_led_state_o, 1);
	shw_io_write(shw_io_led_state_g, 0);

	call_cleanup_cbs();
	exit(0);
}

static int hal_shutdown(void)
{
	call_cleanup_cbs();
	return 0;
}

static void hal_daemonize(void);

/* Main initialization function */
static int hal_init(void)
{
	//trace_log_stderr();

	int line;
	pr_debug("initializing...\n");

	memset(cleanup_cb, 0, sizeof(cleanup_cb));

	pr_info("Using file %s as dot-config\n", dotconfigname);
	line = libwr_cfg_read_file(dotconfigname);

	if (line == -1)
		pr_error("Unable to read dot-config file %s or error in line"
			 "1\n", dotconfigname);
	else if (line)
		pr_error("Error in dot-config file %s, error in line %d\n",
			 dotconfigname, -line);

	shw_sfp_read_db();

	/* Set up trap for some signals - the main purpose is to
	   prevent the hardware from working when the HAL is shut down
	   - launching the HAL on already initialized HW will freeze
	   the system. */
	signal(SIGSEGV, sighandler);
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGILL, sighandler);

	/* Low-level hw init, init non-kernel drivers */
	assert_init(shw_init());

	/* read timing mode from dot-config */
	assert_init(hal_init_timing_mode());

	/* Initialize HAL's shmem - see hal_ports.c */
	assert_init(hal_port_init_shmem(logfilename));

	assert_init(hal_init_timing(logfilename));

	/* Initialize IPC/RPC - see hal_ports.c */
	assert_init(hal_port_init_wripc(logfilename));

	//everything is fine up to here, we can blink green LED
	shw_io_write(shw_io_led_state_o, 0);
	shw_io_write(shw_io_led_state_g, 1);

	if (daemon_mode)
		hal_daemonize();

	return 0;
}

/* Turns a nice and well-behaving HAL into an evil servant of satan. */
static void hal_daemonize(void)
{
	pid_t pid, sid;

	/* already a daemon */
	if (getppid() == 1)
		return;

	/* Fork off the parent process */
	pid = fork();
	if (pid < 0) {
		exit(EXIT_FAILURE);
	}
	/* If we got a good PID, then we can exit the parent process. */
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	/* At this point we are executing as the child process */

	/* Change the file mode mask */
	umask(0);

	/* Create a new SID for the child process */
	sid = setsid();
	if (sid < 0) {
		exit(EXIT_FAILURE);
	}

	/* Change the current working directory.  This prevents the current
	   directory from being locked; hence not being able to remove it. */
	if ((chdir("/")) < 0) {
		exit(EXIT_FAILURE);
	}

	/* Redirect stdin to /dev/null -- keep output/error: they are logged */
	freopen("/dev/null", "r", stdin);
}

static void show_help(void)
{
	printf("WR Switch Hardware Abstraction Layer daemon (wrsw_hal)\n"
	       "Usage: wrsw_hal [options], where [options] can be:\n"
	       "    -d         : fork into background (daemon mode)\n"
	       "    -h         : display help\n"
	       "    -l <file>  : set logfile\n"
	       "    -f <file>  : use custom dot-config file\n"
	       "    -q         : decrease verbosity\n"
	       "    -v         : increase verbosity\n"
	       "\n");
}

static void hal_parse_cmdline(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "dhqvl:f:")) != -1) {
		switch (opt) {
		case 'd':
			daemon_mode = 1;
			break;

		case 'h':
			show_help();
			exit(0);
			break;

		case 'l':
			logfilename = optarg;
			break;

		case 'q': break; /* done in wrs_msg_init() */
		case 'v': break; /* done in wrs_msg_init() */

		case 'f':
			/* custom dot-config file */
			dotconfigname = optarg;
			break;

		default:
			pr_error("Unrecognized option. Call %s -h for help.\n",
				 argv[0]);
			break;
		}
	}
}

int main(int argc, char *argv[])
{
	struct hal_temp_sensors temp_sensors; /* local copy of temperatures */
	static timeout_t update_fan_tmo;
	static timeout_t update_all_tmo;

	wrs_msg_init(argc, argv, LOG_DAEMON);

	/* Print HAL's version */
	pr_info("wrsw_hal. Commit %s, built on " __DATE__ "\n", __GIT_VER__);

	/* Prevent from running HAL twice - it will likely freeze the system */
	if (hal_check_running()) {
		pr_error("Fatal: There is another WR HAL "
			"instance running. We can't work together.\n\n");
		return -1;
	}

	hal_parse_cmdline(argc, argv);

	if (hal_init())
		exit(1);

	libwr_tmo_init(&update_fan_tmo, UPDATE_FAN_PERIOD, 1);
	libwr_tmo_init(&update_all_tmo, UPDATE_ALL_PERIOD, 1);

	/*
	 * Main loop update - polls for WRIPC requests and rolls the port
	 * state machines. This is not a busy loop, as wripc waits for
	 * the "max ms delay". Unless an RPC call comes, in which
	 * case it returns earlier.
	 *
	 * We thus check the actual time, and only proceed with
	 * port and fan update every UPDATE_FAN_PERIOD.  There still
	 * is some jitter from hal_update_wripc() timing.
	 */

	for (;;) {
		hal_update_wripc(25 /* max ms delay */);

		if (libwr_tmo_expired(&update_all_tmo))
			hal_port_update_all();

		if (libwr_tmo_expired(&update_fan_tmo)) {
			/* Update fans and get temperatures values. Don't write
			* temperatures directly to the shmem to reduce the
			* critical section of shmem */
			shw_update_fans(&temp_sensors);
			wrs_shm_write(hal_shmem_hdr, WRS_SHM_WRITE_BEGIN);
			memcpy(&hal_shmem->temp, &temp_sensors,
			       sizeof(temp_sensors));
			wrs_shm_write(hal_shmem_hdr, WRS_SHM_WRITE_END);
		}
	}

	hal_shutdown();

	return 0;
}
