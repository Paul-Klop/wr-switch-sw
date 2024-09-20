/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 *
 */

#include <rt_ipc.h>

#include <libwr/hal_shmem.h>
#include <libwr/config-lpcalib.h>
#include <libwr/wrs-msg.h>
#include <libwr/generic_fsm.h>

#include "hal_exports.h"
#include "driver_stuff.h"
#include "hal_port_leds.h"
#include "hal_timing.h"
#include "hal_main.h"
#include "hal_ports.h"
#include "hal_port_fsm_txP.h"
#include "hal_port_fsm.h"
#include <libwr/shw_io.h>

/**
 * State machine
 * States :
 *    - HAL_PORT_TX_SETUP_STATE_START:
 *    	Inital state
 *    - .....
 *    - HAL_PORT_TX_SETUP_STATE_DONE:
 *    	TX setup terminated
 * Events :
 *    - timer      : triggered regularly to execute background work
 */

/* external prototypes */
static int port_tx_setup_fsm_build_events(fsm_t *fsm);
static int port_tx_setup_fsm_state_start(fsm_t *fsm, int eventMsk, int isNewState);
static int port_tx_setup_fsm_state_validate(fsm_t *fsm, int eventMsk, int isNewState);
static int port_tx_setup_fsm_state_reset_pcs(fsm_t *fsm, int eventMsk, int isNewState);
static int port_tx_setup_fsm_state_wait_lock(fsm_t *fsm, int eventMsk, int isNewState);
static int port_tx_setup_fsm_state_measure_phase(fsm_t *fsm, int eventMsk, int isNewState);
static int port_tx_setup_fsm_state_done(fsm_t *fsm, int eventMsk, int isNewState);
static int port_tx_setup_fsm_state_wait_other_ports(fsm_t *fsm, int eventMsk, int isNewState);

static fsm_state_table_entry_t port_tx_setup_fsm_states[] =
{
		{ .state=HAL_PORT_TX_SETUP_STATE_START,
				.stateName="START",
				FSM_SET_FCT_NAME(port_tx_setup_fsm_state_start)
		},
		{ .state=HAL_PORT_TX_SETUP_STATE_RESET_PCS,
				.stateName="RESET_PCS",
				FSM_SET_FCT_NAME(port_tx_setup_fsm_state_reset_pcs)
		},
		{ .state=HAL_PORT_TX_SETUP_STATE_MEASURE_PHASE,
				.stateName="MEASURE_PHASE",
				FSM_SET_FCT_NAME(port_tx_setup_fsm_state_measure_phase)
		},
		{ .state=HAL_PORT_TX_SETUP_STATE_WAIT_LOCK,
				.stateName="WAIT_LOCK",
				FSM_SET_FCT_NAME(port_tx_setup_fsm_state_wait_lock)
		},
		{ .state=HAL_PORT_TX_SETUP_STATE_VALIDATE,
				.stateName="VALIDATE",
				FSM_SET_FCT_NAME(port_tx_setup_fsm_state_validate)
		},
		{ .state=HAL_PORT_TX_SETUP_STATE_WAIT_OTHER_PORTS,
				.stateName="WAIT OTHER PORTS",
				FSM_SET_FCT_NAME(port_tx_setup_fsm_state_wait_other_ports)
		},
		{ .state=HAL_PORT_TX_SETUP_STATE_DONE,
				.stateName="DONE",
				FSM_SET_FCT_NAME(port_tx_setup_fsm_state_done)
		},
		{.state=-1}
};

static fsm_event_table_entry_t port_tx_setup_fsm_events[] = {
		{
				.evtMask = HAL_PORT_TX_SETUP_EVENT_TIMER,
				.evtName="TIM"
		},
		{ .evtMask = -1 } };

//TODO-ML: the below structure is redundant, consider reogranization to use
//         hal_port_rts_state
static struct rts_pll_state _pll_state;
/* path to the file where Low Phase Drift calib parameters are stored */
static char *_calibrationFileName = "/wr/etc/tx_phase_cal.conf";
struct config_file *_calibrationConfig; // Calibration config form file
static char *_fpgaStatusFileName = "/tmp/load_fpga_status";

