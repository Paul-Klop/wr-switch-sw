/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 *
 */

#ifndef HAL_PORT_GEN_FSM_H
#define HAL_PORT_GEN_FSM_H

#include <libwr/wrs-msg.h>
#include <libwr/hal_shmem.h>

#define FSM_GEN_DEBUG 0


#define FSM_SET_FCT_NAME(name) .fct=name, .fctName=#name
/*
 * Used to define the callback to call for a givent state
 */
typedef struct {
	int state;
	char * stateName;
	int (*fct)(void * vpfg/* struct halPortFsmGen_t *pfg*/, int eventMsk, int newState);
	char *fctName;
}halPortStateTable_t;

typedef struct {
	int evtMask;
	char * evtName;
}halPortEventTable_t;

/*
 * Structure containing all information needed by the state machine
 */

typedef struct {
	char * fsm_name;
	int (*fctBuilEvents)(void * vpfg/* struct halPortFsmGen_t *pfg*/);
	halPortStateTable_t *pt;
	halPortEventTable_t *pe;
	struct hal_port_state *ps;
	halPortFsmState_t *st;
}halPortFsmGen_t;

extern halPortFsmGen_t *pfg;

static __inline__ int _getState(halPortFsmGen_t *pfg) {
	return pfg->st->state;
}

static __inline__ int _setState(halPortFsmGen_t *pfg, int newState) {
	return pfg->st->state=newState;
}


static __inline__ int _getNextState(halPortFsmGen_t *pfg) {
	return pfg->st->nextState;
}

static __inline__ char * _getStateName(halPortFsmGen_t *pfg) {
	halPortStateTable_t *pt=pfg->pt+_getState(pfg);
	return pt->stateName;
}


static __inline__ void _consumeState(halPortFsmGen_t *pfg) {
	pfg->st->state=_getNextState(pfg);
	if (FSM_GEN_DEBUG)
		printf("%s.%s: Enter state %s\n",pfg->ps->name,pfg->fsm_name,_getStateName(pfg));
}

static __inline__ void _fireState(halPortFsmGen_t *pfg, int newState) {
	pfg->st->nextState=newState;
}

static __inline__ int _isPendingState(halPortFsmGen_t *pfg) {
	return _getState(pfg) != _getNextState(pfg);
}

/* prototypes */
int hal_port_generic_fsm( halPortFsmGen_t *pfg);


#endif
