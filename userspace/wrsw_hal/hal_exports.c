/* HAL public API available via WR-IPC */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

#include <rt_ipc.h>
#include <minipc.h>

#include <libwr/wrs-msg.h>
#include <libwr/pps_gen.h> /* for direct access to DMPLL and PPS generator */
#include <libwr/shmem.h>

#include "hal_exports.h" /* for exported structs/function protos */
#include "hal_ports.h"
#include "hal_timing.h"

static struct minipc_ch *hal_ch;
static struct hal_port_state *ports;

/* Locking call - controls the HAL locking state machine. Called by
   the PTPd during the WR Link Setup phase, when it has detected a
   compatible WR master. */
int halexp_lock_cmd(const char *port_name, int command, int priority)
{

/*	pr_debug("halexp_lock_cmd: cmd=%d port=%s\n", command, port_name); */

	switch (command) {
	case HEXP_LOCK_CMD_ENABLE_TRACKING:
		return hal_port_enable_tracking(port_name);

		/* Start locking - i.e. tell the HAL locking state
		   machine to use the port (port_name) as the source
		   of the reference frequency. (priority) parameter
		   allows to distinguish between various reference
		   sources and establish a switchover order. For
		   example when wri1, wri2, wri3 have respectively
		   priorities (1, 0, 2), the primary clock source is
		   wri2. When it dies (e.g. rats ate the fiber), the
		   PLL will automatically switch to wri2, and if wri2
		   dies, to wri3. When all the ports are down, the PLL
		   will switch to holdover mode. In V3, calling this
		   command with negative (priority) removes the port
		   from the locking list.
		 */
	case HEXP_LOCK_CMD_START:
		return hal_port_start_lock(port_name, priority);

		/* Check if locked - called by the PTPd repeatedly
		   after calling "Start locking" to check if the PLL
		   has already locked to and stabilized the reference
		   frequency */

#define returnValue(value) { pr_info("JCB: HEXP_LOCK_CMD_CHECK returns %d\n",value); return value;}

	case HEXP_LOCK_CMD_CHECK:
		switch ( hal_port_check_lock_by_name(port_name) ) {
		case PORT_LOCK_STATE_LOCKED :
			return HEXP_LOCK_STATUS_LOCKED;
		case PORT_LOCK_STATE_UNLOCKED :
			return HEXP_LOCK_STATUS_UNLOCKED;
		case PORT_LOCK_STATE_RELOCK_ERROR :
			return HEXP_LOCK_STATUS_RELOCK_ERROR;
		default:
			return HEXP_LOCK_STATUS_ERROR;
		}
		break;
			
	case HEXP_LOCK_CMD_RESET:
		return hal_port_reset(port_name);
		
	}

	return -100;		/* fixme: add real error code */
}

/* Phase/Clock adjutsment call. Called by the PTPd servo. Controls
 * both the PLLs and the PPS Generator. */
int halexp_pps_cmd(int cmd, hexp_pps_params_t * params)
{
	int ret;

	pr_debug("halexp_pps_cmd: cmd=%d\n", cmd);
	switch (cmd) {
		/* fixme: TODO: implement HEXP_PPSG_CMD_GET call */

		/* Phase adjustment call: adjusts the phase shift
		   between the uplink port (port_name) and the local
		   VCTCXO clock by adding a number of picoseconds
		   given in (params->adjust_phase_shift) to the
		   current phase setpoint (i.e. when adjust is
		   positive, the resulting ref clock/PPS goes a bit
		   into future, it if's negative - it rolls back into
		   past). Note that to have a seamless swictchover,
		   the phase shifts for different uplinks have to be
		   coherent (i.e. the phase of the uplink clock + its
		   adjustment shall result in the same VCTCXO
		   phase. Keeping the coherency between the phase
		   setpoints for different uplinks is the task of the
		   PTPd. */
	case HEXP_PPSG_CMD_ADJUST_PHASE:

		/* PPS adjustment call, independent for the nanosecond
		   (a.k.a. 8ns cycle) counter and the seconds (UTC)
		   counter. The counters are adjusted by atomically
		   adding (params->adjust_nsec/utc). Since there's a
		   single PPS counter, these adjustments are
		   port-independent. Once the coarse (16/8ns) offset
		   is fixed, fine adjustments are done with
		   ADJUST_PHASE call, independently for each uplink to
		   accommodate the different phase shifts on each port
		   (and the fiber/cable connected to it).
		 */
		return rts_adjust_phase(0, params->adjust_phase_shift);

	case HEXP_PPSG_CMD_ADJUST_NSEC:
		shw_pps_gen_adjust(PPSG_ADJUST_NSEC, params->adjust_nsec);
		return 0;

	case HEXP_PPSG_CMD_ADJUST_SEC:
		shw_pps_gen_adjust(PPSG_ADJUST_SEC, params->adjust_sec);
		return 0;

		/* Returns non-zero if the PPS/PLL adjustment is in
		   progress.  WARNING: timestamps of the packets
		   sent/received during the PPS counter adjustment are
		   VERY LIKELY to be broken. Currently, the servo in
		   PTPd just introduces a dumb, 2-second delay after
		   each adjustment, to make sure following packets
		   will have already their timestamps generated using
		   the updated counter.

		   fixme: the NIC driver should check the status of
		   the PPS generator adjustment and if it detects a
		   pending adjustment it shall not timestamp any
		   packets, so the PTPd will simply ignore them during
		   delay calculation. */

	case HEXP_PPSG_CMD_POLL:
		return shw_pps_gen_busy() || hal_port_pshifter_busy();

	case HEXP_PPSG_CMD_SET_VALID:
		return shw_pps_gen_enable_output(params->pps_valid);

	case HEXP_PPSG_CMD_SET_TIMING_MODE:
		return hal_tmg_set_mode(params->timing_mode);

	case HEXP_PPSG_CMD_GET_TIMING_MODE:{
		ret=hal_tmg_get_mode(NULL);
		return ret;
	}

	case HEXP_PPSG_CMD_GET_TIMING_MODE_STATE:
		return shw_pps_get_timing_mode_state();
	}
	return -1;		/* fixme: real error code */
}

