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
#include <stdlib.h>

#include <libwr/wrs-msg.h>
#include <libwr/hal_shmem.h>
#include <hal_exports.h>

#include "driver_stuff.h"
#include "hal_ports.h"
#include "hal_port_leds.h"

#include "hal_port_fsm_rxP.h"
#include "hal_port_fsm_txP.h"

/**
 * State machine
 * States :
 *    - HAL_PORT_RX_SETUP_STATE_START:
 *    	Inital state
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
static int port_rx_setup_fsm_build_events(fsm_t *fsm);
static int _hal_port_rx_setup_state_init(fsm_t *fsm, int eventMsk, int isNewState);
static int _hal_port_rx_setup_state_start(fsm_t *fsm, int eventMsk, int isNewState);
static int _hal_port_rx_setup_state_reset_pcs(fsm_t *fsm, int ventMsk, int isNewState);
static int _hal_port_rx_setup_state_wait_lock(fsm_t *fsm, int ventMsk, int isNewState);
static int _hal_port_rx_setup_state_validate(fsm_t *fsm, int ventMsk, int isNewState);
static int _hal_port_rx_setup_state_restart(fsm_t *fsm, int ventMsk, int isNewState);
static int _hal_port_rx_setup_state_done(fsm_t *fsm, int ventMsk, int isNewState);


static fsm_state_table_entry_t port_rx_setup_fsm_states[] =
{
		{ .state=HAL_PORT_RX_SETUP_STATE_INIT,
				.stateName="INIT",
				FSM_SET_FCT_NAME(_hal_port_rx_setup_state_init)
		},
		{ .state=HAL_PORT_RX_SETUP_STATE_START,
				.stateName="START",		
				FSM_SET_FCT_NAME(_hal_port_rx_setup_state_start)
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
		{ .state=HAL_PORT_RX_SETUP_STATE_RESTART,
				.stateName="RESTART",
				FSM_SET_FCT_NAME(_hal_port_rx_setup_state_restart)
		},
		{ .state=HAL_PORT_RX_SETUP_STATE_DONE,
				.stateName="DONE",
				FSM_SET_FCT_NAME(_hal_port_rx_setup_state_done)
		},
		{ .state=-1 }
};

static fsm_event_table_entry_t port_rx_setup_fsm_events[] = {
		{
				.evtMask = HAL_PORT_RX_SETUP_EVENT_TIMER,
				.evtName="TIM"
		},
		{
				.evtMask = HAL_PORT_RX_SETUP_EVENT_LINK_UP,
				.evtName="LKUP"
		},
		{
				.evtMask = HAL_PORT_RX_SETUP_EVENT_EARLY_LINK_UP,
				.evtName="ELKUP"
		},
		{
				.evtMask = HAL_PORT_RX_SETUP_EVENT_RX_ALIGNED,
				.evtName="RX_ALGN"
		},
		{ .evtMask = -1 } };


//TODO-ML: the below structure is redundant, consider reogranization to use
//         hal_port_rts_state
static struct rts_pll_state _pll_state;


static inline void updatePllState(struct hal_port_state * ps) {
	// update PLL state once for all ports
	rts_get_state(&_pll_state);
}

/* INIT state
 * (Hypothesis: LPDC support has already been determined before )
 *
 * This state is used to start the minCalib RX timer
 */
static int _hal_port_rx_setup_state_init(fsm_t *fsm, int eventMsk, int isNewState) {
	struct hal_port_state * ps = (struct hal_port_state*) fsm->priv;

	if ( ps->lpdc.globalLpdc->numberOfLpdcPorts ) {
		if (ps->lpdc.isSupported) {
			if ( _isHalRxSetupEventEarlyLinkUp(eventMsk) )
				libwr_tmo_restart(&ps->lpdc.minCalibRx_timeout);
				fsm_fire_state(fsm, HAL_PORT_RX_SETUP_STATE_START);
		} else {
			if ( _isHalRxSetupEventLinkUp(eventMsk))
				libwr_tmo_restart(&ps->lpdc.minCalibRx_timeout);
				fsm_fire_state(fsm, HAL_PORT_RX_SETUP_STATE_START);
		}
	} else
		fsm_fire_state(fsm, HAL_PORT_RX_SETUP_STATE_START);
	return 0;
}


/* START state
 * (Hypothesis: LPDC support has already been determined before )
 *
 * if if LPDC
 *     start the LPDC process
 * else
 *     nothing to do, go to DONE and wait for link up
 * fi
 */
