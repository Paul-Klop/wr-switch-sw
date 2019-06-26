#include "wrsSnmp.h"
#include "wrsCpuLoadGroup.h"
#include <sys/sysinfo.h>    /* sysinfo */

static struct pickinfo wrsCpuLoad_pickinfo[] = {
	FIELD(wrsCpuLoad_s, ASN_INTEGER, wrsCPULoadAvg1min),
	FIELD(wrsCpuLoad_s, ASN_INTEGER, wrsCPULoadAvg5min),
	FIELD(wrsCpuLoad_s, ASN_INTEGER, wrsCPULoadAvg15min),
};

struct wrsCpuLoad_s wrsCpuLoad_s;

time_t wrsCpuLoad_data_fill(void)
{
	static time_t time_update;
	time_t time_cur;

	struct sysinfo info;

	time_cur = get_monotonic_sec();
	if (time_update
	    && time_cur - time_update < WRSCPULOAD_CACHE_TIMEOUT) {
		/* cache not updated, return last update time */
		return time_update;
	}
	time_update = time_cur;

	memset(&wrsCpuLoad_s, 0, sizeof(wrsCpuLoad_s));
	if (sysinfo(&info) != 0) {
		snmp_log(LOG_ERR, "SNMP: " SL_ER
			 " wrsMemoryGroup error while reading "
			 "system statistics with function sysinfo\n");
	}

	wrsCpuLoad_s.wrsCPULoadAvg1min =
				(info.loads[0] * 100)/(1 << SI_LOAD_SHIFT);
	wrsCpuLoad_s.wrsCPULoadAvg5min =
				(info.loads[1] * 100)/(1 << SI_LOAD_SHIFT);
	wrsCpuLoad_s.wrsCPULoadAvg15min =
				(info.loads[2] * 100)/(1 << SI_LOAD_SHIFT);

	/* there was an update, return current time */
	return time_update;
}

#define GT_OID WRSCPULOAD_OID
#define GT_PICKINFO wrsCpuLoad_pickinfo
#define GT_DATA_FILL_FUNC wrsCpuLoad_data_fill
#define GT_DATA_STRUCT wrsCpuLoad_s
#define GT_GROUP_NAME "wrsCpuLoadGroup"
#define GT_INIT_FUNC init_wrsCpuLoadGroup

#include "wrsGroupTemplate.h"
