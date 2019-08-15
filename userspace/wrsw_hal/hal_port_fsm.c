/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/if.h>

#include <rt_ipc.h>

#include <libwr/hal_shmem.h>
#include <libwr/switch_hw.h>

#include "hal_exports.h"
#include "hal_ports.h"
#include "hal_port_leds.h"
#include "hal_port_gen_fsm.h"
#include "hal_port_fsmP.h"
#include "hal_port_fsm_rx.h"
#include "hal_port_fsm_tx.h"
#include "hal_port_fsm_pll.h"
#include "hal_timing.h"


/**
 * State machine
 * States :
 *    - HAL_PORT_STATE_INIT:
 *    	Inital state
 *    - HAL_PORT_STATE_DISABLED:
 *      The port is disabled and waiting for a SFP insertion
 *    - HAL_PORT_STATE_LINK_DOWN:
 *    	A Sfp is inserted  but the port is waiting for being up
 *    - HAL_PORT_STATE_LINK_UP:
 *      The port is UP and operational
 * Events :
 *    - timer      : triggered regularly to execute background work
 *    - sfpInserted: A Sfp has been inserted
 *    - sfpRemoved : A Sfp has been removed
 *    - linkUp     : A link up has been detected (driver ioctl)
 *    - Link down  : Port is going down
 *    - reset      : Reset of the port is requested
 *
 */

/* external prototypes */
static  int _builPortEvents(void * vpfg);

static int _hal_port_state_init(void *vpfg, int eventMsk, int isNewState);
static int _hal_port_state_disabled(void *vpfg,  int eventMsk, int isNewState);
static int _hal_port_state_link_down(void *vpfg,  int eventMsk, int isNewState);
static int _hal_port_state_link_up(void *vpfg,  int eventMsk, int isNewState);

static void _init_port(struct hal_port_state * ps);
static void _reset_port(struct hal_port_state * ps);
static void _unlock_port( struct hal_port_state * ps);
static int _get_port_link_state(struct hal_port_state * ps,int *linkUp);

static halPortStateTable_t _fsmStateTable[] =
{
		{ .state=HAL_PORT_STATE_INIT,
				.stateName="INIT",
				FSM_SET_FCT_NAME(_hal_port_state_init)
		},
		{ .state=HAL_PORT_STATE_DISABLED,
				.stateName="DISABLED",
				FSM_SET_FCT_NAME(_hal_port_state_disabled)
		},
		{ .state=HAL_PORT_STATE_LINK_DOWN,
				.stateName="LINK_DOWN",
				FSM_SET_FCT_NAME(_hal_port_state_link_down)
		},
		{ .state=HAL_PORT_STATE_LINK_UP,
				.stateName="LINK_UP",
				FSM_SET_FCT_NAME(_hal_port_state_link_up)
		},
		{.state=1}
};

static halPortEventTable_t _fsmEvtTable[] = {
		{
				.evtMask = HAL_PORT_EVENT_TIMER,
				.evtName="TIMER"
		},
		{
				.evtMask =HAL_PORT_EVENT_SFP_INSERTED,
				.evtName = "SFP_INS"
		},
		{
				.evtMask =HAL_PORT_EVENT_SFP_REMOVED,
				.evtName = "SFP_REM"
		},
		{
				.evtMask =HAL_PORT_EVENT_LINK_UP,
				.evtName = "LINK_UP"
		},
		{ 		.evtMask =HAL_PORT_EVENT_LINK_DOWN,
				.evtName = "LINK_DOWN"
		},
		{ 		.evtMask =HAL_PORT_EVENT_RESET,
				.evtName = "REET"
		},
		{ .evtMask = -1 } };


static halPortFsmGen_t _portFsm = {
		.fsm_name="PortFsm",
		.fctBuilEvents=_builPortEvents,
		.pt=_fsmStateTable,
		.pe=_fsmEvtTable
};

/* INIT state
 *
 * if  entering in state then
 *     init port
 *     init TX SETUP FSM
 * fi
 * run TX SETUP FSM
 * if final state reached (TX SETUP FSM) then state = DISABLED
 *
 */
static int _hal_port_state_init(void *vpfg, int eventMsk, int isNewState) {
	struct hal_port_state * ps=((halPortFsmGen_t *)vpfg)->ps;

	if ( isNewState )  {
		_init_port(ps);
		/* Init the tx state machine */
		hal_port_tx_setup_init_fsm(ps);
	}
	 /* if final state reached for tx setup state machine
	  * then we can go to DISABLED state
	  */
	if (hal_port_tx_setup_state_fsm(ps)==1 )
		_fireState(vpfg,HAL_PORT_STATE_DISABLED);
	return 0;
}

