#include "wrsSnmp.h"
#include "wrsPstatsHCTable.h"

struct wrsPstatsHCTable_s pstats_array[WRS_N_PORTS];

static struct pickinfo wrsPstatsHCTable_pickinfo[] = {
	/* Warning: strings are a special case for snmp format */
	FIELD(wrsPstatsHCTable_s, ASN_UNSIGNED, wrsPstatsHCIndex), /* not reported */
	FIELD(wrsPstatsHCTable_s, ASN_OCTET_STR, wrsPstatsHCPortName),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCTXUnderrun),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXOverrun),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXInvalidCode),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXSyncLost),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXPauseFrames),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXPfilterDropped),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXPCSErrors),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXGiantFrames),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXRuntFrames),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXCRCErrors),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXPclass0),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXPclass1),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXPclass2),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXPclass3),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXPclass4),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXPclass5),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXPclass6),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXPclass7),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCTXFrames),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXFrames),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXDropRTUFull),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXPrio0),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXPrio1),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXPrio2),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXPrio3),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXPrio4),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXPrio5),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXPrio6),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRXPrio7),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRTUValid),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRTUResponses),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCRTUDropped),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCFastMatchPriority),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCFastMatchFastForward),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCFastMatchNonForward),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCFastMatchRespValid),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCFullMatchRespValid),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCForwarded),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCTRURespValid),
	FIELD(wrsPstatsHCTable_s, ASN_COUNTER64, wrsPstatsHCNICTXFrames),
};

