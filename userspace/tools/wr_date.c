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
#include <libwr/softpll_export.h>
#include <libwr/util.h>
#include <time_lib.h>
#include <rt_ipc.h>

#ifndef MOD_TAI
#define MOD_TAI 0x80
#endif
#define WRDATE_CFG_FILE "/etc/wr_date.conf"

/* Address for hardware, from nic-hardware.h */
#define FPGA_BASE_PPSG  0x10010500
#define PPSG_STEP_IN_NS 16 /* we count a 16.5MHz */

extern int init_module(void *module, unsigned long len, const char *options);
extern int delete_module(const char *module, unsigned int flags);

static int opt_verbose, opt_force, opt_not;
static char *opt_cfgfile = WRDATE_CFG_FILE;
static char *prgname;

void help(void)
{
	fprintf(stderr, "%s: Use: \"%s [<options>] <cmd> [<args>]\n",
		prgname, prgname);
	fprintf(stderr,
		"  The program uses %s as default config file name\n"
		"  -f       force: run even if not on a WR switch\n"
		"  -c <cfg> configfile to use in place of the default\n"
		"  -v       verbose: report what the program does\n"
		"  -n       do not act in practice - dry run\n"
		"    get             print WR time to stdout\n"
		"    get tohost      print WR time and set system time\n"
		"    set <value>     set WR time to scalar seconds\n"
		"    set host [tai]  set TAI and WR time from current host time.\n"
		"                    if tai option is set then set only the TAI offset.\n"
		"    stat            print statistics between TAI (WR time) and linux UTC\n"
		"    diff            show the difference between WR FPGA time (HW) and linux time (SW)\n"
/*		"    set ntp         set TAI from ntp and leap seconds" */
/*		"    set ntp:<ip>    set from specified ntp server\n" */
		, WRDATE_CFG_FILE);
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
uint64_t gettimeof_wr(struct timeval *tv, volatile struct PPSG_WB *pps)
{
	uint32_t tai_h,tai_l,nsec, tmp1, tmp2;
	uint64_t tai;

	tai_h = pps->CNTR_UTCHI;

	do {
		tai_l = pps->CNTR_UTCLO;
		nsec = pps->CNTR_NSEC * PPSG_STEP_IN_NS;
		tmp1 = pps->CNTR_UTCHI;
		tmp2 = pps->CNTR_UTCLO;
	} while((tmp1 != tai_h) || (tmp2 != tai_l));

	tai = (uint64_t)(tai_h) << 32 | tai_l;

	tv->tv_sec = tai;
	tv->tv_usec = nsec / 1000;

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


int wrdate_get(volatile struct PPSG_WB *pps, int tohost)
{
	unsigned long taih, tail, nsec, tmp1, tmp2;
	uint64_t tai;
	time_t t;
	struct timeval hw,sw;
	struct tm tm;
	char utcs[64], tais[64];
	int tai_offset;

	tai_offset = get_kern_leaps();

	if (opt_not) {
		gettimeofday(&sw, NULL);
		taih = 0;
		tail = sw.tv_sec + tai_offset;
		nsec = sw.tv_usec * 1000;
	} else {
		taih = pps->CNTR_UTCHI;

		do {
			tail = pps->CNTR_UTCLO;
			nsec = pps->CNTR_NSEC * 16; /* we count a 16.5MHz */
			tmp1 = pps->CNTR_UTCHI;
			tmp2 = pps->CNTR_UTCLO;
		} while((tmp1 != taih) || (tmp2 != tail));
	}
	gettimeofday(&sw, NULL);

	tai = (uint64_t)(taih) << 32 | tail;

	/* Before printing (which takes time), set host time if so asked to */
	if (tohost) {
		hw.tv_sec = tai - tai_offset;
		hw.tv_usec = nsec / 1000;
		if (settimeofday(&hw, NULL))
			fprintf(stderr, "wr_date: settimeofday(): %s\n",
				strerror(errno));
	}

	t = tai; gmtime_r(&t, &tm);
	strftime(tais, sizeof(tais), "%Y-%m-%d %H:%M:%S", &tm);
	t -= tai_offset; gmtime_r(&t, &tm);
	strftime(utcs, sizeof(utcs), "%Y-%m-%d %H:%M:%S", &tm);
	printf("%lli.%09li TAI\n"
	       "%s.%09li TAI\n"
	       "%s.%09li UTC\n", tai, nsec, tais, nsec, utcs, nsec);
	if(opt_verbose)
	{
		gmtime_r(&(sw.tv_sec), &tm);
		strftime(utcs, sizeof(utcs), "%Y-%m-%d %H:%M:%S", &tm);
		printf("%s.%09li UTC (linux)\n", utcs, sw.tv_usec*1000);
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

	gettimeof_wr(&wt, pps);
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
		gettimeof_wr(&tvr, pps);

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
		} else if ( !adjSecOnly ) {
			if (opt_verbose)
				printf("adjusting by %li usecs\n", diff);
			pps->ADJ_UTCLO = 0;
			pps->ADJ_UTCHI = 0;
			pps->ADJ_NSEC = (diff*1000)/16;
			asm("" : : : "memory"); /* barrier... */
			pps->CR = pps->CR | PPSG_CR_CNT_ADJ;
			wait_wr_adjustment(pps);
		}

		if ( deep==0 && modRemoved ) {
			installClockSourceModule();
		}
	}
	if (opt_verbose && deep==0) {
		usleep(100);
		gettimeofday(&tvh, NULL);
		gettimeof_wr(&tvr, pps);

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
	int tai_offset;

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
		if ( (taiOffset=getTaiOffsetFromLeapSecondsFile(NULL,time(NULL),&hasExpired))==-1) {
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

		gettimeof_wr(&wt, pps);
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
		case SPLL_MODE_GRAND_MASTER:
			return wrdate_internal_set_gm(pps,taiOnly);
		case SPLL_MODE_FREE_RUNNING_MASTER :
		case SPLL_MODE_DISABLED :
			return wrdate_internal_set(pps,taiOnly,0);
			break;
		case SPLL_MODE_SLAVE :
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
	int udiff_ref=0,udiff_last;

	printf("Diff_TAI_UTC[sec] Diff_with_last[usec] Diff_with_ref[usec]\n");
	while ( 1 ) {
		int64_t udiff_arr[STAT_SAMPLE_COUNT]; // Diff in useconds
		struct timeval tv_tai,tv_host;
		int i;
		int64_t udiff_sum=0, udiff;

		for ( i=0; i<STAT_SAMPLE_COUNT; i++ ) {
			int64_t *udiff_tmp=&udiff_arr[i];

			// Get time
			usleep(100); // Increase stability of measures : less preempted during time measures
			gettimeof_wr(&tv_tai,pps);
			gettimeofday(&tv_host, NULL);

			// Calculate difference
			*udiff_tmp=(tv_host.tv_sec-tv_tai.tv_sec)*1000000;
			if ( tv_host.tv_usec > tv_tai.tv_usec ) {
				*udiff_tmp+=tv_host.tv_usec-tv_tai.tv_usec;
			} else {
				*udiff_tmp-=tv_tai.tv_usec-tv_host.tv_usec;
			}
			udiff_sum+=*udiff_tmp;
		}
		udiff=udiff_sum/STAT_SAMPLE_COUNT;
		if ( udiff_ref==0) {
			udiff_ref=udiff_last=udiff;
		}

		printf("%03d.%06d %6li %6li\n",
			   (int)(udiff/1000000),
			   abs(udiff%1000000),
			   (long) (udiff_last-udiff),
			   (long) (udiff_ref-udiff)
			   );
		udiff_last=udiff;

		sleep(1);
	}

	return 0;
}


int main(int argc, char **argv)
{
	int c, tohost = 0;
	char *cmd;
	volatile struct PPSG_WB *pps;

	prgname = argv[0];

	while ( (c = getopt(argc, argv, "fc:vn")) != -1) {
		switch(c) {
		case 'f':
			opt_force = 1;
			break;
		case 'c':
			opt_cfgfile = optarg;
			break;
		case 'v':
			opt_verbose = 1;
			break;
		case 'n':
			opt_not = 1;
			printf("Dry run mode: No action will be performed\n");
			break;
		default:
			help();
		}
	}
	if (optind > argc - 1)
		help();

	cmd = argv[optind++];

	wrdate_check_host();
	pps = create_map(FPGA_BASE_PPSG, sizeof(*pps));
	if (!pps) {
		fprintf(stderr, "%s: mmap: %s\n", prgname, strerror(errno));
		exit(1);
	}

	wrdate_cfgfile(opt_cfgfile);

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
		/* parse the optional "tohost" argument */
		return wrdate_stat(pps);
	}

	/* only other command is "set", with one on more arguments */
	if (strcmp(cmd, "set") || optind > argc - 1)
		help();


	return wrdate_set(pps, argc-optind,&argv[optind]);
}
