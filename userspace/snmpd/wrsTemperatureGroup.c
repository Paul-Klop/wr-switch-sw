#include "wrsSnmp.h"
#include "snmp_shmem.h"
#include "wrsTemperatureGroup.h"
#include <libwr/config.h>

static struct pickinfo wrsTemperature_pickinfo[] = {
	FIELD(wrsTemperature_s, ASN_INTEGER, wrsTempFPGA),
	FIELD(wrsTemperature_s, ASN_INTEGER, wrsTempPLL),
	FIELD(wrsTemperature_s, ASN_INTEGER, wrsTempPSL),
	FIELD(wrsTemperature_s, ASN_INTEGER, wrsTempPSR),
	FIELD(wrsTemperature_s, ASN_INTEGER, wrsTempThresholdFPGA),
	FIELD(wrsTemperature_s, ASN_INTEGER, wrsTempThresholdPLL),
	FIELD(wrsTemperature_s, ASN_INTEGER, wrsTempThresholdPSL),
	FIELD(wrsTemperature_s, ASN_INTEGER, wrsTempThresholdPSR),
};

struct wrsTemperature_s wrsTemperature_s;

static void get_thresholds(void)
{
	char *config_item;
	/* check wether config fields exist, atoi has to have valid string */
	config_item = libwr_cfg_get("SNMP_TEMP_THOLD_FPGA");
	if (config_item)
		wrsTemperature_s.wrsTempThresholdFPGA = atoi(config_item);
	config_item = libwr_cfg_get("SNMP_TEMP_THOLD_PLL");
	if (config_item)
		wrsTemperature_s.wrsTempThresholdPLL = atoi(config_item);
	config_item = libwr_cfg_get("SNMP_TEMP_THOLD_PSL");
	if (config_item)
		wrsTemperature_s.wrsTempThresholdPSL = atoi(config_item);
	config_item = libwr_cfg_get("SNMP_TEMP_THOLD_PSR");
	if (config_item)
		wrsTemperature_s.wrsTempThresholdPSR = atoi(config_item);
}

time_t wrsTemperature_data_fill(void)
{
	unsigned ii;
	unsigned retries = 0;
	static time_t time_update;
	time_t time_cur;
	static int first_run = 1;

	time_cur = get_monotonic_sec();
	if (time_update
	    && time_cur - time_update < WRSTEMPERATURE_CACHE_TIMEOUT) {
		/* cache not updated, return last update time */
		return time_update;
	}
	time_update = time_cur;

	if (first_run) {
		/* load thresholds only once */
		get_thresholds();
		first_run = 0;
	}

	if (!shmem_ready_hald()) {
		/* Unable to open shmem, return current time */
		snmp_log(LOG_ERR, "SNMP: " SL_ER
			" %s: Unable to read HAL's shmem\n", __func__);
		return time_update;
	}

	while (1) {
		ii = wrs_shm_seqbegin(hal_head);

		wrsTemperature_s.wrsTempFPGA = hal_shmem->temp.fpga >> 8;
		wrsTemperature_s.wrsTempPLL = hal_shmem->temp.pll >> 8;
		wrsTemperature_s.wrsTempPSL = hal_shmem->temp.psl >> 8;
		wrsTemperature_s.wrsTempPSR = hal_shmem->temp.psr >> 8;

		retries++;
		if (retries > 100) {
			snmp_log(LOG_ERR,  "SNMP: " SL_ER
			"%s: too many retries to read HAL\n", __func__);
			retries = 0;
			}
		if (!wrs_shm_seqretry(hal_head, ii))
			break; /* consistent read */
		usleep(1000);
	}
	/* there was an update, return current time */
	return time_update;
}

#define GT_OID WRSTEMPERATURE_OID
#define GT_PICKINFO wrsTemperature_pickinfo
#define GT_DATA_FILL_FUNC wrsTemperature_data_fill
#define GT_DATA_STRUCT wrsTemperature_s
#define GT_GROUP_NAME "wrsTemperatureGroup"
#define GT_INIT_FUNC init_wrsTemperatureGroup

#include "wrsGroupTemplate.h"
