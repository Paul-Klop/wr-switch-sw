/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 */


#include "hal_port_gen_fsm.h"


static char * _generic_build_events( halPortFsmGen_t *pfg, int *portEventMask);

/* Generic engine used by all ports states machine */
int hal_port_generic_fsm( halPortFsmGen_t *pfg) {
	int ret=0;
	if ( pfg->ps->in_use ) {
       int  portEventMask,isNewState;

		char *evtStr=_generic_build_events(pfg, &portEventMask);

		/* Check if state has changed */
		if ( (isNewState=_isPendingState(pfg))==1 ) {
			_consumeState(pfg);
		}
		/* Call state entry */
		if (portEventMask || isNewState) {
			halPortStateTable_t *pt=pfg->pt;
			while (pt->state!=-1 ) {
				if (pt->state==_getState(pfg) ) {
					if ( FSM_GEN_DEBUG && pt->fctName!=NULL)
						printf("%s.%s:  Calling %s (newState=%d, evts=%s),\n",
								pfg->ps->name,pfg->fsm_name,
								pt->fctName, isNewState,evtStr);
					ret=(*pt->fct)(pfg,portEventMask,isNewState);
					if ( _isPendingState(pfg) ) {
						/* Consume state immediately */
						return hal_port_generic_fsm(pfg);
					}
					break;
				}
				pt++;
			}
		}
	}
	return ret;
}

static char * _generic_build_events( halPortFsmGen_t *pfg, int *portEventMask) {

	*portEventMask=0;

	if ( pfg->fctBuilEvents )
		*portEventMask=(*pfg->fctBuilEvents)(pfg);
	if ( FSM_GEN_DEBUG ) {
		static char str[128];
		int copy=*portEventMask;
		halPortEventTable_t *pe=pfg->pe;

		str[0]=0;
		while (pe->evtMask!=-1 ) {
			if (copy==0) break;
			if ( (pe->evtMask & copy) !=0 ) {
				if ( str[0]!=0)
					strcat(str,"+");
				strcat(str,pe->evtName);
				copy &=~pe->evtMask;
			}
			pe++;
		}
		return str;
	} else {
		static char str[1]="";
		return str;
	}
}
