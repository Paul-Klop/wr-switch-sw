#include <libwr/util.h>
#include <libwr/config.h>
#include "wrsSnmp.h"
#include "wrsCurrentTimeGroup.h"

/* defines for nic-hardware.h */
#define WR_SWITCH
#define WR_IS_NODE 0
#define WR_IS_SWITCH 1
#include "../../kernel/wr_nic/nic-hardware.h"
#include "../../kernel/wbgen-regs/ppsg-regs.h"


/* Macros for fscanf function to read line with maximum of "x" characters
 * without new line. Macro expands to something like: "%10[^\n]" */
#define LINE_READ_LEN_HELPER(x) "%"#x"[^\n]"
#define LINE_READ_LEN(x) LINE_READ_LEN_HELPER(x)

#define WRS_SYSTEMCLOCK_STATUS_CACHE_TIMEOUT 20 /* 20 seconds */
#define WRS_LEAPSEC_STATUS_CACHE_TIMEOUT    20 /* 20 seconds */
#define WRS_LEAPSEC_DOWNLOAD_CACHE_TIMEOUT    20 /* 20 seconds */

#define getStatusFromMapping(map, key) _getStatusFromMapping(map,ARRAY_SIZE(map),key)

typedef struct {
	char * key;
	int status;
}text_status_mapping_t;

/* Expected services */
typedef struct {
	const char *configKey;
	int enabled;	/* expected number of processes */
	const char *snmpObjectName;
}service_exp_t;

typedef enum {
	SRV_SYSTEM_CLOCK,
	SRV_MAX_SERVICES
}service;


static struct PPSG_WB *pps;

static struct pickinfo wrsCurrentTime_pickinfo[] = {
	FIELD(wrsCurrentTime_s, ASN_COUNTER64, wrsDateTAI),
	FIELD(wrsCurrentTime_s, ASN_OCTET_STR, wrsDateTAIString),
	FIELD(wrsCurrentTime_s, ASN_INTEGER, wrsSystemClockStatusDetails),
	FIELD(wrsCurrentTime_s, ASN_INTEGER, wrsSystemClockDrift),
	FIELD(wrsCurrentTime_s, ASN_INTEGER, wrsSystemClockDriftThreshold),
	FIELD(wrsCurrentTime_s, ASN_INTEGER, wrsSystemClockCheckInterval),
	FIELD(wrsCurrentTime_s, ASN_INTEGER, wrsSystemClockCheckIntervalUnit),
	FIELD(wrsCurrentTime_s, ASN_INTEGER, wrsLeapSecSource),
	FIELD(wrsCurrentTime_s, ASN_INTEGER, wrsLeapSecStatusDetails),
	FIELD(wrsCurrentTime_s, ASN_INTEGER, wrsLeapSecSourceStatusDetails),
	FIELD(wrsCurrentTime_s, ASN_OCTET_STR, wrsLeapSecSourceUrl),
	FIELD(wrsCurrentTime_s, ASN_INTEGER, wrsSystemClockDriftUs),
};

static service_exp_t services[]={
		[SRV_SYSTEM_CLOCK]
		 {.configKey="SNMP_SYSTEM_CLOCK_MONITOR_ENABLED",
			.enabled=-1,
			.snmpObjectName="wrsSystemClockStatus"
		 }
};


struct wrsCurrentTime_s wrsCurrentTime_s;

static char *wrsSystemClockStatusDetails_str = "wrsSystemClockStatusDetails";
static char *wrsSystemClockDrift_str = "wrsSystemClockDrift";
static char *wrsSystemClockDriftThreshold_str = "wrsSystemClockDriftThreshold";
static char *wrsSystemClockCheckInterval_str = "wrsSystemClockCheckInterval";
static char *wrsLeapSecStatusDetails_str = "wrsLeapSecStatus";
static char *wrsLeapSecSourceStatusDetails_str = "wrsLeapSecSourceStatusDetails";
static char *wrsLeapSecSource_str = "wrsLeapSecSource";
static char *wrsLeapSecSourceUrl_str = "wrsLeapSecSourceUrl";

static void get_TAI(void);
static void update_expected_services(void);
static int _getStatusFromMapping(text_status_mapping_t *map, int mapSize, const char *key);
static void get_wrsSystemClockStatusDetails(void);
static void get_wrsLeapSecondStatusDetails(void);
static void get_wrsLeapSecondSourceStatusDetails(void);


