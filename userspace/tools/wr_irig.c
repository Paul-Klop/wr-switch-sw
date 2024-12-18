#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <libwr/wrs-msg.h>
#include "wr_irig.h"

#define IRIGB_TIMEOUT_MS	10 * 1000

static int init_alarm_done = 0;
static volatile int  alarmDetected = 0;
static timer_t timer_irigb;

static void sched_handler(int sig, siginfo_t *si, void *uc)
{
    alarmDetected = 1;
}

static void init_alarm(timer_t *timerid)
{
    struct sigevent sev;
    struct sigaction sa;

    /* Set the signal handler */
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = sched_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGALRM, &sa, NULL) == -1) {
	fprintf(stderr, "wr_irigb: cannot set signal handler\n");
	exit(1);
    }

    /* Create the timer */
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGALRM;
    sev.sigev_value.sival_ptr = timerid;
    if (timer_create(CLOCK_MONOTONIC, &sev, timerid) == -1) {
	fprintf(stderr, "wr_irig: Cannot create timer\n");
	exit(1);
    }
}

static void start_alarm(timer_t *timerid, unsigned int delay_ms)
{
    struct itimerspec its;

    its.it_value.tv_sec = delay_ms/1000;
    its.it_value.tv_nsec = (delay_ms%1000) * 1000000;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;

    if (timer_settime(*timerid, 0, &its, NULL) == -1){
	fprintf(stderr, "wr_irig: Cannot start timer. DelayMs=%u. Errno=%d\n",delay_ms, errno);
    }
}

static unsigned int stop_alarm(timer_t *timerid)
{
    struct itimerspec its;
    struct itimerspec ito;

    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;

    if (timer_settime(*timerid, 0, &its, &ito) == -1){
	fprintf(stderr, "wr_irig: Cannot stop timer\n");
	return 0;
    }
    return (int) (ito.it_value.tv_sec*1000+ito.it_value.tv_nsec/1000000);
}


static int irig_get_time(struct irig_slave *irig, struct irig_time *t)
{
    uint32_t tod = irig->TOD;
    uint32_t sec_raw = (tod & IRIG_SLAVE_TOD_SECONDS_MASK) >> IRIG_SLAVE_TOD_SECONDS_SHIFT;
    uint32_t min_raw = (tod & IRIG_SLAVE_TOD_MINUTES_MASK) >> IRIG_SLAVE_TOD_MINUTES_SHIFT;
    uint32_t hr_raw  = (tod & IRIG_SLAVE_TOD_HOURS_MASK) >> IRIG_SLAVE_TOD_HOURS_SHIFT;

    uint32_t valid = (tod & IRIG_SLAVE_TOD_VALID);

    uint32_t secs = (sec_raw & 0x0f);
    uint32_t mins = (min_raw & 0x0f);
    uint32_t hrs = (hr_raw & 0x0f);

    int i;

    for (i = 0; i < 3; i++){
	if ((sec_raw >> 5) & (1 << i)) {
	    secs += 10*(1<<i);
	}

	if (((min_raw & 0xe0) >> 5) & (1 << i)) {
	    mins += 10*(1<<i);
	}

	if(((hr_raw & 0x60) >> 5) & 1<<i) {
	hrs += 10*(1<<i);
	}
    }

    if (valid) {
	t->sec = secs;
	t->min = mins;
	t->hour = hrs;
	return 0;
    }

    return -1;
}

static int irig_get_date(struct irig_slave *irig, struct irig_time *t){

    uint32_t date = irig->DATE;
    uint32_t day_raw = (date & IRIG_SLAVE_DATE_DAYS_MASK) >> IRIG_SLAVE_DATE_DAYS_SHIFT;
    uint32_t yr_raw = (date & IRIG_SLAVE_DATE_YEARS_MASK) >> IRIG_SLAVE_DATE_YEARS_SHIFT;
    uint32_t valid = (date & IRIG_SLAVE_DATE_VALID);

    uint32_t ydays = (day_raw & 0x0f);
    uint32_t yrs = (yr_raw & 0x0f);

    struct tm t0;

    int ret;
    int i = 0;
    for (i = 0; i < 6; i++) {
	if ((day_raw >> 5) & 1 << i) {
	    if (i >= 4) {
		ydays += 100 * (1 << (i - 4));
	    } else {
		ydays += 10 * (1 << i);
	    }
	}
    }

    for (i = 0; i < 4; i++) {
	if ((yr_raw >> 5) & (1 << i)){
	yrs += 10 * (1 << i);
	}
    }

    memset(&t0, 0, sizeof(struct tm));
    t0.tm_isdst = -1;
    /* mktime counts year from 1900, irigb counts from 2000 */
    t0.tm_year = yrs + 100;
    /* Give days in a year as days in a month (tm_mday) to mktime to give
     * month/day */
    t0.tm_mday = ydays;
    ret = mktime(&t0);

    if (valid && ret > 0){
	t->day = t0.tm_mday;
	t->mon = t0.tm_mon + 1; /* t0.tm_mon is 0..11 */
	t->year = t0.tm_year + 1900;
	return 0;
    }

    return -1;
}