static int _hal_port_rx_setup_state_start(fsm_t *fsm, int eventMsk, int isNewState) {
	struct hal_port_state * ps = (struct hal_port_state*) fsm->priv;
	halPortLpdcRx_t *rxSetup=ps->lpdc.rxSetup;

	// prevent RX FSM from starting up when the TX path calibration of the port is
	// not completed.	
	
	int tx_setup_state = fsm_get_state( &ps->lpdc.txSetupFSM);
	if( tx_setup_state != HAL_PORT_TX_SETUP_STATE_DONE) {
		pr_warning("rx_setup FSM is attempted to be started before the"
			" tx_setup FSM has finished (in state %d) - this should"
			" never happen, in theory.\n",
			tx_setup_state);
		return 0;
        }

	if (ps->lpdc.isSupported) {
		if (isNewState)
			// Restart the time-out
			libwr_tmo_restart(&rxSetup->earlyup_timeout);

		/* Wait a bit to make sure early_link_up is reseted. This
		 timeout is initialized in hal_port_rx_setup_init_fsm(),
		 see detailed description there. */
		if (!libwr_tmo_expired(&rxSetup->earlyup_timeout)) {
			return 0;
		}
		// LPDC support
		pcs_writel(ps, MDIO_LPC_CTRL_TX_ENABLE |
		MDIO_LPC_CTRL_DMTD_SOURCE_RXRECCLK,
		MDIO_LPC_CTRL);

		if (_isHalRxSetupEventEarlyLinkUp(eventMsk)) {
			halPortLpdcRx_t *rxSetup = ps->lpdc.rxSetup;

			rxSetup->attempts = 0;
			rts_enable_ptracker(ps->hw_index, 0);
			fsm_fire_state(fsm, HAL_PORT_RX_SETUP_STATE_RESET_PCS);
		} else {
			// Restart the time-out
			libwr_tmo_restart(&rxSetup->earlyup_timeout);
		}
	} else {
		/* nothing to do, go waiting for link_up*/
		fsm_fire_state(fsm, HAL_PORT_RX_SETUP_STATE_DONE);
	}
	return 0;
}


/*
 * RESET_PCS state
 *
 */
static int _hal_port_rx_setup_state_reset_pcs(fsm_t *fsm, int eventMsk, int isNewState) {
		struct hal_port_state * ps = (struct hal_port_state*) fsm->priv;

	halPortLpdcRx_t *rxSetup=ps->lpdc.rxSetup;

	if ( isNewState )
		libwr_tmo_restart(&rxSetup->link_timeout);

	if( _isHalRxSetupEventEarlyLinkUp(eventMsk)) {
		halPortLpdcRx_t *rxSetup=ps->lpdc.rxSetup;

		pcs_writel(ps, MDIO_LPC_CTRL_RESET_RX |
			      MDIO_LPC_CTRL_TX_ENABLE |
			      MDIO_LPC_CTRL_DMTD_SOURCE_RXRECCLK,
			      MDIO_LPC_CTRL);
		shw_udelay(1);
		pcs_writel(ps, MDIO_LPC_CTRL_TX_ENABLE |
			      MDIO_LPC_CTRL_DMTD_SOURCE_RXRECCLK,
			      MDIO_LPC_CTRL);

		rxSetup->attempts++;
		fsm_fire_state(fsm, HAL_PORT_RX_SETUP_STATE_WAIT_LOCK);
	} else {
		if( libwr_tmo_expired( &rxSetup->link_timeout ) )
			fsm_fire_state(fsm, HAL_PORT_RX_SETUP_STATE_INIT);
	}
	return 0;
}

/*
 * WAIT_LOCK state
 *
 */
