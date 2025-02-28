#ifndef __LIBWR_HAL_SHMEM_H__
#define __LIBWR_HAL_SHMEM_H__

#include <string.h>
#include <hal_exports.h>
#include <libwr/sfp_lib.h>
#include <libwr/generic_fsm.h>


#include <libwr/timeout.h>

/* Port state machine states */
typedef enum {
	HAL_PORT_STATE_INIT=0,
	HAL_PORT_STATE_DISABLED,
	HAL_PORT_STATE_LINK_DOWN,
	HAL_PORT_STATE_LINK_UP,
} halPortState_t;

/* Read temperature from SFPs */
#define READ_SFP_DIAG_ENABLE 1
#define READ_SFP_DIAG_DISABLE 0

/* Monitor port in SNMP */
#define HAL_PORT_MONITOR_ENABLE 1
#define HAL_PORT_MONITOR_DISABLE 2

#define DEFAULT_T2_PHASE_TRANS 0
#define DEFAULT_T4_PHASE_TRANS 0

/* Port delay calibration parameters */
typedef struct hal_port_calibration {

	/* PHY delay measurement parameters for PHYs which require
	   external calibration (i.e. with the feedback network. */

	/* minimum possible delay introduced by the PHY. Expressed as time
	   (in picoseconds) between the beginning of the symbol on the serial input
	   and the rising edge of the RX clock at which the deserialized word is
	   available at the parallel output of the PHY. */
	uint32_t phy_rx_min;

	/* the same set of parameters, but for the TX path of the PHY */
	uint32_t phy_tx_min;

	/* Current PHY (clock-to-serial-symbol) TX and RX delays, in ps */
	uint32_t delta_tx_phy;
	uint32_t delta_rx_phy;

	/* bit slide expressed in picos */
	uint32_t bitslide_ps;

	/* Current board routing delays (between the DDMTD inputs to
	   the PHY clock inputs/outputs), in picoseconds */
	uint32_t delta_tx_board;
	uint32_t delta_rx_board;

	/* When non-zero: RX path is calibrated (delta_*_rx contain valid values) */
	int rx_calibrated;
	/* When non-zero: TX path is calibrated */
	int tx_calibrated;

	struct shw_sfp_caldata sfp;
	struct shw_sfp_header sfp_header_raw;
	struct shw_sfp_dom sfp_dom_raw;
} hal_port_calibration_t;

/* Low Phase Drift Calibration for tx */
typedef struct {
	int attempts;
	int cal_saved_phase;
	int cal_saved_phase_valid;
	int measured_phase;
	int expected_phase;
	int tolerance;
	int update_cnt;
	int expected_phase_valid;
	timeout_t calib_timeout;
}halPortLpdcTx_t;

/* Low Phase Drift Calibration for rx */
typedef struct {
	timeout_t link_timeout;
	timeout_t align_timeout;
    timeout_t earlyup_timeout;
	timeout_t align_to_link_timeout;
	timeout_t restart_timeout;
	int attempts;
}halPortLpdcRx_t;

typedef struct  {
	int numberOfLpdcPorts;
	uint32_t maskUsedPorts;
	uint32_t maskTxSetupDonePorts;
	int firstLpdcPort;
	int lastLpdcPort;
	int calFileSynced;
}halGlobalLPDC_t; /* Global data for Low phase drift calibration */

typedef struct {
	int isSupported; /* Set if Low Phase Drift Calibration is supported */
	fsm_t txSetupFSM;
	fsm_t rxSetupFSM;
	timeout_t minCalibRx_timeout;
	halPortLpdcTx_t *txSetup;
	halPortLpdcRx_t *rxSetup;
	halGlobalLPDC_t *globalLpdc;
}halPortLPDC_t; /* per-port data for Low phase drift calibration */

/* Internal port state structure */
struct hal_port_state {
	int in_use; /* non-zero: allocated */
	char name[16]; /* linux i/f name */
	uint8_t hw_addr[6]; /* MAC addr */
	int hw_index; /* ioctl() hw index : 0..n */

	int fd; /* file descriptor for ioctls() */
	int hw_addr_auto;

	/* port FSM state (HAL_PORT_STATE_xxxx) */
	fsm_t fsm;
	fsm_t pllFsm;

	int fiber_index;/* fiber type, used to get alpha for SFP frequency */
	int locked; /* 1: PLL is locked to this port */

	/* calibration data */
	hal_port_calibration_t calib;

	/* current DMTD loopback phase (ps) and whether is it valid or not */
	uint32_t phase_val;
	int phase_val_valid;
	int tx_cal_pending, rx_cal_pending;

	int lock_state; 	/* locking FSM state */

	uint32_t clock_period; /*reference lock period in picoseconds*/

	/* approximate DMTD phase value (on slave port) at which RX timestamp
	 * (T2) counter transistion occurs (picoseconds) */
	uint32_t t2_phase_transition;

	/* approximate phase value (on master port) at which RX timestamp (T4)
	 * counter transistion occurs (picoseconds) */
	uint32_t t4_phase_transition;
	int t24p_from_config; /* non-zero: t24p was loaded from config file */

	uint32_t ep_base;/* Endpoint's base address */

	/* whether SFP has diagnostic Monitoring capability */
	int has_sfp_diag;
	/* True if SFP is inserted */
	int sfpPresent;

	/* whether the port shall be monitored by SNMP */
	int monitor;

	/* PPSi instance information */
	int portMode; // Instance state
	int synchronized; // <>0 if port is synchronized
	int portInfoUpdated; // Set to 1 when updated

	/* Events to process */
	int evt_reset; /* Set if a reset is requested */
	int evt_lock; /* Set if the ptracker must be activated*/
	int evt_linkUp; /* Set if link is up ( driver call )*/
	int evt_powerDown; /* Set if port is in power down state (MII MCR.PDOWN ==1 ) */

	/* Low phase drift calibration data */
	halPortLPDC_t lpdc;
};

struct hal_temp_sensors {
	int fpga;	/* IC19 */
	int pll;	/* IC18 */
	int psl;	/* IC20 Power Supply Left (PSL) */
	int psr;	/* IC17 Power Supply Right (PSR) */
};

/* This is the overall structure stored in shared memory */
#define HAL_SHMEM_VERSION 18 /* Add entry_db for SFPs */

typedef enum {
	HAL_SHMEM_STATE_NOT_INITITALIZED=0,
	HAL_SHMEM_STATE_INITITALIZING,
	HAL_SHMEM_STATE_INITITALIZED
}halShmemState_t;

struct hal_shmem_header {
	int nports;
	int hal_mode;
	halShmemState_t shmemState;
	struct hal_port_state *ports;
	struct hal_temp_sensors temp;
	struct shw_sfp_caldata *shw_sfp_cal_list;
	int read_sfp_diag;
};

static inline int get_port_state(struct hal_port_state *ps)
{
	return fsm_get_state( &ps->fsm );
}

static inline int state_up(struct hal_port_state *ps)
{
	return get_port_state(ps) == HAL_PORT_STATE_LINK_UP;
}

static inline struct hal_port_state *hal_lookup_port(
			struct hal_port_state *ports, int nports,
			const char *name)
{
	int i;
	struct hal_port_state *p=ports;

	for (i = 0; i < nports; i++) {
		if (p->in_use && (!strcmp(name, p->name))) {
			return p;
		}
		p++;
	}
	return NULL;
}

#endif /*  __LIBWR_HAL_SHMEM_H__ */
