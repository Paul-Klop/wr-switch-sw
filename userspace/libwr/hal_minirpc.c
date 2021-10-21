#include <minipc.h>
#include <hal_exports.h> /* for exported structs/function protos */


/* Export structures, shared by server and client for argument matching */

//int halexp_lock_cmd(const char *port_name, int command, int priority);
struct minipc_pd __rpcdef_lock_cmd = {
	.name = "lock_cmd",
	.retval = MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),
	.args = {
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_STRING, char *),
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),
		 MINIPC_ARG_END,
		 },
};

//int halexp_pps_cmd(int cmd, hexp_pps_params_t *params);
struct minipc_pd __rpcdef_pps_cmd = {
	.name = "pps_cmd",
	.retval = MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),
	.args = {
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_STRUCT, hexp_pps_params_t),
		 MINIPC_ARG_END,
		 },
};

//int halexp_info_cmd(hexp_info_params_t *params);
struct minipc_pd __rpcdef_port_info_cmd = {
	.name = "info_cmd",
	.retval = MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),
	.args = {
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_STRUCT, hexp_port_info_params_t),
		 MINIPC_ARG_END,
		 },
};

//int halexp_sfp_tx_cmd(int cmd, int port);
struct minipc_pd __rpcdef_sfp_tx_cmd = {
	.name = "sfp_tx_cmd",
	.retval = MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),
	.args = {
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),
		 MINIPC_ARG_END,
		 },
};