static inline void updatePllState(struct hal_port_state * ps) {
	// update PLL state once for all ports
	rts_get_state(&_pll_state);
}

static inline void txSetupDone(struct hal_port_state * ps) {
	halGlobalLPDC_t * gl = ps->lpdc.globalLpdc;
	gl->maskTxSetupDonePorts|=((uint32_t)1)<<ps->hw_index;
}

static inline void txSetupNotDone(struct hal_port_state * ps) {
	halGlobalLPDC_t * gl = ps->lpdc.globalLpdc;
	gl->maskTxSetupDonePorts&=~(((uint32_t)1)<<ps->hw_index);
}

static inline int txSetupDoneOnAllPorts(struct hal_port_state * ps) {
	halGlobalLPDC_t * gl = ps->lpdc.globalLpdc;
	return gl->maskUsedPorts==gl->maskTxSetupDonePorts;
}

/* prototypes */
static void _write_tx_calibration_file(struct hal_port_state * ports);
static void _load_tx_calibration_file(struct hal_port_state * ports);
static int _within_range(int x, int minval, int maxval, int wrap);

/*
 * START state
 *
 * If entering in state then
 *      Set LPDC supported accordingly to the hardware
 * fi
 * if  LPDC is not supported then state=DONE
 *
 */
static int port_tx_setup_fsm_state_start(fsm_t *fsm, int eventMsk, int isNewState) {
	struct hal_port_state * ps = (struct hal_port_state*) fsm->priv;

	// Force timing mode to FR
	if ( hal_tmg_get_mode(NULL)!=HAL_TIMING_MODE_FREE_MASTER)
		hal_tmg_set_mode(HAL_TIMING_MODE_FREE_MASTER);
	ps->locked=0;

	/* Disable TX light on this port */
	shw_sfp_gpio_set(ps->hw_index, SFP_TX_DISABLE);

	/* Bring LED to a known state (i.e. OFF), just in case*/
	led_set_wrmode(ps->hw_index,SFP_LED_WRMODE_OFF);

	txSetupNotDone(ps);
	if ( !ps->lpdc.isSupported ) {
		// NO LPDC support
		fsm_fire_state(fsm,HAL_PORT_TX_SETUP_STATE_WAIT_OTHER_PORTS);
		return 0;
	} else {
		// LPDC support
		halPortLpdcTx_t *txSetup=ps->lpdc.txSetup;

		txSetup->attempts=0;
		txSetup->expected_phase = 0;
		txSetup->expected_phase_valid = 0;
		txSetup->tolerance = TX_CAL_TOLERANCE;
		txSetup->update_cnt = 0;

		_pll_state.channels[ps->hw_index].flags = 0;

		rts_enable_ptracker(ps->hw_index, 0);
		rts_ptracker_set_average_samples(ps->hw_index, HAL_CAL_DMTD_SAMPLES);

		lpdc_writel(ps,
			    LPDC_MDIO_CTRL_RX_SW_RESET
			    | LPDC_MDIO_CTRL_DMTD_SOURCE_TXOUTCLK,
			    LPDC_MDIO_CTRL);

		/* start indicating LPDC rx calibration. */
		led_set_wrmode(ps->hw_index,SFP_LED_WRMODE_CALIB);
		fsm_fire_state(fsm, HAL_PORT_TX_SETUP_STATE_RESET_PCS);
	}
	return 0;
}

/*
 * RESET_PCS state
 *
 *
 */
static int port_tx_setup_fsm_state_reset_pcs(fsm_t *fsm, int eventMsk, int isNewState) {
	struct hal_port_state * ps = (struct hal_port_state*) fsm->priv;
	
	rts_enable_ptracker(ps->hw_index, 0);
	lpdc_writel(ps,
		    LPDC_MDIO_CTRL_TX_SW_RESET
		    | LPDC_MDIO_CTRL_RX_SW_RESET
		    | LPDC_MDIO_CTRL_DMTD_SOURCE_TXOUTCLK,
		    LPDC_MDIO_CTRL);
	shw_udelay(1);
	lpdc_writel(ps,
		    LPDC_MDIO_CTRL_RX_SW_RESET
		    | LPDC_MDIO_CTRL_DMTD_SOURCE_TXOUTCLK,
		    LPDC_MDIO_CTRL);

	fsm_fire_state(fsm,HAL_PORT_TX_SETUP_STATE_WAIT_LOCK);
	return 0;
}