time_t wrsCurrentTime_data_fill(void)
{
	static time_t time_last_update;
	static time_t time_tai_update;
	static time_t time_system_clock; /* time when system clock data was updated */
	static time_t time_leap_sec; /* time when leap seconds data was updated */
	static time_t time_leap_sec_download; /* time when leap seconds download file data was updated */

	time_t time_cur= get_monotonic_sec();

	if (time_tai_update==0 ||  (time_cur - time_tai_update) >WRSCURRENTTIME_TAI_CACHE_TIMEOUT) {
		time_tai_update=time_last_update=time_cur;
		get_TAI();
	}

	/* Update System clock data every WRS_SYSTEMCLOCK_STATUS_CACHE_TIMEOUT seconds */
	if ( time_system_clock==0 || (time_cur-time_system_clock) > WRS_SYSTEMCLOCK_STATUS_CACHE_TIMEOUT ) {
		time_system_clock=time_last_update=time_cur;
		get_wrsSystemClockStatusDetails();
	}

	/* Update leap second data every WRS_LEAPSEC_STATUS_CACHE_TIMEOUT seconds */
	if ( time_leap_sec==0 || (time_cur-time_leap_sec) > WRS_LEAPSEC_STATUS_CACHE_TIMEOUT ) {
		time_leap_sec=time_last_update=time_cur;
		get_wrsLeapSecondStatusDetails();
	}

	/* Update leap second data every WRS_LEAPSEC_STATUS_CACHE_TIMEOUT seconds */
	if ( time_leap_sec_download==0 || (time_cur-time_leap_sec_download) > WRS_LEAPSEC_DOWNLOAD_CACHE_TIMEOUT ) {
		time_leap_sec_download=time_last_update=time_cur;
		get_wrsLeapSecondSourceStatusDetails();
	}


	/* Return last update time */
	return time_last_update;
}

static void get_TAI(void){
	unsigned long utch, utcl, tmp1, tmp2;
	time_t t;
	struct tm tm;
	uint64_t wrs_d_current_64;

	wrsCurrentTime_s.wrsDateTAI= 0;
	wrsCurrentTime_s.wrsDateTAIString[0]= 0;

	/* get TAI time from FPGA */

	if (!pps) /* first time, map the fpga space */
		pps = create_map(FPGA_BASE_PPSG, sizeof(*pps));

	if (!pps) {
		wrs_d_current_64 = 0;
		strcpy(wrsCurrentTime_s.wrsDateTAIString,
		       "0000-00-00-00:00:00 (failed)");
	} else {

		do {
			utch = pps->CNTR_UTCHI;
			utcl = pps->CNTR_UTCLO;
			tmp1 = pps->CNTR_UTCHI;
			tmp2 = pps->CNTR_UTCLO;
		} while ((tmp1 != utch) || (tmp2 != utcl));

		wrs_d_current_64 = (uint64_t)(utch) << 32 | utcl;
		wrsCurrentTime_s.wrsDateTAI = wrs_d_current_64;

		t = wrs_d_current_64;
		localtime_r(&t, &tm);
		strftime(wrsCurrentTime_s.wrsDateTAIString,
			 sizeof(wrsCurrentTime_s.wrsDateTAIString),
			 "%Y-%m-%d-%H:%M:%S", &tm);
	}
}

#define SYSTEMCLOCK_DIR "/tmp"
#define SYSTEMCLOCK_DRIFT SYSTEMCLOCK_DIR "/system_clock_monitor_drift"
#define SYSTEMCLOCK_STATUS SYSTEMCLOCK_DIR "/system_clock_monitor_status"



static text_status_mapping_t mapping_system_clock_monitor_status[]={
		{ "no_error", WRS_SYSTEM_CLOCK_STATUS_DETAILS_OK},
		{ "exceeded_threshold",WRS_SYSTEM_CLOCK_STATUS_DETAILS_THRESHOLD_EXCEEDED},
		{ "ntp_error",WRS_SYSTEM_CLOCK_STATUS_DETAILS_NTP_ERROR},
		{ "nmea_error", WRS_SYSTEM_CLOCK_STATUS_DETAILS_NMEA_ERROR},
		{ "irigb_error", WRS_SYSTEM_CLOCK_STATUS_DETAILS_IRIGB_ERROR},
};

