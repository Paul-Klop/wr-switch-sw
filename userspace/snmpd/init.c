/*
 * A global (library-wide) init function to register several things
 */
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

/* The sub-init functions */
#include "wrsSnmp.h"
#include "snmp_shmem.h"
#include "libwr/config.h"
#include "wrsGeneralStatusGroup.h"
#include "wrsOSStatusGroup.h"
#include "wrsTimingStatusGroup.h"
#include "wrsNetworkingStatusGroup.h"
#include "wrsVersionGroup.h"
#include "wrsCurrentTimeGroup.h"
#include "wrsBootStatusGroup.h"
#include "wrsTemperatureGroup.h"
#include "wrsMemoryGroup.h"
#include "wrsCpuLoadGroup.h"
#include "wrsDiskTable.h"
#include "wrsStartCntGroup.h"
#include "wrsSpllVersionGroup.h"
#include "wrsSpllStatusGroup.h"
#include "wrsPtpDataTable.h"
#include "wrsPortStatusTable.h"
#include "wrsPstatsHCTable.h"
#include "wrsPtpInstanceTable.h"

#define DOTCONFIG_FILE "/wr/etc/dot-config"

FILE *wrs_logf; /* for the local-hack messages */

void init_wrsSnmp(void)
{
	init_shm();
	if (libwr_cfg_read_file(DOTCONFIG_FILE)) {
		/* unable to read dot-config,
		 * don't crash SNMPd, it will be reported in SNMP objects */
		snmp_log(LOG_ERR, "SNMP: " SL_ER
			"unable to read dot-config file %s\n",
			 DOTCONFIG_FILE);
	}
	init_wrsScalar();
	init_wrsGeneralStatusGroup();
	init_wrsOSStatusGroup();
	init_wrsTimingStatusGroup();
	init_wrsNetworkingStatusGroup();
	init_wrsVersionGroup();
	init_wrsCurrentTimeGroup();
	init_wrsBootStatusGroup();
	init_wrsTemperatureGroup();
	init_wrsMemoryGroup();
	init_wrsCpuLoadGroup();
	init_wrsDiskTable();
	init_wrsStartCntGroup();
	init_wrsSpllVersionGroup();
	init_wrsSpllStatusGroup();
	init_wrsPtpDataTable();
	init_wrsPortStatusTable();
	init_wrsPstatsHCTable();
	init_wrsPtpInstanceTable();

	/* perform first reads, needed to calculate deltas later */
	wrsTimingStatus_data_fill();
	wrsNetworkingStatus_data_fill();

}
