/*
 * wrs_leapsec.c
 *
 * This tools read the local time and checks for a incoming leap second.
 * If it detects a incoming leap seconds in the next 12 hours, the information
 * is set in the kernel and will be available for PPSi.
 *
 *  Created on: Jun 18, 2019
 *  Authors:
 * 		- Jean-Claude BAU / CERN
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <unistd.h>
#include <sys/timex.h>
#include <errno.h>
#include <libwr/wrs-msg.h>
#include <libwr/softpll_export.h>
#include <rt_ipc.h>
#include "time_lib.h"

/* STATUS reported to SNMP */
/* Ordered by priority (low to high) */

typedef enum {
	MSG_NO_ERROR,
	MSG_LEAP_SECOND_FILE_EXPIRED,
	MSG_ERROR,
	MSG_TAI_READ_ERROR,
	MSG_LEAP_SECOND_WILL_BE_INSERTED,
	MSG_LEAP_SECOND_WILL_BE_DELETED,
} check_status_e;

typedef struct {
	check_status_e statusId;
	char * msg;
}statusMsg_t;

static statusMsg_t statusMsg[] = {
		{ MSG_NO_ERROR, "no_changes"},
		{ MSG_LEAP_SECOND_FILE_EXPIRED,"leap_sec_file_expired"},
		{ MSG_ERROR, "error_detected"},
		{ MSG_TAI_READ_ERROR, "tai_read_error"},
		{ MSG_LEAP_SECOND_WILL_BE_INSERTED, "leap_sec_inserted"},
		{ MSG_LEAP_SECOND_WILL_BE_DELETED, "leap_sec_deleted"},
};

static check_status_e status=MSG_NO_ERROR;
static char *leapFileName=NULL;
static int opt_verbose=0;
static int opt_quiet=0;
static int opt_clear=0;
static int opt_dryrun=0;
static int opt_test=0;
static int leapSecondMask=0; /* 1 or -1 */
static char *prgName;


// Log messages : for verbose mode , do not log messages but just print them on screen
#define pr_error(...)	wrs_msg(LOG_ERR, __VA_ARGS__)
#define pr_err(...)	wrs_msg(LOG_ERR, __VA_ARGS__)
#define pr_warning(...)	wrs_msg(LOG_WARNING, __VA_ARGS__)
#define pr_warn(...)	wrs_msg(LOG_WARNING, __VA_ARGS__)
#define pr_info(...)	wrs_msg(LOG_INFO, __VA_ARGS__)
#define pr_debug(...)	wrs_msg(LOG_DEBUG, __VA_ARGS__)

#define STATUS_FILE "/tmp/leapseconds_check_status"


static inline void setStatus(check_status_e newStatus) {
	if ( newStatus>status ) {
		status=newStatus;
	}
}

static char* getStatusAsString(check_status_e st) {

	int i;

	for (i=0; i<(sizeof(statusMsg)/sizeof(statusMsg_t)); i++ )
		if (statusMsg[i].statusId==st) {
			return statusMsg[i].msg;
		}
	return getStatusAsString(MSG_ERROR);
}

static void saveStatus(void) {
	char *msg=getStatusAsString(status);

	FILE *fd=fopen(STATUS_FILE,"w");

	if ( fd==NULL ) {
		pr_error("Cannot open file %s\n", STATUS_FILE);
		return;
	}
	fprintf(fd,"%s\n",msg);
	fclose(fd);
}

static void show_help(void)
{
	printf("WR tool used to update the leap second from leap seconds file.\n"
			"Usage: wrs_leapsec [options], where [options] can be:\n"
			"\t-f leap_second_file  If not set, use the default one\n"
			"\t-n                   Dry run - No actions\n"
			"\t-l value             Testing mode. Set the kernel for an incoming leap second\n"
			"\t   					'value' must be 1 (leap61) or -1 (leap59)\n"
			"\t-h                   display help\n"
			"\t-v                   set verbosity on\n"
			"\t-q                   Quiet mode. Display only important messages\n"
			"\t-c                   Clear leap second in the kernel\n"
			"\n");
}

static void lsec_parse_cmdline(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "cqnhvl:f:")) != -1) {
		switch (opt) {

		case 'h':
			show_help();
			exit(0);
			break;

		case 'f':
			leapFileName = optarg;
			break;

		case 'l':
			if ( sscanf(optarg,"%d",&leapSecondMask )==1 ) {
				leapSecondMask= leapSecondMask>=1 ? STA_INS : STA_DEL;
				opt_test=1;
			} else {
				fprintf(stderr,"%s: Invalid parameter to -l option.\n",prgName);
				show_help();
				exit(-1);
			}
			break;


		case 'n':
			opt_dryrun=1;
			break;

		case 'c':
			opt_clear=1;
			break;

		case 'v':
			opt_verbose=1;
			break;

		case 'q':
			opt_quiet=1;
			break;

		default:
			fprintf(stderr,"Unrecognized option. Call %s -h for help.\n",prgName);
			break;
		}
	}

	if ( opt_verbose && opt_quiet )
		opt_verbose=0; /* Not compatible */
	if ( opt_dryrun && opt_clear )
		opt_clear=0; /* Not compatible */

}