/*
 * WAIT_LOCK state
 *
 *
 */
static int port_tx_setup_fsm_state_wait_lock(fsm_t *fsm, int eventMsk, int isNewState) {
	struct hal_port_state * ps = (struct hal_port_state*) fsm->priv;
	uint32_t value;
	if (lpdc_readl(ps, LPDC_MDIO_STAT,&value) >= 0 ) {
		if ( (value & LPDC_MDIO_STAT_TX_RST_DONE)!=0 ) {
			ps->lpdc.txSetup->attempts++;

			rts_enable_ptracker(ps->hw_index, 1);

			_pll_state.channels[ps->hw_index].flags = 0;
			
			fsm_fire_state(fsm, HAL_PORT_TX_SETUP_STATE_MEASURE_PHASE);
		}
	}
	return 0;
}

/*
 * MEASURE_PHASE state
 *
 *
 */
static int port_tx_setup_fsm_state_measure_phase(fsm_t *fsm, int eventMsk, int isNewState) {
	struct hal_port_state * ps = (struct hal_port_state*) fsm->priv;
	halPortLpdcTx_t *txSetup=ps->lpdc.txSetup;

	updatePllState(ps);

	if ( isNewState ) {
		libwr_tmo_init(&txSetup->calib_timeout,	TX_CAL_PHASE_MEAS_TIMEOUT, 1);
	}
	if (!(_pll_state.channels[ps->hw_index].flags & CHAN_PMEAS_READY)) {
		if (libwr_tmo_expired(&txSetup->calib_timeout) )
		{
			pr_info("Port %d: tx phase measurement timeout expired,"
			" retrying\n", ps->hw_index+1);
			fsm_fire_state(fsm,  HAL_PORT_TX_SETUP_STATE_RESET_PCS);
			return 0; // retry
		}
		return 0; // keep waiting
    }

	txSetup = ps->lpdc.txSetup;
	int phase = _pll_state.channels[ps->hw_index].phase_loopback;
	txSetup->measured_phase = phase;

	if (!txSetup->expected_phase_valid) {
		if (txSetup->cal_saved_phase_valid) {
			// got calibration file already? Aim EXACTLY for the phase
			// bin we used in the first calibration
			pr_info("Using phase from file :%d\n", txSetup->cal_saved_phase);
			txSetup->expected_phase = txSetup->cal_saved_phase;
		} else {
			// First time calibrating? Give ourselves some freedom,
			// let's say the first 1.5 ns of the 16 ns ref clock 
			// cycle, so that we have enough setup time

			txSetup->tolerance = TX_CAL_FIRST_CAL_TOLERANCE;
			txSetup->expected_phase = TX_CAL_FIRST_CAL_EXPECTED_PHASE;
		}
		txSetup->expected_phase_valid = 1;
	}

	int phase_min = txSetup->expected_phase - txSetup->tolerance;
	int phase_max = txSetup->expected_phase + txSetup->tolerance;

//	pr_info("TX Calibration: upd wri%d phase %d after %d "
//			"attempts target %d tolerance %d\n",
//			ps->hw_index+1, txSetup->measured_phase,
//			txSetup->attempts, txSetup->expected_phase, txSetup->tolerance);

	if(_within_range(phase, phase_min, phase_max, 16000)) {
		pr_info("FIX port wri%d phase %d after %d attempts "
				"(temp = %.3f degC)\n", ps->hw_index + 1, txSetup->measured_phase,
				txSetup->attempts, hal_get_fpga_temperature() / 256.0);
		rts_enable_ptracker(ps->hw_index, 0);
		rts_enable_ptracker(ps->hw_index, 1);

		_pll_state.channels[ps->hw_index].flags = 0;

		fsm_fire_state(fsm,  HAL_PORT_TX_SETUP_STATE_VALIDATE);
	} else
		fsm_fire_state(fsm,  HAL_PORT_TX_SETUP_STATE_RESET_PCS);

	return 0;
}