static int _hal_port_rx_setup_state_wait_lock(fsm_t *fsm, int eventMsk, int isNewState) {
		struct hal_port_state * ps = (struct hal_port_state*) fsm->priv;

	halPortLpdcRx_t *rxSetup=ps->lpdc.rxSetup;

	if ( isNewState ) {
		libwr_tmo_restart(&rxSetup->link_timeout);
		libwr_tmo_restart(&rxSetup->align_timeout);
	}
	if ( _isHalRxSetupEventEarlyLinkUp(eventMsk)) {
		// 1ms rx align detection window, described in previous state.
		if(! libwr_tmo_expired(&rxSetup->align_timeout) )
			return 0; // call me again 1ms later...

		if ( _isHalRxSetupEventRxAligned(eventMsk)) {

			rts_enable_ptracker(ps->hw_index, 0);
			rts_enable_ptracker(ps->hw_index, 1);
			_pll_state.channels[ps->hw_index].flags = 0;
			fsm_fire_state(fsm, HAL_PORT_RX_SETUP_STATE_VALIDATE);
		} else {
			if ( libwr_tmo_expired ( &ps->lpdc.minCalibRx_timeout))
				// We are looping from WAIT_LOCK and RESET_PCS for too long
				fsm_fire_state(fsm, HAL_PORT_RX_SETUP_STATE_INIT);
			else
				fsm_fire_state(fsm, HAL_PORT_RX_SETUP_STATE_RESET_PCS);
		}
	} else {
		if( libwr_tmo_expired( &rxSetup->link_timeout ) )
			fsm_fire_state(fsm, HAL_PORT_RX_SETUP_STATE_INIT);
	}

	return 0;
}

/*
 * VALIDATE state
 *
 */
static int _hal_port_rx_setup_state_validate(fsm_t *fsm, int eventMsk, int isNewState) {
	struct hal_port_state * ps = (struct hal_port_state*) fsm->priv;

	updatePllState(ps);

	if (_pll_state.channels[ps->hw_index].flags & CHAN_PMEAS_READY) {
		int phase = _pll_state.channels[ps->hw_index].phase_loopback;
		halPortLpdcRx_t *rxSetup=ps->lpdc.rxSetup;

		pcs_writel(ps, MDIO_LPC_CTRL_RX_ENABLE |
				MDIO_LPC_CTRL_TX_ENABLE |
				MDIO_LPC_CTRL_DMTD_SOURCE_RXRECCLK,
				MDIO_LPC_CTRL);
		pcs_writel(ps, BMCR_ANENABLE | BMCR_ANRESTART, MII_BMCR);

		pr_info("wri%d: RX calibration complete at phase %d "
				"ps (after %d attempts).\n", ps->hw_index + 1,
				phase, rxSetup->attempts);
		rts_enable_ptracker(ps->hw_index, 0);

		fsm_fire_state(fsm,  HAL_PORT_RX_SETUP_STATE_DONE);
	} else {

		if ( !_isHalRxSetupEventEarlyLinkUp(eventMsk) || libwr_tmo_expired( &ps->lpdc.minCalibRx_timeout) )
			// We are waiting for too long
			fsm_fire_state(fsm, HAL_PORT_RX_SETUP_STATE_INIT);
	}
	return 0;
}

/*
 * RESTART state - wait few ms before to mode to START state
 *
 *
 */
static int _hal_port_rx_setup_state_restart(fsm_t *fsm, int eventMsk, int isNewState) {
	struct hal_port_state * ps = (struct hal_port_state*) fsm->priv;

	if ( isNewState ) {
		// This timer is used to leave enough time to the FSM in the other side to detect a link down
		libwr_tmo_restart(&ps->lpdc.rxSetup->restart_timeout);
		pcs_writel(ps, MDIO_LPC_CTRL_TX_ENABLE | MDIO_LPC_CTRL_DMTD_SOURCE_TXOUTCLK,
		      MDIO_LPC_CTRL);
	} else {
		if( libwr_tmo_expired( &ps->lpdc.rxSetup->restart_timeout ) ) {
			fsm_fire_state(fsm,  HAL_PORT_RX_SETUP_STATE_INIT);
		}
	}
	return 0;
}

/*
 * DONE state - wait for link_up
 *
 * if LPDC supported
 *    if early_link_down event then state=START
  * if link up event then return final state machine reached
 *
 */
static int _hal_port_rx_setup_state_done(fsm_t *fsm, int eventMsk, int isNewState) {
	struct hal_port_state * ps = (struct hal_port_state*) fsm->priv;

	int early_up = _isHalRxSetupEventEarlyLinkUp(eventMsk);
	int link_up = _isHalRxSetupEventLinkUp(eventMsk);

	/* earlyLinkUp detection only if LPDC support */
	if ( ps->lpdc.isSupported ) {
		if ( isNewState ) {
			libwr_tmo_restart(&ps->lpdc.rxSetup->align_to_link_timeout);
		}
		if ( !early_up) {
			// Port went done
			pr_info("rxcal: early link flag lost on port wri%d\n",
					ps->hw_index + 1);

			fsm_fire_state(fsm,  HAL_PORT_RX_SETUP_STATE_INIT);
			return 0;
        }
        
		if( libwr_tmo_expired( &ps->lpdc.rxSetup->align_to_link_timeout ) && !link_up)
		{
			
			pr_warning("rxcal: link is fucked up. Retrying calibration on port %d\n",ps->hw_index + 1);

			fsm_fire_state(fsm,  HAL_PORT_RX_SETUP_STATE_RESTART);
			return 0;
		}
	}
	
	if ( ps->lpdc.globalLpdc->numberOfLpdcPorts )
		return link_up && libwr_tmo_expired(&ps->lpdc.minCalibRx_timeout)  ? 1 : 0;
	else
		return link_up ? 1 : 0;
}

