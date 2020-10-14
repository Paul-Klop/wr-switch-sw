/*
 * A global (library-wide) init function to register several things
 */
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

/* The sub-init functions */
#include "wrsSnmp.h"
#include "snmp_shmem.h"
#include "dot1dBase.h"
#include "dot1dBasePortTable/dot1dBasePortTable.h"
#include "dot1dStaticTable/dot1dStaticTable.h"
#include "dot1dTpFdbTable/dot1dTpFdbTable.h"
#include "dot1qFdbTable/dot1qFdbTable.h"
#include "dot1qTpFdbTable/dot1qTpFdbTable.h"

void init_bridge_mib(void)
{
    init_dot1dBase();
    init_dot1dBasePortTable();
    init_dot1dStaticTable();
    init_dot1dTpFdbTable();
    init_dot1qFdbTable();
    init_dot1qTpFdbTable();
}
