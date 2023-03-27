#include "wrsSnmp.h"
#include "wrsMemoryGroup.h"

#define MEMINFO_FILE "/proc/meminfo"
#define MEMINFO_ENTRIES 4 /* How many meminfo are interesting for us, used to
			   * speed up reading meminfo file */

static struct pickinfo wrsMemory_pickinfo[] = {
	FIELD(wrsMemory_s, ASN_INTEGER, wrsMemoryTotal),
	FIELD(wrsMemory_s, ASN_INTEGER, wrsMemoryUsed),
	FIELD(wrsMemory_s, ASN_INTEGER, wrsMemoryUsedPerc),
	FIELD(wrsMemory_s, ASN_INTEGER, wrsMemoryFree),
};

struct wrsMemory_s wrsMemory_s;

time_t wrsMemory_data_fill(void)
{
	static time_t time_update;
	time_t time_cur;

	unsigned long value;
	unsigned long mem_total = 0;
	unsigned long mem_free = 0;
	unsigned long mem_cached = 0;
	unsigned long mem_buffers = 0;
	int found = 0;
	FILE *f;
	int ret = 0;
	char key[41]; /* 1 for null char */

	time_cur = get_monotonic_sec();
	if (time_update
	    && time_cur - time_update < WRSMEMORY_CACHE_TIMEOUT) {
		/* cache not updated, return last update time */
		return time_update;
	}
	time_update = time_cur;

	memset(&wrsMemory_s, 0, sizeof(wrsMemory_s));

	f = fopen(MEMINFO_FILE, "r");
	if (!f) {
		snmp_log(LOG_ERR, "SNMP: " SL_ER
			 " wrsMemoryGroup failed to open " MEMINFO_FILE"\n");
		/* notify snmp about error in kernel modules */

		return time_update;
	}

	while (ret != EOF && found < MEMINFO_ENTRIES) {
		/* read first word from line (module name) ignore rest of
		 * the line */
		ret = fscanf(f, "%40s %lu %*[^\n]", key, &value);
		if (ret != 2)
			continue; /* error... or EOF */
		if (!strcmp(key, "MemTotal:")) {
			mem_total = value;
			found++;
		} else if (!strcmp(key, "MemFree:")) {
			mem_free = value;
			found++;
		} else if (!strcmp(key, "Buffers:")) {
			mem_buffers = value;
			found++;
		} else if (!strcmp(key, "Cached:")) {
			mem_cached = value;
			found++;
		}
	}
	fclose(f);

	if (found == MEMINFO_ENTRIES && mem_total > 0) { /* avoid div 0 */
		unsigned long allFreeMem= mem_free + mem_buffers +mem_cached;
		wrsMemory_s.wrsMemoryTotal = (int) mem_total;
		wrsMemory_s.wrsMemoryUsed = (int) (mem_total - allFreeMem);
		wrsMemory_s.wrsMemoryUsedPerc = (int) ((mem_total - allFreeMem)
						    * 100 / mem_total);
		wrsMemory_s.wrsMemoryFree = (int) (allFreeMem);
	} else { /* if not enough entries found */
		snmp_log(LOG_ERR, "SNMP:  " SL_ER
			 " wrsMemoryGroup error while reading "
			 "values from "MEMINFO_FILE"\n");
	}

	/* there was an update, return current time */
	return time_update;
}

#define GT_OID WRSMEMORY_OID
#define GT_PICKINFO wrsMemory_pickinfo
#define GT_DATA_FILL_FUNC wrsMemory_data_fill
#define GT_DATA_STRUCT wrsMemory_s
#define GT_GROUP_NAME "wrsMemoryGroup"
#define GT_INIT_FUNC init_wrsMemoryGroup

#include "wrsGroupTemplate.h"