/*
 * DISABLED state
 *
 * if entering in state then reset port
 * if SFP inserted the state=LINK_DOWN
 */
static int _hal_port_state_disabled(void *vpfg, int eventMsk, int isNewState) {
	struct hal_port_state * ps=((halPortFsmGen_t *)vpfg)->ps;

	if ( isNewState ) {
		_reset_port(ps);
	}
	if ( _isHalEventSfpInserted(eventMsk) )
		_fireState(vpfg,HAL_PORT_STATE_LINK_DOWN);
	return 0;
}

/* LINK_DOWN state
 *
 * if SFP removed event then state= DISABLED
 * if entering in state then
 *     reset port
 *     init RX_SETUP FSM
 * fi
 * run RX_SETUP FSM
 * if final state (RX_SETUP FSM ) reached then state=LINK_UP
 */
static int _hal_port_state_link_down(void *vpfg, int eventMsk, int isNewState) {
	struct hal_port_state * ps=((halPortFsmGen_t *)vpfg)->ps;

	// High priority event received
	if ( _isHalEventSfpRemoved(eventMsk) ) {
		_fireState(vpfg,HAL_PORT_STATE_DISABLED);
		return 0;
	}

	if ( isNewState )  {
		_reset_port(ps);
		/* Init the rx state machine */
		hal_port_rx_setup_init_fsm(ps);
	}

	/* if final state reached for tx setup state machine then
	 *     we can go LINK_UP state
	 */
	if (hal_port_rx_setup_state_fsm(ps)==1 ) {
		_fireState(vpfg,HAL_PORT_STATE_LINK_UP);
		return 0;
	}
	return 0;
}

/* LINK_UP state :
 *
 * if SFP removed event then state= DISABLED
 * if reset or link down events then state= LINK_DOWN
 * if entering in state then inititialize  PLL FSM
 * Run PLL FSM
 * Update Leds
 */
static int _hal_port_state_link_up(void *vpfg, int eventMsk, int isNewState) {
	struct hal_port_state * ps=((halPortFsmGen_t *)vpfg)->ps;
	if ( _isHalEventSfpRemoved(eventMsk) ) {
		_unlock_port(ps);
		_fireState(vpfg,HAL_PORT_STATE_DISABLED);
		return 0;
	}

	if ( _isHalEventReset(eventMsk) || _isHalEventLinkDown(eventMsk)) {
		_unlock_port(ps);
		_fireState(vpfg,HAL_PORT_STATE_LINK_DOWN);
		return 0;
	}

	if ( isNewState ) {
		// Init PLL FSM
		hal_port_pll_init_fsm(ps);
	}

	if (isRtsStateValid() ) {
		struct channel *ch=&getRtsState().channels[ps->hw_index];
		ps->phase_val = ch->phase_loopback;
		ps->phase_val_valid =ch->flags & CHAN_PMEAS_READY ? 1 : 0;
		if (ps->hw_index==0 ) //JCB
			printf("Phase=%d valid=%d\n",ps->phase_val,ps->phase_val_valid);
	}

	// Run PLL state machine
	hal_port_pll_state_fsm(ps);

	// Update leds
	{
		// Update synced led
		int ledValue= ps->synchronized
				&& (ps->portInfoUpdated--) > -50 // 50 * 100ms (FSM call rate) = 5seconds
				? 1 : 0;
		led_set_synched(ps->hw_index, ledValue);

		// Update link led
		if (ps->portMode == PORT_MODE_SLAVE)
			ledValue=SFP_LED_WRMODE_SLAVE;
		else if (ps->portMode  == PORT_MODE_MASTER)
			ledValue=SFP_LED_WRMODE_MASTER;
		else
			ledValue=SFP_LED_WRMODE_OTHER;

		led_set_wrmode(ps->hw_index,ledValue);
	}

	return 0;
}

/*
 * Build all events
 */
