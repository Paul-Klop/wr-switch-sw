/*\\
 * White Rabbit RTU (Routing Table Unit)
 * Copyright (C) 2010, CERN.
 *
 * Version:     wrsw_rtud v1.0
 *
 * Authors:     Juan Luis Manas (juan.manas@integrasys.es)
 *              Miguel Baizan   (miguel.baizan@integrasys.es)
 *
 * Description: RTU daemon.
 *              Handles the learning and aging processes.
 *              Manages the filtering and VLAN databases.
 *
 * Fixes:
 *              Alessandro Rubini
 *              Tomasz Wlostowski
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if.h>

#include <libwr/wrs-msg.h>
#include <libwr/shmem.h>
#include <libwr/hal_shmem.h>
#include <libwr/mac.h>

#include "rtu.h"
#include "rtu_fd.h"
#include "rtu_drv.h"
#include "rtu_ext_drv.h"
#include "rtu_hash.h"
#include "utils.h"

static pthread_t aging_process;
static pthread_t wripc_process;

struct wrs_shm_head *hal_head;
struct hal_port_state *hal_ports;
/* local copy of port state */
static struct hal_port_state hal_ports_local_copy[HAL_MAX_PORTS];
int hal_nports_local;

int port_was_up[MAX_PORT + 1];

/* copy ports' information from HALs shmem to local memory */
int read_ports(void){
	unsigned ii;
	unsigned retries = 0;

	/* read data, with the sequential lock to have all data consistent */
	while (1) {
		ii = wrs_shm_seqbegin(hal_head);
		memcpy(hal_ports_local_copy, hal_ports,
		       hal_nports_local*sizeof(struct hal_port_state));
		retries++;
		if (retries > 100)
			return -1;
		if (!wrs_shm_seqretry(hal_head, ii))
			break; /* consistent read */
		usleep(1000);
	}


	return 0;
}

/**
 * \brief Creates the static entries in the filtering database
 * @return error code
 */

static int rtu_create_static_entries(void)
{
	uint8_t bcast_mac[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	uint8_t stp_bpdu_mac[] = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x00 };
	uint8_t slow_proto_mac[] = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x01 };
	uint8_t ptp_mcast_mac[] = { 0x01, 0x1b, 0x19, 0x00, 0x00, 0x00 };
	uint8_t udp_ptp_mac[] = { 0x01, 0x00, 0x5e, 0x00, 0x01, 0x81 };
	uint8_t udp_ptp_p2p_mac[] = { 0x01, 0x00, 0x5e, 0x00, 0x00, 0x6b };

	int i, err;
	uint32_t enabled_port_mask = 0;

	read_ports();

	pr_info("Number of physical ports: %d\n",
	      hal_nports_local);

	for (i = 0; i < hal_nports_local; i++) {
		enabled_port_mask |= (1 << hal_ports_local_copy[i].hw_index);

		port_was_up[i] = state_up(hal_ports_local_copy[i].state);
	}

	// VLAN-aware Bridge reserved addresses (802.1Q-2005 Table 8.1)
	pr_info("Adding static routes for slow protocols...\n");
	/* Silently add (R)STP BPDUs broadcasting while no support of STP
	   in software. */
	err =
	    rtu_fd_create_entry(stp_bpdu_mac, 0,
				enabled_port_mask | (1 << hal_nports_local),
				RTU_ENTRY_TYPE_STATIC, OVERRIDE_EXISTING);
	if (err)
		return err;

	/* Add other slow protocols */
	for (i = 1; i < NUM_RESERVED_ADDR; i++) {
		slow_proto_mac[5] = i;
		err =
		    rtu_fd_create_entry(slow_proto_mac, 0,
					(1 << hal_nports_local),
					RTU_ENTRY_TYPE_STATIC,
					OVERRIDE_EXISTING);
		if (err)
			return err;
	}

	/* PTP over UDP */
	pr_info("Adding entry for PTP over UDP\n");
	err =
		rtu_fd_create_entry(udp_ptp_mac, 0, (1 << hal_nports_local),
				    RTU_ENTRY_TYPE_STATIC, OVERRIDE_EXISTING);
	if (err)
		return err;

	err =
		rtu_fd_create_entry(udp_ptp_p2p_mac, 0,
				    (1 << hal_nports_local),
				    RTU_ENTRY_TYPE_STATIC, OVERRIDE_EXISTING);
	if (err)
		return err;

	// Broadcast MAC
	pr_info("Adding static route for broadcast MAC...\n");
	err =
	    rtu_fd_create_entry(bcast_mac, 0,
				enabled_port_mask | (1 << hal_nports_local),
				RTU_ENTRY_TYPE_STATIC, OVERRIDE_EXISTING);
	if (err)
		return err;

	err =
	    rtu_fd_create_entry(ptp_mcast_mac, 0,
				(1 << hal_nports_local), RTU_ENTRY_TYPE_STATIC,
				OVERRIDE_EXISTING);
	if (err)
		return err;

	pr_info("Done creating static entries.\n");

	return 0;
}

