/*
 * Trivial tool to set WR date in the switch from local or NTP time
 *
 * Alessandro Rubini, 2011, for CERN, 2013. GPL2 or later
 */
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/timex.h>
#include "../../kernel/wbgen-regs/ppsg-regs.h"
#include <libwr/util.h>
#include <time_lib.h>
#include <rt_ipc.h>
#include "nmea/serial.h"
#include "nmea/wr_nmea.h"
#include "wr_irig.h"

#ifndef MOD_TAI
#define MOD_TAI 0x80
#endif
#define WRDATE_CFG_FILE "/etc/wr_date.conf"
#define NMEA_SERIAL_PORT "/dev/ttyS2"

/* Address for hardware, from nic-hardware.h */
#define FPGA_BASE_PPSG  0x10010500
#define PPSG_STEP_IN_NS 16 /* we count a 16.5MHz */
#define FPGA_BASE_IRIG  0x1005c000
#define NMEA_DEFAULT_BAUD   9600
#define NMEA_DEFAULT_FORMAT "GPZDA"

extern int init_module(void *module, unsigned long len, const char *options);
extern int delete_module(const char *module, unsigned int flags);

static int opt_verbose, opt_force, opt_not;
static int opt_nmea_en = 0;
static int opt_nmea_baud = NMEA_DEFAULT_BAUD;
static char *opt_nmea_fmt = NMEA_DEFAULT_FORMAT;
static int opt_irig_en = 0;
static struct wr_nmea nmea;
static struct wr_irig wr_irig;
static char *opt_cfgfile = WRDATE_CFG_FILE;
static char *prgname;
static long opt_offset_us = 0;

void help(void)
{
	fprintf(stderr, "%s: Use: \"%s [<options>] [<source>] <cmd> [<args>]\n",
		prgname, prgname);
	fprintf(stderr,
		"  The program uses %s as default config file name\n"
		"  Supported <options>:\n"
		"  -f       force: run even if not on a WR switch\n"
		"  -c <cfg> configfile to use in place of the default\n"
		"  -v       verbose: report what the program does\n"
		"  -n       do not act in practice - dry run\n"
		"  -b <baud>\n"
		"           baudrate for NMEA (default %d)\n"
		"  -m <GPZDA|GPRMC>\n"
		"           format for NMEA (default %s)\n"
		"  -o <offset_us>\n"
		"           for tohost cmd, add an extra offset in us;\n"
		"           possitive offset means linux time is greater than e.g., WR time\n"
		"  Supported <source> of Time Of Day:\n"
		"    irigb  use IRIG-B (NOTE: remember to enable it!)\n"
		"    nmea   use NMEA  \n"
		"    wr     use WR (default)\n"
		"  Supported <cmd>:\n"
		"    get             print WR and (if selected) NMEA/IRIG-B time to stdout\n"
		"    get tohost      print WR or NMEA or IRIG-B time and set system time according to source\n"
		"    set <value>     set WR time to scalar seconds\n"
		"    set host [tai]  set TAI and WR time from current host time.\n"
		"                    if tai option is set then set only the TAI offset.\n"
		"    stat            print statistics between Linux (UTC) and WR (TAI) time,\n"
		"                    if configured NMEA/IRIGB: TOD (UTC) and WR (TAI) time,\n"
		"                    Linux (UTC) and TOD (UTC);\n"
		"                    similar to diff, but prints statistics periodically\n"
		"    diff            show the difference between WR FPGA time (HW) and linux time (SW)\n"
		"    disable         if used with irigb as <source>, disable IRIG-B\n"
		"    enable          if used with irigb as <source>, enable IRIG-B\n"
/*		"    set ntp         set TAI from ntp and leap seconds" */
/*		"    set ntp:<ip>    set from specified ntp server\n" */
		, WRDATE_CFG_FILE, NMEA_DEFAULT_BAUD, NMEA_DEFAULT_FORMAT);
	exit(1);
}


