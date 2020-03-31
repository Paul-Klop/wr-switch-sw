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
#include <stdlib.h>
#include <rt_ipc.h>

#include <libwr/hal_shmem.h>
#include <libwr/switch_hw.h>
#include <libwr/wrs-msg.h>
#include <libwr/generic_fsm.h>

#include "driver_stuff.h"
#include "hal_exports.h"
#include "hal_ports.h"
#include "hal_port_leds.h"
#include "hal_port_fsmP.h"
#include "hal_port_fsm_rx.h"
#include "hal_port_fsm_tx.h"
#include "hal_port_fsm_pll.h"
#include "hal_timing.h"

#define __EXPORTED_HEADERS__ /* prevent a #warning notice from linux/types.h */
#include <linux/mii.h>

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
static  int port_fsm_build_events(fsm_t *fsm);

static int port_fsm_state_init(fsm_t *fsm, int eventMsk, int isNewState);
static int port_fsm_state_disabled(fsm_t *fsm,  int eventMsk, int isNewState);
static int port_fsm_state_link_down(fsm_t *fsm,  int eventMsk, int isNewState);
static int port_fsm_state_link_up(fsm_t *fsm,  int eventMsk, int isNewState);

static void init_port(struct hal_port_state * ps);
static void reset_port(struct hal_port_state * ps);
static int get_port_link_state(struct hal_port_state * ps,int *linkUp);

static fsm_state_table_entry_t port_fsm_states[] =
{
		{ .state=HAL_PORT_STATE_INIT,
				.stateName="INIT",
				FSM_SET_FCT_NAME(port_fsm_state_init)
		},
		{ .state=HAL_PORT_STATE_DISABLED,
				.stateName="DISABLED",
				FSM_SET_FCT_NAME(port_fsm_state_disabled)
		},
		{ .state=HAL_PORT_STATE_LINK_DOWN,
				.stateName="LINK_DOWN",
				FSM_SET_FCT_NAME(port_fsm_state_link_down)
		},
		{ .state=HAL_PORT_STATE_LINK_UP,
				.stateName="LINK_UP",
				FSM_SET_FCT_NAME(port_fsm_state_link_up)
		},
		{ .state=-1 }
};

static fsm_event_table_entry_t port_fsm_events[] = {
		{
				.evtMask = HAL_PORT_EVENT_TIMER,
				.evtName="TIM"
		},
		{
				.evtMask =HAL_PORT_EVENT_SFP_PRESENT,
				.evtName = "SFP"
		},
		{
				.evtMask =HAL_PORT_EVENT_LINK_UP,
				.evtName = "LKUP"
		},
		{ 		.evtMask =HAL_PORT_EVENT_RESET,
				.evtName = "RST"
		},
		{ 		.evtMask =HAL_PORT_EVENT_POWER_DOWN,
				.evtName = "PDOWN"
		},
		{ 		.evtMask =HAL_PORT_EVENT_EARLY_LINK_UP,
				.evtName = "ELKUP"
		},
		{ 		.evtMask =HAL_PORT_EVENT_RX_ALIGNED,
				.evtName = "RXALGN"
		},
		{ .evtMask = -1 } };

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
static int port_fsm_state_init(fsm_t *fsm, int eventMsk, int isNewState) {
	struct hal_port_state * ps = (struct hal_port_state*) fsm->priv;

	if ( isNewState )  {
		init_port(ps);
		/* Init tx state machine */
		hal_port_tx_setup_fsm_init(ps);
	}
	 /* if final state reached for tx setup state machine ON ALL PORTS
	  * then we can go to DISABLED state
	  */
	if (hal_port_tx_setup_fsm_run(ps)==1 )
		fsm_fire_state(fsm,HAL_PORT_STATE_DISABLED);
	return 0;
}

/*
 * DISABLED state
 *
 * if entering in state then reset port
 * if SFP inserted and not port power-down then the state=LINK_DOWN
 */
