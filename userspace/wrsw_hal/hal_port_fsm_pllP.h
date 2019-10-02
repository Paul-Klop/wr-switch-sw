/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 *
 */

#ifndef HAL_PORT_FSM_PLLP_H
#define HAL_PORT_FSM_PLLP_H

#include <libwr/wrs-msg.h>
#include <libwr/generic_fsm.h>

#include "hal_port_fsm_pll.h"

typedef enum {
	HAL_PORT_PLL_STATE_UNLOCKED=0,
	HAL_PORT_PLL_STATE_LOCKING,
	HAL_PORT_PLL_STATE_LOCKED
} halPortPllState_t;

typedef enum
{
	HAL_PORT_PLL_EVENT_TIMER=(1<<0),
	HAL_PORT_PLL_EVENT_UNLOCKED=(1<<1),
	HAL_PORT_PLL_EVENT_LOCK=(1<<2),
	HAL_PORT_PLL_EVENT_LOCKED=(1<<3),
	HAL_PORT_PLL_EVENT_DISABLE=(1<<4),
}halPortPllEventMask_t ;


static	inline int _isHalPllEventLock(halPortPllEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_PLL_EVENT_LOCK;
}

static	inline int _isHalPllEventLocked(halPortPllEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_PLL_EVENT_LOCKED;
}

static	inline int _isHalPllEventUnlock(halPortPllEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_PLL_EVENT_UNLOCKED;
}

static	inline int _isHalPllEventDisable(halPortPllEventMask_t eventMsk) {
	return eventMsk & HAL_PORT_PLL_EVENT_DISABLE;
}

#endif
