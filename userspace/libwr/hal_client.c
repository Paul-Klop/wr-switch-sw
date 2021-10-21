#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <minipc.h>

#define HAL_EXPORT_STRUCTURES
#include <hal_exports.h>

#define DEFAULT_TO 200000	/* ms */

static struct minipc_ch *hal_ch;

int halexp_lock_cmd(const char *port_name, int command, int priority)
{
	int ret, rval;
	ret = minipc_call(hal_ch, DEFAULT_TO, &__rpcdef_lock_cmd,
			  &rval, port_name, command, priority);
	if (ret < 0)
		return ret;
	return rval;
}

int halexp_pps_cmd(int cmd, hexp_pps_params_t * params)
{
	int ret, rval;
	ret = minipc_call(hal_ch, DEFAULT_TO, &__rpcdef_pps_cmd,
			  &rval, cmd, params);
	if (ret < 0)
		return ret;
	return rval;
}

int halexp_sfp_tx_cmd(int cmd, int port)
{
	int ret, rval;
	ret = minipc_call(hal_ch, DEFAULT_TO, &__rpcdef_sfp_tx_cmd,
			  &rval, cmd, port);
	if (ret < 0)
		return ret;
	return rval;
}


/* Some clients call this, some call the client_init() defined later */
int halexp_client_try_connect(int retries, int timeout)
{
	for (;;) {
		hal_ch =
		    minipc_client_create(WRSW_HAL_SERVER_ADDR,
					 MINIPC_FLAG_VERBOSE);
		if (hal_ch == 0)
			retries--;
		else
			return 0;

		if (!retries)
			return -1;

		usleep(timeout);
	}

	return -1;
}
