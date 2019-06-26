/*
 *  White Rabbit Switch versions.
 *
 *  Based on work by Alessandro Rubini for CERN, 2014
 *  Adam Wujek
 */

#include "wrsSnmp.h"
#include "wrsVersionGroup.h"

/* Macros for fscanf function to read line with maximum of "x" characters
 * without new line. Macro expands to something like: "%10[^\n]" */
#define LINE_READ_LEN_HELPER(x) "%"#x"[^\n]"
#define LINE_READ_LEN(x) LINE_READ_LEN_HELPER(x)

#define VERSION_COMMAND "/wr/bin/wrs_version -t"
#define LAST_UPDATE_DATE_FILE "/update/last_update"

struct wrs_v_item {
	char *key;
};

static struct pickinfo wrsVersion_pickinfo[] = {
	FIELD(wrsVersion_s, ASN_OCTET_STR, wrsVersions[wrsVersionSwVersion_i]),
	FIELD(wrsVersion_s, ASN_OCTET_STR, wrsVersions[wrsVersionSwBuildBy_i]),
	FIELD(wrsVersion_s, ASN_OCTET_STR, wrsVersions[wrsVersionSwBuildDate_i]),
	FIELD(wrsVersion_s, ASN_OCTET_STR, wrsVersions[wrsVersionBackplaneVersion_i]),
	FIELD(wrsVersion_s, ASN_OCTET_STR, wrsVersions[wrsVersionFpgaType_i]),
	FIELD(wrsVersion_s, ASN_OCTET_STR, wrsVersions[wrsVersionManufacturer_i]),
	FIELD(wrsVersion_s, ASN_OCTET_STR, wrsVersions[wrsVersionSwitchSerialNumber_i]),
	FIELD(wrsVersion_s, ASN_OCTET_STR, wrsVersions[wrsVersionScbVersion_i]),
	FIELD(wrsVersion_s, ASN_OCTET_STR, wrsVersions[wrsVersionGwVersion_i]),
	FIELD(wrsVersion_s, ASN_OCTET_STR, wrsVersions[wrsVersionGwBuild_i]),
	FIELD(wrsVersion_s, ASN_OCTET_STR, wrsVersions[wrsVersionSwitchHdlCommitId_i]),
	FIELD(wrsVersion_s, ASN_OCTET_STR, wrsVersions[wrsVersionGeneralCoresCommitId_i]),
	FIELD(wrsVersion_s, ASN_OCTET_STR, wrsVersions[wrsVersionWrCoresCommitId_i]),
	FIELD(wrsVersion_s, ASN_OCTET_STR, wrsVersionLastUpdateDate),
};

struct wrsVersion_s wrsVersion_s;

static struct wrs_v_item wrs_version[] = {
	/* Warning: the order here must match the MIB file
	 * wrs_version has to have the same size as wrsVersion_s.wrsVersions */
	[wrsVersionSwVersion_i]			= {.key = "software-version:"},
	[wrsVersionSwBuildBy_i]			= {"bult-by:"},
	[wrsVersionSwBuildDate_i]		= {"build-date:"},
	[wrsVersionBackplaneVersion_i]		= {"backplane-version:"},
	[wrsVersionFpgaType_i]			= {"fpga-type:"},
	[wrsVersionManufacturer_i]		= {"manufacturer:"},
	[wrsVersionSwitchSerialNumber_i]	= {"serial-number:"},
	[wrsVersionScbVersion_i]		= {"scb-version:"},
	[wrsVersionGwVersion_i]			= {"gateware-version:"},
	[wrsVersionGwBuild_i]			= {"gateware-build:"},
	[wrsVersionSwitchHdlCommitId_i]		= {"wr_switch_hdl-commit:"},
	[wrsVersionGeneralCoresCommitId_i]	= {"general-cores-commit:"},
	[wrsVersionWrCoresCommitId_i]		= {"wr-cores-commit:"},
};


static void get_last_update_date(void)
{
	FILE *f;
	f = fopen(LAST_UPDATE_DATE_FILE, "r");
	if (f) {
		/* readline without newline */
		fscanf(f, LINE_READ_LEN(32),
		       wrsVersion_s.wrsVersionLastUpdateDate);
		fclose(f);
	} else {
		snprintf(wrsVersion_s.wrsVersionLastUpdateDate, 32,
			 "0000.00.00-00:00:00");
	}
}

time_t wrsVersion_data_fill(void)
{
	char s[80], key[40], value[40];
	FILE *f;
	int i;
	int guess_index;
	static int run_once = 0;

	time_t time_cur;
	time_cur = get_monotonic_sec();

	/* assume that version does not change in runtime */
	if (run_once) {
		/* return time like there was an update */
		return time_cur;
	}

	run_once = 1;

	f = popen(VERSION_COMMAND, "r");
	if (!f) {
		snmp_log(LOG_ERR, "SNMP: " SL_ER " wrsVersion filed to execute "
			 VERSION_COMMAND"\n");
		/* try again next time */
		run_once = 0;
		return time_cur;
	}

	guess_index = 0;
	while (fgets(s, sizeof(s), f)) {
		if (sscanf(s, "%s %[^\n]", key, value) != 2) {
			/* try again next time */
			run_once = 0;
			continue; /* error... */
		}

		/* try educated guess to find position in array */
		if (!strcmp(key, wrs_version[guess_index].key)) {
			strncpy(wrsVersion_s.wrsVersions[guess_index],
				value, 32);
			guess_index++;
			continue;
		}

		/* check all */
		for (i = 0; i < ARRAY_SIZE(wrs_version); i++) {
			if (strcmp(key, wrs_version[i].key))
				continue;
			strncpy(wrsVersion_s.wrsVersions[i], value, 32);
		}
		guess_index++;
	}
	pclose(f);

	get_last_update_date();
	return time_cur;
}

#define GT_OID WRSVERSION_OID
#define GT_PICKINFO wrsVersion_pickinfo
#define GT_DATA_FILL_FUNC wrsVersion_data_fill
#define GT_DATA_STRUCT wrsVersion_s
#define GT_GROUP_NAME "wrsVersionGroup"
#define GT_INIT_FUNC init_wrsVersionGroup

#include "wrsGroupTemplate.h"