static void rtu_update_ports_state(void)
{
	int i;
	int link_up;
	/* update hal_ports_local_copy */
	read_ports();
	for (i = 0; (i <= MAX_PORT) && (i < hal_nports_local); i++) {
		if (!hal_ports_local_copy[i].in_use)
			continue;

		link_up = state_up(hal_ports_local_copy[i].state);
		if (port_was_up[i] && !link_up) {
			pr_info(
			      "Port %s went down, removing corresponding entries...\n",
			      hal_ports_local_copy[i].name);

			rtu_fd_clear_entries_for_port(hal_ports_local_copy[i].
						      hw_index,
						      RTU_ENTRY_TYPE_DYNAMIC);
		}

		port_was_up[i] = link_up;

	}
}

/**
 * \brief Periodically removes the filtering database old entries.
 *
 */
static void *rtu_daemon_aging_process(void *arg)
{

	while (1) {
		rtu_update_ports_state();
		rtu_fd_flush();
		sleep(1);
	}

	return NULL;
}

/**
 * \brief Handles WRIPC requests.
 * Currently used to dump the filtering database contents when requested by
 * external processes.
 *
 */
static void *rtu_daemon_wripc_process(void *arg)
{
	while (1) {
		rtud_handle_wripc();
		usleep(10000);
	}
	return NULL;
}

/**
 * \brief Handles the learning process.
 * @return error code
 */
static int rtu_daemon_learning_process(void)
{
	int err, i, port_down;
	struct rtu_request req;	// Request read from learning queue
	uint32_t port_map;	// Destination port map
	uint16_t vid;		// VLAN identifier
	struct hal_port_state *p;

	while (1) {
		// Serve pending unrecognised request
		err = rtu_read_learning_queue(&req);
		if (!err) {
			pr_debug(
			      "ureq: port %d (wri%d) src %s VID %d priority %d\n",
			      req.port_id + 1, req.port_id + 1,
			      mac_to_string(req.src),
			      req.has_vid ? req.vid : 0,
			      req.has_prio ? req.prio : 0);

			for (port_down = i = 0; i <= MAX_PORT; i++) {
				p = &hal_ports_local_copy[i];
				if (p->in_use && p->hw_index == req.port_id
				    && !state_up(p->state)) {
					port_down = 1;
					pr_debug("port down %s\n", p->name);
					break;
				}
			}

			/* don't learn on ports that are down (FIFO tail?) */
			if (port_down)
				continue;
			// If req has no VID, use 0 (untagged packet)
			vid = req.has_vid ? req.vid : 0;
			port_map = (1 << req.port_id);
			// create or update entry at filtering database
			err =
			    rtu_fd_create_entry(req.src, vid, port_map,
						RTU_ENTRY_TYPE_DYNAMIC,
						OVERRIDE_EXISTING);
			err = 0;
			if (err == -ENOMEM) {
				// TODO remove oldest entries (802.1D says you MAY do it)
				pr_info("Filtering database full\n");
			} else if (err) {
				pr_error("Create entry: error %d\n",
				      err);
				break;
			}
		} else {
			pr_error("Read learning queue: error %d\n", err);
		}
	}
	return err;
}

/**
 * \brief RTU set up.
 * Initialises routing table cache and RTU at hardware.
 * @param poly hash polinomial.
 * @param aging_time Aging time in seconds.
 * @return error code.
 */
