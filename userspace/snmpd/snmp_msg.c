#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

/* definition of a function __wrs_msg using snmp_vlog */
void __wrs_msg(int level, const char *func, int line, const char *fmt, ...)
{
	va_list args;
	/* The actual message */
	va_start(args, fmt);
	snmp_vlog(level, fmt, args);
	va_end(args);
}

void __pp_diag(void *ppi, int th, int level, const char *fmt, ...)
{
}
