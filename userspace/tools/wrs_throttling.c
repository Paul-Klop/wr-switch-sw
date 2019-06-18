/*
 * Copyright (c) 2016, CERN
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
#include <regs/nic-regs.h>

#define nic_write(reg, val) \
	_fpga_writel(FPGA_BASE_NIC + offsetof(struct NIC_WB, reg), val)

#define nic_read(reg) \
	_fpga_readl(FPGA_BASE_NIC + offsetof(struct NIC_WB, reg))

#define MAX_THR 65535	/* b/w thr in HDL is 16-bit value in KB/s */
#define MIN_THR 2

static char *prgname;

void enable_throttling(int en)
{
	uint32_t val;

	val = nic_read(CR);
	if (en) {
		val |= NIC_CR_RXTHR_EN;
		printf("NIC bandwidth throttling enabled\n");
	} else {
		val &= (~NIC_CR_RXTHR_EN);
		printf("NIC bandwidth throttling disabled\n");
	}

	nic_write(CR, val);
}

void print_bw(int print_bps)
{
	uint32_t bw;

	/* first we read the b/w in Bytes/s */
	bw = nic_read(RXBW);

	if (print_bps)
		printf("NIC Rx bandwidth: %u B/s\n", bw);
	else
		printf("NIC Rx bandwidth: %.3f KB/s\n", bw/1024.0);
}

int set_thr(unsigned thr)
{
	if (thr > MAX_THR || thr < MIN_THR)
		return -1;

	printf("Setting NIC Rx bandwidth threshold: %u KB/s\n", thr);
	nic_write(MAXRXBW, thr);

	return 0;
}

void print_settings(void)
{
	uint32_t en;

	en = nic_read(CR) & NIC_CR_RXTHR_EN;
	printf("Current settings:\n");
	printf("Throttling:    %s\n", en ? "enabled" : "disabled");
	printf("Max bandwidth: %u KB/s\n", nic_read(MAXRXBW));
}

void print_help(char *prgname)
{
	printf("wrs_throttling. Commit %s, built on " __DATE__ "\n",
	       __GIT_VER__);
	printf("usage: %s <options>\n", prgname);
	printf("   -h         Show this help message\n"
		"   -b         Print current b/w in B/s (KB/s by default)\n"
		"   -l         Endless loop mode. Prints the current b/w "
		"every 1 s\n"
		"   -t <KB/s>  Set maximum NIC Rx bandwidth to <KB/s>\n"
		"   -d         Disable b/w throttling in NIC\n"
		"   -s         Print current settings of the throttling\n");
}

int main(int argc, char *argv[])
{
	int c = 0;
	int print_bps = 0;	/* print current b/w in Bytes/s */
	int loop_mode = 0;	/* prints current b/w every 1s */

	prgname = argv[0];


	wrs_msg_init(argc, argv, LOG_USER);

	if (shw_fpga_mmap_init() < 0) {
		pr_error("%s: Can't access device memory\n", prgname);
		exit(1);
	}

	while ((c = getopt(argc, argv, "hbt:lds")) != -1) {
		switch (c) {
		case 'b':
			/* print current b/w in B/s */
			print_bps = 1;
			break;
		case 't':
			/* set b/w threshold */
			if (set_thr(atoi(optarg)) != -1) {
				enable_throttling(1);
			} else {
				pr_error("Threshold outside allowed range "
					"<%d; %d>\n", MIN_THR, MAX_THR);
			}
			break;
		case 'd':
			/* disable b/w throttling */
			enable_throttling(0);
			break;
		case 'l':
			loop_mode = 1;
			break;
		case 's':
			print_settings();
			break;
		case 'h':
		default:
			print_help(prgname);
			exit(1);
		}
	}

	/* do actual printing */
	print_bw(print_bps);
	while (loop_mode) {
		sleep(1);
		print_bw(print_bps);
	}

	return 0;
}
