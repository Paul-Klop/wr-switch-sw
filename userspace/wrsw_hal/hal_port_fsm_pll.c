#include <stdlib.h>

#include <hal_exports.h>
#include <libwr/hal_shmem.h>

#include "hal_ports.h"
#include "hal_timing.h"
#include "hal_port_fsm_pllP.h"

/**
 * State machine
 * States :
 *    - HAL_PORT_PLL_STATE_UNLOCKED: Inital state
 *    - HAL_PORT_PLL_STATE_LOCKING : Waiting PLL locking
 *    - HAL_PORT_PLL_STATE_LOCKED  : Final state. Pll locked
 * Events :
 *    - HAL_PORT_PLL_EVENT_TIMER    : Use to do background stuff
 *    - HAL_PORT_PLL_EVENT_LOCK     : Lock is requested (timing mode=BC)
 *    - HAL_PORT_PLL_EVENT_LOCKED   : Port is locked
 *    - HAL_PORT_PLL_EVENT_UNLOCK   : Pll is unlocked
 *    - HAL_PORT_PLL_EVENT_DISABLED : PLL lock is not requested
 *
 *
 */

/* external prototypes */
static int port_pll_fsm_build_events(fsm_t *pfg);
static int _hal_port_pll_state_unlocked(fsm_t *pfg, int eventMsk, int isNewState);
static int _hal_port_pll_state_locked(fsm_t *pfg, int eventMsk, int isNewState);
static int _hal_port_pll_state_locking(fsm_t *pfg, int eventMsk, int isNewState);


static fsm_state_table_entry_t port_pll_fsm_states[] =
{
		{
				.state=HAL_PORT_PLL_STATE_UNLOCKED,
				.stateName="UNLOCKED",
				FSM_SET_FCT_NAME(_hal_port_pll_state_unlocked)
		},
		{
				.state=HAL_PORT_PLL_STATE_LOCKING,
				.stateName="LOCKING",
				FSM_SET_FCT_NAME(_hal_port_pll_state_locking)
		},
		{
				.state=HAL_PORT_PLL_STATE_LOCKED,
				.stateName="LOCKED",
				FSM_SET_FCT_NAME(_hal_port_pll_state_locked)
		},
		{		.state=-1 }
};

static fsm_event_table_entry_t port_pll_fsm_events[] = {
		{
				.evtMask = HAL_PORT_PLL_EVENT_TIMER,
				.evtName="TIMER"
		},
		{
				.evtMask = HAL_PORT_PLL_EVENT_LOCK,
				.evtName="LOCK"
		},
		{
				.evtMask = HAL_PORT_PLL_EVENT_UNLOCKED,
				.evtName="UNLOCKED"
		},
		{
				.evtMask = HAL_PORT_PLL_EVENT_LOCKED,
				.evtName="LOCKED"
		},
		{
				.evtMask = HAL_PORT_PLL_EVENT_DISABLE,
				.evtName="DISABLE"
		},
		{ .evtMask = -1 } };

/* UNLOCKED state
 *
 * if locked event then
 * 		state=LOCKED (should not happen )
 * else
 *      if lock event then
 *      	lock channel
 *      	state=LOCKING
 *      fi
 * fi
 *
 */
static int _hal_port_pll_state_unlocked(fsm_t *fsm, int eventMsk, int isNewState) {
	struct hal_port_state * ps = (struct hal_port_state*) fsm->priv;

	if ( _isHalPllEventLocked(eventMsk) ) {
		fsm_fire_state(fsm, HAL_PORT_PLL_STATE_LOCKED);
		return 0;
	}
	if ( _isHalPllEventLock(eventMsk) ) {
		if ( rts_lock_channel(ps->hw_index, 0)>=0 ) {
			fsm_fire_state(fsm, HAL_PORT_PLL_STATE_LOCKING);
			return 0;
		}
	}
	return 0;
}

