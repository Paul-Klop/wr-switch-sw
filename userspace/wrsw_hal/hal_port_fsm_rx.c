/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 *
 */

#define __EXPORTED_HEADERS__ /* prevent a #warning notice from linux/types.h */
#include <linux/mii.h>

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
 *    - .....
 *    - HAL_PORT_RX_SETUP_STATE_DONE:
 *    	RX setup terminated
 * Events :
 *    - timer      : triggered regularly to execute background work
 *    - linkUp     : A link up has been detected (driver ioctl)
 *    - Link down  : Port is going down
 *    - Early link up : Early link up detected
 */

/* external prototypes */
static int _buildEvents(void * vpfg);
static int _hal_port_rx_setup_state_start(void *vpfg, int eventMsk, int isNewState);
static int _hal_port_rx_setup_state_calib_no_lpdc(void *vpfg, int ventMsk, int isNewState);
static int _hal_port_rx_setup_state_reset_pcs(void *vpfg, int ventMsk, int isNewState);
static int _hal_port_rx_setup_state_wait_lock(void *vpfg, int ventMsk, int isNewState);
static int _hal_port_rx_setup_state_validate(void *vpfg, int ventMsk, int isNewState);
static int _hal_port_rx_setup_state_done(void *vpfg, int ventMsk, int isNewState);


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
		{ .state=HAL_PORT_RX_SETUP_STATE_RESET_PCS,
				.stateName="RESET_PCS",
				FSM_SET_FCT_NAME(_hal_port_rx_setup_state_reset_pcs)
		},
		{ .state=HAL_PORT_RX_SETUP_STATE_WAIT_LOCK,
				.stateName="WAIT_LOCK",
				FSM_SET_FCT_NAME(_hal_port_rx_setup_state_wait_lock)
		},
		{ .state=HAL_PORT_RX_SETUP_STATE_VALIDATE,
				.stateName="VALIDATE",
				FSM_SET_FCT_NAME(_hal_port_rx_setup_state_validate)
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
				.evtMask = HAL_PORT_RX_SETUP_EVENT_EARLY_LINK_UP,
				.evtName="EARLY_LINK_UP"
		},
		{
				.evtMask = HAL_PORT_RX_SETUP_EVENT_LINK_DOWN,
				.evtName="LINK_DOWN"
		},
		{
				.evtMask = HAL_PORT_RX_SETUP_EVENT_RX_ALIGNED,
				.evtName="RX_ALIGNED"
		},
		{ .evtMask = -1 } };

static halPortFsmGen_t _portFsm = {
		.fsm_name="PortFsmRxSetup",
		.fctBuilEvents=_buildEvents,
		.pt=_fsmStateTable,
		.pe=_fsmEvtTable
};

//TODO-ML: the below structure is redundant, consider reogranization to use
//         hal_port_rts_state
static struct rts_pll_state _pll_state;