/*
 * Validate state
 *
 *
 */
static int port_tx_setup_fsm_state_validate(fsm_t *fsm, int eventMsk, int isNewState) {
	struct hal_port_state * ps = (struct hal_port_state*) fsm->priv;
	halPortLpdcTx_t *txSetup;

	updatePllState(ps);
	if (!(_pll_state.channels[ps->hw_index].flags & CHAN_PMEAS_READY))
		return 0; // keep waiting

	txSetup = ps->lpdc.txSetup;
	txSetup->measured_phase = _pll_state.channels[ps->hw_index].phase_loopback;
	pr_info("Port %d: TX calibration complete\n", ps->hw_index + 1);
	rts_enable_ptracker(ps->hw_index, 0);

	// enable the PCS on the port
	lpdc_writel(ps,
		    LPDC_MDIO_CTRL_RX_SW_RESET
		    | LPDC_MDIO_CTRL_TX_ENABLE
		    | LPDC_MDIO_CTRL_DMTD_SOURCE_RXRECCLK,
		    LPDC_MDIO_CTRL);

	led_set_wrmode(ps->hw_index,SFP_LED_WRMODE_OFF);

	fsm_fire_state(fsm, HAL_PORT_TX_SETUP_STATE_WAIT_OTHER_PORTS);
	return 0;
}
/*
 * Wait for all LPDC-supporting ports to be calibrated
 */

static int port_tx_setup_fsm_state_wait_other_ports(fsm_t *fsm, int eventMsk, int isNewState)
{
	struct hal_port_state * ps = (struct hal_port_state*) fsm->priv;

	if ( isNewState )
		txSetupDone(ps);

	if (txSetupDoneOnAllPorts(ps) ) {
		if (ps->lpdc.globalLpdc->numberOfLpdcPorts )
			_write_tx_calibration_file(ps);
		/* Enable TX light on this port */
		shw_sfp_gpio_set(ps->hw_index, 0);
		fsm_fire_state(fsm,HAL_PORT_TX_SETUP_STATE_DONE);
	}
	return 0;
}


/*
 * DONE state
 *
 * Return final state machine reached
 */
static int port_tx_setup_fsm_state_done(fsm_t *fsm, int eventMsk, int isNewState) {
	/* Set status led to green when the tx LPDC is completed (the DONE
	   state is entered by EACH port when ALL the ports are done,
	   virtually at the same moment).
	   This is Suboptimal: should be done only ONCE for ALL the ports
	   are done, yet it does not harm to do it 18 times...and it simplifies
	   the code. */
	shw_io_write(shw_io_led_state_o, 0);
	shw_io_write(shw_io_led_state_g, 1);
	return 1; /* Final state reached */
}

/* Build events mask */
static  int port_tx_setup_fsm_build_events(fsm_t *fsm) {
	return HAL_PORT_TX_SETUP_EVENT_TIMER;
}