static void get_wrsSystemClockStatusDetails(void){
	static int first_run=1;
	char buff[21]; /* 1 for null char */
	FILE *f;
	int status = 0;
	int drift = 0;
	int drift_us = 0;
	static int 	threshold=0, unit=0, checkInterval=0;

	update_expected_services();


	if (  services[SRV_SYSTEM_CLOCK].enabled ) {
		// Service enabled

		char * slog_obj_name = wrsSystemClockStatusDetails_str;
		if ((f= fopen(SYSTEMCLOCK_STATUS, "r"))!=NULL) {

			/* readline without newline */
			fscanf(f, LINE_READ_LEN(sizeof(buff)-1), buff);
			fclose(f);
			status =getStatusFromMapping(mapping_system_clock_monitor_status, buff);
			if ( status==0 ) {
				status = WRS_SYSTEM_CLOCK_STATUS_DETAILS_UNKNOWN;
				snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: invalid status (%s)\n",
					 slog_obj_name,buff);
			}
		} else {
			/* File not found, probably something else caused
			 * a problem */
			status = WRS_SYSTEM_CLOCK_STATUS_DETAILS_IO_ERROR;
			snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: failed to "
				 "open " SYSTEMCLOCK_STATUS "\n",slog_obj_name);
		}

		/* Read drift value */
		if ( status==WRS_SYSTEM_CLOCK_STATUS_DETAILS_THRESHOLD_EXCEEDED ||
				status == WRS_SYSTEM_CLOCK_STATUS_DETAILS_OK) {
			slog_obj_name = wrsSystemClockDrift_str;

			if ((f=fopen(SYSTEMCLOCK_DRIFT, "r"))!=NULL) {
				/* readline without newline */
				if (fscanf(f, "%d.%d", &drift, &drift_us) != 2) {
					snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: invalid "
						 "drift value in file " SYSTEMCLOCK_DRIFT "\n",slog_obj_name);
					drift = 0;
					drift_us = 0;
				}
				fclose(f);
			} else {
				/* File not found, probably something else caused
				 * a problem */
				snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: failed to "
					 "open " SYSTEMCLOCK_DRIFT "\n",slog_obj_name);
			}
		}

		// Read values depending of dot-config
		if (first_run) {
			char *config_item;

			// Threshold
			slog_obj_name  = wrsSystemClockDriftThreshold_str;

			config_item = libwr_cfg_get("SNMP_SYSTEM_CLOCK_DRIFT_THOLD");
			if (config_item) {
				threshold= atoi(config_item);
			} else {
				snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: failed to "
					 "read SNMP_SYSTEM_CLOCK_DRIFT_THOLD key in dot-config file\n",slog_obj_name);
			}

			// Check interval value and unit
			slog_obj_name  = wrsSystemClockCheckInterval_str;
			if ( (config_item =
					libwr_cfg_get("SNMP_SYSTEM_CLOCK_CHECK_INTERVAL_MINUTES"))!=NULL) {
				checkInterval=atoi(config_item);
				unit=WRS_SYSTEM_CLOCK_CHECK_INTERVAL_UNIT_MINUTES;
			} else if ( (config_item =
					libwr_cfg_get("SNMP_SYSTEM_CLOCK_CHECK_INTERVAL_HOURS"))!=NULL) {
				checkInterval=atoi(config_item);
				unit=WRS_SYSTEM_CLOCK_CHECK_INTERVAL_UNIT_HOURS;
			} else if ( (config_item =
					libwr_cfg_get("SNMP_SYSTEM_CLOCK_CHECK_INTERVAL_DAYS"))!=NULL) {
				checkInterval=atoi(config_item);
				unit=WRS_SYSTEM_CLOCK_CHECK_INTERVAL_UNIT_DAYS;
			} else {
				unit=WRS_SYSTEM_CLOCK_CHECK_INTERVAL_UNIT_ERROR;
				snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: failed to "
					 "read SNMP_SYSTEM_CLOCK_CHECK_INTERVAL_XXXX key in dot-config file\n",slog_obj_name);
			}

			first_run = 0;
		}

	} else {
		// System clock monitoring disabled
		status=WRS_SYSTEM_CLOCK_STATUS_DETAILS_OK;
	}
	wrsCurrentTime_s.wrsSystemClockStatusDetails = status;
	wrsCurrentTime_s.wrsSystemClockDrift = drift;
	wrsCurrentTime_s.wrsSystemClockDriftUs = int_saturate((int64_t)drift * 1000000 + (int64_t)drift_us);
	wrsCurrentTime_s.wrsSystemClockDriftThreshold=threshold;
	wrsCurrentTime_s.wrsSystemClockCheckInterval=checkInterval;
	wrsCurrentTime_s.wrsSystemClockCheckIntervalUnit=unit;
}


