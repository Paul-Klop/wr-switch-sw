/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 *
 */
#include <hal_exports.h>
#include <libwr/hal_shmem.h>

#include "hal_port_gen_fsm.h"
#include "hal_port_fsm_txP.h"

/**
 * State machine
 * States :
 *    - HAL_PORT_TX_SETUP_STATE_START:
 *    	Inital state
 *    - HAL_PORT_TX_SETUP_STATE_DONE:
 *    	TX setup terminated
 * Events :
 *    - timer      : triggered regularly to execute background work
 */

/* external prototypes */
static  int _buildEvents(void * vpfg);
static int _hal_port_tx_setup_state_start(void *vpfg, int eventMsk, int isNewState);
static int _hal_port_tx_setup_state_done(void *vpfg, int eventMsk, int isNewState);

static halPortStateTable_t _fsmStateTable[] =
{
		{ .state=HAL_PORT_TX_SETUP_STATE_START,
				.stateName="START",
				FSM_SET_FCT_NAME(_hal_port_tx_setup_state_start)
		},
		{ .state=HAL_PORT_TX_SETUP_STATE_DONE,
				.stateName="DONE",
				FSM_SET_FCT_NAME(_hal_port_tx_setup_state_done)
		},
		{.state=1}
};

static halPortEventTable_t _fsmEvtTable[] = {
		{
				.evtMask = HAL_PORT_TX_SETUP_EVENT_TIMER,
				.evtName="TIMER"
		},
		{ .evtMask = -1 } };

static halPortFsmGen_t _portFsm = {
		.fsm_name="PortFsmTxSetup",
		.fctBuilEvents=_buildEvents,
		.pt=_fsmStateTable,
		.pe=_fsmEvtTable
};

/*
 * START state
 *
 * If entering in state then
 *      Set LPDC supported accordingly to the hardware
 * fi
 * if  LPDC is not supported then state=DONE
 *
 */
static int _hal_port_tx_setup_state_start(void *vpfg, int eventMsk, int isNewState) {
	struct hal_port_state * ps=((halPortFsmGen_t *)vpfg)->ps;

	if ( isNewState )  {
		ps->lpdc.isSupported=0; // Not yet supported
	}
	if ( !ps->lpdc.isSupported ) {
		// NO LPDC support
		_fireState(vpfg,HAL_PORT_TX_SETUP_STATE_DONE);
		return 0;
	}
	return 0;
}

/*
 * DONE state
 *
 * Return final state machine reached
 */
static int _hal_port_tx_setup_state_done(void *vpfg, int eventMsk, int isNewState) {
	return 1; /* Final state reached */
}

/* Build events mask */
static  int _buildEvents(void *vpfg) {
	return HAL_PORT_TX_SETUP_EVENT_TIMER;
}

/* Init the TX SETUP FSM on a given port */
void hal_port_tx_setup_init_fsm(struct hal_port_state * ps ) {
	_portFsm.ps=ps;
	_portFsm.st=&ps->lpdc.txSetupStates;
	ps->lpdc.txSetupStates.state=-1;
	_fireState(&_portFsm,HAL_PORT_TX_SETUP_STATE_START);
}

/* FSM state machine for TX setup on a given port
 * Returned value:
 *  1: when final state has been reached
 *  0: when final state has not been reached
 *  -1: error detected
 */

int  hal_port_tx_setup_state_fsm( struct hal_port_state * ps ) {
	_portFsm.ps=ps;
	_portFsm.st=&ps->lpdc.txSetupStates;
	return hal_port_generic_fsm(&_portFsm);
}
