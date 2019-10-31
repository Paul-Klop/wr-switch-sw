/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 *
 */

#ifndef GENERIC_FSM_H
#define GENERIC_FSM_H

#include <stdio.h>
#include <string.h>

#define FSM_GEN_DEBUG 0

#define FSM_SET_FCT_NAME(name) .handler=name, .fctName=#name

/*
 * Used to define the callback to call for a givent state
 */

struct fsm_s;

/* States used by the generic FSM */
typedef struct {
	int state;
	int nextState;
} fsm_state_t;

typedef int (*fsm_state_handler_t)( struct fsm_s* fsm, int eventMsk, int newState);
typedef int (*fsm_build_event_handler_t)( struct fsm_s *fsm );

typedef struct {
	int state;
	char * stateName;
	fsm_state_handler_t handler;
	char *fctName;
} fsm_state_table_entry_t;

typedef struct {
	int evtMask;
	char * evtName;
} fsm_event_table_entry_t;

/*
 * Structure containing all information needed by the state machine
 */

typedef struct fsm_s {
	char * fsm_name;
	fsm_build_event_handler_t build_events_handler;
	fsm_state_table_entry_t *state_table;
	fsm_event_table_entry_t *event_table;
	fsm_state_t st;
	int enabled;
	int lastEventMask;
	void *priv;
} fsm_t;


/* returns current state of the FSM */
static inline int fsm_get_state(fsm_t *fsm)
{
	return fsm->st.state;
}

/* sets the current (immediate) state of the FSM */
static inline int fsm_set_state(fsm_t *fsm, int newState)
{
	fsm->st.state = newState;
	return fsm->st.state;
}

/* Set state and next state to -1 */
static inline int fsm_init_state(fsm_t *fsm)
{
	fsm->st.state = fsm->st.nextState=-1;
	return fsm->st.state;
}

/* gets the next (scheduled) state of the FSM */
static inline int fsm_get_next_state(fsm_t *fsm)
{
	return fsm->st.nextState;
}

/* returns a human-readable state name for a given state */
static inline const char * fsm_get_state_name(fsm_t *fsm)
{
	static const char *unknown_msg  = "<Unknown>";
	static const char *not_init_msg = "<NotInit>";
	int st = fsm_get_state(fsm);
	if (st == -1 )
		return not_init_msg;
	if (st < -1 )
		return unknown_msg;
	fsm_state_table_entry_t *pt = &fsm->state_table[ fsm_get_state(fsm) ];
	return pt->stateName;
}

static inline const char *fsm_get_event_mask_as_string(fsm_t *fsm)
{
	static char str[1024];
	fsm_event_table_entry_t *evt;
	int lastEventMask=fsm->lastEventMask;

	str[0]=0;

	for( evt = fsm->event_table; evt->evtMask > 0; evt++ )
	{
		if ( lastEventMask == 0 ) break;
		if (lastEventMask & evt->evtMask)
		{
			if ( str[0]!=0 )
				strcat(str," ");
			strcat(str, evt->evtName);
			lastEventMask &=~evt->evtMask;
		}
	}
	if (lastEventMask!=0)
		strcat(str," ???");

	return str;
}


/* updates the current state of the FSM with the next (scheduled) state */
static inline void fsm_consume_state(fsm_t *fsm)
{
	fsm->st.state = fsm_get_next_state(fsm);
	if (FSM_GEN_DEBUG)
		printf("%s: Enter state %s\n", fsm->fsm_name, fsm_get_state_name(fsm));
}

/* schedules next state of the FSM to newState */
static inline void fsm_fire_state(fsm_t *fsm, int newState)
{
	fsm->st.nextState=newState;
}

/* returns nonzero if there's a next (scheduled) state present */
static inline int fsm_has_pending_state(fsm_t *fsm)
{
	return fsm_get_state(fsm) != fsm_get_next_state(fsm);
}

/* prototypes */

/* Does a single turn of the FSM */

int fsm_generic_create(fsm_t *fsm, const char *name,
		fsm_build_event_handler_t build_events_handler,
		fsm_state_table_entry_t *state_table,
		fsm_event_table_entry_t *event_table, void *priv);

int fsm_generic_run ( fsm_t *fsm );


#endif