#define LEAPSEC_CHECK_DIR "/tmp"
#define LEAPSEC_CHECK LEAPSEC_CHECK_DIR "/leapseconds_check_status"

static text_status_mapping_t mapping_leap_sec_status[]={
		{ "no_changes",WRS_LEAP_SEC_STATUS_DETAILS_OK},
		{ "leap_sec_file_expired",WRS_LEAP_SEC_STATUS_DETAILS_FILE_EXPIRED},
		{ "error_detected",WRS_LEAP_SEC_STATUS_DETAILS_INTERNAL_ERROR},
		{ "tai_read_error",WRS_LEAP_SEC_STATUS_DETAILS_TAI_READ_ERROR},
		{ "leap_sec_inserted",WRS_LEAP_SEC_STATUS_DETAILS_SEC_INSERTED},
		{ "leap_sec_deleted",WRS_LEAP_SEC_STATUS_DETAILS_SEC_DELETED},
};


static void get_wrsLeapSecondStatusDetails(void){
	char buff[31]; /* 1 for null char */
	FILE *f;
	int check_status=0;

	char * slog_obj_name = wrsLeapSecStatusDetails_str;
	if ((f= fopen(LEAPSEC_CHECK, "r"))!=NULL) {

		/* readline without newline */
		fscanf(f, LINE_READ_LEN(sizeof(buff)-1), buff);
		fclose(f);
		check_status =getStatusFromMapping(mapping_leap_sec_status, buff);
		if ( check_status==0 ) {
			check_status=WRS_LEAP_SEC_STATUS_DETAILS_UNKNOWN;
			snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: invalid status (%s)\n",
				 slog_obj_name,buff);
		}
	} else {
		/* File not found, probably something else caused
		 * a problem */
		check_status = WRS_LEAP_SEC_STATUS_DETAILS_IO_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: failed to "
			 "open " LEAPSEC_CHECK "\n",slog_obj_name);
	}

	wrsCurrentTime_s.wrsLeapSecStatusDetails= check_status;
}

#define LEAPSEC_SOURCE_DIR "/tmp"
#define LEAPSEC_SOURCE_STATUS LEAPSEC_SOURCE_DIR "/leapseconds_download_status"
#define LEAPSEC_SOURCE        LEAPSEC_SOURCE_DIR "/leapseconds_download_source"
#define LEAPSEC_SOURCE_URL    LEAPSEC_SOURCE_DIR "/leapseconds_download_url"

static text_status_mapping_t mapping_leap_sec_src_status[]={
		{ "no_changes",WRS_LEAP_SEC_SRC_STATUS_DETAILS_OK},
		{ "dhcp_error",WRS_LEAP_SEC_SRC_STATUS_DETAILS_DHCP_ERROR},
		{ "invalid_url",WRS_LEAP_SEC_SRC_STATUS_DETAILS_INVALID_URL},
		{ "updated",WRS_LEAP_SEC_SRC_STATUS_DETAILS_UPDATED},
		{ "file_invalid",WRS_LEAP_SEC_SRC_STATUS_DETAILS_INVALID_FILE},
		{ "download_error",WRS_LEAP_SEC_SRC_STATUS_DETAILS_DOWNLOAD_ERROR},
};

static text_status_mapping_t mapping_leap_sec_src_mode[]={
		{ "try_remote",WRS_LEAP_SEC_SOURCE_TRY_REMOTE},
		{ "force_remote",WRS_LEAP_SEC_SOURCE_FORCE_REMOTE},
		{ "local",WRS_LEAP_SEC_SOURCE_LOCAL}
};

