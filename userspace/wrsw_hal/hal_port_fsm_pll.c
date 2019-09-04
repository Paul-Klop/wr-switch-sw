
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
static int _buildEvents(void * vpfg);
static int _hal_port_pll_state_unlocked(void *vpfg, int eventMsk, int isNewState);
static int _hal_port_pll_state_locked(void *vpfg, int eventMsk, int isNewState);
static int _hal_port_pll_state_locking(void *vpfg, int eventMsk, int isNewState);


static halPortStateTable_t _fsmStateTable[] =
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

static halPortEventTable_t _fsmEvtTable[] = {
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

static halPortFsmGen_t _portFsm = {
		.fsm_name="PortFsmPll",
		.fctBuilEvents=_buildEvents,
		.pt=_fsmStateTable,
		.pe=_fsmEvtTable
};

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
static int _hal_port_pll_state_unlocked(void *vpfg, int eventMsk, int isNewState) {
	struct hal_port_state * ps=((halPortFsmGen_t *)vpfg)->ps;

	if ( _isHalPllEventLocked(eventMsk) ) {
		_fireState(vpfg,HAL_PORT_PLL_STATE_LOCKED);
		return 0;
	}
	if ( _isHalPllEventLock(eventMsk) ) {
		if ( rts_lock_channel(ps->hw_index, 0)>=0 ) {
			_fireState(vpfg,HAL_PORT_PLL_STATE_LOCKING);
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
static int _hal_port_pll_state_locking(void *vpfg, int eventMsk, int isNewState) {

	if ( _isHalPllEventLocked(eventMsk) ) {
		_fireState(vpfg,HAL_PORT_PLL_STATE_LOCKED);
		return 0;
	}
	if ( _isHalPllEventDisable(eventMsk) ) {
		_fireState(vpfg,HAL_PORT_PLL_STATE_UNLOCKED);
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
static int _hal_port_pll_state_locked(void *vpfg, int eventMsk, int isNewState) {
	if ( _isHalPllEventUnlock(eventMsk) ) {
		_fireState(vpfg,HAL_PORT_PLL_STATE_LOCKING);
		return 0;
	}
	if ( _isHalPllEventDisable(eventMsk) ) {
		_fireState(vpfg,HAL_PORT_PLL_STATE_UNLOCKED);
		return 0;
	}
	return 1; /* final state */
}

/*
 * Build FSM events
 *
 * Lock & Locked events are generated only if the timing mode is BC
 */
static int _buildEvents(void * vpfg) {
	struct hal_port_state * ps=((halPortFsmGen_t *)vpfg)->ps;
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

/* Init PLL FSM */
void hal_port_pll_init_fsm(struct hal_port_state * ps ) {

	_portFsm.ps=ps;
	_portFsm.st=&ps->pllStates;
	ps->pllStates.state=-1;
	_fireState(&_portFsm,HAL_PORT_PLL_STATE_UNLOCKED);
}

/* FSM state machine for PLL on a given port
 * Returned value:
 *  1: when final state has been reached
 *  0: when final state has not been reached
 *  -1: error detected
 */

int  hal_port_pll_state_fsm( struct hal_port_state * ps ) {
	_portFsm.ps=ps;
	_portFsm.st=&ps->pllStates;
	return hal_port_generic_fsm(&_portFsm);
}

