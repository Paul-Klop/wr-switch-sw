/*
 * Copyright (C) 2023 CERN (www.cern.ch)
 * Author: Adam Wujek
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <minipc.h>
#include <hal_exports.h>
#include <libwr/hal_client.h>

#define HAL_CONNECT_RETRIES 5
#define HAL_CONNECT_TIMEOUT 2000000 /* us */

static int verbose = 0;

enum input_arg {
	arg_help = 'z' + 1, /* avoid conflicts with short options */
	arg_gm_pps_in_out_offset,
	arg_verbose,
};
	
static struct option long_opts[] = {
	{"help",	                no_argument,       NULL, arg_help},
	{"gm-pps-in-out-offset",        required_argument, NULL, arg_gm_pps_in_out_offset},
	{"verbose",                     optional_argument, NULL, arg_verbose},
	{NULL, 0, NULL, 0}
};

void help(char *prgname)
{
	fprintf(stderr, "%s: Use: %s [-v] [-h] [option]\n",
		prgname, prgname);
	fprintf(stderr,
		"The program has the following options:\n"
		"  -h|--help         - print help\n"
		"  -v|--verbose      - verbose output\n"
		"Global parameters:\n"
		"  --gm-pps-in-out-offset=<offset_ps>\n"
		"                    - For GM mode set the offset between PPS-in and PPS-out\n"
		"\n\n"
		"Version: " __GIT_VER__ " compiled by " __GIT_USR__ " on " __DATE__ ", " __TIME__ "\n"
	);
	exit(1);
}

void init_ipc(void)
{
	int ret;

	ret = halexp_client_try_connect(HAL_CONNECT_RETRIES,
					HAL_CONNECT_TIMEOUT);
	if (ret < 0) {
		fprintf(stderr, "Can't establish WRIPC connection to the HAL "
			"daemon!\n");
		exit(1);
	}
}

int main(int argc, char *argv[])
{
	int opt;
	int ret;

	/* If no params print help */
	if (argc == 1)
		help(argv[0]);

	while ((opt = getopt_long(argc, argv, "vh", long_opts, NULL)) != -1) {
		if (opt == 'h' || opt == arg_help )
			help(argv[0]);
		if (opt == '?') {
			/* Unrecognized option, exit */
			exit(1);
		}

		if (opt == 'v') {
			verbose = 1;
			printf("Enable verbose mode\n");
		}

		if (opt == arg_verbose) {
			if (!optarg || (optarg && (!strcmp(optarg, "off")
						   || !strcmp(optarg, "0")))
			   ) {
				verbose = 0;
				printf("Enable verbose mode\n");
			} else {
				verbose = 1;
				printf("Disable verbose mode\n");
			}
		}
	}

	init_ipc();

	/* Reset position of argument for getopt_long */
	optind = 0;

	while ((opt = getopt_long(argc, argv, "vh", long_opts, NULL)) != -1) {

		switch (opt) {
		case arg_gm_pps_in_out_offset:
			if (verbose)
				printf("Setting offset between PPS-in and "
				       "PPS-out in GM mode to %d\n",
				       atoi(optarg));
			ret = halexp_gm_pps_in_out_offset_cmd(atoi(optarg));
			if (ret) {
				fprintf(stderr, "Error setting offset between "
					"PPS-in and PPS-out in GM mode to %d\n",
					atoi(optarg));
				exit (1);
			}
			break;

		default:
			break;
		}
	}

	return 0;
}
