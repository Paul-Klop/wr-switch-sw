/**
 * Time to string conversion functions
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <ppsi/ppsi.h>

char * timeIntervalToString(TimeInterval time,char *buf) {

	int64_t sign,nanos,picos;

	if ( time<0 && time !=INT64_MIN) {
		sign=-1;
		time=-time;
	} else {
		sign=1;
	}
	nanos = time >> TIME_INTERVAL_FRACBITS;
	picos = (((time & TIME_INTERVAL_FRACMASK) * 1000) + TIME_INTERVAL_ROUNDING_VALUE ) >> TIME_INTERVAL_FRACBITS;
	sprintf(buf,"%" PRId64 ".%03" PRId64, sign*nanos,picos);
	return buf;
}

char * timeToString(struct pp_time *time, char *buf) {
	char sign = '+';
	int64_t scaled_nsecs = time->scaled_nsecs;
	int64_t secs = time->secs, nanos, picos;

	if (!is_incorrect(time)) {
		if (scaled_nsecs < 0 || secs < 0) {
			sign = '-';
			scaled_nsecs = -scaled_nsecs;
			secs = -secs;
		}
		nanos = scaled_nsecs >> TIME_FRACBITS;
		picos = ((scaled_nsecs & TIME_FRACMASK) * 1000 + TIME_ROUNDING_VALUE)
				>> TIME_INTERVAL_FRACBITS;
		sprintf(buf,"%c%" PRId64 ".%09" PRId64 "%03" PRId64,
			sign,secs,nanos,picos);
	} else {
		sprintf(buf, "--Incorrect--");
	}
	return buf;
}

char * timestampToString(struct Timestamp *time,char *buf){
		uint64_t sec=(time->secondsField.msb << sizeof(time->secondsField.msb)) + time->secondsField.lsb;
		sprintf(buf,"%" PRIu64 ".%09" PRIu32 "000",
				sec, (uint32_t)time->nanosecondsField);
		return buf;
}

char * relativeDifferenceToString(RelativeDifference time, char *buf ) {
    int32_t nsecs=time >> REL_DIFF_FRACBITS;
	uint64_t sub_yocto=0;
    int64_t fraction;
	uint64_t bitWeight=500000000000000000;
	uint64_t mask;


    fraction=time & REL_DIFF_FRACMASK;
	for (mask=(uint64_t) 1<< (REL_DIFF_FRACBITS-1);mask!=0; mask>>=1 ) {
		if ( mask & fraction )
			sub_yocto+=bitWeight;
		bitWeight/=2;
	}
	sprintf(buf,"%"PRId32".%018"PRIu64, nsecs, sub_yocto);
	return buf;
}