/* Receive information about PPSi instances */
int halexp_port_info_cmd(hexp_port_info_params_t * params)
{
	int i;
	/* TODO: Improve/implement handling of many PPSi instances on a 
	   single physical port (see more info in hal_port_update_info() */
	for ( i=0; i< params->numberPortInterfaces; i++ )
		hal_port_update_info(params->hIFace[i].name, params->hIFace[i].mode, params->hIFace[i].synchronized);
	return 1;
}

static void hal_cleanup_wripc(void)
{
	minipc_close(hal_ch);
}

/* The functions to manage packet/args conversions */
static int export_pps_cmd(const struct minipc_pd *pd,
			  uint32_t * args, void *ret)
{
	int rval;

	/* First argument is command next is param structure */
	rval = halexp_pps_cmd(args[0], (hexp_pps_params_t *) (args + 1));
	*(int *)ret = rval;
	return 0;
}

static int export_lock_cmd(const struct minipc_pd *pd,
			   uint32_t * args, void *ret)
{
	int rval;
	char *pname = (void *)args;

	/* jump over the string */
	args = minipc_get_next_arg(args, pd->args[0]);

	rval = halexp_lock_cmd(pname, args[0] /* cmd */ , args[1] /* prio */ );
	*(int *)ret = rval;
	return 0;
}

static int export_port_info_cmd(const struct minipc_pd *pd,
			   uint32_t * args, void *ret)
{
	int rval;

	/* First argument is command next is param structure */
	rval = halexp_port_info_cmd((hexp_port_info_params_t *) args);
	*(int *)ret = rval;
	return 0;
}


/* Creates a wripc server and exports all public API functions */
int hal_wripc_init(struct hal_port_state *hal_ports, char *logfilename)
{
	static FILE *f;

	ports = hal_ports; /* static pointer used later */

	hal_ch = minipc_server_create(WRSW_HAL_SERVER_ADDR, 0);

	if (hal_ch < 0) {
		pr_error("Failed to create mini-rpc server '%s'\n",
		      WRSW_HAL_SERVER_ADDR);
		return -1;
	}
	/* NOTE: check_running is not remotely called, so I don't export it */

	if (!f && logfilename) {
		f = fopen(logfilename, "a");
		if (f) {/* ignore error for logs */
			setvbuf(f, NULL, _IONBF, 0);
			minipc_set_logfile(hal_ch, f);
		}
	}

	/* fill the function pointers */
	__rpcdef_pps_cmd.f = export_pps_cmd;
	__rpcdef_lock_cmd.f = export_lock_cmd;
	__rpcdef_port_info_cmd.f = export_port_info_cmd;

	minipc_export(hal_ch, &__rpcdef_pps_cmd);
	minipc_export(hal_ch, &__rpcdef_lock_cmd);
	minipc_export(hal_ch, &__rpcdef_port_info_cmd);

	/* FIXME: pll_cmd is empty anyways???? */

	hal_add_cleanup_callback(hal_cleanup_wripc);

	pr_info("Started mini-rpc server '%s'\n", WRSW_HAL_SERVER_ADDR);

	return 0;
}

/* wripc update function, must be called in the main program loop */
int hal_wripc_update(int ms_timeout)
{
	minipc_server_action(hal_ch, ms_timeout);
	return 0;
}

/* Returns 1 if there's already an instance of the HAL running. Used
   to prevent from launching multiple HALs simultaneously. */
int hal_check_running()
{
	struct wrs_shm_head *hal_head;
	hal_head = wrs_shm_get(wrs_shm_hal, "", WRS_SHM_READ);
	if (!hal_head) {
		pr_error("Unable to open shm for HAL! Unable to check if there "
			"is another HAL instance running. Error: %s\n",
			strerror(errno));
		exit(-1);
	}

	/* check if pid is 0 (shm not filled) or process with provided
	 * pid does not exist (probably crashed) */
	if ((hal_head->pid == 0) || (kill(hal_head->pid, 0) != 0))
		return 0;

	wrs_shm_put(hal_head);
	return 1;
}
