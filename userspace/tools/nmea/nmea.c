/*
 *
 * NMEA library
 * URL: http://nmea.sourceforge.net
 * Author: Tim (xtimor@gmail.com)
 * Licence: http://www.gnu.org/licenses/lgpl.html
 *
 * Modified for use in wrsw_hal by Tomasz Wlostowski/CERN
 */

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>

#include "nmea.h"

#define NMEA_TOKS_COMPARE   (1)
#define NMEA_TOKS_PERCENT   (2)
#define NMEA_TOKS_WIDTH     (3)
#define NMEA_TOKS_TYPE      (4)

#define NMEA_CONVSTR_BUF	128
#define NMEA_TIMEPARSE_BUF	128

/**
 * \brief Calculate control sum of binary buffer
 */
static int nmea_calc_crc(const char *buff, int buff_sz)
{
    int chsum = 0,
	it;

    for(it = 0; it < buff_sz; ++it)
	chsum ^= (int)buff[it];

    return chsum;
}

/**
 * \brief Convert string to number
 */
static int nmea_atoi(const char *str, int str_sz, int radix)
{
    char *tmp_ptr;
    char buff[NMEA_CONVSTR_BUF];
    int res = 0;

    if (str_sz < NMEA_CONVSTR_BUF) {
	memcpy(&buff[0], str, str_sz);
	buff[str_sz] = '\0';
	res = strtol(&buff[0], &tmp_ptr, radix);
    }

    return res;
}

/**
 * \brief Convert string to fraction number
 */
static double nmea_atof(const char *str, int str_sz)
{
    char *tmp_ptr;
    char buff[NMEA_CONVSTR_BUF];
    double res = 0;

    if (str_sz < NMEA_CONVSTR_BUF) {
	memcpy(&buff[0], str, str_sz);
	buff[str_sz] = '\0';
	res = strtod(&buff[0], &tmp_ptr);
    }

    return res;
}

/**
 * \brief Formating string (like standard printf) with CRC tail (*CRC)
 */
int nmea_printf(char *buff, int buff_sz, const char *format, ...)
{
    int retval, add = 0;
    va_list arg_ptr;

    if (buff_sz <= 0)
	return 0;

    va_start(arg_ptr, format);

    retval = vsnprintf(buff, buff_sz, format, arg_ptr);

    if (retval > 0) {
	add = snprintf(
	    buff + retval, buff_sz - retval, "*%02x\r\n",
	    nmea_calc_crc(buff + 1, retval - 1));
    }

    retval += add;

    if (retval < 0 || retval > buff_sz) {
	memset(buff, ' ', buff_sz);
	retval = buff_sz;
    }

    va_end(arg_ptr);

    return retval;
}

/**
 * \brief Analyse string (specificate for NMEA sentences)
 */
static int nmea_scanf(const char *buff, int buff_sz, const char *format, ...)
{
    const char *beg_tok;
    const char *end_buf = buff + buff_sz;

    va_list arg_ptr;
    int tok_type = NMEA_TOKS_COMPARE;
    int width = 0;
    const char *beg_fmt = 0;
    int snum = 0, unum = 0;

    int tok_count = 0;
    void *parg_target;

    va_start(arg_ptr, format);

    for(; *format && buff < end_buf; ++format) {
	switch(tok_type) {
	case NMEA_TOKS_COMPARE:
	    if ('%' == *format)
		tok_type = NMEA_TOKS_PERCENT;
	    else if (*buff++ != *format)
		goto fail;
	    break;
	case NMEA_TOKS_PERCENT:
	    width = 0;
	    beg_fmt = format;
	    tok_type = NMEA_TOKS_WIDTH;
	case NMEA_TOKS_WIDTH:
	    if (isdigit(*format))
		break;
	    {
		tok_type = NMEA_TOKS_TYPE;
		if (format > beg_fmt)
		    width = nmea_atoi(beg_fmt, (int)(format - beg_fmt), 10);
	    }
	case NMEA_TOKS_TYPE:
	    beg_tok = buff;

	    if (!width && ('c' == *format || 'C' == *format) && *buff != format[1])
		width = 1;

	    if (width) {
		if (buff + width <= end_buf)
		    buff += width;
		else
		    goto fail;
	    } else {
		if (!format[1] || (0 == (buff = (char *)memchr(buff, format[1], end_buf - buff))))
		    buff = end_buf;
	    }

	    if (buff > end_buf)
		goto fail;

	    tok_type = NMEA_TOKS_COMPARE;
	    tok_count++;

	    parg_target = 0; width = (int)(buff - beg_tok);

	    switch(*format) {
	    case 'c':
	    case 'C':
		parg_target = (void *)va_arg(arg_ptr, char *);
		if (width && 0 != (parg_target))
		    *((char *)parg_target) = *beg_tok;
		break;
	    case 's':
	    case 'S':
		parg_target = (void *)va_arg(arg_ptr, char *);
		if (width && 0 != (parg_target)) {
		    memcpy(parg_target, beg_tok, width);
		    ((char *)parg_target)[width] = '\0';
		}
		break;
	    case 'f':
	    case 'g':
	    case 'G':
	    case 'e':
	    case 'E':
		parg_target = (void *)va_arg(arg_ptr, double *);
		if (width && 0 != (parg_target))
		    *((double *)parg_target) = nmea_atof(beg_tok, width);
		break;
	    };

	    if (parg_target)
		break;
	    if (0 == (parg_target = (void *)va_arg(arg_ptr, int *)))
		break;
	    if (!width)
		break;

	    switch(*format) {
	    case 'd':
	    case 'i':
		snum = nmea_atoi(beg_tok, width, 10);
		memcpy(parg_target, &snum, sizeof(int));
		break;
	    case 'u':
		unum = nmea_atoi(beg_tok, width, 10);
		memcpy(parg_target, &unum, sizeof(unsigned int));
		break;
	    case 'x':
	    case 'X':
		unum = nmea_atoi(beg_tok, width, 16);
		memcpy(parg_target, &unum, sizeof(unsigned int));
		break;
	    case 'o':
		unum = nmea_atoi(beg_tok, width, 8);
		memcpy(parg_target, &unum, sizeof(unsigned int));
		break;
	    default:
		goto fail;
	    };

	    break;
	};
    }

fail:

    va_end(arg_ptr);

    return tok_count;
}

