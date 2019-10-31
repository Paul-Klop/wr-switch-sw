/*
 * hal.h
 *
 *  Created on: Oct 3, 2019
 *      Author: baujc
 */

#ifndef USERSPACE_WRSW_HAL_HAL_H_
#define USERSPACE_WRSW_HAL_HAL_H_

typedef int32_t Boolean;

typedef int32_t timeOut_t;

#define RESET_RX 1
#define ENABLE_RX 1
#define RESET_TX 1
#define ENABLE_TX 1
#define DMTD_SRC_TXOUTCLK 1
#define DMTD_SRC_RXRECCLK 1

#define BMCR_ANENABLE 1
#define BMCR_ANRESTART 1

extern void timer_init(timeOut_t tmo);
extern void timer_restart(timeOut_t tmo);
extern Boolean is_timer_expired(timeOut_t tmo);
extern void usleep(int);
extern pcs_write(int );
extern rts_enable_ptracker(void);
extern rts_disable_ptracker(void);
extern Boolean meas_phase(void);
extern Boolean check_phase_range(void);
extern Boolean is_tx_reseted(void);
extern Boolean txSetupDoneOnAllPorts(void);
extern void write_tx_calibration_file(void);
extern Boolean is_rx_aligned(void);
#endif /* USERSPACE_WRSW_HAL_HAL_H_ */
