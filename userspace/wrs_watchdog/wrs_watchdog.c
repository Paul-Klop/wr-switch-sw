/*
 * Copyright (c) 2015, CERN
 *
 * Author: Grzegorz Daniluk <grzegorz.daniluk@cern.ch>
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
 *
 */

#include <stdio.h>
#include <getopt.h>
#include <inttypes.h>
#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <libwr/switch_hw.h>
#include <libwr/wrs-msg.h>
#include <fpga_io.h>
#include <regs/wdog-regs.h>
#include "wrs_watchdog.h"

#include <libwr/shmem.h>
#include <libwr/hal_shmem.h>

#define RST_THR 2

#define wdog_write(reg, val) \
	_fpga_writel(FPGA_BASE_WDOG + offsetof(struct WDOG_WB, reg), val)

#define wdog_read(reg) \
	_fpga_readl(FPGA_BASE_WDOG + offsetof(struct WDOG_WB, reg))

static char *prgname;
int daemon_mode;
int list_mode;
int port_num = 0;

int get_nports_from_hal(void)
{
	struct hal_shmem_header *h;
	struct wrs_shm_head *hal_head = NULL;
	int hal_nports_local; /* local copy of number of ports */
	int ii;
	int n_wait = 0;
	int ret;

	/* wait for HAL */
	while ((ret = wrs_shm_get_and_check(wrs_shm_hal, &hal_head)) != 0) {
		n_wait++;
		if (n_wait > 10) {
			if (ret == WRS_SHM_OPEN_FAILED) {
				pr_error("Unable to open HAL's shmem!\n");
			}
			if (ret == WRS_SHM_WRONG_VERSION) {
				pr_error("Unable to read HAL's version!\n");
			}
			if (ret == WRS_SHM_INCONSISTENT_DATA) {
				pr_error("Unable to read consistent data from "
					 "HAL's shmem!\n");
			}
			exit(1);
		}
		usleep(900000);
	}

	h = (void *)hal_head + hal_head->data_off;

	n_wait = 0;
	while (1) { /* wait 10 sec for HAL to produce consistent nports */
			n_wait++;
		ii = wrs_shm_seqbegin(hal_head);
		/* Assume number of ports does not change in runtime */
		hal_nports_local = h->nports;
		if (!wrs_shm_seqretry(hal_head, ii))
			break;
		if (n_wait > 10) {
			/* print if waiting more than 10 seconds, some waiting
			 * is expected since hal requires few seconds to start
			 */
			pr_error("Wait for HAL.\n");
			exit(1);

		}
		usleep(900000);
	}

	/* check hal's shm version */
	if (hal_head->version != HAL_SHMEM_VERSION) {
		pr_error("unknown hal's shm version %i (known is %i)\n",
			 hal_head->version, HAL_SHMEM_VERSION);
		exit(-1);
	}

	if (hal_nports_local > HAL_MAX_PORTS) {
		pr_error("Too many ports reported by HAL. %d vs %d "
			 "supported\n", hal_nports_local, HAL_MAX_PORTS);
		exit(-1);
	}
	return hal_nports_local;
}

uint32_t show_counter(void)
{
	uint32_t val;
	val = wdog_read(RST_CNT);
	printf("%u\n", val);
	return val;
}

void force_rst(void)
{
	wdog_write(CR, WDOG_CR_RST);
}

struct swc_fsms read_port(int port)
{
	int i;
	struct swc_fsms fsms;
	uint32_t val_fsm, val_act;

	wdog_write(CR, WDOG_CR_PORT_W(port));
	val_fsm = wdog_read(FSM);
	val_act = wdog_read(ACT);

	/* parsing results */
	fsms.state[0] = WDOG_FSM_IB_ALLOC_R(val_fsm);
	fsms.state[1] = WDOG_FSM_IB_TRANS_R(val_fsm);
	fsms.state[2] = WDOG_FSM_IB_RCV_R(val_fsm);
	fsms.state[3] = WDOG_FSM_IB_LL_R(val_fsm);
	fsms.state[4] = WDOG_FSM_OB_PREP_R(val_fsm);
	fsms.state[5] = WDOG_FSM_OB_SEND_R(val_fsm);
	fsms.state[6] = WDOG_FSM_FREE_R(val_fsm);
	for (i = 0; i < FSMS_NO; ++i) {
		fsms.act[i] = (val_act & (1<<i)) >> i;
	}

	return fsms;
}

