/*
 * wrs_status_led.c
 *
 * Turn on/off and change color of status LED
 *
 *  Created on: July 9, 2015
 *  Authors:
 * 		- Adam Wujek
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License...
 */
#include <stdlib.h>
#include <getopt.h>

#include <libwr/shw_io.h>
#include <libwr/wrs-msg.h>

#ifndef __GIT_VER__
#define __GIT_VER__ "x.x"
#endif

#ifndef __GIT_USR__
#define __GIT_USR__ "?"
#endif

void help(const char* prgname)
{
	printf("%s: Commit %s, built on " __DATE__ "\n", prgname,
	       __GIT_VER__);
	printf("usage: %s <command>\n", prgname);
	printf("available commands are:\n"
	       "   -x turn off status led\n"
	       "   -y turn status led yellow\n"
	       "   -g turn status led green\n"
	       "   -o turn status led orange\n"
	       "   -q decrease verbosity\n"
	       "   -v increase verbosity\n"
	       "   -h this help\n"
               "\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int opt;

	/* argc forced to 1: -q and -v are not "quiet" and "verbose" */
	wrs_msg_init(argc, argv, LOG_USER);

	assert_init(shw_pio_mmap_init());
	shw_io_init();
	shw_io_configure_all();

	while ((opt = getopt(argc, argv, "xyoghvq")) != -1) {
		switch (opt) {
		case 'x':
			shw_io_write(shw_io_led_state_o, 0);
			shw_io_write(shw_io_led_state_g, 0);
			break;

		case 'y':
			shw_io_write(shw_io_led_state_o, 1);
			shw_io_write(shw_io_led_state_g, 1);
			break;

		case 'o':
			shw_io_write(shw_io_led_state_o, 1);
			shw_io_write(shw_io_led_state_g, 0);
			break;

		case 'g':
			shw_io_write(shw_io_led_state_o, 0);
			shw_io_write(shw_io_led_state_g, 1);
			break;

		case 'h':
			help(argv[0]);
			exit(0);
			break;

		case 'q': break; /* done in wrs_msg_init() */
		case 'v': break; /* done in wrs_msg_init() */

		default:
			fprintf(stderr,
				"Unrecognized option. Call %s -h for help.\n",
				argv[0]);
			exit (1);
		}
	}

	return 0;
}