/* Check that we actualy are on the wr switch, exit if not */
int wrdate_check_host(void)
{
	int ret;

	/*
	 * Check the specific CPU: a different switch will require a
	 * different memory address so this must be match.
	 * system(3) is bad, but it's fast to code
	 */
	ret = system("grep -q ARM926EJ-S /proc/cpuinfo");
	if (opt_force && ret) {
		fprintf(stderr, "%s: not running on the WR switch\n", prgname);
		if (!opt_not)
			exit(1);
	}
	return ret ? -1 : 0;
}

int wrdate_cfgfile(char *fname)
{
	/* FIXME: parse config file */
	return 0;
}


/* This returns wr time, used for syncing to a second transition */
uint64_t gettimeof_wr(struct timeval *tv, volatile struct PPSG_WB *pps,
		      uint64_t *ret_nsec)
{
	uint32_t tai_h,tai_l,tmp1, tmp2;
	uint64_t tai, nsec;

	do {
		tai_h = pps->CNTR_UTCHI;
		tai_l = pps->CNTR_UTCLO;
		nsec = pps->CNTR_NSEC * PPSG_STEP_IN_NS;
		tmp1 = pps->CNTR_UTCHI;
		tmp2 = pps->CNTR_UTCLO;
	} while((tmp1 != tai_h) || (tmp2 != tai_l));

	tai = (uint64_t)(tai_h) << 32 | tai_l;

	tv->tv_sec = tai;
	tv->tv_usec = nsec / 1000;

	/* If ret_nsec not null return value of nsec */
	if (ret_nsec)
		*ret_nsec = nsec;

	return tai;
}

int get_kern_leaps(void)
{
	struct timex tx = {0};
	int *p;
	if (adjtimex(&tx) < 0) {
		fprintf(stderr, "%s: adjtimex(): %s\n", prgname,
			strerror(errno));
			return 0;
	}

	p = (int *)(&tx.stbcnt) + 1;
	return *p;
	//return t.tai;
}

static int wrdate_get_nmea_utc(int64_t *t_out)
{
	int ret;
	
	ret = nmea_read_utc(&nmea, t_out);

	if (ret == -1) {
		fprintf(stderr, "Timeout on reading nmea\n");
		return -1;
	}

	if (ret == -2) {
		fprintf(stderr, "Error while parsing nmea message\n");
		return -1;
	}

	return ret;
}

static int wrdate_get_irig_utc(int64_t *t_out)
{
	if (irig_wait_sec_transition(&wr_irig) < 0)
		return -1;

	if(irig_read_utc(&wr_irig, t_out) < 0){
		fprintf(stderr, "wr_date: %s: error reading irig\n", __func__);
		return -1;
	}

	/* For IRIG-B it takes an entire second to transfer a message with an
	 * information about second counter. Due to that the available second
	 * value is actually for the previous second, not the current one.
	 * So simply increment it's value by 1. */
	(*t_out)++;

	return 0;
}

static int gettimeofday_tod(struct timeval *tv)
{
	int ret;

	memset(tv, 0, sizeof(struct timeval));
	if (opt_nmea_en) {
		/* Is blocking! */
		ret = wrdate_get_nmea_utc((int64_t *)&tv->tv_sec);
		if (ret < 0)
			return ret;
		tv->tv_usec = (ret - 1) * 1000000 / opt_nmea_baud;
		return ret;
	} else if(opt_irig_en){
		/* Is blocking! */
		return wrdate_get_irig_utc((int64_t *)&tv->tv_sec);
	}

	return -1;
}

