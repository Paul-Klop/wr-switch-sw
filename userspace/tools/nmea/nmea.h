/*
 *
 * NMEA library
 * URL: http://nmea.sourceforge.net
 * Author: Tim (xtimor@gmail.com)
 * Licence: http://www.gnu.org/licenses/lgpl.html
 * $Id: time.h 4 2007-08-27 13:11:03Z xtimor $
 *
 * Stripped-down version for use in wrsw_hal by Tomasz Wlostowski/CERN.
 */

/*! \file */

#ifndef __NMEA_H__
#define __NMEA_H__

typedef struct 
{
    int     year;       /**< Years since 1900 */
    int     mon;        /**< Months since January - [0,11] */
    int     day;        /**< Day of the month - [1,31] */
    int     hour;       /**< Hours since midnight - [0,23] */
    int     min;        /**< Minutes after the hour - [0,59] */
    int     sec;        /**< Seconds after the minute - [0,59] */
    int     hsec;       /**< Hundredth part of second - [0,99] */

} nmea_time_t;

/**
 * RMC packet information structure (Recommended Minimum sentence C)
 */
typedef struct
{
    nmea_time_t utc;    /**< UTC of position */
    char    status;     /**< Status (A = active or V = void) */
    double  lat;        /**< Latitude in NDEG - [degree][min].[sec/60] */
    char    ns;         /**< [N]orth or [S]outh */
    double  lon;        /**< Longitude in NDEG - [degree][min].[sec/60] */
    char    ew;         /**< [E]ast or [W]est */
    double  speed;      /**< Speed over the ground in knots */
    double  direction;  /**< Track angle in degrees True */
    double  declination; /**< Magnetic variation degrees (Easterly var. subtracts from true course) */
    char    declin_ew;  /**< [E]ast or [W]est */
    char    mode;       /**< Mode indicator of fix type (A = autonomous, D = differential, E = estimated, N = not valid, S = simulator) */

} nmea_gprmc_t;

/**
 * ZDA packet information structure
 */
typedef struct
{
    nmea_time_t utc;    /**< UTC of position */
    int day;      /**< day, 01 to 31*/
    int month;    /**< month, 01 to 12*/
    int year;     /**< year */
    int lzd;      /**< local zone description, 00 to +/- 13 hours */
    int lzmd;     /**< local zone minutes description */
} nmea_gpzda_t;

int nmea_parse_gprmc(const char *buff, int buff_sz, nmea_time_t *utc);
int nmea_parse_gpzda(const char *buff, int buff_sz, nmea_time_t *utc);
int64_t nmea_time_to_tai(nmea_time_t t);

#endif /* __NMEA_H__ */
