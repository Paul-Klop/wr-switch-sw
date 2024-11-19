#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <libwr/wrs-msg.h>


#include "serial.h"
#include "wr_nmea.h"


static nmea_gpzda_t nmea_gpzda;
static nmea_gprmc_t nmea_gprmc;

int nmea_init(struct wr_nmea *nmea, char *dev, int baud, char *fmt)
{
	nmea->dev = dev;
	nmea->baud = baud;

	if (strcmp(fmt, "GPZDA") == 0){
		nmea->fmt = fmt;
		nmea->parse = nmea_parse_gpzda;
		nmea->utc = &(nmea_gpzda.utc);
	} else if (strcmp(fmt, "GPRMC") == 0){
		nmea->fmt = fmt;
		nmea->parse = nmea_parse_gprmc;
		nmea->utc = &(nmea_gprmc.utc);
	} else {
		pr_error("%s unsupported format\n", __func__);
	}
	return 0;
}

static int read_nmea_msg(char *msgbuf, int len)
{
	int i = 0;
	char c;
	unsigned int nmea_timeout_max = 10;
	unsigned int nmea_timeout;

	while (1) {
		if (nmea_timeout_max <= 0) {
			/* Timeout */
			return -1;
		}

		nmea_timeout = 1;
		c = serial_read_byte_w_timeout(&nmea_timeout);
		if (nmea_timeout == 0) {
			nmea_timeout_max--;
			continue;
		}

		/* Start of message found */
		if (c == '$')
		    break;
	}

	/* Copy start of message ('$') */
	*msgbuf++ = c;

	while (1) {
		if (nmea_timeout_max <= 0) {
			/* Timeout */
			return -1;
		}

		nmea_timeout = 1;
		c = serial_read_byte_w_timeout(&nmea_timeout);
		if (nmea_timeout == 0) {
			nmea_timeout_max--;
			continue;
		}

		if (c == '\r')
			break;
		i++;
		if (i >= len)
			break;
		*msgbuf++ = c;
	}

	*msgbuf++ = '\r';
	*msgbuf++ = '\n';

	*msgbuf++ = 0;

	return 0;
}

int read_nmea_msg_type(char *msgbuf, int len, const char *msg_type)
{
	do {
		if (read_nmea_msg(msgbuf, len) < 0)
			return -1;
	} while (strncmp(&msgbuf[1], msg_type, 5) != 0); //ignore starting "$"

	return 0;
}

int nmea_read_utc(struct wr_nmea *nmea, int64_t *t_out)
{
    char buf[1024];

    serial_open(nmea->dev, nmea->baud);
    if (read_nmea_msg_type(buf, 1024, nmea->fmt) < 0)
	return -1;
    serial_close();

    /* NMEA time is provided as UTC */
    if(nmea->parse(buf, strlen(buf), (nmea->utc)) < 0)
	return -2;

    pr_info("NMEA time: %d/%d/%d %02d:%02d:%02d.%02d\n",
	    nmea->utc->year+1900,
	    nmea->utc->mon+1,
	    nmea->utc->day,
	    nmea->utc->hour,
	    nmea->utc->min,
	    nmea->utc->sec,
	    nmea->utc->hsec);

    *t_out = utc_time_to_utc_seconds(*(nmea->utc));

    return 0;
}