/* Build FSM events */
static  int port_rx_setup_fsm_build_events (fsm_t *fsm) {
	struct hal_port_state * ps = (struct hal_port_state*) fsm->priv;

	int portEventMask=HAL_PORT_RX_SETUP_EVENT_TIMER;

	if ( ps->evt_linkUp > 0 ) {
		portEventMask |= HAL_PORT_RX_SETUP_EVENT_LINK_UP;
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


/* Initialize rx_setup - this is a global init, executed once for all ports/FSMs.
*/
void hal_port_rx_setup_init_all(struct hal_port_state * ports) {
	char name[64];
	int index;

	for (index = 0; index < HAL_MAX_PORTS; index++){
		struct hal_port_state *ps = &ports[index];

		snprintf(name, sizeof(name), "PortRxSetupFSM.%d", ps->hw_index);

		if (fsm_generic_create(&ps->lpdc.rxSetupFSM, name,
				port_rx_setup_fsm_build_events, port_rx_setup_fsm_states,
				port_rx_setup_fsm_events, ps)) {
			pr_error("Cannot create RxSetup fsm !\n");
			exit(EXIT_FAILURE);
		}
	}
}

/*
 * Init RX SETUP FSM
 */
void hal_port_rx_setup_fsm_init(struct hal_port_state * ps ) {

	fsm_init_state ( &ps->lpdc.rxSetupFSM);
	
	if ( ps->lpdc.isSupported ) {
		/* This timeout is needed when link goes down. In such case
		   the link_down flag is set earlier than the early_link_up
		   flag is reseted. So, after the link goes down, we need to
		   wait some time before executing the rx_setup FSM. Without
		   such a wait, after unplugging fiber, the rx_setup FSM is
		   started and then hangs in in wait_lock state until
		   link_timeout fires.
		   NOTE: We do the initialization of timeout here (and not in
		   the _hal_port_rx_setup_state_start() when isNewState=1) for
		   a reason. If it was done in _hal_port_rx_setup_state_start(),
		   the timeout would also kick in when the START state is
		   entered from WAIT_LOCK*/
		halPortLpdcRx_t *rxSetup = ps->lpdc.rxSetup;
		libwr_tmo_init(&rxSetup->earlyup_timeout, 10, 0);

		// link timeout
		libwr_tmo_init(&rxSetup->link_timeout, 100, 0);

		// Establish a 1ms wait for the LINK_ALIGNED flag -
		// alignment detection takes a little bit more time than early
		// link detect. Without the wait (depending on execution timing of the HAL code)
		// the wait_lock state might detect the early link but never see it's aligned.
		libwr_tmo_init(&rxSetup->align_timeout, 1, 0);

		// Align to link time-out
		libwr_tmo_init( &ps->lpdc.rxSetup->align_to_link_timeout, 5000, 0 );

		// This timer is used to leave enough time to the FSM in the other side to detect a link down
		libwr_tmo_init(&ps->lpdc.rxSetup->restart_timeout, 100, 0);

    }
	/**
	 * This time-out is used to impose the same minimum of RX calibration time
	 * on all ports (including port without LPDC. This is done to try to have
	 * all ports going to state UP at the same time.
	 * It is is not done, PPSi (with BMCA) will take a long time to stabilize
	 * its port states
	 */
	libwr_tmo_init(&ps->lpdc.minCalibRx_timeout,20000,0); // Timeout set to 20s

	
	fsm_fire_state( &ps->lpdc.rxSetupFSM, HAL_PORT_RX_SETUP_STATE_INIT );
}

/* FSM state machine for RX setup on a given port
 * Returned value:
 *  1: when final state has been reached
 *  0: when final state has not been reached
 *  -1: error detected
 */

int hal_port_rx_setup_fsm_run( struct hal_port_state * ps ) {
	if ( !ps->in_use )
		return 1;
	return fsm_generic_run( &ps->lpdc.rxSetupFSM );
}

