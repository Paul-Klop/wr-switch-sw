/* This is just a subset of wr_date, user to test on the host */
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

#ifndef MOD_TAI
#define MOD_TAI 0x80
#endif

int opt_verbose = 1;
char *prgname;

static void unix_time_clear_utc_flags(void)
{
	struct timex t;
	
	/*
	 * We have to call adjtime twice here, as kernels
	 * prior to 6b1859dba01c7 (included in 3.5 and
	 * -stable), had an issue with the state machine
	 * and wouldn't clear the STA_INS/DEL flag directly.
	 */
	t.modes = ADJ_STATUS;
	t.status = STA_PLL;
	adjtimex(&t);

	/* Clear maxerror, as it can cause UNSYNC to be set */
	t.modes = ADJ_MAXERROR;
	t.maxerror = 0;
	adjtimex(&t);

	/* Clear the status */
	t.modes = ADJ_STATUS;
	t.status = 0;
	adjtimex(&t);	
		
}

static int unix_time_get_utc_time(int *hours, int *minutes, int *seconds)
{
	int ret;
	struct timex t;
	time_t now;
	struct tm *date;
	
	/* Get the UTC time */
	memset(&t, 0, sizeof(t));
	ret = adjtimex(&t);
	if (ret >= 0) {
		now = t.time.tv_sec;
		/* use gmtime for correct leap handling */
		date = gmtime(&now);
		*hours = date->tm_hour;
		*minutes = date->tm_min;
		*seconds = date->tm_sec;
		return 0;
	} else {
		*hours = 0;
		*minutes = 0;
		*seconds = 0;
		return -1;
	}
	
	return -1;
}

static int unix_time_get_utc_offset(int *offset, int *leap59, int *leap61)
{
	int ret;
	struct timex t;
	int hours, minutes, seconds;
	
	unix_time_get_utc_time(&hours, &minutes, &seconds);
	
	/*
	 * Get the UTC/TAI difference
	 */
	memset(&t, 0, sizeof(t));
	ret = adjtimex(&t);
	if (ret >= 0) {
		if (hours >= 12) {
			if ((t.status & STA_INS) == STA_INS) {
				*leap59 = 0;
				*leap61 = 1;
			} else if ((t.status & STA_DEL) == STA_DEL) {
				*leap59 = 1;
				*leap61 = 0;
			} else {
				*leap59 = 0;
				*leap61 = 0;
			}	
		} else {
			unix_time_clear_utc_flags();			
			*leap59 = 0;
			*leap61 = 0;
		}
		/*
		 * Our WRS kernel has tai support, but our compiler does not.
		 * We are 32-bit only, and we know for sure that tai is
		 * exactly after stbcnt. It's a bad hack, but it works
		 */
		*offset = *((int *)(&t.stbcnt) + 1);
		return 0;
	} else {
		*leap59 = 0;
		*leap61 = 0;
		*offset = 0;
		return -1;
	}		
}

static int unix_time_set_utc_offset(int offset, int leap59, int leap61) 
{
	struct timex t;
    	int ret;
	
	unix_time_clear_utc_flags();
	
	/* get the current flags first */
	memset(&t, 0, sizeof(t));
	ret = adjtimex(&t);
	
	if (ret >= 0) {
		if (leap59) {
			t.modes = MOD_STATUS;
			t.status |= STA_DEL;
			t.status &= ~STA_INS;
			printf("set leap59 flag\n");
		} else if (leap61) {
			t.modes = MOD_STATUS;
			t.status |= STA_INS;
			t.status &= ~STA_DEL;
			printf("set leap61 flag\n");
		} else {
			t.modes = MOD_STATUS;
			t.status &= ~STA_INS;
			t.status &= ~STA_DEL;
			printf("set no leap flags\n");
		}

	    if (adjtimex(&t) < 0) {
		    printf("set UTC flags failed\n");
		    return -1;
	    }

    } else
		printf("get UTC flags failed\n");
	
	t.modes = MOD_TAI;
	t.constant = offset;
	if (adjtimex(&t) < 0) {
		printf("set UTC offset failed\n");
		return -1;
	} else
		printf("set UTC offset to: %i\n", offset);
    
	
	return 0;
}