static int _nmea_parse_time(const char *buff, int buff_sz, nmea_time_t *res)
{
    int success = 0;

    switch(buff_sz) {
    case sizeof("hhmmss") - 1:
	success = (3 == nmea_scanf(buff, buff_sz,
	    "%2d%2d%2d", &(res->hour), &(res->min), &(res->sec)
	    ));
	break;
    case sizeof("hhmmss.s") - 1:
    case sizeof("hhmmss.ss") - 1:
    case sizeof("hhmmss.sss") - 1:
	success = (4 == nmea_scanf(buff, buff_sz,
	    "%2d%2d%2d.%d", &(res->hour), &(res->min), &(res->sec), &(res->hsec)
	    ));
	break;
    default:
	success = 0;
	break;
    }

    return (success?0:-1);
}

/**
 * \brief Parse RMC packet from buffer.
 * @param buff a constant character pointer of packet buffer.
 * @param buff_sz buffer size.
 * @param pack a pointer of packet which will filled by function.
 * @return 1 (true) - if parsed successfully or 0 (false) - if fail.
 */

int nmea_parse_gprmc(const char *buff, int buff_sz, nmea_time_t *utc)
{
    int nsen;
    char time_buff[NMEA_TIMEPARSE_BUF];

    nmea_gprmc_t *pack = (nmea_gprmc_t *)utc;

    assert(buff && pack);

    memset(pack, 0, sizeof(nmea_gprmc_t));

    nsen = nmea_scanf(buff, buff_sz,
	"$GPRMC,%s,%C,%f,%C,%f,%C,%f,%f,%2d%2d%2d,%f,%C,%C*",
	&(time_buff[0]),
	&(pack->status), &(pack->lat), &(pack->ns), &(pack->lon), &(pack->ew),
	&(pack->speed), &(pack->direction),
	&(pack->utc.day), &(pack->utc.mon), &(pack->utc.year),
	&(pack->declination), &(pack->declin_ew), &(pack->mode));

    if (nsen != 13 && nsen != 14)
    {
	return 0;
    }

    if (0 != _nmea_parse_time(&time_buff[0],
			     (int)strlen(&time_buff[0]),
			     &(pack->utc))
      ) {
	return 0;
    }

    if (pack->utc.year < 90)
	pack->utc.year += 100;
    pack->utc.mon -= 1;

    return 1;
}

/**
 * \brief Parse ZDA packet from buffer.
 * @param buff a constant character pointer of packet buffer.
 * @param buff_sz buffer size.
 * @param pack a pointer of packet which will filled by function.
 * @return 1 (true) - if parsed successfully or 0 (false) - if fail.
 */

int nmea_parse_gpzda(const char *buff, int buff_sz, nmea_time_t *utc)
{
    int nsen;
    char time_buff[NMEA_TIMEPARSE_BUF];

    nmea_gpzda_t *pack = (nmea_gpzda_t *)utc;

    assert(buff && pack);

    memset(pack, 0, sizeof(nmea_gpzda_t));

    nsen = nmea_scanf(buff, buff_sz,
		      "$GPZDA,%s,%2d,%2d,%4d,%2c,%2d*",
		      &(time_buff[0]), &(pack->day), &(pack->month),
		      &(pack->year), &(pack->lzd), &(pack->lzmd));

    if (nsen != 6) {
	return 0;
    }

    if (0 != _nmea_parse_time(&time_buff[0],
			      (int)strlen(&time_buff[0]),
			      &(pack->utc))
       ) {
	return 0;
    }

    //reformat inline with rmc
    pack->utc.year = pack->year-1900;
    if (pack->utc.year < 90)
	pack->utc.year += 100;

    pack->utc.mon = pack->month-1;
    pack->utc.day = pack->day;

    return 1;
}

int64_t nmea_time_to_tai(nmea_time_t t)
{
	short month, year;
	int64_t result;
	const int m_to_d[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

	month = t.mon;
	year = t.year + month / 12 + 1900;
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
	result += t.day;
	result -= 1;
	result *= 24;
	result += t.hour;
	result *= 60;
	result += t.min;
	result *= 60;
	result += t.sec;

	return(result + 19LL); /* TAI = GPS + 19 s */
}
