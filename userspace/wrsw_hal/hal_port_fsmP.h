/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 *
 */

#ifndef HAL_PORT_FSMP_H
#define HAL_PORT_FSMP_H

#include "hal_port_fsm.h"

typedef enum
{
HAL_PORT_EVENT_TIMER=(1<<0),
HAL_PORT_EVENT_SFP_INSERTED=(1<<1),
HAL_PORT_EVENT_SFP_REMOVED=(1<<3),
HAL_PORT_EVENT_LINK_UP=(1<<4),
HAL_PORT_EVENT_LINK_DOWN=(1<<5),
HAL_PORT_EVENT_RESET=(1<<6)
}halPortEventMask_t ;

static	__inline__ int _isHalEventInitialized(halPortEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_EVENT_TIMER;
}

static	__inline__ int _isHalEventSfpInserted(halPortEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_EVENT_SFP_INSERTED;
}

static	__inline__ int _isHalEventSfpRemoved(halPortEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_EVENT_SFP_REMOVED;
}

static	__inline__ int _isHalEventLinkUp(halPortEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_EVENT_LINK_UP;
}

static	__inline__ int _isHalEventLinkDown(halPortEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_EVENT_LINK_DOWN;
}

static	__inline__ int _isHalEventReset(halPortEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_EVENT_RESET;
}

#endif