static int rtu_daemon_init(uint16_t poly, unsigned long aging_time)
{
	int i, err;

	// init RTU HW
	pr_debug("Initializing RTU gateware.\n");
	err = rtu_init();
	if (err)
		return err;
	err = rtux_init();
	if (err)
		return err;

	// disable RTU
	pr_debug("Disabling RTU.\n");
	rtu_disable();

	// init configuration for ports
	pr_info("Initial configuration for all ports.\n");
	for (i = MIN_PORT; i <= MAX_PORT; i++) {
		// MIN_PORT <= port <= MAX_PORT, thus no err returned

		err = rtu_learn_enable_on_port(i, 1);
		err = rtu_pass_all_on_port(i, 1);
		err = rtu_pass_bpdu_on_port(i, 0);
		err = rtu_unset_fixed_prio_on_port(i);
		err = rtu_set_unrecognised_behaviour_on_port(i, 1);
	}

	// init filtering database
	pr_info("Initializing filtering database.\n");
	err = rtu_fd_init(poly, aging_time);
	if (err)
		return err;

	// create static filtering entries
	err = rtu_create_static_entries();
	if (err)
		return err;

	// turn on RTU
	pr_debug("Enabling RTU.\n");
	rtu_enable();

	rtud_init_exports();

	return err;
}

/**
 * \brief RTU shutdown.
 */
static void rtu_daemon_destroy(void)
{
	// Threads stuff
	pthread_cancel(wripc_process);
	pthread_cancel(aging_process);

	// Turn off RTU
	rtu_disable();
	rtu_exit();
}

void sigint(int signum)
{
	rtu_daemon_destroy();
	exit(0);
}

/**
 * \brief Starts up the learning and aging processes.
 */
int main(int argc, char **argv)
{
	int op, err;
	char *s, *name, *optstring;
	int run_as_daemon = 0;
	uint16_t poly = HW_POLYNOMIAL_CCITT;	// Hash polinomial
	unsigned long aging_res = DEFAULT_AGING_RES;	// Aging resolution [sec.]
	unsigned long aging_time = DEFAULT_AGING_TIME;	// Aging time       [sec.]

	wrs_msg_init(argc, argv, LOG_DAEMON);

	/* Print RTUd's version */
	pr_info("wrsw_rtud. Commit %s, built on " __DATE__ "\n", __GIT_VER__);

	if (argc > 1) {
		// Strip out path from argv[0] if exists, and extract command name
		for (name = s = argv[0]; s[0]; s++) {
			if (s[0] == '/' && s[1]) {
				name = &s[1];
			}
		}
		// Parse daemon options
		optstring = "dhp:r:t:qv";
		while ((op = getopt(argc, argv, optstring)) != -1) {
			switch (op) {
			case 'd':
				run_as_daemon = 1;
				break;
			case 'h':
				usage(name);
			case 'p':
				if (strcmp(optarg, "CCITT") == 0) {
					poly = HW_POLYNOMIAL_CCITT;
				} else if (strcmp(optarg, "IBM") == 0) {
					poly = HW_POLYNOMIAL_IBM;
				} else if (strcmp(optarg, "DECT") == 0) {
					poly = HW_POLYNOMIAL_DECT;
				} else {
					fprintf(stderr, "Invalid polynomial\n");
					usage(name);
				}
				break;
			case 'r':
				if ((aging_res = atol(optarg)) <= 0) {
					fprintf(stderr,
						"Invalid aging resolution\n");
					usage(name);
				}
				break;
			case 't':
				aging_time = atol(optarg);
				if ((aging_time < MIN_AGING_TIME) ||
				    (aging_time > MAX_AGING_TIME)) {
					fprintf(stderr, "Invalid aging time\n");
					usage(name);
				}
				break;

			case 'q': break; /* done in wrs_msg_init() */
			case 'v': break; /* done in wrs_msg_init() */

			default:
				usage(name);
			}
		}
	}
	// Initialise RTU.
	if ((err = rtu_daemon_init(poly, aging_time)) < 0) {
		rtu_daemon_destroy();
		return err;
	}
	// Register signal handler
	signal(SIGINT, sigint);

	// daemonize _before_ creating threads
	if (run_as_daemon)
		daemonize();

	// Start up aging process and auxiliary WRIPC thread
	if ((err =
	     pthread_create(&aging_process, NULL, rtu_daemon_aging_process,
			    (void *)aging_res))
	    || (err =
		pthread_create(&wripc_process, NULL, rtu_daemon_wripc_process,
			       NULL))) {
		rtu_daemon_destroy();
		return err;
	}
	// Start up learning process.
	err = rtu_daemon_learning_process();
	// On error, release RTU resources
	rtu_daemon_destroy();
	return err;
}