time_t
wrsPstatsHCTable_data_fill(unsigned int *n_rows)
{
	FILE *f;
	char fname[32];
	int wrport;
	uint32_t i;
	uint64_t counters[PSTATS_MAX_N_COUNTERS];
	static time_t time_update;
	time_t time_cur;
	uint32_t n_counters_in_fpga;
	uint32_t counters_version;
	uint32_t tmp1;
	uint32_t tmp2;
	if (n_rows)
		*n_rows = WRS_N_PORTS;

	time_cur = get_monotonic_sec();
	if (time_update
	    && time_cur - time_update < WRSPSTATSHCTABLE_CACHE_TIMEOUT) {
		/* cache not updated, return last update time */
		return time_update;
	}
	time_update = time_cur;

	/* fill array with 0xff, buy this it will be easy visible in case
	 * some counters are invalid */
	memset(&pstats_array, 0xff, sizeof(pstats_array));
	memset(&counters, 0xff, sizeof(counters));

	/* read counters version and number of counters */
	f = fopen(PSTATS_SYSCTL_PATH PSTATS_SYSCTL_INFO_FILE, "r");
	if (f) {
		if (fscanf(f, "%u", &counters_version) != 1) {
			/* not able to read valid counters version,
			 * assign invalid version number */
			counters_version = 0;
		}
		/* read number of counters per word, not used */
		fscanf(f, "%u", &tmp1);
		if (fscanf(f, "%u", &n_counters_in_fpga) != 1) {
			/* not able to read valid number of counters,
			 * use maximum possible */
			n_counters_in_fpga = PSTATS_MAX_N_COUNTERS;
		}
		if (n_counters_in_fpga > PSTATS_MAX_N_COUNTERS) {
			/* n_counters_in_fpga cannot be bigger than
			 * PSTATS_MAX_N_COUNTERS */
			n_counters_in_fpga = PSTATS_MAX_N_COUNTERS;
		}
		fclose(f);
	} else {
		/* unable to open info file */
		/* use PSTATS_MAX_N_COUNTERS as number of counters */
		n_counters_in_fpga = PSTATS_MAX_N_COUNTERS;
		counters_version = 0; /* invalid version */
	}

	/* read pstats for each port */
	for (wrport = 0; wrport < WRS_N_PORTS; wrport++) {
		snprintf(pstats_array[wrport].wrsPstatsHCPortName, 10,
				 "wri%d", wrport + 1);
		sprintf(fname, PSTATS_SYSCTL_PATH"wrport%i", wrport + 1);
		f = fopen(fname, "r");
		if (!f) {
			snmp_log(LOG_ERR,
				 "SNMP: wrsPstatsHCTable failed to open file "
				 "%s\n", fname);
			continue;
		}
		for (i = 0; i < n_counters_in_fpga; i++) {
			if (fscanf(f, "%" SCNu32, &tmp1) == 1 &&
			    fscanf(f, "%" SCNu32, &tmp2) == 1) {
				counters[i] = (((uint64_t) tmp2) << 32) | tmp1;
			} else {
				counters[i] = 0xffffffffffffffffLL;
			}
		}

		fclose(f);

		/* copy counters */
		switch (counters_version) {
		case 1:
			/* copy counters to array */
			pstats_array[wrport].wrsPstatsHCTXUnderrun = counters[0];
			pstats_array[wrport].wrsPstatsHCRXOverrun = counters[1];
			pstats_array[wrport].wrsPstatsHCRXInvalidCode = counters[2];
			pstats_array[wrport].wrsPstatsHCRXSyncLost = counters[3];
			pstats_array[wrport].wrsPstatsHCRXPauseFrames = counters[4];
			pstats_array[wrport].wrsPstatsHCRXPfilterDropped = counters[5];
			pstats_array[wrport].wrsPstatsHCRXPCSErrors = counters[6];
			pstats_array[wrport].wrsPstatsHCRXGiantFrames = counters[7];
			pstats_array[wrport].wrsPstatsHCRXRuntFrames = counters[8];
			pstats_array[wrport].wrsPstatsHCRXCRCErrors = counters[9];
			pstats_array[wrport].wrsPstatsHCRXPclass0 = counters[10];
			pstats_array[wrport].wrsPstatsHCRXPclass1 = counters[11];
			pstats_array[wrport].wrsPstatsHCRXPclass2 = counters[12];
			pstats_array[wrport].wrsPstatsHCRXPclass3 = counters[13];
			pstats_array[wrport].wrsPstatsHCRXPclass4 = counters[14];
			pstats_array[wrport].wrsPstatsHCRXPclass5 = counters[15];
			pstats_array[wrport].wrsPstatsHCRXPclass6 = counters[16];
			pstats_array[wrport].wrsPstatsHCRXPclass7 = counters[17];
			pstats_array[wrport].wrsPstatsHCTXFrames = counters[18];
			pstats_array[wrport].wrsPstatsHCRXFrames = counters[19];
			pstats_array[wrport].wrsPstatsHCRXDropRTUFull = counters[20];
			pstats_array[wrport].wrsPstatsHCRXPrio0 = counters[21];
			pstats_array[wrport].wrsPstatsHCRXPrio1 = counters[22];
			pstats_array[wrport].wrsPstatsHCRXPrio2 = counters[23];
			pstats_array[wrport].wrsPstatsHCRXPrio3 = counters[24];
			pstats_array[wrport].wrsPstatsHCRXPrio4 = counters[25];
			pstats_array[wrport].wrsPstatsHCRXPrio5 = counters[26];
			pstats_array[wrport].wrsPstatsHCRXPrio6 = counters[27];
			pstats_array[wrport].wrsPstatsHCRXPrio7 = counters[28];
			pstats_array[wrport].wrsPstatsHCRTUValid = counters[29];
			pstats_array[wrport].wrsPstatsHCRTUResponses = counters[30];
			pstats_array[wrport].wrsPstatsHCRTUDropped = counters[31];
			pstats_array[wrport].wrsPstatsHCFastMatchPriority = counters[32];
			pstats_array[wrport].wrsPstatsHCFastMatchFastForward = counters[33];
			pstats_array[wrport].wrsPstatsHCFastMatchNonForward = counters[34];
			pstats_array[wrport].wrsPstatsHCFastMatchRespValid = counters[35];
			pstats_array[wrport].wrsPstatsHCFullMatchRespValid = counters[36];
			pstats_array[wrport].wrsPstatsHCForwarded = counters[37];
			pstats_array[wrport].wrsPstatsHCTRURespValid = counters[38];
			pstats_array[wrport].wrsPstatsHCNICTXFrames = counters[39];
			break;
		case 2:
		default:
			break;
		}

	}
	/* there was an update, return current time */
	return time_update;
}

#define TT_OID WRSPSTATSHCTABLE_OID
#define TT_PICKINFO wrsPstatsHCTable_pickinfo
#define TT_DATA_FILL_FUNC wrsPstatsHCTable_data_fill
#define TT_DATA_ARRAY pstats_array
#define TT_GROUP_NAME "wrsPstatsHCTable"
#define TT_INIT_FUNC init_wrsPstatsHCTable
#define TT_CACHE_TIMEOUT WRSPSTATSHCTABLE_CACHE_TIMEOUT

#include "wrsTableTemplate.h"