static int irig_get_sbs(struct irig_slave *irig, struct irig_time *t)
{
    uint32_t sbs = irig->SBS;
    uint32_t valid = (sbs & IRIG_SLAVE_SBS_VALID);
    uint32_t hrs;
    uint32_t mins;
    uint32_t secs;

    sbs = (sbs & IRIG_SLAVE_SBS_SBS_MASK) >> IRIG_SLAVE_SBS_SBS_SHIFT;

    hrs = sbs / 3600;
    mins = (sbs / 60) % 60;
    secs = sbs % 60;

    if (valid) {
	t->sec = secs;
	t->min = mins;
	t->hour = hrs;
	return 0;
    }
    return -1;
}

static int64_t irig_time_to_seconds(struct irig_time *t)
{
    int64_t result;
    struct tm t0;

    memset(&t0, 0, sizeof(struct tm));
    t0.tm_isdst = -1;
    t0.tm_year = t->year;
    t0.tm_mon = t->mon;
    t0.tm_mday = t->day;
    t0.tm_hour = t->hour;
    t0.tm_min = t->min;
    t0.tm_sec = t->sec;
    result = mktime(&t0);

    return result;
}

int irig_read_utc(struct wr_irig *wr_irig, int64_t *t_out)
{
    struct irig_time t0;
    struct irig_time t1;

    if (irig_get_time(wr_irig->irig, &(wr_irig->t)) < 0){
	return -1;
    }
    if (irig_get_date(wr_irig->irig, &(wr_irig->t)) < 0){
	return -1;
    }
    if (irig_get_sbs(wr_irig->irig, &t1) < 0){
	return -1;
    }
    //sanity check, time vs sbs
    if ((wr_irig->t.sec != t1.sec)
	|| (wr_irig->t.min != t1.min)
	|| (wr_irig->t.hour != t1.hour)
    ) {
	return -1;
    }

#if DEBUG
    printf("IRIG time: %d/%d/%d %02d:%02d:%02d\n",
	   wr_irig->t.year, wr_irig->t.mon, wr_irig->t.day,
	   wr_irig->t.hour, wr_irig->t.min, wr_irig->t.sec);
#endif

    t0 = wr_irig->t;
    t0.year -= 1900;
    t0.mon  -= 1;

    *t_out = irig_time_to_seconds(&t0);
    return 0;
}

int irig_wait_sec_transition(volatile struct wr_irig *wr_irig)
{
    uint32_t new_tod;
    uint32_t ref_tod = 0;

    uint32_t valid = 0;

    if (!init_alarm_done) {
	init_alarm(&timer_irigb);
	init_alarm_done = 1;
    }

    start_alarm(&timer_irigb, IRIGB_TIMEOUT_MS);
    /* Wait until get valid TOD */
    while (!valid && !alarmDetected) {
        ref_tod = wr_irig->irig->TOD;
        valid = (ref_tod & IRIG_SLAVE_TOD_VALID);
    }

    if (alarmDetected) {
	printf("Timeout on waiting for valid IRIG-B signal\n");
	return -1;
    }

    alarmDetected = 0;
    start_alarm(&timer_irigb, IRIGB_TIMEOUT_MS);
    valid = 0;
    new_tod = ref_tod;
    while (!valid || (new_tod & IRIG_SLAVE_TOD_SECONDS_MASK) == (ref_tod & IRIG_SLAVE_TOD_SECONDS_MASK)) {
        new_tod = wr_irig->irig->TOD;
        valid = (new_tod & IRIG_SLAVE_TOD_VALID);
    }

    stop_alarm(&timer_irigb);

    if (alarmDetected) {
	printf("Timeout on waiting for new IRIG-B second\n");
	return -2;
    }

    return 0;
}

int irig_enable(struct wr_irig *wr_irig, int en) {

    wr_irig->irig->CR = (en ? IRIG_SLAVE_CR_ENABLE : 0);
    return 0;
}

int irig_enable_status(struct wr_irig *wr_irig)
{
    return !!(wr_irig->irig->CR & IRIG_SLAVE_CR_ENABLE);
}
