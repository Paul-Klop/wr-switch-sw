#include "wrsSnmp.h"
#include "wrsBootStatusGroup.h"
#include "wrsTemperatureGroup.h"
#include "wrsMemoryGroup.h"
#include "wrsCpuLoadGroup.h"
#include "wrsDiskTable.h"
#include "wrsVersionGroup.h"
#include "wrsOSStatusGroup.h"

#define WRSMEMORYFREELOW_TRESHOLD_ERROR 80
#define WRSMEMORYFREELOW_TRESHOLD_WARNING 50

/* To avoid float values for cpu load, they are multiplied by 100 */
#define WRSCPULOAD_1MIN_WARNING 200
#define WRSCPULOAD_5MIN_WARNING 150
#define WRSCPULOAD_15MIN_WARNING 100
#define WRSCPULOAD_1MIN_ERROR 300
#define WRSCPULOAD_5MIN_ERROR 200
#define WRSCPULOAD_15MIN_ERROR 150

#define WRSDISKSPACELOW_TRESHOLD_ERROR 90
#define WRSDISKSPACELOW_TRESHOLD_WARNING 80

static char *slog_obj_name;
static char *wrsBootSuccessful_str = "wrsBootSuccessful";
static char *wrsTemperatureWarning_str = "wrsTemperatureWarning";
static char *wrsMemoryFreeLow_str = "wrsMemoryFreeLow";
static char *wrsCpuLoadHigh_str = "wrsCpuLoadHigh";
static char *wrsDiskSpaceLow_str = "wrsDiskSpaceLow";


static struct pickinfo wrsOSStatus_pickinfo[] = {
	FIELD(wrsOSStatus_s, ASN_INTEGER, wrsBootSuccessful),
	FIELD(wrsOSStatus_s, ASN_INTEGER, wrsTemperatureWarning),
	FIELD(wrsOSStatus_s, ASN_INTEGER, wrsMemoryFreeLow),
	FIELD(wrsOSStatus_s, ASN_INTEGER, wrsCpuLoadHigh),
	FIELD(wrsOSStatus_s, ASN_INTEGER, wrsDiskSpaceLow),
};

struct wrsOSStatus_s wrsOSStatus_s;

