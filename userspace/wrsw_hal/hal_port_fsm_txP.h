/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 *
 */

#ifndef HAL_PORT_FSM_TXP_H
#define HAL_PORT_FSM_TXP_H

#include <libwr/wrs-msg.h>
#include "hal_port_gen_fsm.h"


#define TX_CAL_PHASE_MEAS_TIMEOUT 1000 /* ms */

#define TX_CAL_TOLLERANCE 300 /* ps */
#define TX_CAL_FIRST_CAL_TOLLERANCE 750 /* ps */
#define TX_CAL_FIRST_CAL_EXPECTED_PHASE 750 /* ps */

typedef enum {
HAL_PORT_TX_SETUP_STATE_START=0,
HAL_PORT_TX_SETUP_STATE_RESET_PCS,
HAL_PORT_TX_SETUP_STATE_WAIT_LOCK,
HAL_PORT_TX_SETUP_STATE_MEASURE_PHASE,
HAL_PORT_TX_SETUP_STATE_VALIDATE,
HAL_PORT_TX_SETUP_STATE_DONE
} hapPortTxSetupState_t;

typedef enum
{
HAL_PORT_TX_SETUP_EVENT_TIMER=(1<<0),
}halPortTxSetupEventMask_t ;

static	__inline__ int _isHalTxSetupEventTimer(halPortTxSetupEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_TX_SETUP_EVENT_TIMER;
}

#endif