/* Initialize tx_setup-this is a global init, executed once for all ports/FSMs.
   It includes:
   - loading calibration file if exists
   - initializing a global structure and filling it in - this structure is
     needed by tx calibration only (so far)
*/
void hal_port_tx_setup_init_all(struct hal_port_state * ports, halGlobalLPDC_t *globalLpdc) {
	int index;
	int numberOfLpdcPorts = 0;
	uint32_t maskUsedPorts=0;
	int firstLpdcPort = -1;
	int lastLpdcPort = -1;
	char name[64];
	
	/* Initialize pointer to the global structure for each port.
	   Check whether there is any port that supports LPDC,
	   if there is/are such port(s), remember index of the first/last and
	   their number. We need this info in operation.*/
	for (index = 0; index < HAL_MAX_PORTS; index++){
		struct hal_port_state *ps = &ports[index];
		
		if(ps->in_use ) {
			/* Link the global structure from each port.
			   NOTE: Even ports that do not support LPDC require access to
			   this global structure as they need to know whether all
			   the LPDC-supporting ports have been calibrated */
			ps->lpdc.globalLpdc = globalLpdc;
			maskUsedPorts|=((uint32_t)1)<<ps->hw_index;

			/* Fill in global info needed for operation */
			if (ps->lpdc.isSupported) {
				/* if this is he first supported port, save its index*/
				if (firstLpdcPort < 0)
					firstLpdcPort = index;

				/*remember the index, just in case it is the last LPDC port*/
				lastLpdcPort = index;

				/* count number of supported ports*/
				numberOfLpdcPorts++;

			}
			/* Fill txSetup fsm structure */
			snprintf(name, sizeof(name), "PortTxSetupFSM.%d", ps->hw_index);

			if (fsm_generic_create(&ps->lpdc.txSetupFSM, name,
					port_tx_setup_fsm_build_events, port_tx_setup_fsm_states,
					port_tx_setup_fsm_events, ps)) {
				pr_error("Cannot create TxSetup fsm !\n");
				exit(EXIT_FAILURE);
			}
		}
	}

	globalLpdc->maskUsedPorts=maskUsedPorts;
	/* if there are any LPDC ports, do some preparation */
	if(globalLpdc && numberOfLpdcPorts) {

		/* fill in the global structure */
		globalLpdc->numberOfLpdcPorts = numberOfLpdcPorts;
		globalLpdc->maskTxSetupDonePorts = 0;
		globalLpdc->calFileSynced = 0;
		globalLpdc->firstLpdcPort = firstLpdcPort;
		globalLpdc->lastLpdcPort = lastLpdcPort;
		pr_info("WR switch supports LPDC on %d ports ("
		        "first port is wri%d, last port is wri%d)\n",
		        globalLpdc->numberOfLpdcPorts,
		        globalLpdc->firstLpdcPort + 1,
		        globalLpdc->lastLpdcPort + 1);

		/* load the calib file*/
		_load_tx_calibration_file(ports);

		/* Force going to FREE running master for the calibration */
		if ( hal_tmg_get_mode(NULL)!=HAL_TIMING_MODE_FREE_MASTER)
			hal_tmg_set_mode(HAL_TIMING_MODE_FREE_MASTER);
	}
}

//static void hal_port_tx_setup_fsm_reset(struct hal_port_state * ps )
//{
//	fsm_set_state( &ps->lpdc.txSetupFSM, -1 ); // reset state
//	fsm_fire_state( &ps->lpdc.txSetupFSM, HAL_PORT_TX_SETUP_STATE_START );
//
//	if ( hal_tmg_get_mode(NULL)==HAL_TIMING_MODE_BC)
//		hal_tmg_set_mode(HAL_TIMING_MODE_FREE_MASTER);
//	ps->locked=0;
//
//	pcs_writel(ps, MDIO_LPC_CTRL_RESET_RX |
//			      MDIO_LPC_CTRL_DMTD_SOURCE_TXOUTCLK,
//			      MDIO_LPC_CTRL);
//}

/* Init the TX SETUP FSM on a given port */
void hal_port_tx_setup_fsm_init(struct hal_port_state * ps ) {
	fsm_init_state(&ps->lpdc.txSetupFSM);
	fsm_fire_state(&ps->lpdc.txSetupFSM,HAL_PORT_TX_SETUP_STATE_START);
}

/* FSM state machine for TX setup on a given port
 * Returned value:
 *  1: when final state has been reached
 *  0: when final state has not been reached
 *  -1: error detected
 */

int  hal_port_tx_setup_fsm_run( struct hal_port_state * ps ) 
{
	if ( !ps->in_use )
		return 1;
	return fsm_generic_run(&ps->lpdc.txSetupFSM);
}

static int get_fpga_md5(char *buf)
{
	FILE *fpga_status = fopen(_fpgaStatusFileName, "r");

	if (!fpga_status)
		return 0;
	/* first line is the status */
	fscanf(fpga_status, "%33s\n", buf);
	if (strncmp(buf, "load_ok", 20) || feof(fpga_status)) {
		/* something went wrong and FPGA is not loaded or MD5 not
		 * present in the file */
		fclose(fpga_status);
		return 0;
	}
	fscanf(fpga_status, "%33s\n", buf);
	fclose(fpga_status);
	return 1;
}


