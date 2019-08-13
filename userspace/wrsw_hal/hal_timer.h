/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 */

#ifndef HAL_TIMER_H
#define HAL_TIMER_H

#include <libwr/timeout.h>

typedef struct {
	int id;
	uint32_t tmoMs; // Time out value in ms
	int repeat; // restart timeout automatically
	timeout_t timer;
	void (*cb)(int timerId);
}timer_parameter_t;


extern void timerInit(timer_parameter_t *p,int nbTimers);
extern void timerScan(timer_parameter_t *p,int nbTimers);

#endif