int wrdate_get(volatile struct PPSG_WB *pps, int tohost)
{
	uint64_t wr_nsec;
	time_t t;
	struct timeval time_hw, time_sw, time_tod;
	struct tm tm;
	char utcs[64], tais[64];
	int tai_offset;

	tai_offset = get_kern_leaps();

	if(opt_verbose)
		printf("TAI offset %d\n", tai_offset);

	/* Note for NMEA and IRIG-B function is blocking! */
	gettimeofday_tod(&time_tod);

	gettimeof_wr(&time_hw, pps, &wr_nsec);

	if (gettimeofday(&time_sw, NULL) < 0)
		return 1;

	/* Before printing (which takes time), set host time if so asked to */
	if (tohost) {
		if (opt_nmea_en) {
			time_hw = time_tod;
		} else if (opt_irig_en) {
			time_hw.tv_sec = time_tod.tv_sec;
			time_hw.tv_usec = 0;
		}

		/* Apply provided offset as parameter */
		while (time_hw.tv_usec + opt_offset_us > 1000000) {
			opt_offset_us -= 1000000;
			time_hw.tv_sec++;
		}

		while (time_hw.tv_usec + opt_offset_us < 0) {
			opt_offset_us += 1000000;
			time_hw.tv_sec--;
		}

		time_hw.tv_usec += opt_offset_us;

		if (!opt_not) {
			if (settimeofday(&time_hw, NULL)) {
				fprintf(stderr, "wr_date: settimeofday(): %s\n",
					strerror(errno));
			}
		}

		time_sw = time_hw;
	}

	t = time_tod.tv_sec;
	gmtime_r(&t, &tm);
	strftime(tais, sizeof(tais), "%Y-%m-%d %H:%M:%S", &tm);
	if (opt_nmea_en)
		printf("NMEA time: %s.%09li\n", tais, time_tod.tv_usec * 1000);
	if (opt_irig_en)
		printf("IRIG time: %s.%09li\n", tais, time_tod.tv_usec * 1000);

	t = time_hw.tv_sec; gmtime_r(&t, &tm);
	strftime(tais, sizeof(tais), "%Y-%m-%d %H:%M:%S", &tm);
	t -= tai_offset; gmtime_r(&t, &tm);
	strftime(utcs, sizeof(utcs), "%Y-%m-%d %H:%M:%S", &tm);
	printf("%"PRIu64".%09"PRIu64" TAI (WR)\n"
	       "%s.%09"PRIu64" TAI (WR)\n"
	       "%s.%09"PRIu64" UTC (WR)\n",
	       (uint64_t) time_hw.tv_sec, wr_nsec,
	       tais, wr_nsec,
	       utcs, wr_nsec);
	if(opt_verbose)
	{
		gmtime_r(&(time_sw.tv_sec), &tm);
		strftime(utcs, sizeof(utcs), "%Y-%m-%d %H:%M:%S", &tm);
		printf("%s.%09li UTC (linux)\n", utcs, time_sw.tv_usec*1000);
	}

	return 0;
}


int __wrdate_diff(volatile struct PPSG_WB *pps,struct timeval *ht, struct timeval *wt) {
	struct timeval diff;
	int neg=0;

	neg=timeval_subtract(&diff, wt, ht);

	printf("%s%c%li.%06li\n",opt_verbose?("TAI(HW)-UTC(SW): "):(""),neg?'-':'+',labs(diff.tv_sec),labs(diff.tv_usec));
	if(opt_verbose)
	{

		wt->tv_sec-=get_kern_leaps(); //Convert HW clock from TAI to UTC

		neg=timeval_subtract(&diff, wt, ht);
		printf("UTC(HW)-UTC(SW): %c%li.%06li\n",neg?'-':'+',labs(diff.tv_sec),labs(diff.tv_usec));
	}
	return 0;

}

int wrdate_diff(volatile struct PPSG_WB *pps)
{
	struct timeval ht, wt;

	/* wrdate_gettimeofday has to be first, since NMEA and IRIG-B
	 * may be blocking */
	gettimeof_wr(&wt, pps, NULL);
	gettimeofday(&ht, NULL);
	return __wrdate_diff(pps,&ht, &wt);
}

#define ADJ_SEC_ITER 10

static int wait_wr_adjustment(volatile struct PPSG_WB *pps)
{
	int count=0;

	while ((pps->CR & PPSG_CR_CNT_ADJ)==0) {
		if ( (count++)>=ADJ_SEC_ITER ) {
			fprintf(stderr, "%s: warning: WR time adjustment not finished after %ds !!!\n",prgname,ADJ_SEC_ITER);
			return 0;
		}
		if (opt_verbose)
			printf("WR time adjustment: waiting.\n");
		sleep(1);
	}
	if (opt_verbose)
		printf("WR time adjustment: done.\n");
	return 1;
}