time_t wrsOSStatus_data_fill(void)
{
	static time_t time_update; /* time of last update */
	time_t time_temp; /* time when temperature data was updated */
	time_t time_boot; /* time when boot data was updated */
	time_t time_free_mem; /* time when free memory data was updated */
	time_t time_cpu_load; /* time when cpu load data was updated */
	time_t time_disk_space; /* time when disk space data was updated */
	unsigned int n_rows_disk_space; /* number of rows in wrsDiskTable_array
					 */
	unsigned int i;
	struct wrsOSStatus_s *o;
	struct wrsBootStatus_s *b;
	struct wrsMemory_s *f;
	struct wrsCpuLoad_s *c;
	struct wrsDiskTable_s *d;
	struct wrsTemperature_s *t;
	struct wrsVersion_s *v;
	char *unknown = "UNKNOWN";

	time_boot = wrsBootStatus_data_fill();
	time_temp = wrsTemperature_data_fill();
	time_free_mem = wrsMemory_data_fill();
	time_cpu_load = wrsCpuLoad_data_fill();
	time_disk_space = wrsDiskTable_data_fill(&n_rows_disk_space);

	if (time_boot <= time_update
		&& time_temp <= time_update
		&& time_free_mem <= time_update
		&& time_cpu_load <= time_update
		&& time_disk_space <= time_update) {
		/* cache not updated, return last update time */
		return time_update;
	}
	time_update = get_monotonic_sec();

	memset(&wrsOSStatus_s, 0, sizeof(wrsOSStatus_s));

	o = &wrsOSStatus_s;
	/*********************************************************************\
	|************************* wrsBootSuccessful *************************|
	\*********************************************************************/

	slog_obj_name = wrsBootSuccessful_str;
	b = &wrsBootStatus_s;
	v = &wrsVersion_s;

	/* check if error */
	if (b->wrsBootCnt == 0) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: Unable to read wrsBootCnt\n",
			 slog_obj_name);
	}
	if (b->wrsRestartReason == WRS_RESTART_REASON_ERROR) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: Unable to read wrsRestartReason\n",
			 slog_obj_name);
	}
	if (b->wrsConfigSource == WRS_CONFIG_SOURCE_ERROR) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: Unknown config source in wrsConfigSource\n",
			 slog_obj_name);
	}
	if (b->wrsBootConfigStatus == WRS_CONFIG_STATUS_ERROR) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: Unable to read wrsBootConfigStatus\n",
			 slog_obj_name);
	}
	if (b->wrsBootConfigStatus == WRS_CONFIG_STATUS_DL_ERROR) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: Downloading of dot-config failed\n",
			 slog_obj_name);
	}
	if (b->wrsBootConfigStatus == WRS_CONFIG_STATUS_CHECK_ERROR) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: Config file is not valid or too short (less than 200 characters)\n",
			 slog_obj_name);
	}

	/* error only when dhcp failed for force_dhcp */
	if (b->wrsBootConfigStatus == WRS_CONFIG_STATUS_DHCP_ERROR
		    && b->wrsConfigSource == WRS_CONFIG_SOURCE_FORCE_DHCP) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
			 "Failed to get URL to dot-config via DHCP (wrsConfigSource is set to forceDhcp)\n",
			 slog_obj_name);
	}
	if (b->wrsBootHwinfoReadout == WRS_BOOT_HWINFO_ERROR) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: Hwinfo readout failed\n",
			 slog_obj_name);
	}
	if (b->wrsBootLoadFPGA == WRS_BOOT_LOAD_FPGA_ERROR) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: Failed to program FPGA\n",
			 slog_obj_name);
	}
	if (b->wrsBootLoadFPGA == WRS_BOOT_LOAD_FPGA_FILE_NOT_FOUND) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: FPGA bitstream not found\n",
			 slog_obj_name);
	}
	if (b->wrsBootLoadLM32 == WRS_BOOT_LOAD_LM32_ERROR) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: LM32 load failed\n",
			 slog_obj_name);
	}
	if (b->wrsBootLoadLM32 == WRS_BOOT_LOAD_LM32_FILE_NOT_FOUND) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: LM32 binary not found\n",
			 slog_obj_name);
	}
	/* check the number of missing modules */
	if (b->wrsBootKernelModulesMissing > 0) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: %d kernel modules not loaded\n",
			 slog_obj_name, b->wrsBootKernelModulesMissing);
	}
	/* check the number of missing daemons */
	if (b->wrsBootUserspaceDaemonsMissing > 0) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: wrong number of userspace daemons (differs by %d)\n",
			 slog_obj_name, b->wrsBootUserspaceDaemonsMissing);
	}

	/* custom boot script source error */
	if (b->wrsCustomBootScriptSource == WRS_CUSTOM_BOOT_SCRIPT_SOURCE_ERROR) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: unrecognized source of custom boot script\n",
			 slog_obj_name);
	}
	if (b->wrsCustomBootScriptStatus == WRS_CUSTOM_BOOT_SCRIPT_STATUS_FAILED) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: custom boot script failed during execution\n",
			 slog_obj_name);
	}
	if (b->wrsCustomBootScriptStatus == WRS_CUSTOM_BOOT_SCRIPT_STATUS_WRONG_SRC) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: unrecognized source of custom boot script\n",
			 slog_obj_name);
	}
	if (b->wrsCustomBootScriptStatus == WRS_CUSTOM_BOOT_SCRIPT_STATUS_DL_ERROR) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: error while downloading custom boot script\n",
			 slog_obj_name);
	}
	if (b->wrsCustomBootScriptStatus == WRS_CUSTOM_BOOT_SCRIPT_STATUS_ERROR) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: error in status file of custom boot script execution\n",
			 slog_obj_name);
	}
	if (b->wrsCustomBootScriptSource == WRS_CUSTOM_BOOT_SCRIPT_SOURCE_REMOTE
	    && strnlen(b->wrsCustomBootScriptSourceUrl, WRS_CUSTOM_BOOT_SCRIPT_SOURCE_URL_LEN) == 0) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: empty URL for custom boot script\n",
			 slog_obj_name);
	}

	if (b->wrsAuxClkSetStatus == WRS_AUXCLK_SET_STATUS_FAILED) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: setting up auxclk on connector clk2 failed\n",
			 slog_obj_name);
	}
	if (b->wrsAuxClkSetStatus == WRS_AUXCLK_SET_STATUS_ERROR) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: error in status file of setting up auxclk on connector clk2\n",
			 slog_obj_name);
	}

	if (b->wrsThrottlingSetStatus == WRS_THROTTLING_SET_STATUS_FAILED) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: setting up a throttling limit failed\n",
			 slog_obj_name);
	}
	if (b->wrsThrottlingSetStatus == WRS_THROTTLING_SET_STATUS_ERROR) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: error in status file of setting up a throttling limit\n",
			 slog_obj_name);
	}

	if (b->wrsVlansSetStatus == WRS_VLANS_SET_STATUS_FAILED) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: setting up a throttling limit failed\n",
			 slog_obj_name);
	}
	if (b->wrsVlansSetStatus == WRS_VLANS_SET_STATUS_ERROR) {
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: error in status file of setting up a throttling limit\n",
			 slog_obj_name);
	}

	/* check if warning */
	if (!o->wrsBootSuccessful) {

		if (b->wrsConfigSource == WRS_CONFIG_SOURCE_ERROR_MINOR) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING;
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: Unable to read status file of wrsConfigSource\n",
				slog_obj_name);
		}
		if (b->wrsBootConfigStatus == WRS_CONFIG_STATUS_ERROR_MINOR) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING;
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: Unable to read status file of wrsBootConfigStatus\n",
				slog_obj_name);
		}
		if (b->wrsBootHwinfoReadout == WRS_BOOT_HWINFO_ERROR_MINOR) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING;
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: Unable to read status file of wrsBootHwinfoReadout\n",
				slog_obj_name);
		}
		if (b->wrsBootHwinfoReadout == WRS_BOOT_HWINFO_WARNING) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING;
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: HWINFO partition not found\n",
				slog_obj_name);
		}
		if (b->wrsBootLoadFPGA == WRS_BOOT_LOAD_FPGA_ERROR_MINOR) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING;
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: Unable to read status file of wrsBootLoadFPGA\n",
				slog_obj_name);
		}
		if (b->wrsBootLoadLM32 == WRS_BOOT_LOAD_LM32_ERROR_MINOR) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING;
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: Unable to read status file of wrsBootLoadLM32\n",
				slog_obj_name);
		}
		if (b->wrsFwUpdateStatus == WRS_FW_UPDATE_STATUS_CHECKSUM_ERROR) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING;
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: Last update of the firmware failed\n",
				slog_obj_name);
		}
		if (!strcmp(v->wrsVersions[wrsVersionFpgaType_i], unknown)) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING;
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: wrong data in hwinfo, unknown version of FPGA\n",
				slog_obj_name);
		}
		if (!strcmp(v->wrsVersions[wrsVersionSwitchSerialNumber_i], unknown)) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING;
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: wrong data in hwinfo, unknown Switch's serial number\n",
				slog_obj_name);
		}
		if (!strcmp(v->wrsVersions[wrsVersionScbVersion_i], "000")) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING;
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: wrong data in hwinfo, unknown version of SCB\n",
				slog_obj_name);
		}
		if (b->wrsCustomBootScriptSource == WRS_CUSTOM_BOOT_SCRIPT_SOURCE_ERROR_MINOR) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING;
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: Unable to read status file of wrsCustomBootScriptSource\n",
				slog_obj_name);
		}
		if (b->wrsCustomBootScriptStatus == WRS_CUSTOM_BOOT_SCRIPT_STATUS_ERROR_MINOR) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING;
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: Unable to read status file of wrsCustomBootScriptStatus\n",
				slog_obj_name);
		}
		if (b->wrsAuxClkSetStatus == WRS_AUXCLK_SET_STATUS_ERROR_MINOR) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING;
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: Unable to read status file of wrsAuxClkSetStatus\n",
				slog_obj_name);
		}
		if (b->wrsThrottlingSetStatus == WRS_THROTTLING_SET_STATUS_ERROR_MINOR) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING;
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: Unable to read status file of wrsThrottlingSetStatus\n",
				slog_obj_name);
		}
		if (b->wrsVlansSetStatus == WRS_VLANS_SET_STATUS_ERROR_MINOR) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING;
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: Unable to read status file of wrsVlansSetStatus\n",
				slog_obj_name);
		}
	}

	/* check if any of fields equal to 0 */
	if (!o->wrsBootSuccessful) {
		if (b->wrsRestartReason == 0) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING_NA;
			snmp_log(LOG_WARNING, "SNMP: " SL_NA " %s: Status of wrsRestartReason not available\n",
				slog_obj_name);
		}
		if (b->wrsBootConfigStatus == 0) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING_NA;
			snmp_log(LOG_WARNING, "SNMP: " SL_NA " %s: Status of wrsBootConfigStatus not available\n",
				slog_obj_name);
		}
		if (b->wrsBootHwinfoReadout == 0) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING_NA;
			snmp_log(LOG_WARNING, "SNMP: " SL_NA " %s: Status of wrsBootHwinfoReadout not available\n",
				slog_obj_name);
		}
		if (b->wrsBootLoadFPGA == 0) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING_NA;
			snmp_log(LOG_WARNING, "SNMP: " SL_NA " %s: Status of wrsBootLoadFPGA not available\n",
				slog_obj_name);
		}
		if (b->wrsBootLoadLM32 == 0) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING_NA;
			snmp_log(LOG_WARNING, "SNMP: " SL_NA " %s: Status of wrsBootLoadLM32 not available\n",
				slog_obj_name);
		}
		if (b->wrsFwUpdateStatus == 0) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING_NA;
			snmp_log(LOG_WARNING, "SNMP: " SL_NA " %s: Status of wrsFwUpdateStatus not available\n",
				slog_obj_name);
		}
		if (b->wrsCustomBootScriptSource == 0) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING_NA;
			snmp_log(LOG_WARNING, "SNMP: " SL_NA " %s: Status of wrsCustomBootScriptSource not available\n",
				slog_obj_name);
		}
		if (b->wrsCustomBootScriptStatus == 0) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING_NA;
			snmp_log(LOG_WARNING, "SNMP: " SL_NA " %s: Status of wrsCustomBootScriptStatus not available\n",
				slog_obj_name);
		}
		if (b->wrsAuxClkSetStatus == 0) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING_NA;
			snmp_log(LOG_WARNING, "SNMP: " SL_NA " %s: Status of wrsAuxClkSetStatus not available\n",
				slog_obj_name);
		}
		if (b->wrsThrottlingSetStatus == 0) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING_NA;
			snmp_log(LOG_WARNING, "SNMP: " SL_NA " %s: Status of wrsThrottlingSetStatus not available\n",
				slog_obj_name);
		}
		if (b->wrsVlansSetStatus == 0) {
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_WARNING_NA;
			snmp_log(LOG_WARNING, "SNMP: " SL_NA " %s: Status of wrsVlansSetStatus not available\n",
				slog_obj_name);
		}
	}

	if ((!o->wrsBootSuccessful) 
	    && ( /* check if OK */
		b->wrsBootCnt != 0
		&& b->wrsRestartReason != WRS_RESTART_REASON_ERROR
		&& b->wrsConfigSource != WRS_CONFIG_SOURCE_ERROR
		&& b->wrsConfigSource != WRS_CONFIG_SOURCE_ERROR_MINOR /* warning */
		&& (b->wrsBootConfigStatus == WRS_CONFIG_STATUS_OK
		    || b->wrsConfigSource == WRS_CONFIG_SOURCE_TRY_DHCP)
		&& b->wrsBootHwinfoReadout == WRS_BOOT_HWINFO_OK
		&& b->wrsBootLoadFPGA == WRS_BOOT_LOAD_FPGA_OK
		&& b->wrsBootLoadLM32 == WRS_BOOT_LOAD_LM32_OK
		&& b->wrsBootKernelModulesMissing == 0
		&& b->wrsBootUserspaceDaemonsMissing == 0
		&& b->wrsFwUpdateStatus == WRS_FW_UPDATE_STATUS_OK
		/* custom boot script source and status */
		&& (/* disabled */
		    (b->wrsCustomBootScriptSource == WRS_CUSTOM_BOOT_SCRIPT_SOURCE_DISABLED
		     && b->wrsCustomBootScriptStatus == WRS_CUSTOM_BOOT_SCRIPT_STATUS_DISABLED
		    ) || (/* local */
			  b->wrsCustomBootScriptSource == WRS_CUSTOM_BOOT_SCRIPT_SOURCE_LOCAL
			  && b->wrsCustomBootScriptStatus == WRS_CUSTOM_BOOT_SCRIPT_STATUS_OK
		    ) || (/* remote */
			  b->wrsCustomBootScriptSource == WRS_CUSTOM_BOOT_SCRIPT_SOURCE_REMOTE
			  && b->wrsCustomBootScriptStatus == WRS_CUSTOM_BOOT_SCRIPT_STATUS_OK
		    )
		   )
		&& (b->wrsAuxClkSetStatus == WRS_AUXCLK_SET_STATUS_OK
		    || b->wrsAuxClkSetStatus == WRS_AUXCLK_SET_STATUS_DISABLED)
		&& (b->wrsThrottlingSetStatus == WRS_THROTTLING_SET_STATUS_OK
		    || b->wrsThrottlingSetStatus == WRS_THROTTLING_SET_STATUS_DISABLED)
		&& (b->wrsVlansSetStatus == WRS_VLANS_SET_STATUS_OK
		    || b->wrsVlansSetStatus == WRS_VLANS_SET_STATUS_DISABLED)
	       )
	   ) { /* OK, but check source */
		/* additional check of source */
		if (
			b->wrsConfigSource == WRS_CONFIG_SOURCE_LOCAL
			|| b->wrsConfigSource == WRS_CONFIG_SOURCE_TRY_DHCP
			|| (
				(
					b->wrsConfigSource == WRS_CONFIG_SOURCE_REMOTE
					|| b->wrsConfigSource == WRS_CONFIG_SOURCE_FORCE_DHCP
				)
				&& strnlen(b->wrsConfigSourceUrl, WRS_CONFIG_SOURCE_URL_LEN + 1)
			)
		) { /* OK */
			/* when dotconfig is local or try_dhcp or
			 * ((remote or force_dhcp) and url not empty) */
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_OK;
		} else { /* error because of empty source url */
			o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_ERROR;
			snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: empty dot-config's source URL\n",
				 slog_obj_name);
		}
	}
	
	if (!o->wrsBootSuccessful) {
		/* probably bug in previous conditions,
		 * this should never happen */
		o->wrsBootSuccessful = WRS_BOOT_SUCCESSFUL_BUG;
		SLOG(SL_BUG);
	}

	/*********************************************************************\
	|*********************** wrsTemperatureWarning ***********************|
	\*********************************************************************/

	slog_obj_name = wrsTemperatureWarning_str;
	t = &wrsTemperature_s;

	o->wrsTemperatureWarning = WRS_TEMPERATURE_WARNING_OK;
	/* warning when at least temperature threshold is not set (is 0) */
	if (t->wrsTempThresholdFPGA == 0) {
		o->wrsTemperatureWarning = WRS_TEMPERATURE_WARNING_THOLD_NOT_SET;
		snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: "
			 "Temperature threshold for the FPGA is not set\n",
			 slog_obj_name);
	}
	if (t->wrsTempThresholdPLL == 0) {
		o->wrsTemperatureWarning = WRS_TEMPERATURE_WARNING_THOLD_NOT_SET;
		snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: "
			 "Temperature threshold for the PLL is not set\n",
			 slog_obj_name);
	}
	if (t->wrsTempThresholdPSL == 0) {
		o->wrsTemperatureWarning = WRS_TEMPERATURE_WARNING_THOLD_NOT_SET;
		snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: "
			 "Temperature threshold for the Power Supply Left is not set\n",
			 slog_obj_name);
	}
	if (t->wrsTempThresholdPSR == 0) {
		o->wrsTemperatureWarning = WRS_TEMPERATURE_WARNING_THOLD_NOT_SET;
		snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: "
			 "Temperature threshold for the Power Supply Right is not set\n",
			 slog_obj_name);
	}

	/* rise temperature too high warning when at least one threshold level
	 * is exceeded */
	if (t->wrsTempThresholdFPGA && (t->wrsTempFPGA > t->wrsTempThresholdFPGA)) {
		o->wrsTemperatureWarning = WRS_TEMPERATURE_WARNING_TOO_HIGH;
		snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: "
			 "Temperature of the FPGA (%d) exceeded threshold value (%d)\n",
			 slog_obj_name, t->wrsTempFPGA, t->wrsTempThresholdFPGA);
	}
	if (t->wrsTempThresholdPLL && (t->wrsTempPLL > t->wrsTempThresholdPLL)) {
		o->wrsTemperatureWarning = WRS_TEMPERATURE_WARNING_TOO_HIGH;
		snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: "
			 "Temperature of the PLL (%d) exceeded threshold value (%d)\n",
			 slog_obj_name, t->wrsTempPLL, t->wrsTempThresholdPLL);
	}
	if (t->wrsTempThresholdPSL && (t->wrsTempPSL > t->wrsTempThresholdPSL)) {
		o->wrsTemperatureWarning = WRS_TEMPERATURE_WARNING_TOO_HIGH;
		snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: "
			 "Temperature of the Power Supply Left (%d) exceeded threshold value (%d)\n",
			 slog_obj_name, t->wrsTempPSL, t->wrsTempThresholdPSL);
	}
	if (t->wrsTempThresholdPSR && (t->wrsTempPSR > t->wrsTempThresholdPSR)) {
		o->wrsTemperatureWarning = WRS_TEMPERATURE_WARNING_TOO_HIGH;
		snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: "
			 "Temperature of the Power Supply Right (%d) exceeded threshold value (%d)\n",
			 slog_obj_name, t->wrsTempPSR, t->wrsTempThresholdPSR);
	}

	/*********************************************************************\
	|************************* wrsMemoryFreeLow  *************************|
	\*********************************************************************/
	/* Check memory usage */

	slog_obj_name = wrsMemoryFreeLow_str;
	f = &wrsMemory_s;

	if (f->wrsMemoryUsedPerc > WRSMEMORYFREELOW_TRESHOLD_ERROR) {
		o->wrsMemoryFreeLow = WRS_MEMORY_FREE_LOW_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
			 "Percentage of used memory (%d) exceeded threshold of error level (%d)\n",
			 slog_obj_name, f->wrsMemoryUsedPerc, WRSMEMORYFREELOW_TRESHOLD_ERROR);
	}
	if (!o->wrsMemoryFreeLow && (f->wrsMemoryUsedPerc > WRSMEMORYFREELOW_TRESHOLD_WARNING)) {
		o->wrsMemoryFreeLow = WRS_MEMORY_FREE_LOW_WARNING;
		snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: "
			 "Percentage of used memory (%d) exceeded threshold of warning level (%d)\n",
			 slog_obj_name, f->wrsMemoryUsedPerc, WRSMEMORYFREELOW_TRESHOLD_WARNING);
	}
	if (!o->wrsMemoryFreeLow && (f->wrsMemoryUsedPerc == 0)) {
		o->wrsMemoryFreeLow = WRS_MEMORY_FREE_LOW_WARNING_NA;
		snmp_log(LOG_WARNING, "SNMP: " SL_NA " %s: "
			 "Unable to read percentage of used memory\n",
			 slog_obj_name);
	}
	if (!o->wrsMemoryFreeLow) {
		/* Memory usage below threshold levels */
		o->wrsMemoryFreeLow = WRS_MEMORY_FREE_LOW_OK;
	}

	/*********************************************************************\
	|************************** wrsCpuLoadHigh  **************************|
	\*********************************************************************/
	/* Check CPU load */

	slog_obj_name = wrsCpuLoadHigh_str;
	c = &wrsCpuLoad_s;

	/* CPU load above error threshold level */
	if (c->wrsCPULoadAvg1min > WRSCPULOAD_1MIN_ERROR) {
		o->wrsCpuLoadHigh = WRS_CPU_LOAD_HIGH_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
			 "Average CPU load for 1 min (%d) exceeded threshold of error level (%d)\n",
			 slog_obj_name, c->wrsCPULoadAvg1min, WRSCPULOAD_1MIN_ERROR);
	}
	if (c->wrsCPULoadAvg5min > WRSCPULOAD_5MIN_ERROR) {
		o->wrsCpuLoadHigh = WRS_CPU_LOAD_HIGH_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
			 "Average CPU load for 5 min (%d) exceeded threshold of error level (%d)\n",
			 slog_obj_name, c->wrsCPULoadAvg5min, WRSCPULOAD_5MIN_ERROR);
	}
	if (c->wrsCPULoadAvg15min > WRSCPULOAD_15MIN_ERROR) {
		o->wrsCpuLoadHigh = WRS_CPU_LOAD_HIGH_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
			 "Average CPU load for 15 min (%d) exceeded threshold of error level (%d)\n",
			 slog_obj_name, c->wrsCPULoadAvg15min, WRSCPULOAD_15MIN_ERROR);
	}

	if (!o->wrsCpuLoadHigh) {
		/* CPU load above warning threshold level */
		if (c->wrsCPULoadAvg1min > WRSCPULOAD_1MIN_WARNING) {
			o->wrsCpuLoadHigh = WRS_CPU_LOAD_HIGH_WARNING;
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: "
				"Average CPU load for 1 min (%d) exceeded threshold of warning level (%d)\n",
				slog_obj_name, c->wrsCPULoadAvg1min, WRSCPULOAD_1MIN_WARNING);
		}
		if (c->wrsCPULoadAvg5min > WRSCPULOAD_5MIN_WARNING) {
			o->wrsCpuLoadHigh = WRS_CPU_LOAD_HIGH_WARNING;
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: "
				"Average CPU load for 5 min (%d) exceeded threshold of warning level (%d)\n",
				slog_obj_name, c->wrsCPULoadAvg5min, WRSCPULOAD_5MIN_WARNING);
		}
		if (c->wrsCPULoadAvg15min > WRSCPULOAD_15MIN_WARNING) {
			o->wrsCpuLoadHigh = WRS_CPU_LOAD_HIGH_WARNING;
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: "
				"Average CPU load for 15 min (%d) exceeded threshold of warning level (%d)\n",
				slog_obj_name, c->wrsCPULoadAvg15min, WRSCPULOAD_15MIN_WARNING);
		}
	}

	if (!o->wrsCpuLoadHigh) {
		/* CPU load below threshold levels */
		o->wrsCpuLoadHigh = WRS_CPU_LOAD_HIGH_OK;
	}

	/*********************************************************************\
	|************************** wrsDiskSpaceLow **************************|
	\*********************************************************************/
	/* Check disk usage */

	slog_obj_name = wrsDiskSpaceLow_str;
	d = wrsDiskTable_array;

	o->wrsDiskSpaceLow = WRS_DISK_SPACE_LOW_OK;
	for (i = 0; i < n_rows_disk_space; i++) {
		if (d[i].wrsDiskUseRate > WRSDISKSPACELOW_TRESHOLD_ERROR) {
			/* Disk usage above error threshold level */
			o->wrsDiskSpaceLow = WRS_DISK_SPACE_LOW_ERROR;
			snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
				 "Percentage of used disk space (%d) exceeded threshold of error level (%d) "
				 "for disk mounted at %s\n", slog_obj_name,
				 d[i].wrsDiskUseRate,
				 WRSDISKSPACELOW_TRESHOLD_ERROR,
				 d[i].wrsDiskMountPath);
		} else if (d[i].wrsDiskUseRate > WRSDISKSPACELOW_TRESHOLD_WARNING) {
			/* Disk usage above warning threshold level */
			if (o->wrsDiskSpaceLow == WRS_DISK_SPACE_LOW_OK
			    || o->wrsDiskSpaceLow == WRS_DISK_SPACE_LOW_WARNING_NA) {
				/* set to warning only if before no
				 * errors/warnings nor warning_na */
				o->wrsDiskSpaceLow = WRS_DISK_SPACE_LOW_WARNING;
			}
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: "
				 "Percentage of used disk space (%d) exceeded threshold of warning level (%d) "
				 "for disk mounted at %s\n", slog_obj_name,
				 d[i].wrsDiskUseRate,
				 WRSDISKSPACELOW_TRESHOLD_WARNING,
				 d[i].wrsDiskMountPath);
		} else if (d[i].wrsDiskSize == 0) {
			/* disk size is 0, propably error while reading size,
			 * but don't overwrite regular warning */
			if (o->wrsDiskSpaceLow == WRS_DISK_SPACE_LOW_OK) {
				o->wrsDiskSpaceLow = WRS_DISK_SPACE_LOW_WARNING_NA;
			}
			snmp_log(LOG_WARNING, "SNMP: " SL_NA " %s: "
				 "Unable to read percentage of used disk space"
				 "for disk mounted at %s\n", slog_obj_name,
				 d[i].wrsDiskMountPath);

		}
	}

	/* there was an update, return current time */
	return time_update;
}

#define GT_OID WRSOSSTATUS_OID
#define GT_PICKINFO wrsOSStatus_pickinfo
#define GT_DATA_FILL_FUNC wrsOSStatus_data_fill
#define GT_DATA_STRUCT wrsOSStatus_s
#define GT_GROUP_NAME "wrsOSStatusGroup"
#define GT_INIT_FUNC init_wrsOSStatusGroup

#include "wrsGroupTemplate.h"
