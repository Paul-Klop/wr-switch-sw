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

#include "hal_exports.h"
#include "hal_ports.h"


int hal_tmg_init(const char * logfilename)
{
	if (rts_connect(NULL) < 0) {
		pr_error(
		      "Failed to establish communication with the RT subsystem.\n");
		return -1;
	}

	if( rts_set_mode( RTS_MODE_GM_FREERUNNING ) < 0 )
	{
		pr_error(
		      "Failed to configure PLL in free-running master mode.\n");
		return -1;
	}

	return 0;
}

int hal_tmg_get_mode(uint32_t *hwIndex)
{
	struct rts_pll_state *hs = getRtsStatePtr();

	if (isRtsStateValid()) {
		if ( hwIndex!=NULL )
			*hwIndex=hs->current_ref;
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
	}
	return -1;
}

int  hal_tmg_set_mode(uint32_t tm) {
	int ret=shw_pps_set_timing_mode(tm);
	hal_port_poll_rts_state();
	return ret;
}


