/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 *
 */

#include <sys/ioctl.h>
#include <net/if.h>
#include <stdint.h>

#include <libwr/wrs-msg.h>
#include <libwr/hal_shmem.h>
#include <hal_exports.h>

#include "driver_stuff.h"
#include "hal_ports.h"
#include "hal_port_leds.h"

#include "hal_port_fsm_rxP.h"

/**
 * State machine
 * States :
 *    - HAL_PORT_RX_SETUP_STATE_START:
 *    	Inital state
 *    - HAL_PORT_RX_SETUP_STATE_CALIB_NO_LPDC :
 *      Calibration when LPDC is not supported
 *    - HAL_PORT_RX_SETUP_STATE_DONE:
 *    	RX setup terminated
 * Events :
 *    - timer      : triggered regularly to execute background work
 *    - linkUp     : A link up has been detected (driver ioctl)
 *    - Link down  : Port is going down
 */

/* external prototypes */
static int _buildEvents(void * vpfg);
static int _hal_port_rx_setup_state_start(void *vpfg, int eventMsk, int isNewState);
static int _hal_port_rx_setup_state_calib_no_lpdc(void *vpfg, int ventMsk, int isNewState);
static int _hal_port_rx_setup_state_done(void *vpfg, int ventMsk, int isNewState);
static uint32_t _pcs_readl(struct hal_port_state * ps, int reg);

static halPortStateTable_t _fsmStateTable[] =
{
		{ .state=HAL_PORT_RX_SETUP_STATE_START,
				.stateName="START",		
				FSM_SET_FCT_NAME(_hal_port_rx_setup_state_start)
		},
		{ .state=HAL_PORT_RX_SETUP_STATE_CALIB_NO_LPDC,
				.stateName="CALIB_NO_LPDC",
				FSM_SET_FCT_NAME(_hal_port_rx_setup_state_calib_no_lpdc)
		},
		{ .state=HAL_PORT_RX_SETUP_STATE_DONE,
				.stateName="DONE",
				FSM_SET_FCT_NAME(_hal_port_rx_setup_state_done)
		},
		{.state=1}
};

static halPortEventTable_t _fsmEvtTable[] = {
		{
				.evtMask = HAL_PORT_RX_SETUP_EVENT_TIMER,
				.evtName="TIMER"
		},
		{
				.evtMask = HAL_PORT_RX_SETUP_EVENT_LINK_UP,
				.evtName="LINK_UP"
		},
		{
				.evtMask = HAL_PORT_RX_SETUP_EVENT_LINK_DOWN,
				.evtName="LINK_DOWN"
		},
		{ .evtMask = -1 } };

static halPortFsmGen_t _portFsm = {
		.fsm_name="PortFsmRxSetup",
		.fctBuilEvents=_buildEvents,
		.pt=_fsmStateTable,
		.pe=_fsmEvtTable
};

/* START state
 * (Hypothesis: LPDC support has already been determined before )
 *
 * if link up event then
 *     if LPDC is not supported then state = CALIB_NO_LPDC
 *     else state = DONE
 * fi
 */
static int _hal_port_rx_setup_state_start(void *vpfg, int eventMsk, int isNewState) {
	struct hal_port_state * ps=((halPortFsmGen_t *)vpfg)->ps;

	if ( _isHalRxSetupEventLinkUp(eventMsk) ) {

		_fireState(vpfg,ps->lpdc.isSupported ?
				HAL_PORT_RX_SETUP_STATE_DONE : // Not yet implemented
				HAL_PORT_RX_SETUP_STATE_CALIB_NO_LPDC);
	}
	return 0;
}

/* CALIB_NO_LPDC state
 *
 * if  link down event then state = START
 * if  link up state then
 *     Calculate the bit slide.
 *     if bit slide successfully calculated then state=DONE
 * fi
*/
static int _hal_port_rx_setup_state_calib_no_lpdc(void *vpfg, int eventMsk, int isNewState) {
	struct hal_port_state * ps=((halPortFsmGen_t *)vpfg)->ps;
	int32_t bit_slide_steps=(_pcs_readl(ps, 16) >> 4) & 0x1f;

	if ( _isHalRxSetupEventLinkDown(eventMsk) ) {
		_fireState(vpfg,HAL_PORT_RX_SETUP_STATE_START);
		return 0;
	}

	if ( _isHalRxSetupEventLinkUp(eventMsk) ) {
		if ( bit_slide_steps !=-1 ) {
			ps->calib.tx_calibrated = 1;
			ps->calib.rx_calibrated = 1;
			/* FIXME: use proper register names */
			ps->calib.bitslide_ps=(uint32_t)bit_slide_steps*(uint32_t)800; /* 1 step = 800ps */
			pr_info("%s:%s: bitslide= %d\n",__func__,ps->name,bit_slide_steps);

			ps->calib.delta_rx_phy = ps->calib.phy_rx_min;
			ps->calib.delta_tx_phy = ps->calib.phy_tx_min;

			ps->tx_cal_pending = 0;
			ps->rx_cal_pending = 0;
			_fireState(vpfg,HAL_PORT_RX_SETUP_STATE_DONE);
		}
	}
	return 0;
}

/*
 * DONE state
 *
 * if link down event then state=START
 * if link up event then return final state machine reached
 *
 */
static int _hal_port_rx_setup_state_done(void *vpfg, int eventMsk, int isNewState) {
	if ( _isHalRxSetupEventLinkDown(eventMsk) ) {
		_fireState(vpfg,HAL_PORT_RX_SETUP_STATE_START);
		return 0;
	}
	if ( _isHalRxSetupEventLinkUp(eventMsk) ) {
		return 1; /* Final state reached */;
	}
	return 0;
}

/* Build FSM events */
static  int _buildEvents(void *vpfg) {
	struct hal_port_state * ps=((halPortFsmGen_t *)vpfg)->ps;
	int portEventMask=HAL_PORT_RX_SETUP_EVENT_TIMER;


	if ( ps->evt_linkUp != -1 ) {
		portEventMask |= ps->evt_linkUp ?
				HAL_PORT_RX_SETUP_EVENT_LINK_UP  : HAL_PORT_RX_SETUP_EVENT_LINK_DOWN;
	}

	return portEventMask;
}


/*
 * Init RX SETUP FSM
 */
void hal_port_rx_setup_init_fsm(struct hal_port_state * ps ) {
	_portFsm.ps=ps;
	_portFsm.st=&ps->lpdc.rxSetupStates;
	ps->lpdc.rxSetupStates.state=-1;
	_fireState(&_portFsm,HAL_PORT_RX_SETUP_STATE_START);
}

/* FSM state machine for RX setup on a given port
 * Returned value:
 *  1: when final state has been reached
 *  0: when final state has not been reached
 *  -1: error detected
 */

int hal_port_rx_setup_state_fsm( struct hal_port_state * ps ) {
	_portFsm.ps=ps;
	_portFsm.st=&ps->lpdc.rxSetupStates;
	return hal_port_generic_fsm(&_portFsm);
}

static uint32_t _pcs_readl(struct hal_port_state * p, int reg)
{
	struct ifreq ifr;
	uint32_t rv;

	strncpy(ifr.ifr_name, p->name, sizeof(ifr.ifr_name));

	rv = NIC_READ_PHY_CMD(reg);
	ifr.ifr_data = (void *)&rv;
	if (ioctl(halPorts.hal_port_fd, PRIV_IOCPHYREG, &ifr) < 0) {
		pr_error("%s: ioctl error: Cannot read bitslide \n",__func__);
		return -1;
	}

	return NIC_RESULT_DATA(rv);
}