static void get_wrsLeapSecondSourceStatusDetails(void){
	char buff[WRS_LEAP_SECOND_SOURCE_URL_LEN]; /* 1 for null char */
	FILE *f;
	int check_status=0,source=0;
	char *srcUrl=NULL;

	char * slog_obj_name = wrsLeapSecSource_str;
	if ((f= fopen(LEAPSEC_SOURCE, "r"))!=NULL) {

		/* readline without newline */
		fscanf(f, LINE_READ_LEN(sizeof(buff)-1), buff);
		fclose(f);
		source =getStatusFromMapping(mapping_leap_sec_src_mode, buff);
		if ( source==0 ) {
			source=WRS_LEAP_SEC_SOURCE_ERROR;
			snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: invalid source (%s)\n",
				 slog_obj_name,buff);
		}
	} else {
		/* File not found, probably something else caused
		 * a problem */
		source = WRS_LEAP_SEC_SOURCE_IO_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: failed to "
			 "open " LEAPSEC_SOURCE "\n",slog_obj_name);
	}

	wrsCurrentTime_s.wrsLeapSecSource= source;

	// get the URL source if needed
	if ( source == WRS_LEAP_SEC_SOURCE_FORCE_REMOTE || source== WRS_LEAP_SEC_SOURCE_TRY_REMOTE ) {
		slog_obj_name = wrsLeapSecSourceUrl_str;
		if ((f= fopen(LEAPSEC_SOURCE_URL, "r"))!=NULL) {

			/* readline without newline */
			if ( fscanf(f, LINE_READ_LEN(WRS_LEAP_SECOND_SOURCE_URL_LEN), buff)==1 )
				srcUrl=buff;
			else
				buff[0]=0;
			fclose(f);
			if ( !srcUrl ) {
				snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: invalid file contents (%s)\n",
					 slog_obj_name,buff);
			}
		} else {
			/* File not found, probably something else caused
			 * a problem */
			snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: failed to "
				 "open " LEAPSEC_SOURCE_URL"\n",slog_obj_name);
		}

		// get the leap second source status
		slog_obj_name = wrsLeapSecSourceStatusDetails_str;
		if ((f= fopen(LEAPSEC_SOURCE_STATUS, "r"))!=NULL) {

			/* readline without newline */
			if ( fscanf(f, LINE_READ_LEN(sizeof(buff)-1), buff)==1)
				check_status =getStatusFromMapping(mapping_leap_sec_src_status, buff);
			else
				buff[0]=0;
			fclose(f);
			if ( check_status==0 ) {
				check_status=WRS_LEAP_SEC_SRC_STATUS_DETAILS_UNKNOWN;
				snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: invalid  status (%s)\n",
					 slog_obj_name,buff);
			}
		} else {
			/* File not found, probably something else caused
			 * a problem */
			check_status = WRS_LEAP_SEC_SRC_STATUS_DETAILS_IO_ERROR;
			snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: failed to "
				 "open " LEAPSEC_SOURCE_STATUS "\n",slog_obj_name);
		}
	} else {
		/* Local leapsecond file, no error */
		check_status = WRS_LEAP_SEC_SRC_STATUS_DETAILS_OK;
	}
	if (srcUrl==NULL)
		srcUrl="";
	strcpy(wrsCurrentTime_s.wrsLeapSecSourceUrl,srcUrl);
	wrsCurrentTime_s.wrsLeapSecSourceStatusDetails=check_status;
}

static void update_expected_services(void)
{
	static int run_once = 1;
	int i;

	/* Read the information about disabled daemons from dot-config only
	 * once. Another read makes no sense, because SNMP daemon reads
	 * dot-config only once at startup */
	if (!run_once)
		return;

	run_once = 0;

	for ( i=0 ; i < SRV_MAX_SERVICES; i++ ){
		service_exp_t *s=&services[i];

		if ( s->configKey!=NULL) {
			char *tmp;

			tmp = libwr_cfg_get((char*)s->configKey);
			if (tmp && !strncmp(tmp, "y",1)) {

				s->enabled=1;
				snmp_log(LOG_INFO, "SNMP: " SL_INFO " %s: "
					"CONFIG_%s=y in dot-config\n",
					 s->snmpObjectName, s->configKey);
			} else {
				s->enabled=0;
			}
		}
	}
}

static int _getStatusFromMapping(text_status_mapping_t *map, int mapSize, const char *key) {
	int i;
	for( i=0; i<mapSize; i++ ) {
		if (!strcmp(map->key,key)) {
			return map->status;
		}
		map++;
	}
	return 0;
}


#define GT_OID WRSCURRENTTIME_OID
#define GT_PICKINFO wrsCurrentTime_pickinfo
#define GT_DATA_FILL_FUNC wrsCurrentTime_data_fill
#define GT_DATA_STRUCT wrsCurrentTime_s
#define GT_GROUP_NAME "wrsCurrentTimeGroup"
#define GT_INIT_FUNC init_wrsCurrentTimeGroup

#include "wrsGroupTemplate.h"
