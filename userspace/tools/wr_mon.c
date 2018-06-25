#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <time.h>
#include <sys/timex.h>
#include <inttypes.h>
#include <ppsi/ppsi.h>
#include <libwr/shmem.h>
#include <libwr/hal_shmem.h>
#include <libwr/switch_hw.h>
#include <libwr/wrs-msg.h>
#include <libwr/pps_gen.h>
#include <fpga_io.h>
#include <minipc.h>
#include <signal.h>

#include "term.h"

#define PTP_EXPORT_STRUCTURES
#include "ptpd_exports.h"

#define SHOW_GUI		0
#define SHOW_SLAVE_PORTS	1
#define SHOW_MASTER_PORTS	(1<<1)
#define SHOW_OTHER_PORTS	(1<<2) /* non-wr and none */
#define SHOW_SERVO		(1<<3)
#define SHOW_TEMPERATURES	(1<<4)
#define WEB_INTERFACE		(1<<5)
#define SHOW_WR_TIME		(1<<6)

/* for convenience when any or all ports needs a print statement */
#define SHOW_ALL_PORTS		(SHOW_SLAVE_PORTS | SHOW_MASTER_PORTS | \
				SHOW_OTHER_PORTS)
/* for convenience with -a option */
#define SHOW_ALL		(SHOW_ALL_PORTS | SHOW_SERVO | \
				SHOW_TEMPERATURES | SHOW_WR_TIME)


#define MAX_INST_SERVO 2
struct inst_servo_t {
	struct pp_instance * ppi;              /* pointer to the ppi instance */
	int                  valid_servo_data; /* 1 means servo data are vaild */
	TimeInterval         offsetFromMaster; /* currentDS.offsetFromMaster */
	TimeInterval         meanDelay;        /* currentDS.meanDelay */
	TimeInterval         delayAsymmetry;   /* portDS.delayAsymmetry */
	RelativeDifference	 scaledDelayCoefficient; /* AsymmetryCorrectionPortDS.scaledDelayCoefficient */
	TimeInterval         egressLatency;    /* timestampCorrectionPortDS.egressLatency */
	TimeInterval         ingressLatency;   /* timestampCorrectionPortDS.ingressLatency */
	TimeInterval         semistaticLatency;/* timestampCorrectionPortDS.semistaticLatency */
	TimeInterval	     constantAsymmetry;/* asymmetryCorrectionPortDS.constantAsymmetry */
	struct pp_servo      servo_snapshot;  /* image of a the ppsi servo */
	void *               servo_ext_snapshot; /* image of the extension servo */
};

/* protocol extension data */
#define IS_PROTO_EXT_INFO_AVAILABLE( proto_id ) \
	((proto_id < sizeof(proto_ext_info)/sizeof(struct proto_ext_info_t)) \
			&& (proto_ext_info[proto_id].valid==1))

struct proto_ext_info_t {
	int valid;
	char *ext_name; /* Extension name */
	char short_ext_name; /* Very short extension name - just one character */
	int servo_ext_size; /* Size of the extension */
	int ipc_cmd_tacking; /* Command to enable/disable servo tacking*/
	int track_onoff;     /* Tracking on/off */
	time_t lastt;
	int last_count;
};

static struct proto_ext_info_t proto_ext_info [] = {
		[PPSI_EXT_NONE] = {
				.valid=1,
				.ext_name="PTP",
				.short_ext_name='P',
				.ipc_cmd_tacking=-1, /* Invalid */
		},

#if CONFIG_EXT_WR == 1
		[PPSI_EXT_WR] = {
				.valid=1,
				.ext_name="White-Rabbit",
				.short_ext_name='W',
				.servo_ext_size=sizeof(struct wr_servo_state),
				.ipc_cmd_tacking=PTPDEXP_COMMAND_WR_TRACKING,
				.track_onoff = 1,
		},
#endif
#if CONFIG_EXT_L1SYNC == 1
		[PPSI_EXT_L1S] = {
				.valid=1,
				.ext_name="L1Sync",
				.short_ext_name='L',
				.servo_ext_size=sizeof(struct l1e_servo_state),
				.ipc_cmd_tacking=PTPDEXP_COMMAND_L1SYNC_TRACKING,
				.track_onoff = 1,
		}
#endif
};

static struct inst_servo_t servos[MAX_INST_SERVO];

int mode = SHOW_GUI;

static struct minipc_ch *ptp_ch;

static struct wrs_shm_head *hal_head;
static struct hal_port_state *hal_ports;
/* local copy of port state */
static struct hal_port_state hal_ports_local_copy[HAL_MAX_PORTS];
static int hal_nports_local;
static struct wrs_shm_head *ppsi_head;
static struct pp_globals *ppg;
static pid_t ptp_ch_pid; /* pid of ppsi connected via minipc */
static struct hal_temp_sensors *temp_sensors;
/* local copy of temperature sensor readings */
static struct hal_temp_sensors temp_sensors_local;

static uint64_t seconds;
static uint32_t nanoseconds;
/* ignore checking if process is alive */
static int ignore_alive;