/* if config is present then update the calibration data */
static void _load_tx_calibration_file(struct hal_port_state * ports) {

	int i = 0;
	halGlobalLPDC_t * gl = ports[0].lpdc.globalLpdc;
	char md5[33], lpdc_md5[33];

	// Read calibration file, if it exists
	_calibrationConfig = cfg_load(_calibrationFileName, 0);


	if ( !_calibrationConfig ) {
		pr_info("Can't load TX phase calibration data file: %s\n",
			_calibrationFileName);
		gl->calFileSynced = 0;
		return;
	}
	/* use tx_phase_cal.conf file only if it was generated for the currently
	 * running FPGA bitstream */
	if (get_fpga_md5(md5) && cfg_get_str(_calibrationConfig, "MD5", lpdc_md5)) {
		if (strncmp(md5, lpdc_md5, 32)) {
			pr_error("FPGA bitstream MD5 not matching, cannot load LPDC file\n");
			pr_error("loaded md5 = %32s\n", md5);
			pr_error("lpdc   md5 = %32s\n", lpdc_md5);
			cfg_close(_calibrationConfig);
			return;
		} else
			pr_info("Matched FPGA bitstream MD5, loading LPDC file\n");
	} else {
		pr_warning("Can't get MD5 of loaded bitstream\n");
	}


	pr_info("Loading LPDC config data from %s\n", _calibrationFileName);

	for (i = 0; i < HAL_MAX_PORTS; i++){
		struct hal_port_state *ps = &ports[i];

		if (ps->in_use && ps->lpdc.isSupported)
		{
			char key_name[80];
			int value;
			snprintf(key_name, sizeof(key_name), "TX_PHASE_PORT%02d",
				ps->hw_index + 1);

			if(cfg_get_int( _calibrationConfig, key_name, &value) )
			{
				pr_info("cal: wri%d %d\n", ps->hw_index+1, value);
				ps->lpdc.txSetup->cal_saved_phase = value;
				ps->lpdc.txSetup->cal_saved_phase_valid = 1;
			}
		}
	}
	gl->calFileSynced = 1;
	cfg_close(_calibrationConfig);
}

static void _write_tx_calibration_file(struct hal_port_state * ps)
{
	int i;
	halGlobalLPDC_t * gl = ps->lpdc.globalLpdc;
	char md5[33];

	/* Only the first LPDC-supporting port writes the file. Otherwise,
	   there is problem with pointers when looping through port structures
	   to write data for all ports, see the "for" below.  */
	if(gl->firstLpdcPort != ps->hw_index)
		return;

	if(gl->calFileSynced)
		return;


	struct config_file *cfg = cfg_load(_calibrationFileName, 1);
	struct hal_port_state *_ps=ps;

	/* first, store MD5 of the bitstream */
	if (!get_fpga_md5(md5))
		sprintf(md5, "ERROR");
	cfg_set_str(cfg, "MD5", md5);

	for (i = gl->firstLpdcPort; i <= gl->lastLpdcPort; i++) {		

		if (_ps->in_use && _ps->lpdc.isSupported)
		{
			char key_name[80];
			snprintf(key_name, sizeof(key_name), "TX_PHASE_PORT%02d", _ps->hw_index + 1);
			cfg_set_int(cfg, key_name, _ps->lpdc.txSetup->measured_phase);
		}
		_ps++;
	}

	pr_info( "Writing TX phase calibration data to %s\n", _calibrationFileName );
	cfg_save( cfg, _calibrationFileName );
	cfg_close(cfg);

	gl->calFileSynced = 1;
}

//min -269 max 331 x 9631 Rv 1
static int _within_range(int x, int minval, int maxval, int wrap)
{
	int rv;

	while (maxval >= wrap)
		maxval -= wrap;

	while (maxval < 0)
		maxval += wrap;

	while (minval >= wrap)
		minval -= wrap;

	while (minval < 0)
		minval += wrap;

	while (x < 0)
		x += wrap;

	while (x >= wrap)
		x -= wrap;

	if (maxval > minval)
		rv = (x >= minval && x <= maxval) ? 1 : 0;
	else
		rv = (x >= minval || x <= maxval) ? 1 : 0;

	return rv;
}


