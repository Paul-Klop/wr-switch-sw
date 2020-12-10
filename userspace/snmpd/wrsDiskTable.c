#include "wrsSnmp.h"
#include "snmp_shmem.h"
#include "wrsDiskTable.h"

#define DISKUSAGE_COMMAND "/bin/df -P"

struct wrsDiskTable_s wrsDiskTable_array[WRS_MAX_N_DISKS];

static struct pickinfo wrsDiskTable_pickinfo[] = {
	/* Warning: strings are a special case for snmp format */
	FIELD(wrsDiskTable_s, ASN_UNSIGNED, wrsDiskIndex), /* not reported */
	FIELD(wrsDiskTable_s, ASN_OCTET_STR, wrsDiskMountPath),
	FIELD(wrsDiskTable_s, ASN_INTEGER, wrsDiskSize),
	FIELD(wrsDiskTable_s, ASN_INTEGER, wrsDiskUsed),
	FIELD(wrsDiskTable_s, ASN_INTEGER, wrsDiskFree),
	FIELD(wrsDiskTable_s, ASN_INTEGER, wrsDiskUseRate),
	FIELD(wrsDiskTable_s, ASN_OCTET_STR, wrsDiskFilesystem),
};

static int32_t int_saturate(int64_t value)
{
	if (value >= INT32_MAX)
		return INT32_MAX;
	else if (value <= INT32_MIN)
		return INT32_MIN;

	return value;
}

time_t wrsDiskTable_data_fill(unsigned int *ret_n_rows)
{
	static time_t time_update;
	time_t time_cur;
	FILE *f;
	char filesystem[64];
	uint64_t disk_size;
	uint64_t disk_used;
	uint64_t disk_available;
	char mount_path[32];
	char s[80];
	static int n_rows;

	/* number of rows depends on numbef or mounted disks */
	if (ret_n_rows)
		*ret_n_rows = n_rows;

	time_cur = get_monotonic_sec();
	if (time_update
	    && time_cur - time_update < WRSDISKTABLE_CACHE_TIMEOUT) {
		/* cache not updated, return last update time */
		return time_update;
	}
	time_update = time_cur;

	memset(&wrsDiskTable_array, 0, sizeof(wrsDiskTable_array));

	f = popen(DISKUSAGE_COMMAND, "r");
	if (!f) {
		snmp_log(LOG_ERR, "SNMP: " SL_ER " wrsDiskTable failed to execute \""
			 DISKUSAGE_COMMAND"\"\n");
		return time_cur;
	}
	/* skip first line with columns' descriptions */
	fgets(s, sizeof(s), f);
	n_rows = 0;
	while (fgets(s, sizeof(s), f)) {
		if (n_rows > WRS_MAX_N_DISKS) {
			n_rows = WRS_MAX_N_DISKS;
			break;
		}
		if (5 != sscanf(s, "%s %llu %llu %llu %*s %s %*[^\n]",
			       filesystem, &disk_size, &disk_used,
			       &disk_available, mount_path))
			continue; /* error while reading df's output */

		strncpy(wrsDiskTable_array[n_rows].wrsDiskFilesystem,
			filesystem, 64);
		wrsDiskTable_array[n_rows].wrsDiskSize =
						int_saturate(disk_size);
		wrsDiskTable_array[n_rows].wrsDiskUsed =
						int_saturate(disk_used);
		wrsDiskTable_array[n_rows].wrsDiskFree =
						int_saturate(disk_available);
		wrsDiskTable_array[n_rows].wrsDiskUseRate =
				(wrsDiskTable_array[n_rows].wrsDiskUsed * 100)
				/wrsDiskTable_array[n_rows].wrsDiskSize;
		strncpy(wrsDiskTable_array[n_rows].wrsDiskMountPath,
			mount_path, 32);

		n_rows++;
	}

	pclose(f);
	if (ret_n_rows)
		*ret_n_rows = n_rows;
	/* there was an update, return current time */
	return time_update;
}

#define TT_OID WRSDISKTABLE_OID
#define TT_PICKINFO wrsDiskTable_pickinfo
#define TT_DATA_FILL_FUNC wrsDiskTable_data_fill
#define TT_DATA_ARRAY wrsDiskTable_array
#define TT_GROUP_NAME "wrsDiskTable"
#define TT_INIT_FUNC init_wrsDiskTable
#define TT_CACHE_TIMEOUT WRSDISKTABLE_CACHE_TIMEOUT

#include "wrsTableTemplate.h"