/* define size of pp_instance_state_to_name as a last element + 1 */
#define PP_INSTANCE_STATE_MAX (WR_PORT_CALIBRATION_8 + 1)
/* define conversion array for the field state in the struct pp_instance */
static char *pp_instance_state_to_name[PP_INSTANCE_STATE_MAX] = {
	/* from ppsi/include/ppsi/ieee1588_types.h, enum pp_std_states */
	/* PPS_END_OF_TABLE = 0 */
	[PPS_END_OF_TABLE] =      "EOT       ",
	[PPS_INITIALIZING] =      "initing   ",
	[PPS_FAULTY] =            "faulty    ",
	[PPS_DISABLED] =          "disabled  ",
	[PPS_LISTENING] =         "listening ",
	[PPS_PRE_MASTER] =        "pre master",
	[PPS_MASTER] =            "master    ",
	[PPS_PASSIVE] =           "passive   ",
	[PPS_UNCALIBRATED] =      "uncalib   ",
	[PPS_SLAVE] =             "slave     ",
	/* from ppsi/proto-ext-whiterabbit/wr-constants.h */
	/* WRS_PRESENT = 100 */
	[WRS_PRESENT] =           "WR_PRESENT",
	[WRS_S_LOCK] =            "WR_S_LOCK ",
	[WRS_M_LOCK] =            "WR_M_LOCK ",
	[WRS_LOCKED] =            "WR_LOCKED ",
	[WRS_CALIBRATION] =       "WR_CAL-ION",
	[WRS_CALIBRATED] =        "WR_CAL-ED ",
	[WRS_RESP_CALIB_REQ] =    "WR_RSP_CAL",
	[WRS_WR_LINK_ON] =        "WR_LINK_ON",
	/* substates: used within WRS_CALIBRATED as wrPortState field */
	[WR_PORT_CALIBRATION_0] = "WR_P_CAL0 ",
	[WR_PORT_CALIBRATION_1] = "WR_P_CAL1 ",
	[WR_PORT_CALIBRATION_2] = "WR_P_CAL2 ",
	[WR_PORT_CALIBRATION_3] = "WR_P_CAL3 ",
	[WR_PORT_CALIBRATION_4] = "WR_P_CAL4 ",
	[WR_PORT_CALIBRATION_5] = "WR_P_CAL5 ",
	[WR_PORT_CALIBRATION_6] = "WR_P_CAL6 ",
	[WR_PORT_CALIBRATION_7] = "WR_P_CAL7 ",
	[WR_PORT_CALIBRATION_8] = "WR_P_CAL8 ",
};

#define EMPTY_EXTENSION_STATE_NAME "          "

#if CONFIG_EXT_L1SYNC == 1
static char * l1e_instance_extension_state[]={
		[__L1SYNC_MISSING   ] = "MISSING   ",
		[L1SYNC_DISABLED    ] = "DISABLED  ",
		[L1SYNC_IDLE        ] = "IDLE      ",
		[L1SYNC_LINK_ALIVE  ] = "LINK ALIVE",
		[L1SYNC_CONFIG_MATCH] = "CFG MATCH ",
		[L1SYNC_UP          ] = "UP        "
};
#define L1S_INSTANCE_EXTENSION_STATE_MAX (sizeof (l1e_instance_extension_state)/sizeof(char *) )

#endif


int64_t interval_to_picos(TimeInterval interval)
{
	return (interval * 1000) >>  TIME_INTERVAL_FRACBITS;
}

int64_t pp_time_to_picos(struct pp_time *ts)
{
	return ts->secs * PP_NSEC_PER_SEC
		+ ((ts->scaled_nsecs * 1000 + 0x8000) >> TIME_INTERVAL_FRACBITS);
}

static double pp_time_to_double (struct pp_time *ts) {
	return (double) (ts->secs * PP_NSEC_PER_SEC
		+ (ts->scaled_nsecs >> TIME_INTERVAL_FRACBITS));

}

static double interval_to_double (TimeInterval interval) {
	  double f ;
	  int neg = interval<0;

	  if(neg) interval= ~interval+1;
	  f= (double)interval/(double)(1LL<<TIME_INTERVAL_FRACBITS);
	  return  neg ? -f : f;
}

static double relDiff_to_double(RelativeDifference relDiff) {
  double f ;
  int neg = relDiff<0;

  if(neg) relDiff= ~relDiff+1;
  f= (double)relDiff/(double)(1LL<<REL_DIFF_FRACBITS);
  return  neg ? -f : f;
}

static double alpha_to_double(int32_t alpha) {
  double f ;
  int neg = alpha<0;

  if(neg) alpha= ~alpha+1;
  f= (double)alpha/(double)(1LL<<FIX_ALPHA_FRACBITS);
  return  neg ? -f : f;
}

void help(char *prgname)
{
	fprintf(stderr, "%s: Use: \"%s [<options>] <cmd> [<args>]\n",
		prgname, prgname);
	fprintf(stderr,
		"  The program has the following options\n"
		"  -h   print help\n"
		"  -i   show White Rabbit time.\n"
		"	   very close\n"
		"  -m   show master ports\n"
		"  -s   show slave ports\n"
		"  -o   show other ports\n"
		"  -e   show servo statistics\n"
		"  -t   show temperatures\n"
		"  -a   show all (same as -i -m -s -o -e -t options)\n"
		"  -b   black and white output\n"
		"  -w   web interface mode\n"
		"  -H <dir> Open shmem dumps from the given directory\n"
		"\n"
		"During execution the user can enter 'q' to exit the program\n"
		"and 't' to toggle printing of state information on/off\n");
	exit(1);
}

int read_hal(void){
	unsigned ii;
	unsigned retries = 0;

	/* read data, with the sequential lock to have all data consistent */
	while (1) {
		ii = wrs_shm_seqbegin(hal_head);
		memcpy(hal_ports_local_copy, hal_ports,
		       hal_nports_local*sizeof(struct hal_port_state));
		memcpy(&temp_sensors_local, temp_sensors,
		       sizeof(*temp_sensors));
		retries++;
		if (retries > 100)
			return -1;
		if (!wrs_shm_seqretry(hal_head, ii))
			break; /* consistent read */
		usleep(1000);
	}

	return 0;
}