static int port_fsm_state_disabled(fsm_t *fsm, int eventMsk, int isNewState) {
	struct hal_port_state * ps = (struct hal_port_state*) fsm->priv;

	if ( isNewState ) {
		reset_port(ps);

		// make sure the PHY calibration circuitry is put in a KNOWN state
		if( ps->lpdc.isSupported )	{
			pcs_writel(ps,
					MDIO_LPC_CTRL_RESET_RX | MDIO_LPC_CTRL_DMTD_SOURCE_RXRECCLK
							| MDIO_LPC_CTRL_TX_ENABLE, MDIO_LPC_CTRL);

		}
		// Disable tracker
		rts_enable_ptracker(ps->hw_index, 0);
	}

	if ( _isHalEventSfpPresent(eventMsk)  && !_isHalEventPortPowerDown( eventMsk ))
		fsm_fire_state(fsm,HAL_PORT_STATE_LINK_DOWN);
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
static int port_fsm_state_link_down(fsm_t *fsm, int eventMsk, int isNewState) {
	struct hal_port_state * ps = (struct hal_port_state*) fsm->priv;

	// High priority event received
	if ( !_isHalEventSfpPresent(eventMsk) ) {
		fsm_fire_state(fsm,HAL_PORT_STATE_DISABLED);
		return 0;
	}

	if( _isHalEventPortPowerDown( eventMsk ))
	{
		pr_info("%s: Port wri%d PDOWN detected\n" ,__func__,ps->hw_index + 1 );
		// MII power down
		fsm_fire_state(fsm,HAL_PORT_STATE_DISABLED);
		return 0;
	}

	if (isNewState) {
		reset_port(ps); // clears ps->tx_cal_pending & ps->calib*, except ps->calib.bitslide_ps
		/* Init the rx state machine */
		hal_port_rx_setup_fsm_init(ps);

		/* Turn off both leds when detecting link down. The WRMODE led
		 might be overriden later by the rx_setup_state_fsm*/
		led_set_wrmode(ps->hw_index, SFP_LED_WRMODE_OFF);
		led_set_synched(ps->hw_index, 0);

	}

	/* if final state reached for tx setup state machine then
	 *     we can go LINK_UP state
	 */
	if (hal_port_rx_setup_fsm_run(ps) == 1 && _isHalEventLinkUp(eventMsk)) {

		/* measure bitslide regardless of LPDC support,
		 (if not supported, the value of the register will be zero) */
		uint32_t bit_slide_steps;
		/* Finish blinking for LPCC rx calibration, the led will be
		   set appropriately if it gets to the link_up state. */
		led_set_wrmode(ps->hw_index,SFP_LED_WRMODE_OFF);
		if (pcs_readl(ps, 16, &bit_slide_steps) >= 0) {
			bit_slide_steps = (bit_slide_steps >> 4) & 0x1f;
			/* FIXME: use proper register names */
			ps->calib.bitslide_ps = bit_slide_steps * (uint32_t) 800; /* 1 step = 800ps */
			/* any calibration, if any, has been done*/
			ps->calib.tx_calibrated = 1;
			ps->calib.rx_calibrated = 1;
			ps->calib.delta_rx_phy = ps->calib.phy_rx_min;
			ps->calib.delta_tx_phy = ps->calib.phy_tx_min;
			ps->tx_cal_pending = 0;
			ps->rx_cal_pending = 0;
			pr_info("%s:%s: bitslide= %u [ps]\n", __func__, ps->name,
					ps->calib.bitslide_ps);
			fsm_fire_state(fsm, HAL_PORT_STATE_LINK_UP);
		} else
			pr_warning("Cannot read bitslide, retrying...\n");
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
static int port_fsm_state_link_up(fsm_t *fsm, int eventMsk, int isNewState) {
	struct hal_port_state * ps = (struct hal_port_state*) fsm->priv;

	if ( ps->lpdc.isSupported) {
		if ( !_isHalEventPortEarlyLinkUp(eventMsk)) {
			fsm_fire_state(fsm,HAL_PORT_STATE_LINK_DOWN);
		}
		if ( !_isHalEventPortRxAligned(eventMsk)) {
			fsm_fire_state(fsm,HAL_PORT_STATE_LINK_DOWN);
		}
	}

	if ( !_isHalEventSfpPresent(eventMsk) ) {
		fsm_fire_state(fsm,HAL_PORT_STATE_DISABLED);
		return 0;
	}

	if( _isHalEventPortPowerDown( eventMsk ))
	{
		pr_info("%s: Port wri%d PDOWN detected\n" ,__func__,ps->hw_index + 1 );
		// MII power down
		fsm_fire_state(fsm,HAL_PORT_STATE_DISABLED);
		return 0;
	}

	if ( _isHalEventReset(eventMsk) || !_isHalEventLinkUp(eventMsk)) {
		fsm_fire_state(fsm,HAL_PORT_STATE_LINK_DOWN);
		return 0;
	}

	if ( isNewState ) {
		// Init PLL FSM
		hal_port_pll_fsm_init(ps);
	}

	if (isRtsStateValid() ) {
		struct channel *ch=&getRtsState().channels[ps->hw_index];
		ps->phase_val = ch->phase_loopback;
		ps->phase_val_valid =ch->flags & CHAN_PMEAS_READY ? 1 : 0;
		pr_debug("wri%d: phase=%d valid=%d\n",ps->hw_index+1,
			ps->phase_val,ps->phase_val_valid);
	}

	// Run PLL state machine
	hal_port_pll_fsm_run(ps);

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
static  int port_fsm_build_events(fsm_t *fsm) {
	struct hal_port_state * ps = (struct hal_port_state*) fsm->priv;

	int portEventMask=HAL_PORT_EVENT_TIMER;

	if ( ps->evt_linkUp > 0 ) {
		portEventMask |= HAL_PORT_EVENT_LINK_UP;
	}
	if ( ps->evt_reset ) {
		portEventMask |= HAL_PORT_EVENT_RESET;
		ps->evt_reset=0;
	}
	if (ps->sfpPresent)
		portEventMask |= HAL_PORT_EVENT_SFP_PRESENT;

	if ( ps->evt_powerDown )
		portEventMask |= HAL_PORT_EVENT_POWER_DOWN;

	if ( ps->lpdc.isSupported ) {
		uint32_t mioLpcStat;

		if ( pcs_readl(ps, MDIO_LPC_STAT,&mioLpcStat) >= 0 ) {
			if (mioLpcStat & MDIO_LPC_STAT_LINK_UP)
				portEventMask |= HAL_PORT_EVENT_EARLY_LINK_UP;
			if (mioLpcStat & MDIO_LPC_STAT_LINK_ALIGNED)
				portEventMask |= HAL_PORT_EVENT_RX_ALIGNED;
		}
	}

	return portEventMask;
}


/* Init the FSM on all ports. Called one time at startup */
void hal_port_state_fsm_init_all( struct hal_port_state * ports, halGlobalLPDC_t *globalLpdc)
{
	int portIndex;

	for (portIndex = 0; portIndex < HAL_MAX_PORTS; portIndex++) {
		struct hal_port_state* ps = &ports[portIndex];
		
		if ( ps->in_use)
		{
			char name[64];
			snprintf(name, sizeof(name), "PortFsm.%d", portIndex);
			if ( fsm_generic_create( &ps->fsm, name, port_fsm_build_events, port_fsm_states, port_fsm_events, ps ) ){
				pr_error("Cannot create main fsm !\n");
				exit(EXIT_FAILURE);
			}
			 
			fsm_fire_state(&ps->fsm , HAL_PORT_STATE_INIT);
		}
	}

	hal_port_tx_setup_init_all(ports, globalLpdc);
	hal_port_rx_setup_init_all(ports);
	hal_port_pll_setup_init_all(ports);
}

/* Call FSM for on all ports */
void hal_port_state_fsm_run_all( struct hal_port_state * ports) {
	int portIndex;

	hal_port_poll_rts_state(); // Update rts state on all ports

	/* Call state machine for all ports */
	for (portIndex = 0; portIndex < HAL_MAX_PORTS; portIndex++) {
		struct hal_port_state* ps = &ports[portIndex];

		if ( ps->in_use) {
			uint32_t bmcr;
			pcs_readl( ps, MII_BMCR, &bmcr );

			ps->evt_powerDown = (bmcr & BMCR_PDOWN);

			if ( get_port_link_state(ps, &ps->evt_linkUp) < 0 ) {
				// IOTCL error : We put -1 in the link state. It will be considered as invalid
				ps->evt_linkUp=-1;
			}

			fsm_generic_run(&ps->fsm);
		}
	}
}

/* Reset port
 * Called when entering in states DISABLED and LINK_DOWN and when the port is initialized the first time
 */
static void reset_port(struct hal_port_state * ps)
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

	/* Turn off link/wrmode LED. Master/slave
	 * color is set in the different place */
	led_set_wrmode(ps->hw_index,SFP_LED_WRMODE_OFF);
	/* turn off synced LED */
	led_set_synched(ps->hw_index, 0);

}

/* Port initialization */
static void init_port(struct hal_port_state * ps)
{
	reset_port(ps);
	ps->t2_phase_transition = DEFAULT_T2_PHASE_TRANS;
	ps->t4_phase_transition = DEFAULT_T4_PHASE_TRANS;
	ps->clock_period = REF_CLOCK_PERIOD_PS;
}


/* Checks if the link is up on inteface (if_name). Returns non-zero if yes. */
static int get_port_link_state(struct hal_port_state * ps,int *linkUp)
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