int getTimingMode(void) {
	static int connected=FALSE;
	struct rts_pll_state pstate;

	if ( !connected ) {
		if(	rts_connect(NULL) < 0)
			return -1;
		connected=TRUE;
	}
	if (rts_get_state(&pstate)<0 )
		return -1;
	return pstate.mode;
}

static inline int adjustTimeX(struct timex *t) {
	int ret;

	if ((ret=adjtimex(t)) == -1) {
		pr_error("Fatal: adjtimex(): %s\n",strerror(errno));
		setStatus(MSG_ERROR);
	}
	return ret;
}

int applyToKernel(void ) {
	struct timex t;
	int mask=leapSecondMask;
	int leapSecEnabled=0;

	/* Read the kernel information */
	memset(&t, 0, sizeof(t));
	if (adjustTimeX(&t) == -1)
		return -1;

	if ( (t.status & STA_INS)!=0 ) {
		if ( opt_verbose )
			printf("%s: A leap second will be inserted tonight.\n",prgName);
		mask=0;
		leapSecEnabled=1;
		setStatus(MSG_LEAP_SECOND_WILL_BE_INSERTED);
	}
	if ( (t.status & STA_DEL)!=0 ) {
		if ( opt_verbose )
			printf("%s: A leap second will be removed tonight.\n",prgName);
		mask=0;
		leapSecEnabled=1;
		setStatus(MSG_LEAP_SECOND_WILL_BE_DELETED);
	}

	// opt_clear and opt_dryrun never set at the same time
	if ( opt_clear && leapSecEnabled ) {
		if ( opt_verbose )
			printf("%s: Clearing leap second in the kernel.\n",prgName);
		t.modes=ADJ_STATUS;
		t.status&=~(STA_INS|STA_DEL);
		if (adjustTimeX(&t) == -1)
			return -1;
	}

	if ( !opt_dryrun && mask !=0 ) {
		if ( mask == STA_INS ) {
			pr_info("Leap second inserted. Will be applied after the last second of the UTC day\n");
			setStatus(MSG_LEAP_SECOND_WILL_BE_INSERTED);
		}
		if ( mask == STA_DEL ) {
			pr_info("Leap second deleted. Will be applied after the last second of the UTC day\n");
			setStatus(MSG_LEAP_SECOND_WILL_BE_DELETED);
		}
		memset(&t, 0, sizeof(t));
		t.modes=ADJ_STATUS;
		t.status|=mask;
		if (adjustTimeX(&t) == -1)
			return -1;
	}
	return 1;
}

int main(int argc, char *argv[])
{
	int ret;

	wrs_msg_init(argc,argv);

	prgName=argv[0];

	lsec_parse_cmdline(argc,argv);

	pr_info("Commit %s, built on " __DATE__ "\n",__GIT_VER__);

	if ( ! opt_test ) {
		int taiOffset, nextTaiOffset, hasExpired;
		int tm=getTimingMode();

		switch (tm) {
		case SPLL_MODE_GRAND_MASTER:
		case SPLL_MODE_FREE_RUNNING_MASTER :
			if ( (taiOffset=getTaiOffsetFromLeapSecondsFile(leapFileName,time(NULL),&nextTaiOffset,&hasExpired))==-1) {
				pr_error("Fatal: Cannot read TAI offset from leap seconds file\n");
				setStatus(MSG_TAI_READ_ERROR);
				ret=-1;
				goto error;
			}
			if ( hasExpired )
				setStatus(MSG_LEAP_SECOND_FILE_EXPIRED);
			if ( nextTaiOffset > taiOffset ) {
				leapSecondMask=STA_INS;
			}
			if ( nextTaiOffset < taiOffset ) {
				leapSecondMask=STA_DEL;
			}
			if ( opt_verbose && hasExpired ) {
				printf("%s: The leap seconds file has expired !!!\n", prgName);
			}
			break;
		default:
			if ( opt_verbose )
				printf("%s: Leap second can only by applied when timing mode is GM or FR\n",prgName);
		}
	}

	ret=applyToKernel();

	error:;
	saveStatus();
	return ret;
}
