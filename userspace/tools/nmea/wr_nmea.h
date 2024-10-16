#ifndef __WR_NMEA_H
#define __WR_NMEA_H

#include <time.h>
#include "nmea.h"

struct wr_nmea {
    nmea_time_t *utc;
    char *dev;
    int baud;
    char *fmt;
    int (*parse)(const char *buff, int buff_sz, nmea_time_t *pack);
};

int nmea_init(struct wr_nmea *nmea, char *dev, int baud, char *fmt);
int nmea_read_tai(struct wr_nmea *nmea, int64_t *t_out);
#endif
