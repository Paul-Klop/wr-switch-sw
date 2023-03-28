#ifndef WRS_SNMP_H
#define WRS_SNMP_H

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>
#include "libwr/util.h"

/* Crap! -- everybody makes them different, and even ppsi::ieee wants them */
#undef FALSE
#undef TRUE

/* conflict between definition in net-snmp-agent-includes.h (which include
 * snmp_vars.h) and ppsi.h where INST is defined as a inline function */
#undef INST
#include <ppsi/ieee1588_types.h> /* for ClockIdentity */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>



/*  be safe, in case some other header had them slightly differntly */
#undef container_of
#undef offsetof
#undef ARRAY_SIZE

#define container_of(ptr, type, member) ({                      \
	const typeof( ((type *)0)->member ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type, member) );})

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))


#define WRS_N_PORTS  18

/* Scalar is just a stupid thing, but let's keep it */
extern void init_wrsScalar(void);

#define WRS_OID 1, 3, 6, 1, 4, 1, 96, 100

struct pickinfo {
	/* Following fields are used to format the output */
	int type; int offset; int len;
};

#define FIELD(_struct, _type, _field) {			\
	.type = _type,					\
	.offset = offsetof(struct _struct, _field),	\
	.len = sizeof(((struct _struct *)0)->_field),			\
	 }

/*
 * Print a message and execute assignment when the value of _object grows
 * faster than _rate.
 * Special version for wrsNetworkingStatusGroup.
 *
 * usage:
 * _log_type -- on of the SL_* string defines
 * _obj -- object to be checked in the arrays
 * _new -- array with the new values
 * _old -- array with the old values
 * _i -- port number
 * _t_delta -- time since last read, used to calculate rate
 * _rate -- maximum allowed rate
 *
 * _assign -- assignment to be performed when condition is met
 * NOTE:
 *
 * Valid example:
 * strcpy(slog_obj_name, "my_obj");
 * SLOG_IF_COMP_WNSG(SL_ER, a, new, old, 1, t_delta, 1.0, ret = 1);
 * When condition (new.a[i] - old.a[i])/t_delta > 1.0 is met, macro will assign 1 to ret and print
 * the following message:
 * SNMP: Error my_obj: a for port 1 (wri1) increased by 10.0, allowed 1.0
 */
 /* WNSG = WrsNetworkingStatusGroup */
#define SLOG_IF_COMP_WNSG(_log_type, _obj, _new, _old, _i, _t_delta, _rate, _assign) \
     do { if ((_new[_i]._obj - _old[_i]._obj)/_t_delta > _rate) {\
             _assign; \
             snmp_log(LOG_ERR, "SNMP: " _log_type " %s: " #_obj " for port %i " \
                      "(wri%i) increased by more than %.0f during %.0fs, " \
                      "allowed %.0f, old value %llu new value %llu\n", \
                      slog_obj_name, _i + 1, _i + 1, \
                      (_new[_i]._obj - _old[_i]._obj)/_t_delta, _t_delta, \
                      _rate, _old[_i]._obj, _new[_i]._obj); \
             } \
     } while (0)

/*
 * Print a message about an object
 *
 * Valid example2:
 * strcpy(slog_obj_name, "my_obj");
 * SLOG(SL_BUG);
 * prints:
 * SNMP: BUG my_obj
 */
#define SLOG(_log_type) \
     do { \
         snmp_log(LOG_ERR, "SNMP: " _log_type " %s\n", slog_obj_name); \
     } while (0)

/* String definitions for functions SLOG*, the same lenght of string
   for better visual experience... */
#define SL_ER    "Error   "
#define SL_W     "Warning "
#define SL_NA    "Warn(NA)"
#define SL_BUG   "Err(BUG)"
#define SL_INFO  "Info    "
#define SL_DEBUG "DEBUG   "

int32_t int_saturate(int64_t value);

#endif /* WRS_SNMP_H */
