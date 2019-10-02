/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 */

#include <stdlib.h>
#include <string.h>

#include <libwr/wrs-msg.h>
#include <libwr/generic_fsm.h>


static char * fsm_generic_build_events( fsm_t *pfg, int *eventMask );

/* Generic engine used by all ports states machine */
int fsm_generic_run( fsm_t *pfg) {
	int ret=0;
    int  eventMask,isNewState;
    fsm_generic_build_events(pfg, &eventMask);

	pfg->lastEventMask = eventMask;

	isNewState=fsm_has_pending_state(pfg);

	/* Check if state has changed */
	if ( isNewState ) {
		fsm_consume_state(pfg); // update the current state with the scheduled one
	}

	/* Call state entry */
	if (eventMask || isNewState) {
		fsm_state_table_entry_t *pt;;

		for( pt = pfg->state_table; pt->state >= 0; pt++)
		{
			if (pt->state == fsm_get_state(pfg) ) {
/*					if ( FSM_GEN_DEBUG  && pt->fctName!=NULL)
					printf("%s:  Calling %s (newState=%d, evts=%s),\n",
							pfg->fsm_name,
							pt->fctName, isNewState,evtStr);*/
				ret= (*pt->handler)(pfg,eventMask,isNewState);
				if ( fsm_has_pending_state(pfg) ) {
					/* Consume state immediately */
					return fsm_generic_run(pfg);
				}
				break;
			}
		}
	}
	return ret;
}

static char * fsm_generic_build_events( fsm_t *pfg, int *eventMask) 
{
	*eventMask=0;

	if ( pfg->build_events_handler )
		*eventMask=(*pfg->build_events_handler)(pfg);
	if ( FSM_GEN_DEBUG ) {
		static char str[128];
		int copy=*eventMask;


		fsm_event_table_entry_t *pe;

		str[0]=0;

		for( pe = pfg->event_table; pe->evtMask > 0; pe++)
		{
			if (copy==0) break;
			if ( (pe->evtMask & copy) !=0 ) {
				if ( str[0]!=0)
					strcat(str,"+");
				strcat(str,pe->evtName);
				copy &=~pe->evtMask;
			}
		}
		return str;
	} else {
		static char str[1]="";
		return str;
	}
}

/*
 * Fill the FSM structure
 * returns 0 on success or -1 on error
 */
int fsm_generic_create(fsm_t *fsm,
	const char *name,
	fsm_build_event_handler_t build_events_handler,
	fsm_state_table_entry_t *state_table,
	fsm_event_table_entry_t *event_table,
	void *priv)
	{
		if (fsm->fsm_name!=NULL )
			free(fsm->fsm_name); // In case the structure is filled more than one time
		fsm->fsm_name = strdup(name);
		if ( fsm->fsm_name==NULL )
			return -1;
		fsm->event_table = event_table;
		fsm->state_table = state_table;
		fsm->build_events_handler = build_events_handler;
		fsm->enabled = 1;
		fsm->priv = priv;
		fsm_init_state(fsm);

		return 0;
	}