int read_servo(void){

	struct pp_instance *pp_array;
	unsigned int i, servoIdx;


	/* Clear servo structure */
	for (i=0; i<MAX_INST_SERVO; i++) {
		if (servos[i].ppi ) {
			if ( servos[i].servo_ext_snapshot )
				free(servos[i].servo_ext_snapshot);
		}
	}
	bzero(&servos, sizeof(servos));

	if ( !(pp_array = wrs_shm_follow(ppsi_head, ppg->pp_instances)) )
		return -1;

	servoIdx=0;
	for (i = 0; i < ppg->nlinks; i++) {
		struct pp_instance *ppi = &pp_array[i];

		/* we are only interested  on instances in SLAVE state */
		if (ppi->state == PPS_SLAVE ) {
			struct inst_servo_t *servo=&servos[servoIdx++];
			int alloc_size=IS_PROTO_EXT_INFO_AVAILABLE(ppi->protocol_extension) ?
					proto_ext_info[ppi->protocol_extension].servo_ext_size :
					0;

			/* Allocate extension data memory if needed */
			if ( alloc_size > 0 )
				if ( !(servo->servo_ext_snapshot=malloc(alloc_size)) )
					return -1;

			while (1) {
				unsigned ii = wrs_shm_seqbegin(ppsi_head);
				unsigned retries = 0;
				struct pp_servo *ppsi_servo;

				/* Copy common data */
				if ( !(ppsi_servo = wrs_shm_follow(ppsi_head, ppg->servo)) )
						break;
				memcpy(&servo->servo_snapshot, ppsi_servo, sizeof(struct pp_servo));

				/* Copy extension servo data */
				if ( servo->servo_ext_snapshot ) {
					void *ppsi_servo_ext;

					if ( !(ppsi_servo_ext = wrs_shm_follow(ppsi_head, ppi->ext_data)) )
							break;
					memcpy(servo->servo_ext_snapshot, ppsi_servo_ext,alloc_size);
				}

				/* Copy extra interesting data */
				{
					currentDS_t *currenDS;

					if ( !(currenDS = wrs_shm_follow(ppsi_head, ppg->currentDS) ) )
							break;

					servo->offsetFromMaster=currenDS->offsetFromMaster; /* currentDS.offsetFromMaster */
					servo->meanDelay=currenDS->meanDelay;    /* currentDS.meanDelay */
				}
				{
					portDS_t *portDS;

					if ( !(portDS = wrs_shm_follow(ppsi_head, ppi->portDS) ) )
							break;
					servo->delayAsymmetry=portDS->delayAsymmetry;   /* portDS.delayAsymmetry */
				}
				servo->scaledDelayCoefficient=ppi->asymmetryCorrectionPortDS.scaledDelayCoefficient; /* AsymmetryCorrectionPortDS.scaledDelayCoefficient */
				servo->constantAsymmetry=ppi->asymmetryCorrectionPortDS.constantAsymmetry;/* asymmetryCorrectionPortDS.constantAsymmetry */
				servo->egressLatency=ppi->timestampCorrectionPortDS.egressLatency;   /* timestampCorrectionPortDS.egressLatency */
				servo->ingressLatency=ppi->timestampCorrectionPortDS.ingressLatency;  /* timestampCorrectionPortDS.ingressLatency */
				servo->semistaticLatency=ppi->timestampCorrectionPortDS.semistaticLatency;  /* timestampCorrectionPortDS.semistaticLatency */
				if (!wrs_shm_seqretry(ppsi_head, ii)) {
					servo->valid_servo_data=1;
					break; /* consistent read */
				}
				retries++;
				if (retries > 100)
					break;
			}
			if ( servo->valid_servo_data ) {
				servo->ppi=ppi;
			} else {
				if ( servo->servo_ext_snapshot ) {
					free (servo->servo_ext_snapshot);
				}
			}
		}
	}
	return 0;
}

void ppsi_connect_minipc(void)
{
	if (ptp_ch) {
		/* close minipc, if connected before */
		minipc_close(ptp_ch);
	}
	ptp_ch = minipc_client_create("ptpd", 0);
	if (!ptp_ch) {
		pr_error("Can't establish WRIPC connection to the PTP "
			 "daemon!\n");
		exit(1);
	}
	/* store pid of ppsi connected via minipc */
	ptp_ch_pid = ppsi_head->pid;
}

void init_shm(void)
{
	struct hal_shmem_header *h;
	int ret;
	int n_wait = 0;
	while ((ret = wrs_shm_get_and_check(wrs_shm_hal, &hal_head)) != 0) {
		n_wait++;
		if (ret == WRS_SHM_OPEN_FAILED) {
			pr_error("Unable to open HAL's shm !\n");
		}
		if (ret == WRS_SHM_WRONG_VERSION) {
			pr_error("Unable to read HAL's version!\n");
		}
		if (ret == WRS_SHM_INCONSISTENT_DATA) {
			pr_error("Unable to read consistent data from HAL's "
				 "shmem!\n");
		}
		if (n_wait > 10) {
			/* timeout! */
			exit(-1);
		}
		sleep(1);
	}

	if (hal_head->version != HAL_SHMEM_VERSION) {
		pr_error("Unknown HAL's shm version %i (known is %i)\n",
			 hal_head->version, HAL_SHMEM_VERSION);
		exit(1);
	}
	h = (void *)hal_head + hal_head->data_off;
	/* Assume number of ports does not change in runtime */
	hal_nports_local = h->nports;
	if (hal_nports_local > HAL_MAX_PORTS) {
		pr_error("Too many ports reported by HAL. %d vs %d "
			 "supported\n", hal_nports_local, HAL_MAX_PORTS);
		exit(1);
	}
	/* Even after HAL restart, HAL will place structures at the same
	 * addresses. No need to re-dereference pointer at each read.
	 */
	hal_ports = wrs_shm_follow(hal_head, h->ports);
	if (!hal_ports) {
		pr_error("Unable to follow hal_ports pointer in HAL's "
			 "shmem\n");
		exit(1);
	}
	temp_sensors = &(h->temp);

	n_wait = 0;
	while ((ret = wrs_shm_get_and_check(wrs_shm_ptp, &ppsi_head)) != 0) {
		n_wait++;
		if (ret == WRS_SHM_OPEN_FAILED) {
			pr_error("Unable to open PPSI's shm !\n");
		}
		if (ret == WRS_SHM_WRONG_VERSION) {
			pr_error("Unable to read PPSI's version!\n");
		}
		if (ret == WRS_SHM_INCONSISTENT_DATA) {
			pr_error("Unable to read consistent data from PPSI's "
				 "shmem!\n");
		}
		if (n_wait > 10) {
			/* timeout! */
			exit(-1);
		}
		sleep(1);
	}

	/* check hal's shm version */
	if (ppsi_head->version != WRS_PPSI_SHMEM_VERSION) {
		pr_error("Unknown PPSI's shm version %i (known is %i)\n",
			 ppsi_head->version, WRS_PPSI_SHMEM_VERSION);
		exit(1);
	}
	ppg = (void *)ppsi_head + ppsi_head->data_off;

	ppsi_connect_minipc();
}

