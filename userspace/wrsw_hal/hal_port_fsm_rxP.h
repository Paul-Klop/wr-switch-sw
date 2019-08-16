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
#include "hal_port_gen_fsm.h"

typedef enum {
HAL_PORT_RX_SETUP_STATE_START=0,
HAL_PORT_RX_SETUP_STATE_CALIB_NO_LPDC,
HAL_PORT_RX_SETUP_STATE_RESET_PCS,
HAL_PORT_RX_SETUP_STATE_WAIT_LOCK,
HAL_PORT_RX_SETUP_STATE_VALIDATE,
HAL_PORT_RX_SETUP_STATE_DONE
} hapPortRxSetupState_t;

typedef enum
{
HAL_PORT_RX_SETUP_EVENT_TIMER=(1<<0),
HAL_PORT_RX_SETUP_EVENT_LINK_UP=(1<<1),
HAL_PORT_RX_SETUP_EVENT_LINK_DOWN=(1<<2),
HAL_PORT_RX_SETUP_EVENT_EARLY_LINK_UP=(1<<3),
HAL_PORT_RX_SETUP_EVENT_RX_ALIGNED=(1<<4)
}halPortRxSetupEventMask_t ;

static	__inline__ int _isHalRxSetupEventTimer(halPortRxSetupEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_RX_SETUP_EVENT_TIMER;
}

static	__inline__ int _isHalRxSetupEventLinkDown(halPortRxSetupEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_RX_SETUP_EVENT_LINK_DOWN;
}

static	__inline__ int _isHalRxSetupEventLinkUp(halPortRxSetupEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_RX_SETUP_EVENT_LINK_UP;
}

static	__inline__ int _isHalRxSetupEventEarlyLinkUp(halPortRxSetupEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_RX_SETUP_EVENT_EARLY_LINK_UP;
}

static	__inline__ int _isHalRxSetupEventRxAligned(halPortRxSetupEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_RX_SETUP_EVENT_RX_ALIGNED;
}

#endif