/*
 *  LOCKING state
 *
 *  if locked event then  state=LOCKED
 *  else if unlock event then state=UNLOCKED
 */
static int _hal_port_pll_state_locking(fsm_t *fsm, int eventMsk, int isNewState) {

	if ( _isHalPllEventLocked(eventMsk) ) {
		fsm_fire_state(fsm, HAL_PORT_PLL_STATE_LOCKED);
		return 0;
	}
	if ( _isHalPllEventDisable(eventMsk) ) {
		fsm_fire_state(fsm, HAL_PORT_PLL_STATE_UNLOCKED);
		return 0;
	}
	return 0;
}

/*
 * LOCKED state
 *
 * if unlock event then
 * 	  state = LOCKING
 * else if disabled event then state=UNLOCK
 *      else return final state machine reached
 * fi
 */
static int _hal_port_pll_state_locked(fsm_t *fsm, int eventMsk, int isNewState) {
	if ( _isHalPllEventUnlock(eventMsk) ) {
		fsm_fire_state(fsm, HAL_PORT_PLL_STATE_LOCKING);
		return 0;
	}
	if ( _isHalPllEventDisable(eventMsk) ) {
		fsm_fire_state(fsm, HAL_PORT_PLL_STATE_UNLOCKED);
		return 0;
	}
	return 1; /* final state */
}

/*
 * Build FSM events
 *
 * Lock & Locked events are generated only if the timing mode is BC
 */
static int port_pll_fsm_build_events(fsm_t *fsm) {
	struct hal_port_state * ps = (struct hal_port_state*) fsm->priv;
	int portEventMask=HAL_PORT_PLL_EVENT_TIMER;
	int tm;

	tm=	hal_tmg_get_mode();

	ps->locked=0;
	if ( tm == HAL_TIMING_MODE_BC) {
		int locked=hal_port_check_lock(ps);
		if ( locked >=0 ) {
			ps->locked = locked;
			portEventMask|= locked ?
				HAL_PORT_PLL_EVENT_LOCKED : HAL_PORT_PLL_EVENT_UNLOCKED;
		}
		if ( ps->evt_lock )
			portEventMask |= HAL_PORT_PLL_EVENT_LOCK;
	} else {
		portEventMask |= HAL_PORT_PLL_EVENT_DISABLE;
	}
	ps->evt_lock=0;// Clear event

	return portEventMask;
}

/* Initialize rx_setup - this is a global init, executed once for all ports/FSMs.
*/
void hal_port_pll_setup_init_all(struct hal_port_state * ports) {
	char name[64];
	int index;

	for (index = 0; index < HAL_MAX_PORTS; index++){
		struct hal_port_state *ps = &ports[index];

		snprintf(name, sizeof(name), "PortPllFSM.%d", ps->hw_index);

		if (fsm_generic_create(&ps->pllFsm, name, port_pll_fsm_build_events,
				port_pll_fsm_states, port_pll_fsm_events, ps)) {
			pr_error("Cannot create PLL fsm !\n");
			exit(EXIT_FAILURE);
		}
	}
}

/* Init PLL FSM */
void hal_port_pll_fsm_init(struct hal_port_state * ps )
{
	fsm_init_state(&ps->pllFsm);
	fsm_fire_state(&ps->pllFsm, HAL_PORT_PLL_STATE_UNLOCKED);
}

void hal_port_pll_fsm_reset(struct hal_port_state * ps )
{
	fsm_init_state(&ps->pllFsm);
	fsm_fire_state(&ps->pllFsm, HAL_PORT_PLL_STATE_UNLOCKED);
}

/* FSM state machine for PLL on a given port
 * Returned value:
 *  1: when final state has been reached
 *  0: when final state has not been reached
 *  -1: error detected
 */

int  hal_port_pll_fsm_run( struct hal_port_state * ps ) {
	if ( !ps->in_use )
		return 1;
	return fsm_generic_run( &ps->pllFsm );	
}

