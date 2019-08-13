/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 */



#include "hal_timer.h"

void timerInit(timer_parameter_t *p,int nbTimers) {
	int index;

	for ( index=0; index < nbTimers; index++ ) {
		libwr_tmo_init(&p->timer,p->tmoMs, p->repeat);
		p++;
	}
}


void timerScan(timer_parameter_t *p,int nbTimers) {
	int index;

	for ( index=0; index < nbTimers; index++ ) {
		if (libwr_tmo_expired(&p->timer) && p->cb!=NULL) {
			(*p->cb)(p->id);
		}
		p++;
	}
}
