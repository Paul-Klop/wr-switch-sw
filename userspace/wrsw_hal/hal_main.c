/* Main HAL file */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <time.h>
#include <errno.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <rt_ipc.h>

#include <libwr/wrs-msg.h>
#include <libwr/switch_hw.h>
#include <libwr/shw_io.h>
#include <libwr/sfp_lib.h>
#include <libwr/config.h>
#include <libwr/shmem.h>
#include <libwr/hal_shmem.h>
#include <libwr/util.h>
#include <libwr/timeout.h>

#include "hal_ports.h"
#include "hal_timer.h"
#include "hal_timing.h"
#include "hal_port_leds.h"

#define MAX_CLEANUP_CALLBACKS 16

typedef enum {
	TMO_UPDATE_ALL=0,
	TMO_UPDATE_FAN,
	TMO_COUNT
}main_tmo_id_t;

static void cb_timer_update_fan(int timerId);
static void cb_timer_update_all(int timerId);

/* Polling timeouts (RT Subsystem & SFP detection) */
static timer_parameter_t _timerParameters[] = {
		{
				.id=TMO_UPDATE_ALL,
				.tmoMs=100, // 100ms
				.repeat=1,
				.cb=cb_timer_update_all
		},
		{
				.id=TMO_UPDATE_FAN,
				.tmoMs=500, // 500ms
				.repeat=1,
				.cb=cb_timer_update_fan
		},
};

#define MAIN_TIMER_COUNT (sizeof(_timerParameters)/sizeof(timer_parameter_t))


static int daemon_mode = 0;
static hal_cleanup_callback_t cleanup_cb[MAX_CLEANUP_CALLBACKS];
static char *logfilename;
static char *dotconfigname = "/wr/etc/dot-config";

struct hal_shmem_header *hal_shmem;
struct wrs_shm_head *hal_shmem_hdr;
struct hal_temp_sensors temp_sensors;

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

	// Reset all port leds
	led_clear_all_ports();

	call_cleanup_cbs();
	exit(0);
}

static int hal_shutdown(void)
{
	call_cleanup_cbs();
	return 0;
}

static void hal_daemonize(void);

void *sfp_db_alloc(size_t alloc_size)
{
	return wrs_shm_alloc(hal_shmem_hdr, alloc_size);
}

int hal_shmem_init(char *logfilename)
{
	/* Allocate the ports in shared memory, so wr_mon etc can see them
	   Use lock since some (like rtud) wait for hal to be available */
	hal_shmem_hdr = wrs_shm_get(wrs_shm_hal, "wrsw_hal",
				WRS_SHM_WRITE | WRS_SHM_LOCKED);
	if (!hal_shmem_hdr) {
		pr_error("Can't join shmem: %s\n", strerror(errno));
		return -1;
	}
	hal_shmem = wrs_shm_alloc(hal_shmem_hdr, sizeof(*hal_shmem));
	if (!hal_shmem) {
		pr_error("Can't allocate in shmem for hal_shmem\n");
		return -1;
	}

	hal_shmem->shmemState = HAL_SHMEM_STATE_NOT_INITITALIZED;

	return 0;
}

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


	/* Initialize HAL's shmem */
	assert_init(hal_shmem_init(logfilename));

	shw_sfp_read_db(sfp_db_alloc);
	hal_shmem->shw_sfp_cal_list = shw_sfp_cal_list;

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

	/* Init timing part */
	assert_init(hal_tmg_init(logfilename));

	/* Initialize HAL's shmem - see hal_ports.c */
	assert_init(hal_port_shmem_init(logfilename));

	/* Initialize IPC/RPC - see hal_ports.c */
	assert_init(hal_port_wripc_init(logfilename));

	/* While we are leaving initialization function, there might be
	   still LPDC tx calibation ongoing - this is still initialization.
	   The status LED is set to green once the LPDC tx calibration is
	   completed. This is done in hal_port_fms_tx.c, whenit is detected
	   that all the ports have been calibrated (i.e. in the 
	   port_tx_setup_fsm_state_done state.*/

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

static void cb_timer_update_fan(int timerId) {

	/* Update fans and get temperatures values. Don't write
	* temperatures directly to the shmem to reduce the
	* critical section of shmem */
	shw_update_fans(&temp_sensors);
	wrs_shm_write(hal_shmem_hdr, WRS_SHM_WRITE_BEGIN);
	memcpy(&hal_shmem->temp, &temp_sensors,
	       sizeof(temp_sensors));
	wrs_shm_write(hal_shmem_hdr, WRS_SHM_WRITE_END);
}

static void cb_timer_update_all(int timerId) {
	hal_port_update_all();
}

int hal_get_fpga_temperature(void)
{
	return temp_sensors.fpga;
}

int main(int argc, char *argv[])
{

	wrs_msg_init(argc, argv, LOG_DAEMON);

	/* Print HAL's version */
	pr_info("wrsw_hal. Commit %s, built on " __DATE__ "\n", __GIT_VER__);

	hal_parse_cmdline(argc, argv);

	/* Prevent from running HAL twice - it will likely freeze the system */
	if (hal_check_running()) {
		pr_error("Fatal: There is another WR HAL "
			"instance running. We can't work together.\n\n");
		return -1;
	}

	if (hal_init())
		exit(1);

	timer_init(_timerParameters,MAIN_TIMER_COUNT);

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
		hal_wripc_update(25 /* max ms delay */);

		// Check main timers and call callback if timeout expires
		timer_scan(_timerParameters,MAIN_TIMER_COUNT);

		if ( hal_shmem->shmemState== HAL_SHMEM_STATE_INITITALIZING) {
			// Check if all ports have been initialized
			if ( hal_port_all_ports_initialized())
				hal_shmem->shmemState= HAL_SHMEM_STATE_INITITALIZED;
		}
	}

	hal_shutdown();

	return 0;
}