void show_ports(int alive)
{
	int i, j;
	time_t t;
	struct timex timex_val;
	struct tm *tm;
	char datestr[32];
	struct hal_port_state *port_state;
	struct pp_instance *pp_array;
	int vlan_i;
	int nvlans;
	int *p;

	if (!alive) {
		if (mode == SHOW_GUI)
			term_cprintf(C_RED, "HAL is dead!\n");
		else if (mode == SHOW_ALL)
			printf("HAL is dead!\n");
		return;
	}

	pp_array = wrs_shm_follow(ppsi_head, ppg->pp_instances);

	if (mode == SHOW_GUI) {
		t = (time_t)_fpga_readl(FPGA_BASE_PPS_GEN + 8 /* UTC_LO */);
		tm = localtime(&t);
		strftime(datestr, sizeof(datestr), "%Y-%m-%d %H:%M:%S", tm);
		term_cprintf(C_BLUE, "WR time (TAI):     ");
		term_cprintf(C_WHITE, "%s\n", datestr);

		time(&t);
		tm = localtime(&t);
		strftime(datestr, sizeof(datestr), "%Y-%m-%d %H:%M:%S", tm);
		term_cprintf(C_BLUE, "Switch time (UTC): ");
		term_cprintf(C_WHITE, "%s\n", datestr);

		term_cprintf(C_BLUE, "Leap seconds: ");
		if (adjtimex(&timex_val) < 0) {
			term_cprintf(C_WHITE, "error\n");
		} else {
			p = (int *)(&timex_val.stbcnt) + 1;
			term_cprintf(C_WHITE, "%3d\n", *p);
		}

/*                                    -------------------------------------------------------------------------------*/
		term_cprintf(C_CYAN, "------------- HAL -----------|-------------- PPSI ----------------------------------------\n");
		term_cprintf(C_CYAN, " Port | Link | WRconf | Freq |Inst| MAC of peer port  |    PTP/EXT states    | Pro | VLANs\n");
		term_cprintf(C_CYAN, "------|------|--------|------|----|-------------------|----------------------|-----|------\n");
	}
	if (mode & (SHOW_SLAVE_PORTS|SHOW_MASTER_PORTS)) {
		printf("PORTS ");
	}

	for (i = 0; i < hal_nports_local; i++) {
		char if_name[10];
		char if_mode[15];
		int print_port = 0;
		int instance_port = 0;

		snprintf(if_name, 10, "wri%d", i + 1);

		port_state = hal_lookup_port(hal_ports_local_copy,
						hal_nports_local, if_name);
		if (!port_state)
			continue;

		switch (port_state->mode) {
		case HEXP_PORT_MODE_WR_MASTER:
			if (mode == SHOW_GUI) {
				strcpy(if_mode, "Master");
			} else if (mode & SHOW_MASTER_PORTS) {
				print_port = 1;
				strcpy(if_mode, "M");
			} else if (mode & WEB_INTERFACE) {
				strcpy(if_mode, "Master");
			}
			break;
		case HEXP_PORT_MODE_WR_SLAVE:
			if (mode == SHOW_GUI) {
				strcpy(if_mode, "Slave ");
			} else if (mode & SHOW_SLAVE_PORTS) {
				print_port = 1;
				strcpy(if_mode, "S");
			} else if (mode & WEB_INTERFACE) {
				strcpy(if_mode, "Slave");
			}
			break;
		case HEXP_PORT_MODE_NON_WR:
			if (mode == SHOW_GUI) {
				strcpy(if_mode, "Non WR");
			} else if (mode & SHOW_OTHER_PORTS) {
				print_port = 1;
				strcpy(if_mode, "N");
			} else if (mode & WEB_INTERFACE) {
				strcpy(if_mode, "Non WR");
			}
			break;
		case HEXP_PORT_MODE_NONE:
			if (mode == SHOW_GUI) {
				strcpy(if_mode, "None  ");
			} else if (mode & SHOW_OTHER_PORTS) {
				print_port = 1;
				strcpy(if_mode, "X");
			} else if (mode & WEB_INTERFACE) {
				strcpy(if_mode, "None");
			}
			break;
		case HEXP_PORT_MODE_WR_M_AND_S:
			if (mode == SHOW_GUI) {
				strcpy(if_mode, "Auto  ");
			} else if (mode &
				(SHOW_SLAVE_PORTS|SHOW_MASTER_PORTS)) {
				print_port = 1;
				strcpy(if_mode, "A");
			} else if (mode & WEB_INTERFACE) {
				strcpy(if_mode, "Auto");
			}
			break;
		default:
			if (mode == SHOW_GUI) {
				strcpy(if_mode, "Unkn  ");
			} else if (mode & SHOW_OTHER_PORTS) {
				print_port = 1;
				strcpy(if_mode, "U");
			} else if (mode & WEB_INTERFACE) {
				strcpy(if_mode, "Unknown");
			}
			break;
		}

		if (mode == SHOW_GUI) {
			term_cprintf(C_WHITE, "%-5s", if_name);
			term_cprintf(C_CYAN, " | ");
			/* check if link is up */
			if (state_up(port_state->state))
				term_cprintf(C_GREEN, "up  ");
			else
				term_cprintf(C_RED, "down");
			term_cprintf(C_CYAN, " | ");
			term_cprintf(C_WHITE, if_mode);
			term_cprintf(C_CYAN, " | ");
			if (port_state->locked)
				term_cprintf(C_GREEN, "Lock ");
			else
				term_cprintf(C_RED, "     ");

			term_cprintf(C_CYAN, "|");

			instance_port = 0;
			/*
			 * Actually, what is interesting is the PTP state.
			 * For this lookup, the port in ppsi shmem
			 */
			for (j = 0; j < ppg->nlinks; j++) {
				struct pp_instance *ppi=&pp_array[j];
				int proto_extension=ppi->protocol_extension;
				struct proto_ext_info_t *pe_info= IS_PROTO_EXT_INFO_AVAILABLE(proto_extension) ? &proto_ext_info[proto_extension] :  &proto_ext_info[0] ;

				if (strcmp(if_name,
						ppi->cfg.iface_name)) {
					/* Instance not for this interface
					 * skip */
					continue;
				}
				if (instance_port > 0) {
					term_cprintf(C_CYAN, "\n      |      |"
						     "        |      |");
				}
				instance_port++;
				/* print instance number */
				term_cprintf(C_WHITE, " %2d ", j);
				term_cprintf(C_CYAN, "| ");
				/* Note: we may have more pp instances per
				 * port */
				if (state_up(port_state->state)) {
					unsigned char *p = ppi->peer;
					char * extension_state_name=EMPTY_EXTENSION_STATE_NAME;

					term_cprintf(C_WHITE, "%02x:%02x"
						     ":%02x:%02x:%02x:%02x ",
						     p[0], p[1], p[2], p[3],
						     p[4], p[5]);
					term_cprintf(C_CYAN, "| ");
					if (ppi->state < PP_INSTANCE_STATE_MAX) {
						/* Known state */
						term_cprintf(C_GREEN, "%s/",
							pp_instance_state_to_name[ppi->state]);
					} else {
						/* Unknown ptp state */
						term_cprintf(C_GREEN,
							"unkn(%3i)",
							ppi->state);
					}
					/* print extension state */
					switch (ppi->protocol_extension ) {
					case PPSI_EXT_WR :
						break;
#if CONFIG_EXT_L1SYNC == 1
					case PPSI_EXT_L1S :
					{
						portDS_t *portDS;

						extension_state_name="????????? ";
						if ( (portDS = wrs_shm_follow(ppsi_head, ppi->portDS) ) ) {
							l1e_ext_portDS_t *extPortDS;

							if ( (extPortDS = wrs_shm_follow(ppsi_head, portDS->ext_dsport) ) ) {
								if ( extPortDS->basic.L1SyncState <= L1S_INSTANCE_EXTENSION_STATE_MAX )
									extension_state_name=l1e_instance_extension_state[extPortDS->basic.L1SyncState];
							}
						}
						break;
					}
#endif
					}
					term_cprintf(C_GREEN, "%s",extension_state_name);
				} else {
					term_cprintf(C_WHITE, "                  ");
					term_cprintf(C_CYAN, "|");
					term_cprintf(C_WHITE, "                      ");
				}
				term_cprintf(C_CYAN, "| ");
				if (ppi->proto == PPSI_PROTO_RAW) {
					term_cprintf(C_WHITE, "R");
				} else if (ppi->proto
					   == PPSI_PROTO_UDP) {
					term_cprintf(C_WHITE, "U");
				} else if (ppi->proto
					   == PPSI_PROTO_VLAN) {
					term_cprintf(C_WHITE, "V");
				} else {
					term_cprintf(C_WHITE, "?");
				}
				term_cprintf(C_WHITE, "-%c",pe_info->short_ext_name);

				nvlans = ppi->nvlans;
				term_cprintf(C_CYAN, " | ");
				for (vlan_i = 0; vlan_i < nvlans; vlan_i++) {
					term_cprintf(C_WHITE, "%d",
						    ppi->vlans[vlan_i]);
					if (vlan_i < nvlans - 1)
						term_cprintf(C_WHITE, ",");
				}
			}
			if (!instance_port) {
				term_cprintf(C_WHITE, " -- ");
				term_cprintf(C_CYAN, "|                   |                      |     |");
			}
			term_cprintf(C_WHITE, "\n");
		} else if (mode & WEB_INTERFACE) {
			printf("%s ", state_up(port_state->state)
				? "up" : "down");
			printf("%s ", if_mode);
			printf("%s ", port_state->locked
				? "Locked" : "NoLock");
			printf("%s ", port_state->calib.rx_calibrated
				&& port_state->calib.tx_calibrated
				? "Calibrated" : "Uncalibrated");
		} else if (print_port) {
			printf("port:%s ", if_name);
			printf("lnk:%d ", state_up(port_state->state));
			printf("mode:%s ", if_mode);
			printf("lock:%d ", port_state->locked);
			print_port = 0;
		}
	}
	if (mode == SHOW_GUI) {
		term_cprintf(C_BLUE, "Pro - Protocol mapping: V-Ethernet over "
			     "VLAN; U-UDP; R-Ethernet\n");
	}
}

