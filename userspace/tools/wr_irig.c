#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <libwr/wrs-msg.h>
#include "wr_irig.h"

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

    uint32_t days = (day_raw & 0x0f);
    uint32_t yrs = (yr_raw & 0x0f);
    uint32_t mon = 0;

    const int m_to_d[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304,
			     334};
    int i = 0;
    for (i = 0; i < 6; i++) {
	if ((day_raw >> 5) & 1 << i) {
	    if (i >= 4) {
		days += 100 * (1 << (i - 4));
	    } else {
		days += 10 * (1 << i);
	    }
	}
    }

    //fixme
    days -= 1;

    for (i = 0; i < 4; i++) {
	if ((yr_raw >> 5) & (1 << i)){
	yrs += 10 * (1 << i);
	}
    }

    yrs += 2000;

    for (i = 0; i < 11; i++){
	if (days >= m_to_d[i] && days < m_to_d[i + 1]) {
	    mon = i+1;
	    days -= m_to_d[i];
	}
    }

    if (valid){
	t->day = days;
	t->mon = mon;
	t->year = yrs;
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
    short month, year;
    int64_t result;
    const int m_to_d[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304,
			     334 };

    month = t->mon;
    year = t->year + month / 12 + 1900;
    month %= 12;
    if (month < 0) {
	year -= 1;
	month += 12;
    }
    result = (year - 1970) * 365 + (year - 1969) / 4 + m_to_d[month];
    result = (year - 1970) * 365 + m_to_d[month];

    if (month <= 1)
	year -= 1;

    result += (year - 1968) / 4;
    result -= (year - 1900) / 100;
    result += (year - 1600) / 400;
    result += t->day;
    result -= 1;
    result *= 24;
    result += t->hour;
    result *= 60;
    result += t->min;
    result *= 60;
    result += t->sec;

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

int irig_enable(struct wr_irig *wr_irig, int en) {

    wr_irig->irig->CR = (en ? IRIG_SLAVE_CR_ENABLE : 0);
    return 0;
}

int irig_enable_status(struct wr_irig *wr_irig)
{
    return !!(wr_irig->irig->CR & IRIG_SLAVE_CR_ENABLE);
}