void list_fsms(void)
{
	int i;
	struct swc_fsms fsms;

	for (i = 0; i <= port_num; ++i) {
		/* yes, it is i<=port_num because we're reading FSMs for all WR
		 * ports + NIC */
		fsms = read_port(i);
		printf("PORT %2d: ", i);
		printf("ib_alloc(%d): %-14s",
			fsms.act[ALLOC_IDX], alloc_states[fsms.state[ALLOC_IDX]]);
		printf("ib_trans(%d): %-15s",
			fsms.act[TRANS_IDX], trans_states[fsms.state[TRANS_IDX]]);
		printf("ib_rcv(%d): %-12s",
			fsms.act[RCV_IDX], rcv_states[fsms.state[RCV_IDX]]);
		printf("ib_ll(%d): %-17s",
			fsms.act[LL_IDX], ll_states[fsms.state[LL_IDX]]);
		printf("ob_prep(%d): %-18s",
			fsms.act[PREP_IDX], prep_states[fsms.state[PREP_IDX]]);
		printf("ob_send(%d): %-14s",
			fsms.act[SEND_IDX], send_states[fsms.state[SEND_IDX]]);
		printf("free(%d): %s\n",
			fsms.act[FREE_IDX], free_states[fsms.state[FREE_IDX]]);
	}
}

int update_stuck(struct swc_fsms *fsms, int *stuck_cnt)
{
	int fsm_it;

	for (fsm_it = 0; fsm_it < FSMS_NO; ++fsm_it) {
		if (fsms->state[fsm_it] != idles[fsm_it] &&
			fsms->act[fsm_it] == 0) {

			stuck_cnt[fsm_it]++;
		} else {
			stuck_cnt[fsm_it] = 0;
		}
		/* and we also check if reset is needed */
		if (stuck_cnt[fsm_it] >= RST_THR) {
			return 1;
		}
	}
	return 0;
}

void clear_stuck(int cnt[][FSMS_NO])
{
	int port_it, fsm_it;

	for (port_it = 0; port_it <= port_num; ++port_it) {
	for (fsm_it = 0; fsm_it < FSMS_NO; ++fsm_it) {
		cnt[port_it][fsm_it] = 0;
	}
	}
}

void write_pidfile(char *file, pid_t pid)
{
	FILE *f;

	if (file == NULL)
		return;

	f = fopen(file, "w");
	if (f == NULL) {
		pr_error("Could not create PID file\n");
		return;
	}
	fprintf(f, "%d\n", (int)pid);
	fclose(f);
}

void endless_watchdog(void)
{
	struct swc_fsms fsms[19];
	int stuck_cnt[19][FSMS_NO] = { {0} };
	int port_it;
	int rst = 0;

	while (1) {
		/* first we read all the ports */
		for (port_it = 0; port_it <= port_num; ++port_it) {
			fsms[port_it] = read_port(port_it);
			rst = update_stuck(&(fsms[port_it]), stuck_cnt[port_it]);
			if (rst)
				break;
		}

		/* handle reset if needed */
		if (rst) {
			pr_warning("Switching core stuck... resetting\n");
			force_rst();
			clear_stuck(stuck_cnt);
		}

		sleep(1);
	}
}

void print_help(char *prgname)
{
	printf("wrs_watchdog. Commit %s, built on " __DATE__ "\n",
	       __GIT_VER__);
	printf("usage: %s <options>\n", prgname);
	printf("   -d        Run as daemon\n"
		"   -l        List FSMs state for all ports\n"
		"   -n <8/18> Set the number of ports\n"
		"   -g        Show current value of the restart counter\n"
		"   -r        Force restart of the Swcore\n"
		"   -h        Show this help message\n");
}

int main(int argc, char *argv[])
{
	int c = 0;

	prgname = argv[0];

	if (argc == 1) {
		print_help(prgname);
		return 0;
	}

	wrs_msg_init(argc, argv, LOG_DAEMON);

	if (shw_fpga_mmap_init() < 0) {
		pr_error("%s: Can't access device memory\n", prgname);
		exit(1);
	}

	while ((c = getopt(argc, argv, "dhrgqvn:lp:")) != -1) {
		switch (c) {
		case 'd':
			daemon_mode = 1;
			break;
		case 'l':
			list_mode = 1;
			break;
		case 'r':
			force_rst();
			break;
		case 'g':
			show_counter();
			exit(0);
		case 'n':
			port_num = atoi(optarg);
			pr_info("Read %d ports from cmdline\n", port_num);
			break;
		case 'p':
			pr_debug("use pidfile %s", optarg);
			write_pidfile(optarg, getpid());
			break;
		case 'q': break; /* done in wrs_msg_init() */
		case 'v': break; /* done in wrs_msg_init() */
		case 'h':
		default:
			print_help(prgname);
			exit(1);
		}
	}

	if (!port_num) {
		/* if port_num not read from parameter read it from HAL */
		port_num = get_nports_from_hal();
		pr_info("Read %d ports from HAL\n", port_num);
	}

	if (!daemon_mode && list_mode) {
		list_fsms();
	}

	if (daemon_mode) {
		pr_info("wrs_watchdog. Commit %s, built on "
			__DATE__ "\n", __GIT_VER__);

		pr_info("Demonize\n");
		endless_watchdog();
	}

	return 0;
}