void show_servo(struct inst_servo_t *servo, int alive)
{

	struct wr_servo_state * wr_servo;
	struct l1e_servo_state * l1e_servo;
	int proto_extension=servo->ppi->protocol_extension;
	struct proto_ext_info_t *pe_info= IS_PROTO_EXT_INFO_AVAILABLE(proto_extension) ? &proto_ext_info[proto_extension] :  &proto_ext_info[0] ;

	wr_servo= (servo->ppi->protocol_extension==PPSI_EXT_WR) ?
			( struct wr_servo_state * ) servo->servo_ext_snapshot : NULL;

	l1e_servo= (servo->ppi->protocol_extension==PPSI_EXT_L1S) ?
			( struct l1e_servo_state * ) servo->servo_ext_snapshot : NULL;

	if (mode == SHOW_GUI) {
		term_cprintf(C_CYAN, "\n--------------------------- Synchronization status ----------------------------\n");
	}

	if (!alive) {
		if (mode == SHOW_GUI)
			term_cprintf(C_RED, "PPSI is dead!\n");
		return;
	}

	if (mode == SHOW_GUI) {
		if (!(servo->servo_snapshot.flags & PP_SERVO_FLAG_VALID)) {
			term_cprintf(C_RED,
				     "Master mode or sync info not valid\n");
			return;
		}

		term_cprintf(C_BLUE, "Servo state:          ");
		if (pe_info->lastt && time(NULL) - pe_info->lastt > 5) {
			term_cprintf(C_RED, "--- not updating ---\n");
		} else {
			term_cprintf(C_WHITE, "%s:%s: %s%s\n",
				     servo->ppi->cfg.iface_name,
					 pe_info->ext_name,
					 servo->servo_snapshot.servo_state_name,
					 servo->servo_snapshot.flags & PP_SERVO_FLAG_WAIT_HW ?
				     " (wait for hw)" : "");
		}

		/* "tracking disabled" is just a testing tool */
		if (wr_servo  && !wr_servo->tracking_enabled)
			term_cprintf(C_RED, "Tracking forcibly disabled\n");
		term_cprintf(C_CYAN, "\n +- Timing parameters ---------------------------------------------------------\n");

		term_cprintf(C_CYAN," | ");term_cprintf(C_BLUE,  "meanDelay        : ");
		term_cprintf(C_WHITE, "%15.3f nsec\n", interval_to_double(servo->meanDelay) );

		term_cprintf(C_CYAN," | ");term_cprintf(C_BLUE,  "delayMS          : ");
		term_cprintf(C_WHITE, "%15.3f nsec\n",	pp_time_to_double(&servo->servo_snapshot.delayMS));

		//term_cprintf(C_BLUE, "Estimated link length:     ");
		/* (RTT - deltas) / 2 * c / ri
		 c = 299792458 - speed of light in m/s
		 ri = 1.4682 - refractive index for fiber g.652. However,
			       experimental measurements using long (~5km) and
			       short (few m) fibers gave a value 1.4827
		 */
		//term_cprintf(C_WHITE, "%10.2f meters\n",
		//	crtt / 2 / 1e6 * 299.792458 / 1.4827);


		term_cprintf(C_CYAN," | ");term_cprintf(C_BLUE,  "delayAsymmetry   : ");
		term_cprintf(C_WHITE, "%15.3f nsec\n",   interval_to_double(servo->delayAsymmetry));
		term_cprintf(C_CYAN," | ");term_cprintf(C_BLUE,  "scaledDelayCoef  : ");
		term_cprintf(C_WHITE, "%.9f fpa(%" PRId64 ")",  relDiff_to_double(servo->scaledDelayCoefficient), (int64_t)servo->scaledDelayCoefficient);
		if ( wr_servo ) {
			term_cprintf(C_BLUE,  "  Fixed Alpha : ");
			term_cprintf(C_WHITE, "%.9f fpa(%d)", alpha_to_double(wr_servo->fiber_fix_alpha), wr_servo->fiber_fix_alpha);
		}
		if ( l1e_servo ) {
			term_cprintf(C_BLUE,  "  Fixed Alpha : ");
			term_cprintf(C_WHITE, "%.9f fpa(%" PRId64 ")", alpha_to_double(l1e_servo->fiber_fix_alpha), l1e_servo->fiber_fix_alpha);
		}
		term_cprintf(C_WHITE, "\n");
		term_cprintf(C_CYAN," | ");term_cprintf(C_BLUE,  "ingressLatency   : ");
		term_cprintf(C_WHITE, "%15.3f nsec\n",   interval_to_double(servo->ingressLatency));
		term_cprintf(C_CYAN," | ");term_cprintf(C_BLUE,  "egressLatency    : ");
		term_cprintf(C_WHITE, "%15.3f nsec\n",   interval_to_double(servo->egressLatency));
		term_cprintf(C_CYAN," | ");term_cprintf(C_BLUE,  "semistaticLatency: ");
		term_cprintf(C_WHITE, "%15.3f nsec\n",   interval_to_double(servo->semistaticLatency));

		/*if (0) {
			term_cprintf(C_BLUE, "Fiber asymmetry:   ");
			term_cprintf(C_WHITE, "%.3f nsec\n",
				ss.fiber_asymmetry/1000.0);
		}*/

		term_cprintf(C_CYAN," | ");term_cprintf(C_BLUE, "offsetFromMaster : ");
		term_cprintf(C_WHITE, "%15.3f nsec\n", interval_to_double (servo->offsetFromMaster));

		if ( wr_servo ) {
			term_cprintf(C_CYAN," | ");term_cprintf(C_BLUE, "Phase setpoint   : ");
			term_cprintf(C_WHITE, "%15.3f nsec\n",wr_servo->cur_setpoint/1000.0);

			term_cprintf(C_CYAN," | ");term_cprintf(C_BLUE, "Skew             : ");
			term_cprintf(C_WHITE, "%15.3f nsec\n",wr_servo->skew/1000.0);
		}

		if ( l1e_servo ) {
			term_cprintf(C_CYAN," | ");term_cprintf(C_BLUE, "Phase setpoint   : ");
			term_cprintf(C_WHITE, "%15.3f nsec\n",l1e_servo->cur_setpoint_ps/1000.0);

			term_cprintf(C_CYAN," | ");term_cprintf(C_BLUE, "Skew             : ");
			term_cprintf(C_WHITE, "%15.3f nsec\n",l1e_servo->skew_ps/1000.0);
		}
		term_cprintf(C_CYAN," | ");term_cprintf(C_BLUE, "Update counter  : ");
		term_cprintf(C_WHITE, "%15u times\n", servo->servo_snapshot.update_count);
		if (servo->servo_snapshot.update_count != pe_info->last_count) {
			pe_info->lastt = time(NULL);
			pe_info->last_count = servo->servo_snapshot.update_count;
		}

		if ( wr_servo ) {
			term_cprintf(C_CYAN," | ");term_cprintf(C_BLUE, "Master PHY delays ");
			term_cprintf(C_BLUE, "TX: ");
			term_cprintf(C_WHITE, "%11.3f nsec, ", wr_servo->delta_txm_ps/1000.0);
			term_cprintf(C_BLUE, "RX: ");
			term_cprintf(C_WHITE, "%11.3f nsec\n", wr_servo->delta_rxm_ps/1000.0);

			term_cprintf(C_CYAN," | ");term_cprintf(C_BLUE, "Slave  PHY delays ");
			term_cprintf(C_BLUE, "TX: ");
			term_cprintf(C_WHITE, "%11.3f nsec, ", wr_servo->delta_txs_ps/1000.0);
			term_cprintf(C_BLUE, "RX: ");
			term_cprintf(C_WHITE, "%11.3f nsec\n",wr_servo->delta_rxs_ps/1000.0);
		}
	} else {
		/* TJP: commented out fields are present on the SPEC,
		 *      does the switch have similar fields?
		 */
		printf("SERVO ");
/*		printf("lnk:");*/
/*		printf("rx:");*/
/*		printf("tx:");*/
		printf("sv:%d ", servo->servo_snapshot.flags & PP_SERVO_FLAG_VALID ? 1 : 0);
		printf("ss:'%s' ", servo->servo_snapshot.servo_state_name);
/*		printf("aux:");*/
		printf("md:%llu ", interval_to_picos(servo->meanDelay));
		printf("dms:%llu ", pp_time_to_picos(&servo->servo_snapshot.delayMS));
		if ( wr_servo ) {
			int64_t crtt= wr_servo->delayMM_ps - wr_servo->delta_txm_ps -
					wr_servo->delta_rxm_ps - wr_servo->delta_txs_ps -
					wr_servo->delta_rxs_ps;

			printf("lock:%i ", wr_servo->tracking_enabled);
			printf("dtxm:%d drxm:%d ", wr_servo->delta_txm_ps, wr_servo->delta_rxm_ps);
			printf("dtxs:%d drxs:%d ", wr_servo->delta_txs_ps, wr_servo->delta_rxs_ps);
		/* (RTT - deltas) / 2 * c / ri
		 c = 299792458 - speed of light in m/s
		 ri = 1.4682 - refractive index for fiber g.652. However,
			       experimental measurements using long (~5km) and
			       short (few m) fibers gave a value 1.4827
		 */
		//printf("ll:%d ",
		//       (int) (crtt / 2 / 1e6 * 299.792458 / 1.4827 * 100));
			printf("crtt:%llu ", crtt);
			printf("setp:%d ", wr_servo->cur_setpoint);
		}
		if ( l1e_servo ) {
			printf("lock:%i ", l1e_servo->tracking_enabled);
			printf("setp:%d ", l1e_servo->cur_setpoint_ps);
		}
		printf("asym:%lld ", interval_to_picos(servo->delayAsymmetry));
		printf("cko:%lld ", interval_to_picos(servo->offsetFromMaster));
/*		printf("hd:");*/
/*		printf("md:");*/
/*		printf("ad:");*/
		printf("ucnt:%u ", servo->servo_snapshot.update_count);
		/* SPEC shows temperature, but that can be selected separately
		 * in this program
		 */
	}
}

