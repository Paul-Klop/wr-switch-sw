/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 *
 */

#ifndef HAL_PORT_FSM_RXP_H
#define HAL_PORT_FSM_RXP_H

#include <libwr/wrs-msg.h>
#include <libwr/generic_fsm.h>

typedef enum {
HAL_PORT_RX_SETUP_STATE_INIT=0,
HAL_PORT_RX_SETUP_STATE_START,
HAL_PORT_RX_SETUP_STATE_RESET_PCS,
HAL_PORT_RX_SETUP_STATE_WAIT_LOCK,
HAL_PORT_RX_SETUP_STATE_VALIDATE,
HAL_PORT_RX_SETUP_STATE_RESTART,
HAL_PORT_RX_SETUP_STATE_DONE
} halPortRxSetupState_t;

typedef enum
{
HAL_PORT_RX_SETUP_EVENT_TIMER=(1<<0),
HAL_PORT_RX_SETUP_EVENT_LINK_UP=(1<<1),
HAL_PORT_RX_SETUP_EVENT_EARLY_LINK_UP=(1<<2),
HAL_PORT_RX_SETUP_EVENT_RX_ALIGNED=(1<<3)
}halPortRxSetupEventMask_t ;

static inline int _isHalRxSetupEventTimer(halPortRxSetupEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_RX_SETUP_EVENT_TIMER;
}

static inline int _isHalRxSetupEventLinkUp(halPortRxSetupEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_RX_SETUP_EVENT_LINK_UP;
}

static inline int _isHalRxSetupEventEarlyLinkUp(halPortRxSetupEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_RX_SETUP_EVENT_EARLY_LINK_UP;
}

static inline int _isHalRxSetupEventRxAligned(halPortRxSetupEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_RX_SETUP_EVENT_RX_ALIGNED;
}

#endif
