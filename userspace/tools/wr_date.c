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

#ifndef MOD_TAI
#define MOD_TAI 0x80
#endif
#define WRDATE_CFG_FILE "/etc/wr_date.conf"
#define WRDATE_LEAP_FILE "/etc/leap-seconds.list"

/* Address for hardware, from nic-hardware.h */
#define FPGA_BASE_PPSG  0x10010500
#define PPSG_STEP_IN_NS 16 /* we count a 16.5MHz */

extern int init_module(void *module, unsigned long len, const char *options);
extern int delete_module(const char *module, unsigned int flags);

void help(char *prgname)
{
	fprintf(stderr, "%s: Use: \"%s [<options>] <cmd> [<args>]\n",
		prgname, prgname);
	fprintf(stderr,
		"  The program uses %s as default config file name\n"
		"  -f       force: run even if not on a WR switch\n"
		"  -c <cfg> configfile to use in place of the default\n"
		"  -v       verbose: report what the program does\n"
		"  -n       do not act in practice\n"
		"    get             print WR time to stdout\n"
		"    get tohost      print WR time and set system time\n"
		"    set <value>     set WR time to scalar seconds\n"
		"    set host        set TAI from current host time\n"
		"    stat            print statistics between TAI (WR time) and linux UTC\n"
		"    diff            show the difference between WR FPGA time (HW) and linux time (SW)\n"
/*		"    set ntp         set TAI from ntp and leap seconds" */
/*		"    set ntp:<ip>    set from specified ntp server\n" */
		, WRDATE_CFG_FILE);
	exit(1);
}

int opt_verbose, opt_force, opt_not;
char *opt_cfgfile = WRDATE_CFG_FILE;
char *prgname;

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
uint64_t gettimeof_wr(struct timeval *tv, struct PPSG_WB *pps)
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


int wrdate_get(struct PPSG_WB *pps, int tohost)
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


/**
 * Function to subtract timeval in a robust way
 *
 * In order to properly print the result on screen you can use:
 *
 *     int neg=timeval_subtract(&diff, &a, &b);
 *     printf("%c%li.%06li\n",neg?'-':'+',labs(diff.tv_sec),labs(diff.tv_usec));
 *
 * @ref: https://stackoverflow.com/questions/15846762/timeval-subtract-explanation
 * @note for safety reason a copy of x,y is used internally so x,y are never modified
 * @param[inout] result A pointer on a timeval structure where the result will be stored.
 * @param[in] x A pointer on x timeval struct
 * @param[in] y A pointer on y timeval struct
 * @return 1 if result is negative (seconds or useconds)
 *
 *
 */
int timeval_subtract(struct timeval *result, struct timeval *x, struct timeval *y)
{
	struct timeval xx = *x;
	struct timeval yy = *y;
	x = &xx; y = &yy;

	if (x->tv_usec > 999999)
	{
		x->tv_sec += x->tv_usec / 1000000;
		x->tv_usec %= 1000000;
	}

	if (y->tv_usec > 999999)
	{
		y->tv_sec += y->tv_usec / 1000000;
		y->tv_usec %= 1000000;
	}

	result->tv_sec = x->tv_sec - y->tv_sec;
	result->tv_usec = x->tv_usec - y->tv_usec;

	if(result->tv_sec>0 && result->tv_usec < 0)
	{
		result->tv_usec += 1000000;
		result->tv_sec--; // borrow
	}
	else if(result->tv_sec<0 && result->tv_usec > 0)
	{
		result->tv_usec -= 1000000;
		result->tv_sec++; // borrow
	}

	return (result->tv_sec < 0) || (result->tv_usec<0);
}



int wrdate_diff(struct PPSG_WB *pps)
{
	struct timeval sw, hw, diff;
	int neg=0;

	gettimeof_wr(&hw, pps);
	gettimeofday(&sw, NULL);

	neg=timeval_subtract(&diff, &hw, &sw);

	printf("%s%c%li.%06li\n",opt_verbose?("TAI(HW)-UTC(SW): "):(""),neg?'-':'+',labs(diff.tv_sec),labs(diff.tv_usec));
	if(opt_verbose)
	{

		hw.tv_sec-=get_kern_leaps(); //Convert HW clock from TAI to UTC

		neg=timeval_subtract(&diff, &hw, &sw);
		printf("UTC(HW)-UTC(SW): %c%li.%06li\n",neg?'-':'+',labs(diff.tv_sec),labs(diff.tv_usec));
	}
	return 0;
}