void show_servos(int alive) {

	int i;

	for (i=0; i<MAX_INST_SERVO; i++)
		if (servos[i].ppi )
			show_servo(&servos[i], alive);
}


void show_temperatures(void)
{
	if ((mode == SHOW_GUI) || (mode & WEB_INTERFACE)) {
		if (mode == SHOW_GUI) {
/*                                              -------------------------------------------------------------------------------*/
			term_cprintf(C_CYAN, "\n-------------------------------- Temperatures ---------------------------------\n");
		} else {
			term_cprintf(C_CYAN, "\nTemperatures:\n");
		}

		term_cprintf(C_BLUE, "FPGA: ");
		term_cprintf(C_WHITE, "%2.2f ",
			     temp_sensors_local.fpga/256.0);
		term_cprintf(C_BLUE, "PLL: ");
		term_cprintf(C_WHITE, "%2.2f ",
			     temp_sensors_local.pll/256.0);
		term_cprintf(C_BLUE, "PSL: ");
		term_cprintf(C_WHITE, "%2.2f ",
			     temp_sensors_local.psl/256.0);
		term_cprintf(C_BLUE, "PSR: ");
		term_cprintf(C_WHITE, "%2.2f\n",
			     temp_sensors_local.psr/256.0);
	} else {
		printf("TEMP ");
		printf("fpga:%2.2f ", temp_sensors_local.fpga/256.0);
		printf("pll:%2.2f ", temp_sensors_local.pll/256.0);
		printf("psl:%2.2f ", temp_sensors_local.psl/256.0);
		printf("psr:%2.2f", temp_sensors_local.psr/256.0);
	}
}

