/* PPS Generator - a.k.a. WR Real-time clock driver */

#ifndef __LIBWR_PPS_GEN_H
#define __LIBWR_PPS_GEN_H

#include <stdio.h>
#include <inttypes.h>

#define PPSG_ADJUST_SEC 0x1
#define PPSG_ADJUST_NSEC 0x2

#define PPSG_PPS_IN_TERM_50OHM_ENABLE 0x1
#define PPSG_PPS_IN_TERM_50OHM_DISABLE 0x0

/* Initializes the PPS Generator. 0 on success, negative on failure. */
int shw_pps_gen_init(void);

/* Adjusts the <counter> (PPSG_ADJUST_SEC/NSEC) by (how_much) seconds/nanoseconds */
int shw_pps_gen_adjust(int counter, int64_t how_much);

/* Returns 1 when the PPS is busy adjusting its time counters, 0 if PPS gen idle */
int shw_pps_gen_busy(void);

/* Enables/disables PPS Generator PPS output */
int shw_pps_gen_enable_output(int enable);

/* Reads the current time and stores at <seconds,nanoseconds>. */
void shw_pps_gen_read_time(uint64_t * seconds, uint32_t * nanoseconds);

/* Enables PPS_IN 50Ohm termination */
void shw_pps_gen_in_term_enable(int enable);

/* Reads status of PPS_IN 50Ohm termination */
int shw_pps_gen_in_term_read(void);

/* Enables PPS_IN 50Ohm termination based on dot-config option */
int shw_pps_gen_in_term_init(void);

#endif /* __LIBWR_PPS_GEN_H */
