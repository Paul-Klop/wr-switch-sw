#include <ppsi/ppsi.h>

/* Prototypes */
char * timeIntervalToString(TimeInterval time,char *buf);
char * timeToString(struct pp_time *time,char *buf);
char * timestampToString(struct Timestamp *time,char *buf);
char * relativeDifferenceToString(RelativeDifference time, char *buf );
int timeval_subtract(struct timeval *result, struct timeval *x, struct timeval *y);