void show_time(void)
{
	printf("TIME sec:%lld nsec:%d ", seconds, nanoseconds);
}

void show_all(void)
{
	int hal_alive;
	int ppsi_alive;

	if (mode == SHOW_GUI) {
		term_clear();
		term_pcprintf(1, 1, C_BLUE, "WR Switch Sync Monitor ");
		term_cprintf(C_WHITE, "%s", __GIT_VER__);
		term_cprintf(C_BLUE, " [q = quit]\n\n");
	}

	hal_alive = (hal_head->pid && (kill(hal_head->pid, 0) == 0))
								+ ignore_alive;
	ppsi_alive = (ppsi_head->pid && (kill(ppsi_head->pid, 0) == 0))
								+ ignore_alive;

	if (mode & SHOW_WR_TIME) {
		if (ppsi_alive)
			show_time();
		else if (mode == SHOW_ALL)
			printf("PPSI is dead!\n");
	}

	if ((mode & (SHOW_ALL_PORTS|WEB_INTERFACE)) || mode == SHOW_GUI) {
		show_ports(hal_alive);
	}

	if (mode & SHOW_SERVO || mode == SHOW_GUI) {
		show_servos(ppsi_alive);
	}

	if (mode & (SHOW_TEMPERATURES | WEB_INTERFACE) || mode == SHOW_GUI) {
		if (hal_alive)
			show_temperatures();
	}

	if (!(mode & WEB_INTERFACE || mode == SHOW_GUI)) {
		/* the newline for all in non-GUI or non-WEB mode... */
		printf("\n");
	}
	fflush(stdout);
}