static  int _builPortEvents(void * vpfg) {
	struct hal_port_state * ps=((halPortFsmGen_t *)vpfg)->ps;
	int portEventMask=HAL_PORT_EVENT_TIMER;

	if ( ps->evt_linkUp != -1 ) {
		portEventMask |= ps->evt_linkUp ?
				HAL_PORT_EVENT_LINK_UP  : HAL_PORT_EVENT_LINK_DOWN;
	}
	if ( ps->evt_reset ) {
		portEventMask |= HAL_PORT_EVENT_RESET;
		ps->evt_reset=0;
	}
	portEventMask |= ps->sfpPresent  ?
			HAL_PORT_EVENT_SFP_INSERTED : HAL_PORT_EVENT_SFP_REMOVED;
	return portEventMask;
}


/* Init the FSM on all ports. Called one time at startup */
void hal_port_state_fsm_init( struct hal_port_state * ps ) {
	int portIndex;

	for (portIndex = 0; portIndex < HAL_MAX_PORTS; portIndex++) {
		if ( ps->in_use)
			_portFsm.ps=ps;
			_portFsm.st=&ps->portStates;
			 ps->portStates.state=-1;
			_fireState(&_portFsm,HAL_PORT_STATE_INIT);
		ps++; /* Next port */
	}

}

/* Call FSM for on all ports */
void hal_port_state_fsm( struct hal_port_state * ps ) {
	int portIndex;

	hal_port_poll_rts_state(); // Update rts state on all ports

	/* Call state machine for all ports */
	for (portIndex = 0; portIndex < HAL_MAX_PORTS; portIndex++) {
		if ( ps->in_use) {
			_portFsm.ps=ps;
			_portFsm.st=&ps->portStates;

			/* Update evt_linkUp */
			if ( _get_port_link_state(ps,&ps->evt_linkUp)==-1 ) {
				// IOTCL error : We put -1 in the link state. It will be considered as invalid
				ps->evt_linkUp=-1;
			}
			hal_port_generic_fsm(&_portFsm);
		}
		ps++; /* Next port */
	}

}

/* Reset port
 * Called when entering in states DISABLED and LINK_DOWN and when the port is initialized the first time
 */
static void _reset_port(struct hal_port_state * ps)
{
	// Disable ptracker : Needed if we were in state LINK_UP with a timing mode set to BC
	rts_enable_ptracker(ps->hw_index, 0);

	// Clear data
	ps->calib.rx_calibrated =
			ps->calib.tx_calibrated =
					ps->locked = 0;
	ps->lock_state =
			ps->tx_cal_pending =
					ps->rx_cal_pending = 0;
	ps->portMode= PORT_MODE_OTHER;
	ps->synchronized=ps->portInfoUpdated=0;
	ps->locked=0;

	ps->calib.phy_tx_min = ps->calib.phy_rx_min = 0; // No longer used

	ps->calib.delta_tx_board = 0; /* never set */
	ps->calib.delta_rx_board = 0; /* never set */

	ps->tx_cal_pending = 0;
	ps->rx_cal_pending = 0;

	/* Set link/wrmode LEDs to other. Master/slave
	 * color is set in the different place */
	led_set_wrmode(ps->hw_index,SFP_LED_WRMODE_OTHER);
	/* turn off synced LED */
	led_set_synched(ps->hw_index, 0);

}

/* Port initialization */
static void _init_port(struct hal_port_state * ps)
{

	_reset_port(ps);

	ps->t2_phase_transition = DEFAULT_T2_PHASE_TRANS;
	ps->t4_phase_transition = DEFAULT_T4_PHASE_TRANS;
	ps->clock_period = REF_CLOCK_PERIOD_PS;
}

/* Action done when leaving states locking/up */
static void _unlock_port( struct hal_port_state * ps)
{

	if ( hal_tmg_get_mode()==HAL_TIMING_MODE_BC ) {
		hal_tmg_set_mode(HAL_TIMING_MODE_FREE_MASTER);
	}

	// Disable tracker
	rts_enable_ptracker(ps->hw_index, 0);
	ps->locked=0;
}

/* Checks if the link is up on inteface (if_name). Returns non-zero if yes. */
static int _get_port_link_state(struct hal_port_state * ps,int *linkUp)
{
	struct ifreq ifr;

	strncpy(ifr.ifr_name, ps->name, sizeof(ifr.ifr_name));

	if (ioctl(halPorts.hal_port_fd, SIOCGIFFLAGS, &ifr) < 0 ) {
		pr_error("%s: IOCTL error detected : Cannot check link up on %s",__func__,ps->name);
		return -1;
	}

	*linkUp=(ifr.ifr_flags & IFF_UP && ifr.ifr_flags & IFF_RUNNING);
	return 0;
}

