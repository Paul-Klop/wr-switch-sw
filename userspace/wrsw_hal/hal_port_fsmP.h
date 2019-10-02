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
	HAL_PORT_EVENT_TIMER = (1 << 0),
	HAL_PORT_EVENT_SFP_INSERTED = (1 << 1),
	HAL_PORT_EVENT_SFP_REMOVED = (1 << 2),
	HAL_PORT_EVENT_LINK_UP = (1 << 3),
	HAL_PORT_EVENT_LINK_DOWN = (1 << 4),
	HAL_PORT_EVENT_RESET = (1 << 5),
	HAL_PORT_EVENT_POWER_DOWN = (1 << 6),
	HAL_PORT_EVENT_EARLY_LINK_UP = (1 << 7),
	HAL_PORT_EVENT_RX_ALIGNED = (1 << 8)
} halPortEventMask_t;

static	inline int _isHalEventInitialized(halPortEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_EVENT_TIMER;
}

static	inline int _isHalEventSfpInserted(halPortEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_EVENT_SFP_INSERTED;
}

static	inline int _isHalEventSfpRemoved(halPortEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_EVENT_SFP_REMOVED;
}

static	inline int _isHalEventLinkUp(halPortEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_EVENT_LINK_UP;
}

static	inline int _isHalEventLinkDown(halPortEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_EVENT_LINK_DOWN;
}

static	inline int _isHalEventReset(halPortEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_EVENT_RESET;
}

static	inline int _isHalEventPortPowerDown(halPortEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_EVENT_POWER_DOWN;
}

static inline int _isHalEventPortEarlyLinkUp(halPortEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_EVENT_EARLY_LINK_UP;
}

static inline int _isHalEventPortRxAligned(halPortEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_EVENT_RX_ALIGNED;
}


#endif