#define CLOCK_SOURCE_MODULE_NAME "wr_clocksource"
#define CLOCK_SOURCE_MODULE_ELF  "/wr/lib/modules/" CLOCK_SOURCE_MODULE_NAME ".ko"

int removeClockSourceModule(void) {
  int ret= delete_module(CLOCK_SOURCE_MODULE_NAME, 0);
  if ( ret<0 ) {
		fprintf(stderr, "%s: Warning: Cannot remove module "CLOCK_SOURCE_MODULE_NAME " : error=%s\n" ,
				prgname,
				strerror(errno));
		return 0;
  }
  if (opt_verbose) {
	  printf("Driver module "CLOCK_SOURCE_MODULE_NAME" removed.\n");
  }
  return 1;
}

int installClockSourceModule(void) {
    struct stat st;
    void *image=NULL;
    int fd;
    int ret=0;
    const char *params="";

    if ((fd = open(CLOCK_SOURCE_MODULE_ELF, O_RDONLY))<0 ) {
		fprintf(stderr, "%s: Warning: Cannot open file "CLOCK_SOURCE_MODULE_ELF " : error=%s\n" ,
				prgname,
				strerror(errno));
		goto out;;
    }
    if ( fstat(fd, &st)<0 ) {
		fprintf(stderr, "%s: Warning: Cannot stat file "CLOCK_SOURCE_MODULE_ELF " : error=%s\n" ,
				prgname,
				strerror(errno));
		goto out;
    }
    if ( (image = malloc(st.st_size)) ==NULL ) {
		fprintf(stderr, "%s: Warning: Cannot allocate memory : error=%s\n" ,
				prgname,
				strerror(errno));
		goto out;
    }
    if ( read(fd, image, st.st_size)< 0 ) {
		fprintf(stderr, "%s: Warning: Cannot read file "CLOCK_SOURCE_MODULE_ELF " : error=%s\n" ,
				prgname,
				strerror(errno));
		goto out;
    }
    if (init_module(image, st.st_size, params) != 0) {
		fprintf(stderr, "%s: Warning: Cannot init module "CLOCK_SOURCE_MODULE_NAME " : error=%s\n" ,
				prgname,
				strerror(errno));
		goto out;
    }
    ret=1;

    if (opt_verbose) {
  	  printf("Driver module "CLOCK_SOURCE_MODULE_NAME" installed.\n");
    }

    out:;
    if ( fd >=0 )
    	close(fd);
    if ( image!=NULL)
    	free(image);
    return ret;
}

/* This sets WR time from host time */
int __wrdate_internal_set(volatile struct PPSG_WB *pps, int adjSecOnly, int tai_offset, int deep)