static void enable_disable_tracking(int proto_extension) {

	if ( IS_PROTO_EXT_INFO_AVAILABLE(proto_extension)  ) {
		struct proto_ext_info_t *pe_info= &proto_ext_info[proto_extension];

		if ( pe_info->ipc_cmd_tacking!=-1 ) {
			int rval;

			pe_info->track_onoff = 1-pe_info->track_onoff;
			if (ptp_ch_pid != ppsi_head->pid) {
				/* ppsi was restarted since minipc
				 * connection, reconnect now */
				ppsi_connect_minipc();
			}
			minipc_call(ptp_ch, 200, &__rpcdef_cmd,
				&rval, pe_info->ipc_cmd_tacking,pe_info->track_onoff);
		}
	}
}

int main(int argc, char *argv[])
{
	int opt;
	int usecolor = 1;

	/* try a pps_gen based approach */
	uint64_t last_seconds = 0;

	wrs_msg_init(argc, argv);

	while ((opt = getopt(argc, argv, "himsoetabwqvH:")) != -1) {
		switch(opt)
		{
			case 'h':
				help(argv[0]);
			case 'i':
				mode |= SHOW_WR_TIME;
				break;
			case 's':
				mode |= SHOW_SLAVE_PORTS;
				break;
			case 'm':
				mode |= SHOW_MASTER_PORTS;
				break;
			case 'o':
				mode |= SHOW_OTHER_PORTS;
				break;
			case 'e':
				mode |= SHOW_SERVO;
				break;
			case 't':
				mode |= SHOW_TEMPERATURES;
				break;
			case 'a':
				mode |= SHOW_ALL;
				break;
			case 'b':
				usecolor = 0;
				break;
			case 'w':
				mode |= WEB_INTERFACE;
				break;
			case 'H':
				wrs_shm_set_path(optarg);
				/* ignore WRS_SHM_LOCKED flag */
				wrs_shm_ignore_flag_locked(1);
				ignore_alive = 1;
				break;
			case 'q': break; /* done in wrs_msg_init() */
			case 'v': break; /* done in wrs_msg_init() */
			default:
				help(argv[0]);
		}
	}

	init_shm();

	if (shw_fpga_mmap_init() < 0) {
		pr_error("Can't initialize FPGA mmap\n");
		exit(1);
	}

	if (mode & WEB_INTERFACE) {
		shw_pps_gen_read_time(&seconds, &nanoseconds);
		read_servo();
		read_hal();
		show_all();
		exit(0);
	}

	term_init(usecolor);
	setvbuf(stdout, NULL, _IOFBF, 4096);

	/* main loop */
	for(;;)
	{
		if (term_poll(10)) {
			int c = term_get();

			switch (c) {
			case 'q':
				goto quit;
			case 'w' :
				enable_disable_tracking (PPSI_EXT_WR);
				break;
			case 'l' :
				enable_disable_tracking (PPSI_EXT_L1S);
				break;
			}
		}

		shw_pps_gen_read_time(&seconds, &nanoseconds);
		if (seconds != last_seconds) {
			read_servo();
			read_hal();
			show_all();

			last_seconds = seconds;
		}

		/* If we got broken pipe or anything, exit */
		if (ferror(stdout))
			exit(1);
	}

	quit:;
	term_restore();
	setlinebuf(stdout);
	printf("\n");
	return 0;
}
