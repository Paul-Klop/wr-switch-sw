/*
 * A global (library-wide) init function to register several things
 */
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

/* The sub-init functions */
#include "wrsSnmp.h"
#include "snmp_shmem.h"
#include "dot1qFdbTable/dot1qFdbTable.h"

void init_bridge_mib(void)
{
    init_dot1qFdbTable();
}
