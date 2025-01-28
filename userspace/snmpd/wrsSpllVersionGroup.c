#include "wrsSnmp.h"
#include "wrsSpllVersionGroup.h"
#include <libwr/softpll.h>
#include <libwr/util.h>

static struct spll_stats *spll_stats_p;

static struct pickinfo wrsSpllVersion_pickinfo[] = {
	FIELD(wrsSpllVersion_s, ASN_OCTET_STR, wrsSpllVersion),
	FIELD(wrsSpllVersion_s, ASN_OCTET_STR, wrsSpllBuildDate),
	FIELD(wrsSpllVersion_s, ASN_OCTET_STR, wrsSpllBuildBy),
};

struct wrsSpllVersion_s wrsSpllVersion_s;

time_t wrsSpllVersion_data_fill(void)
{
	static time_t time_update;
	time_t time_cur;

	time_cur = get_monotonic_sec();
	if (time_update
	    && time_cur - time_update < WRSSPLLVERSION_CACHE_TIMEOUT) {
		/* cache not updated, return last update time */
		return time_update;
	}
	time_update = time_cur;

	memset(&wrsSpllVersion_s, 0, sizeof(wrsSpllVersion_s));

	if (!spll_stats_p) /* first time, map the fpga space */
		spll_stats_p = create_map(FPGA_SPLL_STAT,
					  sizeof(*spll_stats_p));
	if (!spll_stats_p) {
		/* unable to mmap */
		return time_update;
	}
	/* check magic number in SPLL stat memory */
	if (spll_stats_p->magic != SPLL_STATS_MAGIC) {
		/* wrong magic */
		snmp_log(LOG_ERR, "SNMP: " SL_ER
			 "wrsSpllVersionGroup Wrong SPLL magic number\n");
		return time_update;
	}
	/* check version of SPLL's stat structure, version fields are from
	 * version 2 */
	if (spll_stats_p->ver == SPLL_STATS_VER) {
		int len;
		strncpy(wrsSpllVersion_s.wrsSpllVersion, spll_stats_p->build_id.commit_id, 32);
		/* concatenate date and time */
		strncpy(wrsSpllVersion_s.wrsSpllBuildDate, spll_stats_p->build_id.build_date, 16);
		len = strnlen(wrsSpllVersion_s.wrsSpllBuildDate, 32);
		wrsSpllVersion_s.wrsSpllBuildDate[len] = ' '; /* put space instead of null */
		/* add time after added space at the end of string */
		strncpy(&wrsSpllVersion_s.wrsSpllBuildDate[len + 1], spll_stats_p->build_id.build_time, 16 - 1);
		strncpy(wrsSpllVersion_s.wrsSpllBuildBy, spll_stats_p->build_id.build_by, 32);
	}
	else
	{
		snmp_log(LOG_ERR, "SNMP: " SL_ER 
			"wrsSpllVersionGroup unsupported version of spll_stats "
			"registers (reading ver %d, supported %d) \n",
			spll_stats_p->ver, SPLL_STATS_VER);
	}

	/* there was an update, return current time */
	return time_update;
}

#define GT_OID WRSSPLLVERSION_OID
#define GT_PICKINFO wrsSpllVersion_pickinfo
#define GT_DATA_FILL_FUNC wrsSpllVersion_data_fill
#define GT_DATA_STRUCT wrsSpllVersion_s
#define GT_GROUP_NAME "wrsSpllVersionGroup"
#define GT_INIT_FUNC init_wrsSpllVersionGroup

#include "wrsGroupTemplate.h"
