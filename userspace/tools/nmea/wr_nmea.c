#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

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
		fprintf(stderr, "NMEA: unsupported format %s\n", fmt);
	}
	return 0;
}

static int read_nmea_msg(char *msgbuf, int len)
{
	char c;
	unsigned int nmea_timeout_max = 10;
	unsigned int nmea_timeout;
	char *msgbuf_start = msgbuf;

	/* Make sure there is enought room for \r, \n \0 at the end */
	len -= 3;

	while (1) {
		if (nmea_timeout_max <= 0) {
			/* Timeout */
			return -1;
		}

		nmea_timeout = 2;
		c = serial_read_byte_w_timeout(&nmea_timeout);
		if (nmea_timeout == 0) {
			nmea_timeout_max--;
			fprintf(stderr, "NMEA: receive timeout\n");
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

		nmea_timeout = 2;
		c = serial_read_byte_w_timeout(&nmea_timeout);
		if (nmea_timeout == 0) {
			nmea_timeout_max--;
			fprintf(stderr, "NMEA: receive timeout\n");
			continue;
		}

		if (c == '\r')
			break;

		if (msgbuf - msgbuf_start >= len)
			break;

		*msgbuf++ = c;
	}

	*msgbuf++ = '\r';
	*msgbuf++ = '\n';

	*msgbuf++ = 0;

	/* Return message length, don't count null char at the end */
	return msgbuf - msgbuf_start - 1;
}

int read_nmea_msg_type(char *msgbuf, int len, const char *msg_type)
{
	int ret;
	do {
		ret = read_nmea_msg(msgbuf, len);
		if (ret < 0)
			return -1;

	/* ignore starting "$" */
	} while (strncmp(&msgbuf[1], msg_type, strlen(msg_type)) != 0);

	/* Return message length */
	return ret;
}

int nmea_read_utc(struct wr_nmea *nmea, int64_t *t_out)
{
    int ret;
    char buf[1024];

    serial_open(nmea->dev, nmea->baud);
    ret = read_nmea_msg_type(buf, 1024, nmea->fmt);
    if (ret < 0)
	return -1;
    serial_close();

    /* NMEA time is provided as UTC */
    if(nmea->parse(buf, strlen(buf), (nmea->utc)) < 0)
	return -2;

#if DEBUG
    printf("NMEA time: %d/%d/%d %02d:%02d:%02d.%02d\n",
	   nmea->utc->year+1900,
	   nmea->utc->mon+1,
	   nmea->utc->day,
	   nmea->utc->hour,
	   nmea->utc->min,
	   nmea->utc->sec,
	   nmea->utc->hsec);
#endif

    *t_out = utc_time_to_utc_seconds(*(nmea->utc));

    /* Return message length */
    return ret;
}