{
	struct timeval tvh, tvr; /* host, rabbit */
	signed long long diff64;
	signed long diff;
	int modRemoved=0;

	if ( deep > 4 )
		return 0; /* Avoid stack overflow (recursive function) in case of error */

	if ( ! opt_not) {
		if ( deep==0) {
			modRemoved=removeClockSourceModule(); // The driver must be removed otherwise the time cannot be set properly
		}

		usleep(100);
		gettimeofday(&tvh, NULL);
		gettimeof_wr(&tvr, pps, NULL);

		/* diff is the expected step to be added, so host - WR */
		diff = tvh.tv_usec - tvr.tv_usec;
		diff64 = tvh.tv_sec + tai_offset - tvr.tv_sec;
		if (diff > 500 * 1000) {
			diff64++;
			diff -= 1000 * 1000;
		}
		if (diff < -500 * 1000) {
			diff64--;
			diff += 1000 * 1000;
		}

		/* Warn if more than 200ms away */
		if (diff > 200 * 1000 || diff < -200 * 1000)
			fprintf(stderr, "%s: Warning: fractional second differs by"
				"more than 0.2 (%li ms)\n", prgname, diff / 1000);

		if (opt_verbose && deep==0) {
			printf("Host time: %9li.%06li\n", (long)(tvh.tv_sec),
				   (long)(tvh.tv_usec));
			printf("WR   time: %9li.%06li\n", (long)(tvr.tv_sec),
				   (long)(tvr.tv_usec));
			__wrdate_diff(pps,&tvh,&tvr);
		}

		if (diff64) {
			if (opt_verbose)
				printf("adjusting by %lli seconds\n", diff64);
			/* We must write a signed "adjustment" value */
			pps->ADJ_UTCLO = diff64 & 0xffffffff;
			pps->ADJ_UTCHI = (diff64 >> 32) & 0xff;
			pps->ADJ_NSEC = 0;
			asm("" : : : "memory"); /* barrier... */
			pps->CR = pps->CR | PPSG_CR_CNT_ADJ;
			if ( wait_wr_adjustment(pps) && !adjSecOnly )
					__wrdate_internal_set(pps,0,tai_offset,deep+1); /* adjust the usecs */
			/* Make sure registers are empty, probably there is a
			 * bug in HDL causing a jump of a fractional part of
			 * a second (see bug #213 in wr-switch-sw repo) */
			pps->ADJ_UTCLO = 0;
			pps->ADJ_UTCHI = 0;
			pps->ADJ_NSEC = 0;
			asm("" : : : "memory"); /* barrier... */
		} else if ( !adjSecOnly ) {
			if (opt_verbose)
				printf("adjusting by %li usecs\n", diff);
			pps->ADJ_UTCLO = 0;
			pps->ADJ_UTCHI = 0;
			pps->ADJ_NSEC = (diff*1000)/16;
			asm("" : : : "memory"); /* barrier... */
			pps->CR = pps->CR | PPSG_CR_CNT_ADJ;
			wait_wr_adjustment(pps);
			/* Make sure registers are empty, probably there is a
			 * bug in HDL causing a jump of a fractional part of
			 * a second (see bug #213 in wr-switch-sw repo) */
			pps->ADJ_NSEC = 0;
			asm("" : : : "memory"); /* barrier... */
		}

		if ( deep==0 && modRemoved ) {
			installClockSourceModule();
		}
	}
	if (opt_verbose && deep==0) {
		usleep(100);
		gettimeofday(&tvh, NULL);
		gettimeof_wr(&tvr, pps, NULL);

		printf("Host time: %9li.%06li\n", (long)(tvh.tv_sec),
		       (long)(tvh.tv_usec));
		printf("WR   time: %9li.%06li\n", (long)(tvr.tv_sec),
		       (long)(tvr.tv_usec));
		__wrdate_diff(pps,&tvh,&tvr);
	}
	return 0;
}

/* This sets WR time from host time */
int wrdate_internal_set(volatile struct PPSG_WB *pps, int taiOnly, int adjSecOnly) {
	int tai_offset = 0;

	if (opt_not) {
		// Do not change the TAI but display only information if verbose is enabled
		struct timex t;
		int taiOffset, hasExpired;

		if (!opt_verbose) return 0;
		/* first: get the current offset */
		memset(&t, 0, sizeof(t));
		if (adjtimex(&t) < 0) {
			fprintf(stderr, "%s: adjtimex(): %s\n", __func__,
				strerror(errno));
			return 0;
		}
		printf("Current TAI offset= %d\n",t.tai);
		if ( (taiOffset=getTaiOffsetFromLeapSecondsFile(NULL,time(NULL),NULL,&hasExpired))==-1) {
			fprintf(stderr, "%s: Cannot fix read TAI offset from leap seconds file\n" ,prgname);
			return 0;
		}
		printf("TAI offset form leap seconds file= %d\n",taiOffset);
		if ( hasExpired )
			printf("Leap seconds file has expired!\n");
	} else {
		if ( (tai_offset=fixHostTai(NULL,time(NULL),NULL,opt_verbose)) == -1 ) {
			fprintf(stderr, "%s: Cannot fix TAI offset\n" ,prgname);
			return 0;
		}
	}
	if ( taiOnly )
		return 0;
	return __wrdate_internal_set(pps,adjSecOnly,tai_offset,0);
}

