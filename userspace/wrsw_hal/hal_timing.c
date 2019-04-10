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

#include "wrsw_hal.h"
#include <rt_ipc.h>
#include <hal_exports.h>

extern struct rts_pll_state hal_port_rts_state;
extern int hal_port_rts_state_valid;

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
	struct rts_pll_state *hs = &hal_port_rts_state;

	if (hal_port_rts_state_valid)
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

int  hal_update_timing_mode(void) {
	return hal_port_poll_rts_state();
}