/* Fix the TAI representation looking at the leap file */
int fix_host_tai(void)
{
	struct timex t;
	char s[128];
	unsigned long long now, now_2014, leapt, expire = 0;
	int i, *p, tai_offset = 0;

	/* first: get the current offset */
	memset(&t, 0, sizeof(t));
	if (adjtimex(&t) < 0) {
		fprintf(stderr, "%s: adjtimex(): %s\n", prgname,
			strerror(errno));
		return 0;
	}

	/*
	 * At the very start, we believe to be Jan 1st 1970. But
	 * what we really want is counting the tai_offset, so WR
	 * can then set system time by itself.  And, being wrong by
	 * 1-2 seconds is ok (system time is for log messages only),
	 * but being off by 35 seconds is not. So let's use "35" by
	 * default, i.e. be aware we are at least in 2014
	 */
	now_2014 = 1417806803; /* as I write this */

	/* then, find the current time, using such offset */
	now = time(NULL);
	if (now < now_2014)
		now = now_2014;

	now += 2208988800LL; /* (for TAI: + utc_offset) */

	FILE *f = fopen(WRDATE_LEAP_FILE, "r");
	if (!f) {
		fprintf(stderr, "%s: %s: %s\n", prgname, WRDATE_LEAP_FILE,
			strerror(errno));
		return 0;
	}
	while (fgets(s, sizeof(s), f)) {
		if (sscanf(s, "#@ %lli", &expire) == 1)
			continue;
		if (sscanf(s, "%lli %i", &leapt, &i) != 2)
			continue;
		/* check this line, and apply it if it's in the past */
		if (leapt < now)
			tai_offset = i;
	}
	fclose(f);

	/*
	 * Our WRS kernel has tai support, but our compiler does not.
	 * We are 32-bit only, and we know for sure that tai is
	 * exactly after stbcnt. It's a bad hack, but it works
	 */
	p = (int *)(&t.stbcnt) + 1;

	if (tai_offset != *p) {
		if (opt_verbose)
			printf("Previous TAI offset: %i\n", *p);
		t.constant = tai_offset;
		t.modes = MOD_TAI;
		if (adjtimex(&t) < 0) {
			fprintf(stderr, "%s: adjtimex(): %s\n", prgname,
				strerror(errno));
			return tai_offset;
		}
	}
	if (opt_verbose)
		printf("Current TAI offset: %i\n", *p);
	return tai_offset;
}

#define ADJ_SEC_ITER 10

static int wait_wr_adjustment(struct PPSG_WB *pps)
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

    out:;
    if ( fd >=0 )
    	close(fd);
    if ( image!=NULL)
    	free(image);
    return ret;
}

/* This sets WR time from host time */
int __wrdate_internal_set(struct PPSG_WB *pps, int deep)

{
	struct timeval tvh, tvr; /* host, rabbit */
	signed long long diff64;
	signed long diff;
	int tai_offset;
	int modRemoved=0;

	if ( deep > 4 )
		return 0; /* Avoid stack overflow (recursive function) in case of error */
	if ( deep==0 ) {
		modRemoved=removeClockSourceModule(); // The driver must be removed otherwise the time cannot be set properly
	}
	tai_offset = fix_host_tai();

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
		printf("Fractional difference: %li usec\n", diff);
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
		if ( wait_wr_adjustment(pps) )
		__wrdate_internal_set(pps,deep+1); /* adjust the usecs */
	} else {
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

	if (opt_verbose && deep==0) {
		usleep(100);
		gettimeofday(&tvh, NULL);
		gettimeof_wr(&tvr, pps);

		printf("Host time: %9li.%06li\n", (long)(tvh.tv_sec),
		       (long)(tvh.tv_usec));
		printf("WR   time: %9li.%06li\n", (long)(tvr.tv_sec),
		       (long)(tvr.tv_usec));
	}
	return 0;
}

/* This sets WR time from host time */
int wrdate_internal_set(struct PPSG_WB *pps) {
	return __wrdate_internal_set(pps,0);
}

/* Frontend to the set mechanism: parse the argument */
int wrdate_set(struct PPSG_WB *pps, char *arg)
{
	char *s;
	unsigned long t; /* WARNING: 64 bit */
	struct timeval tv;


	if (!strcmp(arg, "host"))
		return wrdate_internal_set(pps);

	s = strdup(arg);
	if (sscanf(arg, "%li%s", &t, s) == 1) {
		tv.tv_sec = t;
		tv.tv_usec = 0;
		if (settimeofday(&tv, NULL) < 0) {
			fprintf(stderr, "%s: settimeofday(%s): %s\n",
				prgname, arg, strerror(errno));
			exit(1);
		}
		return wrdate_internal_set(pps);
	}

	/* FIXME: other time formats */
	printf(" FIXME\n");
	return 0;
}

/* Print statistics between TAI and UTC dates */
#define STAT_SAMPLE_COUNT 20

int wrdate_stat(struct PPSG_WB *pps)
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
	struct PPSG_WB *pps;

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
			break;
		default:
			help(argv[0]);
		}
	}
	if (optind > argc - 1)
		help(argv[0]);

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
			help(argv[0]);
		return wrdate_get(pps, tohost);
	}

	if (!strcmp(cmd, "diff")) {
		return wrdate_diff(pps);
	}

	if (!strcmp(cmd, "stat")) {
		/* parse the optional "tohost" argument */
		return wrdate_stat(pps);
	}

	/* only other command is "set", with one argument */
	if (strcmp(cmd, "set") || optind != argc - 1)
		help(argv[0]);


	return wrdate_set(pps, argv[optind]);
}
