#ifndef WRS_CURRENT_TIME_GROUP_H
#define WRS_CURRENT_TIME_GROUP_H

#define WRSCURRENTTIME_TAI_CACHE_TIMEOUT 5
#define WRSCURRENTTIME_OID WRS_OID, 7, 1, 1

#define WRS_LEAP_SECOND_SOURCE_URL_LEN 128

#define WRS_LEAP_SEC_SOURCE_ERROR                  		1		/* Error detected */
#define WRS_LEAP_SEC_SOURCE_IO_ERROR	     			2		/* Cannot acces local file */
#define WRS_LEAP_SEC_SOURCE_LOCAL                  		3       /* The local leap seconds file is used*/
#define WRS_LEAP_SEC_SOURCE_TRY_REMOTE             		4		/* Try to download the file (No errors) */
#define WRS_LEAP_SEC_SOURCE_FORCE_REMOTE           		5       /* Try to download the file (propagate errors) */

#define WRS_SYSTEM_CLOCK_STATUS_DETAILS_OK         		1		/* ok */
#define WRS_SYSTEM_CLOCK_STATUS_DETAILS_THRESHOLD_EXCEEDED 	\
														2		/* Threshold exceeded */
#define WRS_SYSTEM_CLOCK_STATUS_DETAILS_NTP_ERROR  		3		/* Error accessing NTP server */
#define WRS_SYSTEM_CLOCK_STATUS_DETAILS_ERROR           4       /* Generic error */
#define WRS_SYSTEM_CLOCK_STATUS_DETAILS_IO_ERROR        5		/* Error: Status file is missing */
#define WRS_SYSTEM_CLOCK_STATUS_DETAILS_UNKNOWN    		6		/* Error: Unknown status */
#define WRS_SYSTEM_CLOCK_STATUS_DETAILS_NMEA_ERROR		7		/* Error getting NMEA data */
#define WRS_SYSTEM_CLOCK_STATUS_DETAILS_IRIGB_ERROR		8		/* Error getting IRIG-B data */

#define WRS_SYSTEM_CLOCK_CHECK_INTERVAL_UNIT_ERROR      1		/* ok */
#define WRS_SYSTEM_CLOCK_CHECK_INTERVAL_UNIT_MINUTES    2		/* Minutes */
#define WRS_SYSTEM_CLOCK_CHECK_INTERVAL_UNIT_HOURS    	3		/* Hours */
#define WRS_SYSTEM_CLOCK_CHECK_INTERVAL_UNIT_DAYS       4 	    /* Days */

#define WRS_LEAP_SEC_STATUS_DETAILS_OK                 1		/* Everything ok */
#define WRS_LEAP_SEC_STATUS_DETAILS_IO_ERROR           2		/* Status file is missing */
#define WRS_LEAP_SEC_STATUS_DETAILS_UNKNOWN            3		/* Unknown status */
#define WRS_LEAP_SEC_STATUS_DETAILS_FILE_EXPIRED       4		/* The current leap second file is out-dated */
#define WRS_LEAP_SEC_STATUS_DETAILS_INTERNAL_ERROR     5		/* Threshold exceeded */
#define WRS_LEAP_SEC_STATUS_DETAILS_TAI_READ_ERROR     6		/* Cannot read the TAI time */
#define WRS_LEAP_SEC_STATUS_DETAILS_SEC_INSERTED       7		/* A leap second will be inserted at 00:00*/
#define WRS_LEAP_SEC_STATUS_DETAILS_SEC_DELETED        8		/* A leap second will be deleted at 00:00*/

#define WRS_LEAP_SEC_SRC_STATUS_DETAILS_OK                 1		/* Everything ok  */
#define WRS_LEAP_SEC_SRC_STATUS_DETAILS_IO_ERROR           2		/* Status file is missing */
#define WRS_LEAP_SEC_SRC_STATUS_DETAILS_UNKNOWN            3		/* Unknown status */
#define WRS_LEAP_SEC_SRC_STATUS_DETAILS_UPDATED            4        /* Local leap seconds file has been updated */
#define WRS_LEAP_SEC_SRC_STATUS_DETAILS_DHCP_ERROR         5		/* DHCP error detected */
#define WRS_LEAP_SEC_SRC_STATUS_DETAILS_INVALID_URL        6        /* The URL is not reachable or invalid */
#define WRS_LEAP_SEC_SRC_STATUS_DETAILS_INVALID_FILE       7        /* The download file is invalid */
#define WRS_LEAP_SEC_SRC_STATUS_DETAILS_DOWNLOAD_ERROR     8        /* Error detected during the download */

struct wrsCurrentTime_s {
	uint64_t wrsDateTAI;		/* current time in TAI */
	char wrsDateTAIString[32];	/* current time in TAI as string */
	int wrsSystemClockStatusDetails;   /* System clock status details*/
	int wrsSystemClockDrift;    /* Current system clock drift value */
	int wrsSystemClockDriftThreshold;    /* System clock drift threshold*/
	int wrsSystemClockCheckInterval;    /* System clock check interval */
	int wrsSystemClockCheckIntervalUnit;    /* System clock check interval unit */
	int wrsLeapSecStatusDetails; /* Leap seconds details */
	int wrsLeapSecSourceStatusDetails; /* Leap second source status details*/
	int wrsLeapSecSource;       /* Source of the leap seconds file */
	char wrsLeapSecSourceUrl[WRS_LEAP_SECOND_SOURCE_URL_LEN + 1]; /* URL to download leap second file */
	int wrsSystemClockDriftUs;    /* Current system clock drift value in us */
};

extern struct wrsCurrentTime_s wrsCurrentTime_s;
time_t wrsCurrentTime_data_fill(void);

void init_wrsCurrentTimeGroup(void);
#endif /* WRS_CURRENT_TIME_GROUP_H */
