/*
 * This work is part of the White Rabbit project
 *
 * Copyright (C) 2024 CERN (www.cern.ch)
 * Author: Harvey Leicester <harvey.leicester@cern.ch>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libwr/wrs-msg.h>
#include <libwr/softpll_export.h>
#include <rt_ipc.h>

static int loop = SPLL_LOOP_MAIN;
static int loop_setting[2] = {0};
static char *prgName;

static void show_help(void)
{
  printf("Tool to set spll pi loop parameters.\n"
      "Usage: spll_conf <main/helper> <kp> <ki>\n"
      "where:\n"
      "\t<main/helper> pll to update\n"
      "\t<kp> proportional gain\n"
      "\t<ki> integral gain\n"
      "\n"
      "Version: " __GIT_VER__ " compiled by " __GIT_USR__ " on " __DATE__ ", " __TIME__ "\n"
      );
}

static void spll_pi_parse_cmdline(int argc, char *argv[])
{
    int i = 0;

    if (argc > 1) {
	if (strcmp (argv[1], "-h") == 0 || strcmp (argv[1], "--help") == 0) {
	    show_help();
	    exit(0);
	}
    }

    if (argc != 4) {
	fprintf(stderr, "Invalid number of arguments. Try %s -h\n", prgName);
	exit(-1);
    }

    if (!strcmp(argv[1], "main")) {
	loop = SPLL_LOOP_MAIN;
    } else if (!strcmp(argv[1], "helper")) {
	loop = SPLL_LOOP_HELPER;
    } else {
	fprintf(stderr,"Unrecognized loop option %s. Try %s -h\n",
		argv[1], prgName);
	exit(-1);
    }

    for(i = 2; i < argc; i++) {
	loop_setting[i - 2] = atoi(argv[i]);
    }

}

int set_spll_pi_gain(int loop, int kp, int ki)
{
    static int connected = 0;
    struct rts_pll_state pstate;

    if (!connected) {
	if (rts_connect(NULL) < 0)
	return -1;
	connected = 1;
    }

    if (rts_get_state(&pstate)<0 )
	return -1;

    pr_info("setting %s pll, parameters: kp=%i ki=%i\n",
	    (loop == SPLL_LOOP_MAIN) ? "main" : "helper", kp, ki);

    if (rts_set_pi_gain(loop, kp, ki) < 0) {
	return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int ret;

    wrs_msg_init(argc, argv, LOG_DAEMON);

    prgName = argv[0];

    spll_pi_parse_cmdline(argc, argv);

    ret = set_spll_pi_gain(loop, loop_setting[0], loop_setting[1]);

    return ret;
}