/* This sets WR time from host time for grand master mode
 * For a GM we adjust only the seconds part of the time.
 */
#define FULL_SEC (1000*1000) /* 1000 ms */
#define HALF_SEC (FULL_SEC/2) /* 500 ms */
#define LOW_LIMIT_HALF_SEC (300*1000) /* 300 ms */
#define HIGH_LIMIT_HALF_SEC (700*1000) /* 300 ms */

int wrdate_internal_set_gm(volatile struct PPSG_WB *pps, int taiOnly) {

	if (opt_verbose ) {
		printf("Set WR time for grand master.\n");
	}
	/* We try to set the seconds between to PPS ticks */
	while (1==1) {
		struct timeval wt;

		gettimeof_wr(&wt, pps, NULL);
		if ( wt.tv_usec>LOW_LIMIT_HALF_SEC && wt.tv_usec<HIGH_LIMIT_HALF_SEC ) {
			wrdate_internal_set(pps,taiOnly,1);
			return 0;
		} else {
			useconds_t usec;

			usec= ( wt.tv_usec>HALF_SEC ) ?
					(FULL_SEC-wt.tv_usec)+HALF_SEC :
					HALF_SEC-wt.tv_usec;

			usleep(usec);
		}
	}
	return 0;
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

/* Frontend to the set mechanism: parse the argument */
int wrdate_set(volatile struct PPSG_WB *pps, int argc, char **argv)
{
	char *s;
	unsigned long t; /* WARNING: 64 bit */
	struct timeval tv;

	if (argc>=1 && !strcmp(argv[0], "host")) {
		int tm=getTimingMode();
		int taiOnly;

		taiOnly=argc>=2 && !strcmp(argv[1], "tai");

		switch (tm) {
		case RTS_MODE_GM_EXTERNAL:
			return wrdate_internal_set_gm(pps,taiOnly);
		case RTS_MODE_GM_FREERUNNING :
		case RTS_MODE_DISABLED :
			return wrdate_internal_set(pps,taiOnly,0);
			break;
		case RTS_MODE_BC :
			fprintf(stderr, "Slave timing mode: WR time and TAI offset cannot be set!!!\n");
			return -1;
		default:
			fprintf(stderr, "Cannot read Soft PLL timing mode. WR time and TAI offset cannot be set (ret=%d)\n",tm);
			return -1;
		}
	}

	if ( argc>=1 ) {
		s = strdup(argv[0]);
		if (sscanf(argv[0], "%li%s", &t, s) == 1) {
			tv.tv_sec = t;
			tv.tv_usec = 0;
			if (settimeofday(&tv, NULL) < 0) {
				fprintf(stderr, "%s: settimeofday(%s): %s\n",
					prgname, argv[0], strerror(errno));
				exit(1);
			}
			return wrdate_internal_set(pps,1,0);
		}

		/* FIXME: other time formats */
		printf(" FIXME\n");
		return 0;
	}
	fprintf(stderr, "Missing parameter!! \n\n");
	return 0;
}

/* Print statistics between TAI and UTC dates */
#define STAT_SAMPLE_COUNT 20

int wrdate_stat(volatile struct PPSG_WB *pps)
{
	enum {
		diff_lin_wr,
		diff_tod_wr,
		diff_lin_tod,
		diff_n_size,
	};

	int udiff_ref[diff_n_size] = {0,0,0};
	int udiff_last[diff_n_size];

	int stat_sample_count = STAT_SAMPLE_COUNT;
	struct timeval tv_wr_tai, tv_host, tv_tod;
	int tod_en = 0;
	char *tod_str = "";

	if (opt_nmea_en || opt_irig_en) {
		/* gettimeofday_tod for NMEA and IRIG-B is blocking till
		 * the boundary of a second (+some time) */
		stat_sample_count = 1;
	}

	/* First readout is sometimes shifted for NMEA */
	(void) gettimeofday_tod(&tv_tod);

	if (opt_nmea_en) {
		tod_str = "NMEA";
		tod_en = 1;
	}

	if (opt_irig_en) {
		tod_str = "IRIGB";
		tod_en = 1;
	}

	if (!tod_en) {
		printf("          Diff WR(TAI)-SW(UTC)   |\n");
		printf("        Diff Diff_last  Diff_ref |\n");
		printf("       [sec]    [usec]    [usec] |\n");
		printf("---------------------------------+\n");
	} else {
		printf("          Diff WR(TAI)-SW(UTC)   |         Diff WR(TAI)-%s(UTC)%*s |         Diff SW(UTC)-%s(UTC)%*s |\n",
		       tod_str, 5 - strlen(tod_str), "", tod_str, 5 - strlen(tod_str), "");
		printf("        Diff Diff_last  Diff_ref |        Diff Diff_last  Diff_ref |        Diff Diff_last  Diff_ref |\n");
		printf("       [sec]    [usec]    [usec] |       [sec]    [usec]    [usec] |       [sec]    [usec]    [usec] |\n");
		printf("---------------------------------+---------------------------------+---------------------------------+\n");
	}

	while ( 1 ) {
		int i;
		int64_t udiff = 0;
		int64_t udiff_sum[diff_n_size] = {0,0,0};

		for ( i=0; i<stat_sample_count; i++ ) {
			int64_t udiff_tmp;

			// Get time
			usleep(100); // Increase stability of measures : less preempted during time measures
			gettimeofday_tod(&tv_tod);
			gettimeofday(&tv_host, NULL);
			gettimeof_wr(&tv_wr_tai, pps, NULL);

			/* Calculate difference WR(TAI)-Linux(UTC) */
			udiff_tmp = ((int64_t)(tv_wr_tai.tv_sec - tv_host.tv_sec)) * 1000000;
			if (tv_wr_tai.tv_usec > tv_host.tv_usec) {
				udiff_tmp += tv_wr_tai.tv_usec - tv_host.tv_usec;
			} else {
				udiff_tmp -= tv_host.tv_usec - tv_wr_tai.tv_usec;
			}
			udiff_sum[diff_lin_wr] += udiff_tmp;

			/* Calculate difference WR(TAI)-TOD(UTC) */
			udiff_tmp = ((int64_t)(tv_wr_tai.tv_sec - tv_tod.tv_sec)) * 1000000;
			if (tv_wr_tai.tv_usec > tv_tod.tv_usec) {
				udiff_tmp += tv_wr_tai.tv_usec - tv_tod.tv_usec;
			} else {
				udiff_tmp -= tv_tod.tv_usec - tv_wr_tai.tv_usec;
			}
			udiff_sum[diff_tod_wr] += udiff_tmp;

			/* Calculate difference SW(UTC)-TOD(UTC) */
			udiff_tmp = ((int64_t)(tv_host.tv_sec - tv_tod.tv_sec)) * 1000000;
			if (tv_host.tv_usec > tv_tod.tv_usec) {
				udiff_tmp += tv_host.tv_usec - tv_tod.tv_usec;
			} else {
				udiff_tmp -= tv_tod.tv_usec - tv_host.tv_usec;
			}
			udiff_sum[diff_lin_tod] += udiff_tmp;
		}

		for (i = 0; i < (tod_en ? diff_n_size : 1); i++) {
			udiff = udiff_sum[i] / stat_sample_count;
			if (udiff_ref[i] == 0) {
				udiff_ref[i] = udiff_last[i] = udiff;
			}

			/*  Display diffs */
			printf("%5d.%06d %9li %9li |",
				(int)(udiff / 1000000),
				abs(udiff % 1000000),
				(long) (udiff_last[i] - udiff),
				(long) (udiff_ref[i] - udiff)
				);

			udiff_last[i] = udiff;
		}
		printf("\n");
		/* Readout for NMEA or IRIG-B will wait till the boundary of
		 * a second anyway */
		if (!opt_nmea_en && !opt_irig_en) {
			sleep(1);
		}
	}

	return 0;
}


int main(int argc, char **argv)
{
	int c, tohost = 0;
	char *cmd;
	char *source_tod;
	volatile struct PPSG_WB *pps;
	int something_done = 0;

	prgname = argv[0];

	while ( (c = getopt(argc, argv, "fc:b:m:vno:")) != -1) {
		switch(c) {
		case 'f':
			opt_force = 1;
			break;
		case 'c':
			opt_cfgfile = optarg;
			break;
		case 'b':
			opt_nmea_baud = atoi(optarg);
			break;
		case 'm':
			opt_nmea_fmt = optarg;
			break;
		case 'v':
			opt_verbose = 1;
			break;
		case 'o':
			opt_offset_us = atol(optarg);
			break;
		case 'n':
			opt_not = 1;
			printf("Dry run mode: No action will be performed\n");
			break;
		default:
			help();
		}
	}

	/* No command nor source of time */
	if (optind > argc - 1)
		help();

	source_tod = argv[optind];
	if (!strcmp(source_tod, "nmea")) {
		opt_nmea_en = 1;
		optind++;
	} else if (!strcmp(source_tod, "irigb")) {
		opt_irig_en = 1;
		optind++;
	} else if (!strcmp(source_tod, "wr")) {
		optind++;
	}

	wrdate_check_host();
	pps = create_map(FPGA_BASE_PPSG, sizeof(*pps));
	if (!pps) {
		fprintf(stderr, "%s: mmap: %s\n", prgname, strerror(errno));
		exit(1);
	}

	wrdate_cfgfile(opt_cfgfile);

	if (opt_nmea_en) {
		nmea_init(&nmea, NMEA_SERIAL_PORT, opt_nmea_baud, opt_nmea_fmt);
	}

	if (opt_irig_en) {
		wr_irig.irig = create_map(FPGA_BASE_IRIG,
					  sizeof(*wr_irig.irig));
		if (!wr_irig.irig){
			fprintf(stderr, "%s: mmap: %s\n", prgname,
				strerror(errno));
			exit(1);
		}
	}

	cmd = argv[optind++];

	if (cmd == NULL) {
		fprintf(stderr, "No command provided\n");
		return 1;
	}

	if (!strcmp(cmd, "enable")) {
		if (opt_irig_en) {
			irig_enable(&wr_irig, 1);
			printf("IRIG-B enabled\n");
			cmd = argv[optind++];
			something_done = 1;
		} else {
			fprintf(stderr, "\"enable\" command is valid only for IRIG-B\n");
			return 1;
		}
	} else if (!strcmp(cmd, "disable")) {
		if (opt_irig_en) {
			irig_enable(&wr_irig, 0);
			printf("IRIG-B disabled\n");
			cmd = argv[optind++];
			something_done = 1;
		} else {
			fprintf(stderr, "\"disable\" command is valid only for IRIG-B\n");
			return 1;
		}
	}

	if (cmd == NULL && something_done) {
		/* No more arguments */
		return 0;
	}

	if (opt_irig_en && !irig_enable_status(&wr_irig)) {
		fprintf(stderr, "Warning IRIG-B is disabled!\n");
	}

	if (!strcmp(cmd, "get")) {
		/* parse the optional "tohost" argument */
		if (optind == argc - 1 && !strcmp(argv[optind], "tohost"))
			tohost = 1;
		else if (optind < argc)
			help();
		return wrdate_get(pps, tohost);
	}

	if (!strcmp(cmd, "diff")) {
		return wrdate_diff(pps);
	}

	if (!strcmp(cmd, "stat")) {
		return wrdate_stat(pps);
	}

	/* only other command is "set", with one on more arguments */
	if (strcmp(cmd, "set") || optind > argc - 1)
		help();


	return wrdate_set(pps, argc-optind,&argv[optind]);
}