static __inline__ void updatePllState(struct hal_port_state * ps) {
	if (ps->hw_index == 0)
	{
		// update PLL state once for all ports
		rts_get_state(&_pll_state);
	}
}

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

	if ( ps->lpdc.isSupported ) {
		// LPDC support
		pcs_writel(ps, MDIO_LPC_CTRL_TX_ENABLE |
			      MDIO_LPC_CTRL_DMTD_SOURCE_RXRECCLK,
			      MDIO_LPC_CTRL);

		if( _isHalRxSetupEventEarlyLinkUp(eventMsk)) {
			halPortLpdcRx_t *rxSetup=ps->lpdc.rxSetup;

			rxSetup->attempts=0;
			_fireState(vpfg,HAL_PORT_RX_SETUP_STATE_RESET_PCS);
		}
	} else {
		if ( _isHalRxSetupEventLinkUp(eventMsk) ) {
			_fireState(vpfg,HAL_PORT_RX_SETUP_STATE_CALIB_NO_LPDC);
		}
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

	if ( _isHalRxSetupEventLinkDown(eventMsk) ) {
		_fireState(vpfg,HAL_PORT_RX_SETUP_STATE_START);
		return 0;
	}

	if ( _isHalRxSetupEventLinkUp(eventMsk) ) {
		uint32_t bit_slide_steps;

		if ( pcs_readl(ps, 16,&bit_slide_steps)  >=0 ) {

			bit_slide_steps= (bit_slide_steps>> 4) & 0x1f;
			ps->calib.tx_calibrated = 1;
			ps->calib.rx_calibrated = 1;
			/* FIXME: use proper register names */
			ps->calib.bitslide_ps=bit_slide_steps*(uint32_t)800; /* 1 step = 800ps */
			pr_info("%s:%s: bitslide= %u\n",__func__,ps->name,bit_slide_steps);

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
 * RESET_PCS state
 *
 */
static int _hal_port_rx_setup_state_reset_pcs(void *vpfg, int eventMsk, int isNewState) {
	struct hal_port_state * ps=((halPortFsmGen_t *)vpfg)->ps;

	if( _isHalRxSetupEventEarlyLinkUp(eventMsk)) {
		halPortLpdcRx_t *rxSetup=ps->lpdc.rxSetup;

		libwr_tmo_init(&rxSetup->link_timeout, 100, 1);

		pcs_writel(ps, MDIO_LPC_CTRL_RESET_RX |
			      MDIO_LPC_CTRL_TX_ENABLE |
			      MDIO_LPC_CTRL_DMTD_SOURCE_RXRECCLK,
			      MDIO_LPC_CTRL);
		shw_udelay(1);
		pcs_writel(ps, MDIO_LPC_CTRL_TX_ENABLE |
			      MDIO_LPC_CTRL_DMTD_SOURCE_RXRECCLK,
			      MDIO_LPC_CTRL);

		rxSetup->attempts++;
		_fireState(vpfg,HAL_PORT_RX_SETUP_STATE_WAIT_LOCK);

	}
	return 0;
}

/*
 * WAIT_LOCK state
 *
 */
static int _hal_port_rx_setup_state_wait_lock(void *vpfg, int eventMsk, int isNewState) {
	struct hal_port_state * ps=((halPortFsmGen_t *)vpfg)->ps;
	halPortLpdcRx_t *rxSetup=ps->lpdc.rxSetup;

	if ( _isHalRxSetupEventEarlyLinkUp(eventMsk)) {
		if ( _isHalRxSetupEventRxAligned(eventMsk)) {

			rts_enable_ptracker(ps->hw_index, 0);
			rts_enable_ptracker(ps->hw_index, 1);
			_pll_state.channels[ps->hw_index].flags = 0;
			_fireState(vpfg,HAL_PORT_RX_SETUP_STATE_VALIDATE);
		} else {
			_fireState(vpfg,HAL_PORT_RX_SETUP_STATE_RESET_PCS);
		}
	} else {
		if( libwr_tmo_expired( &rxSetup->link_timeout ) )
			_fireState(vpfg,HAL_PORT_RX_SETUP_STATE_START);
	}

	return 0;
}

/*
 * VALIDATE state
 *
 */
static int _hal_port_rx_setup_state_validate(void *vpfg, int eventMsk, int isNewState) {
	struct hal_port_state * ps=((halPortFsmGen_t *)vpfg)->ps;

	updatePllState(ps);

	if (_pll_state.channels[ps->hw_index].flags & CHAN_PMEAS_READY) {
		int phase = _pll_state.channels[ps->hw_index].phase_loopback;
		halPortLpdcRx_t *rxSetup=ps->lpdc.rxSetup;
		uint32_t value;

		pcs_writel(ps, MDIO_LPC_CTRL_RX_ENABLE |
				MDIO_LPC_CTRL_TX_ENABLE |
				MDIO_LPC_CTRL_DMTD_SOURCE_RXRECCLK,
				MDIO_LPC_CTRL);
		pcs_writel(ps, BMCR_ANENABLE | BMCR_ANRESTART, MII_BMCR);
		//TODO-ML: change to port name
		pr_info("Port %d: RX calibration complete at phase %d "
				"ps (after %d attempts).\n", ps->hw_index + 1, phase,
				rxSetup->attempts);
		if ( pcs_readl(ps, 0, &value)>=0 )
			printf("MDIO_MCR %04x\n", value);
		rts_enable_ptracker(ps->hw_index, 0);
		sleep(1);
		if ( pcs_readl(ps, 1, &value)>=0 )
			printf("MDIO_MSR %04x\n", value);
		_fireState(vpfg, HAL_PORT_RX_SETUP_STATE_DONE);
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
	struct hal_port_state * ps=((halPortFsmGen_t *)vpfg)->ps;

	/* earlyLinkUp detection only if LPDC support */
	if ( ps->lpdc.isSupported ) {
		if ( !_isHalRxSetupEventEarlyLinkUp(eventMsk)) {
			// Port went done
			_fireState(vpfg, HAL_PORT_RX_SETUP_STATE_START);
			return 0;
                }
	}
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

	if ( ps->lpdc.isSupported ) {
		uint32_t mioLpcStat;

		if ( pcs_readl(ps, MDIO_LPC_STAT,&mioLpcStat) >= 0 ) {
			if (mioLpcStat & MDIO_LPC_STAT_LINK_UP)
				portEventMask |= HAL_PORT_RX_SETUP_EVENT_EARLY_LINK_UP;
			if (mioLpcStat & MDIO_LPC_STAT_LINK_ALIGNED)
				portEventMask |= HAL_PORT_RX_SETUP_EVENT_RX_ALIGNED;
		}
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

