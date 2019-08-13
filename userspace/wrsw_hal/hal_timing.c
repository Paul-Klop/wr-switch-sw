/* Port initialization and state machine */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include <libwr/switch_hw.h>
#include <libwr/config.h>
#include <libwr/wrs-msg.h>
#include <libwr/timeout.h>

#include <hal_exports.h>
#include "wrsw_hal.h"
#include "hal_ports.h"


int hal_init_timing_mode(void)
{
	if (rts_connect(NULL) < 0) {
		pr_error(
		      "Failed to establish communication with the RT subsystem.\n");
		return -1;
	}
	return 0;
}

int hal_init_timing(char *filename)
{
	return 0;
}

int hal_get_timing_mode(void)
{
	struct rts_pll_state *hs = getRtsStatePtr();

	if (isRtsStateValid())
		switch (hs->mode) {
		case RTS_MODE_GM_EXTERNAL:
			return HAL_TIMING_MODE_GRAND_MASTER;
		case RTS_MODE_GM_FREERUNNING:
			return HAL_TIMING_MODE_FREE_MASTER;
		case RTS_MODE_BC:
			return HAL_TIMING_MODE_BC;
		case RTS_MODE_DISABLED:
			return HAL_TIMING_MODE_DISABLED;
		}
	return -1;
}

int  hal_set_timing_mode(uint32_t tm) {
	int ret=shw_pps_set_timing_mode(tm);
	hal_port_poll_rts_state();
	return ret;
}