int main(int argc, char **argv)
{
	int i, j;
	int offset, leap59, leap61;
	int hours, minutes, seconds;
	struct timespec tp;
	
	for (j=0; j<3; j++) {

		/* Get the UTC time */
		unix_time_get_utc_time(&hours, &minutes, &seconds);
		unix_time_get_utc_offset(&offset, &leap59, &leap61);	
		printf("current time %d:%d:%d, offset=%d, l59=%d, l61=%d\n", hours, minutes, seconds, offset, leap59, leap61);


		/* Set time to 30. June 2017 23:59:50 */
		tp.tv_sec = 1498867190;
		tp.tv_nsec = 0;
		clock_settime(CLOCK_REALTIME, &tp);


		/* Get the UTC time */
		unix_time_get_utc_time(&hours, &minutes, &seconds);
		unix_time_get_utc_offset(&offset, &leap59, &leap61);	
		printf("new current time %d:%d:%d, offset=%d, l59=%d, l61=%d\n", hours, minutes, seconds, offset, leap59, leap61);


		if (j == 0) {
			/* Set leap61 and UTC offset to 37 */
			offset = 37;
			leap59 = 0;
			leap61 = 1;		
			unix_time_set_utc_offset(offset, leap59, leap61);
		} else if (j == 1) {
			/* Set leap59 and UTC offset to 37 */
			offset = 37;
			leap59 = 1;
			leap61 = 0;		
			unix_time_set_utc_offset(offset, leap59, leap61);
		} else {
			/* Set no leaps and UTC offset to 37 */
			offset = 37;
			leap59 = 0;
			leap61 = 0;		
			unix_time_set_utc_offset(offset, leap59, leap61);
		}			

		if (j == 0) {
			if ((hours == 23) && (minutes == 59) && (seconds == 50) && (offset == 37) && (leap59 == 0) && (leap61 == 1)) {
				printf("pre leap61 handling correct\n");
			} else {
				printf("pre leap61 handling failed\n");
				return -1;
			}				
		} else if (j == 1) {
			if ((hours == 23) && (minutes == 59) && (seconds == 50) && (offset == 37) && (leap59 == 1) && (leap61 == 0)) {
				printf("pre leap59 handling correct\n");
			} else {
				printf("pre leap59 handling failed\n");
				return -2;
			}
		} else {
			if ((hours == 23) && (minutes == 59) && (seconds == 50) && (offset == 37) && (leap59 == 0) && (leap61 == 0)) {
				printf("pre no-leap handling correct\n");
			} else {
				printf("pre no-leap handling failed\n");
				return -3;
			}
		}	
		
		for (i=0; i< 20; i++) {
			/* Get the UTC time */
			unix_time_get_utc_offset(&offset, &leap59, &leap61);	
			unix_time_get_utc_time(&hours, &minutes, &seconds);
			printf("new current time after offset and leap %d:%d:%d, offset=%d, l59=%d, l61=%d\n", hours, minutes, seconds, offset, leap59, leap61);
			sleep(1);
		}
		
		unix_time_get_utc_offset(&offset, &leap59, &leap61);	
		unix_time_get_utc_time(&hours, &minutes, &seconds);
		
		if (j == 0) {
			if ((hours == 0) && (minutes == 0) && (seconds == 9) && (offset == 38) && (leap59 == 0) && (leap61 == 0)) {
				printf("post leap61 handling correct\n\n");
			} else {
				printf("post leap61 handling failed\n\n");
				return -1;
			}				
		} else if (j == 1) {
			if ((hours == 0) && (minutes == 0) && (seconds == 11) && (offset == 36) && (leap59 == 0) && (leap61 == 0)) {
				printf("post leap59 handling correct\n\n");
			} else {
				printf("post leap59 handling failed\n\n");
				return -2;
			}
		} else {
			if ((hours == 0) && (minutes == 0) && (seconds == 10) && (offset == 37) && (leap59 == 0) && (leap61 == 0)) {
				printf("post no-leap handling correct\n\n");
			} else {
				printf("post no-leap handling failed\n\n");
				return -3;
			}
		}				
	}

	printf("all leap handling tests passed\n\n");
	return 0;	
}
