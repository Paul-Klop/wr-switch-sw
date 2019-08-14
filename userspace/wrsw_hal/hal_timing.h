/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 *
 */

#ifndef HAL_TIMING_H
#define HAL_TIMING_H

extern int hal_tmg_set_mode(uint32_t tm);
extern int hal_tmg_get_mode(void);
extern int hal_tmg_init(const char * logfilename);

#endif
