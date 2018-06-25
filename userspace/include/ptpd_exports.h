#ifndef __PTPD_EXPORTS_H
#define __PTPD_EXPORTS_H

#include <stdio.h>
#include <stdlib.h>

#define PTPDEXP_COMMAND_WR_TRACKING 1
#define PTPDEXP_COMMAND_L1SYNC_TRACKING 2

extern int ptpdexp_cmd(int cmd, int value);

/* Export structures, shared by server and client for argument matching */
#ifdef PTP_EXPORT_STRUCTURES

//int ptpdexp_cmd(int cmd, int value);
struct minipc_pd __rpcdef_cmd = {
	.name = "cmd",
	.retval = MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),
	.args = {
		MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),
		MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),
		MINIPC_ARG_END,
	},
};

#endif /* PTP_EXPORT_STRUCTURES */

#endif
