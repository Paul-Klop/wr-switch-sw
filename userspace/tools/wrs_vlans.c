/*
 * Copyright (c) 2014, CERN
 *
 * Author: Grzegorz Daniluk <grzegorz.daniluk@cern.ch>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <minipc.h>
#include <rtud_exports.h>
#include "regs/endpoint-regs.h"

#include <libwr/switch_hw.h>
#include "fpga_io.h"
#include "wrs_vlans.h"

#include <libwr/shmem.h>
#include <libwr/rtu_shmem.h>
#include <libwr/config.h>
#include <libwr/wrs-msg.h>

static struct minipc_ch *rtud_ch;
static struct rtu_vlans_t *rtu_vlans = NULL;

/* runtime options */
static struct option ropts[] = {
	{"help", 0, NULL, OPT_HELP},
	{"clear", 0, NULL, OPT_CLEAR},
	{"list", 0, NULL, OPT_LIST},
	{"port", 1, NULL, OPT_P_PORT},
	{"pmode", 1, NULL, OPT_P_QMODE},
	{"pvid", 1, NULL, OPT_P_VID},
	{"pprio", 1, NULL, OPT_P_PRIO},
	{"puntag", 1, NULL, OPT_P_UNTAG},
	{"plist", 0, NULL, OPT_P_LIST},
	{"rvid", 1, NULL, OPT_RTU_VID},
	{"rfid", 1, NULL, OPT_RTU_FID},
	{"rmask", 1, NULL, OPT_RTU_PMASK},
	{"rdrop", 1, NULL, OPT_RTU_DROP},
	{"rprio", 1, NULL, OPT_RTU_PRIO},
	{"del", 0, NULL, OPT_RTU_DEL},
	{"file", 1, NULL, OPT_FILE_READ},
	{"hpmask", 1, NULL, OPT_RTU_HP_MASK},
	{0,}};
/*******************/
static struct vlan_sets dot_config_vlan_sets[] = {
	{"VLANS_ENABLE_SET1", 0, 22},
	{"VLANS_ENABLE_SET2", 23, 100},
	{"VLANS_ENABLE_SET3", 101, 4094},
	{NULL, 0, 0}
};
static struct s_port_vlans vlans[NPORTS];

static unsigned long portmask;

static void set_p_pmode(int ep, int arg_mode);
static void set_p_vid(int ep, char *arg_vid);
static void set_p_prio(int ep, char *arg_prio);
static void set_p_untag(int ep, int arg_untag);
static void set_hp_mask(char *mask_str);
static int check_rtu(char *name, char *arg_val, int min, int max);
static int check_rtu_prio(char *arg_val);
static int print_help(char *prgname);
static void print_config_rtu(struct s_port_vlans *vlans);
static void print_config_vlan(void);
static int apply_settings(struct s_port_vlans *vlans);
static void default_vlan_config(void);
static int clear_all(void);
static int set_rtu_vlan(int vid, int fid, int pmask, int drop, int prio,
			int del, int flags);
static void free_rtu_vlans(struct rtu_vlans_t *ptr);
static void list_rtu_vlans(void);
static void list_p_vlans(void);
static void print_hp_mask(void);
static int rtu_find_vlan(struct rtu_vlan_table_entry *rtu_vlan_entry, int vid,
					 int fid);
static int config_rtud(void);
static int read_dot_config(char *dot_config_file);
static void read_dot_config_vlans(int vlan_min, int vlan_max);

struct rtu_vlan_table_entry *vlan_tab_shm;
struct wrs_shm_head *rtu_port_shmem;

static inline int nextport(int i, unsigned long pmask) /* helper for for_each_port() below */
{
	while (++i < NPORTS)
		if (pmask & (1 << i))
			return i;
	return -1;
}

#define iterate_ports(i, pmask) \
	for (i = -1; (i = nextport(i, pmask)) >= 0;)

#define for_each_port(i) \
	iterate_ports(i, portmask)

static int parse_mask(char *arg, unsigned long *pmask)
{
	int p1, p2;
	char c, *newarg, *s;

	newarg = strdup(arg);
	while ( (s = strtok(newarg, ",;")) ) {
		newarg = NULL; /* for next iteration */
		switch (sscanf(s, "%i-%i%c", &p1, &p2, &c)) {
		case 1:
			p2 = p1;
		case 2:
			break;
		default:
			return -1;
		}
		/* parameter --port should be from the range 1..18,
		 * but internally we use 0..17 */
		p1--;
		p2--;
		if ((p1 > p2) || (p1 < 0) || (p2 >= NPORTS))
			return -1;
		for (; p1 <= p2; p1++) {
			*pmask |= (1 << p1);
		}
	}
	if (wrs_msg_level < LOG_DEBUG)
		return 0;

	printf("working on ports:\n");
	iterate_ports(p1, *pmask)
		printf(" %i", p1 + 1);
	printf("\n");
	return 0;
}

int main(int argc, char *argv[])
{
	int c, i, arg;
	unsigned long conf_pmask = 0; /* current '--port' port mask */
	struct rtu_shmem_header *rtu_hdr;
	int n_wait = 0;
	int ret;
	char *prgname;

	wrs_msg_level = LOG_WARNING;
	wrs_msg_init(argc, argv, LOG_USER);
	prgname = argv[0];

	if (NPORTS > 8 * sizeof(portmask)) {
		/* build error: too big maxports */
		static __attribute__((used)) int
			array[8 * sizeof(portmask) - NPORTS];
	}

	if (argc == 1) {
		print_help(prgname);
		exit(0);
	}

	n_wait = 0;
	/* connect to the RTUd mini-rpc */
	while((rtud_ch = minipc_client_create("rtud", 0)) == 0) {
		n_wait++;
		if (n_wait > 10) {
			pr_error("Can't connect to RTUd mini-rpc server\n");
			exit(1);
		}
		sleep(1);
	}

	n_wait = 0;
	/* open rtu shm */
	while ((ret = wrs_shm_get_and_check(wrs_shm_rtu, &rtu_port_shmem)) != 0) {
		n_wait++;
		if (n_wait > 10) {
			if (ret == WRS_SHM_OPEN_FAILED) {
				pr_error("Unable to open RTUd's shmem!\n");
			}
			if (ret == WRS_SHM_WRONG_VERSION) {
				pr_error("Unable to read RTUd's version!\n");
			}
			if (ret == WRS_SHM_INCONSISTENT_DATA) {
				pr_error("Unable to read consistent data from "
					 "RTUd's shmem!\n");
			}
			exit(1);
		}
		sleep(1);
	}

	/* check rtu shm version */
	if (rtu_port_shmem->version != RTU_SHMEM_VERSION) {
		pr_error("unknown version %i (known is %i)\n",
			 rtu_port_shmem->version, RTU_SHMEM_VERSION);
		exit(1);
	}



	/* get vlans array */
	rtu_hdr = (void *)rtu_port_shmem + rtu_port_shmem->data_off;
	vlan_tab_shm = wrs_shm_follow(rtu_port_shmem, rtu_hdr->vlans);

	if (!vlan_tab_shm) {
		pr_error("cannot follow pointer to vlans in RTU's shmem\n");
		exit(1);
	}

	if (shw_fpga_mmap_init() < 0) {
		pr_error("Can't access device memory\n");
		exit(1);
	}

	/*parse parameters*/
	while ((c = getopt_long(argc, argv, "hf:vq", ropts, NULL)) != -1) {
		switch(c) {
		case OPT_P_PORT:
			/* port number */
			conf_pmask = 0;
			if (parse_mask(optarg, &conf_pmask) < 0) {
				pr_error("wrong port mask \"%s\"\n", optarg);
				exit(1);
			}
			/* add this set of ports to the global list of ports
			 * that are configured */
			portmask |= conf_pmask;
			break;

		case OPT_P_QMODE:
			/* pmode for port */
			arg = check_rtu("pmode", optarg, 0, 3);
			if (arg < 0)
				exit(1);
			iterate_ports(i, conf_pmask) {
				set_p_pmode(i, arg);
			}
			break;

		case OPT_P_PRIO:
			/* priority value for port, forces fix_prio=1 */
			iterate_ports(i, conf_pmask) {
				set_p_prio(i, optarg);
			}
			break;

		case OPT_P_VID:
			/* VID for port */
			iterate_ports(i, conf_pmask) {
				set_p_vid(i, optarg);
			}
			break;

		case OPT_P_UNTAG:
			/* untag mask -- currently 0 or 1. Overrides default
			 * set in QMODE above */
			arg = check_rtu("umask bit", optarg, 0, 1);
			if (arg < 0)
				exit(1);
			iterate_ports(i, conf_pmask) {
				set_p_untag(i, arg);
			}
			break;

		case OPT_P_LIST:
			/* list endpoint stuff */
			print_hp_mask();
			list_p_vlans();
			break;

	  /****************************************************/
		/* RTU settings */
		case OPT_RTU_VID:
			ret = check_rtu("rtu vid", optarg, RTU_VID_MIN,
					RTU_VID_MAX);
			if (ret < 0)
				exit(1);
			set_rtu_vlan(ret, -1, -1, 0, -1, 0, VALID_VID);
			break;
		case OPT_RTU_FID:
			ret = check_rtu("rtu fid", optarg, RTU_FID_MIN,
					RTU_FID_MAX);
			if (ret < 0)
				exit(1);
			set_rtu_vlan(-1, ret, -1, 0, -1, 0, VALID_FID);
			break;
		case OPT_RTU_PMASK:
			ret = check_rtu("pmask", optarg, RTU_PMASK_MIN,
					RTU_PMASK_MAX);
			if (ret < 0)
				exit(1);
			set_rtu_vlan(-1, -1, ret, 0, -1, 0, VALID_PMASK);
			break;
		case OPT_RTU_DROP:
			ret = check_rtu("rtu drop", optarg, 0, 1);
			if (ret < 0)
				exit(1);
			set_rtu_vlan(-1, -1, -1, ret, -1, 0, VALID_DROP);
			break;
		case OPT_RTU_PRIO:
			ret = check_rtu_prio(optarg);
			if (ret < RTU_PRIO_DISABLE)
				exit(1);
			set_rtu_vlan(-1, -1, -1, 0, ret, 0, VALID_PRIO);
			break;
		case OPT_RTU_DEL:
			set_rtu_vlan(-1, -1, -1, 0, -1, 1, 0);
			break;
		case OPT_RTU_HP_MASK:
			set_hp_mask(optarg);
			break;
	  /****************************************************/
		/* Other settings */
		case OPT_CLEAR:
			if (clear_all())
				exit(1); /* message already printed */
			default_vlan_config();
			break;
		case 0:
			break;
		case OPT_LIST:
			if (wrs_msg_level >= LOG_DEBUG)
				minipc_set_logfile(rtud_ch, stderr);
			list_rtu_vlans();
			break;
		case OPT_FILE_READ:
			if (wrs_msg_level >= LOG_DEBUG)
				pr_info("Using file %s as dot-config\n",
					optarg);

			/* read dot-config */
			ret = read_dot_config(optarg);
			if (ret < 0)
				exit(-ret);

			break;
		case 'q': break; /* done in wrs_msg_init() */
		case 'v': break; /* done in wrs_msg_init() */
		case '?':
		case OPT_HELP:
		default:
			print_help(prgname);
			exit(0);
		}
	}

	if (wrs_msg_level >= LOG_DEBUG)
		minipc_set_logfile(rtud_ch, stderr);

	if (wrs_msg_level >= LOG_INFO) {
		print_config_rtu(vlans);
		print_config_vlan();
	}
	/* apply vlans and rtu_vlans */
	apply_settings(vlans);
	free_rtu_vlans(rtu_vlans);

	return 0;
}

static void set_p_pmode(int ep, int arg_mode)
{
	vlans[ep].pmode = arg_mode;
	vlans[ep].valid_mask |= VALID_QMODE;
	/* untag is all-or-nothing, by default:
	   - untag enabled : ACCESS QMODE (i.e. mode == 0)
	   - untag disabled: other  QMODEs
	 When changing QMODE, we always need to set the default untagging
	 property, otherwise the user might get lost (e.g. when setting
	 ACCESS qmode, untagging is enabled, when the QMODE is changed from
	 ACCESS to TRUNK and untagging is not disabled, the behavior will be
	 very hard to debug.
	 If the user knows what he/she is doing, he/she can override default
	 setting with "--puntag 0|1" in such case either:
	 - vlans[ep].valid_mask == VALID_UNTAG, or
	 - vlans[ep].untag_mask will be overriden later
	 */
	if ((vlans[ep].valid_mask & VALID_UNTAG) == 0) // user not specified
		set_p_untag(ep,(arg_mode == 0));       // apply default value
}

static void set_p_vid(int ep, char *arg_vid)
{
	int vid;
	char *endptr;

	vid = strtol(arg_vid, &endptr, 0);
	if (*arg_vid == '\0' /* empty string */
	    || *endptr != '\0' /* more chars after number in the string */
	    || vid < PORT_VID_MIN || vid > PORT_VID_MAX
	   ) {
		pr_error("invalid vid \"%s\" for port %d\n",
			 arg_vid, ep + 1);
		exit(1);
	}
	vlans[ep].vid = vid;
	vlans[ep].valid_mask |= VALID_VID;
}

static void set_p_prio(int ep, char *arg_prio)
{
	int prio;
	char *endptr;

	prio = strtol(arg_prio, &endptr, 0);

	if (*arg_prio == '\0' /* empty string */
	    || *endptr != '\0' /* more chars after number in the string */
	    || ((prio < PORT_PRIO_MIN || prio > PORT_PRIO_MAX)
	        && (prio != PORT_PRIO_DISABLE))
	   ) {
		pr_error("invalid priority \"%s\" for port %d\n",
			 arg_prio, ep + 1);
		exit(1);
	}
	/* prio was touched, so set VALID_PRIO */
	vlans[ep].valid_mask |= VALID_PRIO;
	if (prio == PORT_PRIO_DISABLE) {
		vlans[ep].prio_val = PORT_PRIO_MIN;
		vlans[ep].fix_prio = 0;
	} else {
		vlans[ep].prio_val = prio;
		vlans[ep].fix_prio = 1;
	}
}

static void set_p_untag(int ep, int arg_untag)
{
	vlans[ep].untag_mask = arg_untag;
	vlans[ep].valid_mask |= VALID_UNTAG;
}

static void set_hp_mask(char *mask_str)
{
	int hp_mask;
	int val;
	int ret;
	char *endptr;

	hp_mask = strtol(mask_str, &endptr, 0);

	if (*mask_str == '\0' /* empty string */
	    || *endptr != '\0' /* more chars after number in the string */
	    || hp_mask >= (1 << 8)) {
		pr_error("Wrong HP mask %s\n", mask_str);
		exit(1);
	}

	ret = minipc_call(rtud_ch, MINIPC_TIMEOUT,
			  &rtud_export_hp_mask, &val,
			  RTU_SET_HP_MASK, hp_mask);
	ret = (ret < 0) ? ret : val;
	if (ret < 0) {
		pr_error("failed to set HP mask 0x%x (%s), ret %d\n",
			  hp_mask, mask_str, ret);
		exit(1);
	}

}

static int check_rtu(char *name, char *arg_val, int min, int max)
{
	int val;
	char *endptr;

	val = strtol(arg_val, &endptr, 0);

	if (*arg_val == '\0' /* empty string */
	    || *endptr != '\0' /* more chars after number in the string */
	    || val < min || val > max) {
		pr_error("invalid %s \"%s\"\n", name, arg_val);
		return -1;
	}
	return val;
}

static int check_rtu_prio(char *arg_val)
{
	int val;
	char *endptr;

	val = strtol(arg_val, &endptr, 0);

	if (*arg_val == '\0' /* empty string */
	    || *endptr != '\0' /* more chars after number in the string */
	    || ((val < RTU_PRIO_MIN || val > RTU_PRIO_MAX)
	        && (val != RTU_PRIO_DISABLE))
	   ) {
		pr_error("invalid rtu prio \"%s\"\n", arg_val);
		return -2;
	}
	return val;
}

static int print_help(char *prgname)
{
	fprintf(stderr, "Use: %s [-v] [-q] [--hpmask <mask>]"
			"[--port <port number 1..18> <port options> "
			"--port <port number> <port options> ...] "
			"[--rvid <vid> --rfid <fid> --rmask <mask> --rdrop "
			"--rprio <prio> --rvid <vid>...]  "
			"[-f|--file <dot-config>]\n", prgname);

	fprintf(stderr,
			"Port options:\n"
			"\t --port <1..18>     apply following options to particular set of ports\n"
			"\t                    for example 1-3,5-6 will apply settings to ports 1,2,3,5,6\n"
			"\t --pmode <0..3>     sets pmode for a port, possible values:\n"
			"\t \t 0: ACCESS           - tags untagged frames, drops tagged frames not belonging to configured VLAN\n"
			"\t \t                       (by default untagging enabled, override with --puntag)\n"
			"\t \t 1: TRUNK            - passes only tagged frames, drops all untagged frames\n"
			"\t \t                       (by default untagging disabled, override with --puntag)\n"
			"\t \t 2: VLANs disabled   - passes all frames as is\n"
			"\t \t                       (by default untagging disabled, override with --puntag)\n"
			"\t \t 3: Unqualified port - passes all frames regardless of VLAN config\n"
			"\t \t                       (by default untagging disabled, override with --puntag)\n");
	fprintf(stderr, "\t --pprio <%d|%d..%d>  sets priority for retagging; -1 disables retagging;\n",
			PORT_PRIO_DISABLE, PORT_PRIO_MIN, PORT_PRIO_MAX);
	fprintf(stderr, "\t --pvid <%d..%d>   sets VLAN Id for port\n",
			PORT_VID_MIN, PORT_VID_MAX);
	fprintf(stderr, "\t --puntag <0|1>     if 1 untag all vlan tags on a port\n"
			"\t --plist            lists current ports configuration\n"
			"RTU options:\n");
	fprintf(stderr, "\t --rvid <%d..%d>       VID value for which the other parameters are set in rtud\n",
			RTU_VID_MIN, RTU_VID_MAX);
	fprintf(stderr, "\t --del                  delete selected VLAN from rtud\n");
	fprintf(stderr, "\t --rfid <%d..%d>       assign fid to configured VLAN\n",
			RTU_FID_MIN, RTU_FID_MAX);
	fprintf(stderr, "\t --rmask <0x%x..0x%x> ports belonging to configured VLAN\n",
			RTU_PMASK_MIN, RTU_PMASK_MAX);
	fprintf(stderr, "\t --rdrop <0|1>          don't drop or drop frames on VLAN (note that frame can belong\n"
			"\t                        to a VID as a consequence of per-port Endpoint configuration)\n");
	fprintf(stderr, "\t --rprio <%d|%d..%d>      force priority for VLAN; -1 cancels priority override\n",
			RTU_PRIO_DISABLE, RTU_PRIO_MIN, RTU_PRIO_MAX);
	fprintf(stderr, "Other options:\n"
			"\t --hpmask <mask>   Set the mask which priorities are considered High Priority\n"
			"\t                   (this only concerns the traffic which is fast-forwarded)\n"
			"\t --clear           clears ports' and VLAN configuration\n"
			"\t --list            prints the content of RTUd VLAN table\n"
			"\t -f|--file <file>  clears configuration, then applies configuration from the provided\n"
			"\t                   dot-config file\n"
			"\t -v                be more verbose (can be used 1 or 2 times), to be used with other\n"
			"\t                   arguments, e.g.: wrsw_vlans -v -v --port 1-18\n"
			"\t -q                be less verbose\n"
			"\t --help            prints this help message\n");
	return 0;
}

static void print_config_rtu(struct s_port_vlans *vlans)
{
	int i;

	for_each_port(i) {
		printf("port: %2d, pmode: %d, pmode_valid: %d, fix_prio: %d, "
		       "prio_val: %d, prio_valid: %d, vid: %2d, vid_valid: %d,"
		       " untag_mask: 0x%X, untag_valid: %d\n",
		       i + 1,
		       vlans[i].pmode,
		       ((vlans[i].valid_mask & VALID_QMODE) != 0),
		       vlans[i].fix_prio,
		       vlans[i].prio_val,
		       ((vlans[i].valid_mask & VALID_PRIO) != 0),
		       vlans[i].vid,
		       ((vlans[i].valid_mask & VALID_VID) != 0),
		       vlans[i].untag_mask,
		       ((vlans[i].valid_mask & VALID_UNTAG) != 0)
		       );
	}
}

static void print_config_vlan(void)
{
	struct rtu_vlans_t *cur;
	cur = rtu_vlans;
	while (cur) {
		printf("vid: ");
		if (cur->flags & VALID_VID)
			printf("%4d ", cur->vid);
		else
			printf("     ");

		printf("fid: ");
		if (cur->flags & VALID_FID)
			printf("%4d ", cur->fid);
		else
			printf("     ");

		printf("port_mask: ");
		if (cur->flags & VALID_PMASK)
			printf("0x%05x ", cur->pmask);
		else
			printf("        ");

		printf("drop: ");
		if (cur->flags & VALID_DROP)
			printf("%1d ", cur->drop);
		else
			printf("  ");

		printf("prio: ");
		if (cur->flags & VALID_PRIO)
			printf("%1d", cur->prio);

		printf("\n");
		cur = cur->next;
	}
}

static uint32_t ep_read(int ep, int offset)
{
	return _fpga_readl(0x30000 + ep * 0x400 + offset);
}

static void ep_write(int ep, int offset, uint32_t value)
{
	_fpga_writel(0x30000 + ep * 0x400 + offset, value);
}

static int apply_settings(struct s_port_vlans *vlans)
{
	int ep;
	uint32_t v, r;

	for_each_port(ep) {
		/* VCR0 */
		r = offsetof(struct EP_WB, VCR0);
		v = ep_read(ep, r);
		if (vlans[ep].valid_mask & VALID_QMODE)
			v = (v & ~EP_VCR0_QMODE_MASK) | EP_VCR0_QMODE_W(vlans[ep].pmode);
		if (vlans[ep].valid_mask & VALID_PRIO) {
			if (vlans[ep].fix_prio)
				v |= EP_VCR0_FIX_PRIO;
			else
				v &= ~EP_VCR0_FIX_PRIO;
			v = (v & ~EP_VCR0_PRIO_VAL_MASK) | EP_VCR0_PRIO_VAL_W(vlans[ep].prio_val);
		}
		if (vlans[ep].valid_mask & VALID_VID)
			v = (v & ~EP_VCR0_PVID_MASK) | EP_VCR0_PVID_W(vlans[ep].vid);
		ep_write(ep, r, v);
		/* VCR1: loop over the whole bitmask */
		if (vlans[ep].valid_mask & VALID_UNTAG) {
			int i;

			r = offsetof(struct EP_WB, VCR1);
			for (i = 0;i < 4096/16; i++) {
				if (vlans[ep].untag_mask)
					ep_write(ep, r, (0xffff << 10) | i);
				else
					ep_write(ep, r, (0x0000 << 10) | i);
			}
		}
	}
	config_rtud();

	return 0;
}

static int config_rtud(void)
{
	struct rtu_vlans_t *cur;
	struct rtu_vlan_table_entry rtu_vlan_entry;
	int val;

	cur = rtu_vlans;
	while(cur) {
		if (rtu_find_vlan(&rtu_vlan_entry, cur->vid,
				  (cur->flags & VALID_FID) ? cur->fid : -1)) {

		/*preserve previous settings if not overwritten*/
			if (!(cur->flags & VALID_FID))
				cur->fid = rtu_vlan_entry.fid;
			if (!(cur->flags & VALID_PMASK))
				cur->pmask = rtu_vlan_entry.port_mask;
			if (!(cur->flags & VALID_DROP))
				cur->drop = rtu_vlan_entry.drop;
			if (!(cur->flags & VALID_PRIO)) {
				cur->prio = rtu_vlan_entry.prio;
				cur->has_prio = rtu_vlan_entry.has_prio;
				cur->prio_override =
						rtu_vlan_entry.prio_override;
			}
		}

		minipc_call(rtud_ch, MINIPC_TIMEOUT, &rtud_export_vlan_entry, &val,
				cur->vid, cur->fid, cur->pmask, cur->drop, cur->prio, cur->has_prio,
				cur->prio_override);
		cur = cur->next;
	}

	return 0;
}

static void list_rtu_vlans(void)
{
	unsigned ii;
	unsigned retries = 0;
	static struct rtu_vlan_table_entry vlan_tab_local[NUM_VLANS];

	/* read data, with the sequential lock to have all data consistent */
	while (1) {
		ii = wrs_shm_seqbegin(rtu_port_shmem);
		memcpy(&vlan_tab_local, vlan_tab_shm,
		       NUM_VLANS * sizeof(*vlan_tab_shm));
		retries++;
		if (retries > 100) {
			pr_error("couldn't read consistent data from RTU's "
				 "shmem. Use inconsistent\n");
			break; /* use inconsistent data */
			}
		if (!wrs_shm_seqretry(rtu_port_shmem, ii))
			break; /* consistent read */
		usleep(1000);
	}

	printf("# VID    FID       MASK       DROP    PRIO    PRIO_OVERRIDE\n");
	printf("#----------------------------------------------------------\n");

	for (ii = 0; ii < NUM_VLANS; ii++) {
		/* ignore entires that are not active */
		if ((vlan_tab_local[ii].drop != 0)
		    && (vlan_tab_local[ii].port_mask == 0x0))
			continue;
		printf("%4d   %4d      0x%08x    ", ii, vlan_tab_local[ii].fid,
		       vlan_tab_local[ii].port_mask);

		if (vlan_tab_local[ii].drop == 0)
			printf("NO ");
		else
			printf("YES");

		if (vlan_tab_local[ii].has_prio == 0)
			printf("     --    ");
		else
			printf("     %1d    ", vlan_tab_local[ii].prio);

		if (vlan_tab_local[ii].prio_override == 0)
			printf("     NO ");
		else
			printf("     YES ");

		printf("\n");
	}
	printf("\n");

}

static void list_p_vlans(void)
{
	uint32_t v, r;
	int ep;
	static char *names[] = {"ACCESS", "TRUNK", "disabled", "unqualified"};

	printf("#        QMODE    FIX_PRIO  PRIO    PVID     MAC\n");
	printf("#-----------------------------------------------\n");
	for (ep = 0; ep < NPORTS; ep++) {
		r = offsetof(struct EP_WB, VCR0);
		v = ep_read(ep, r);
		printf("wri%-2i    %i %6.6s     %i      %i     %4i    %04x%08x\n",
		       ep + 1, v & 3, names[v & 3],
		       v & EP_VCR0_FIX_PRIO ? 1 : 0,
		       EP_VCR0_PRIO_VAL_R(v),
		       EP_VCR0_PVID_R(v),
		       (int)ep_read(ep, offsetof(struct EP_WB, MACH)),
		       (int)ep_read(ep, offsetof(struct EP_WB, MACL)));
	}
	return;
}

static void print_hp_mask(void)
{
	int hp_mask;
	int ret;
	ret = minipc_call(rtud_ch, MINIPC_TIMEOUT, &rtud_export_hp_mask,
			  &hp_mask, RTU_GET_HP_MASK, NULL);
	if (ret < 0) {
		pr_error("failed to read HP mask, ret %d\n", ret);
		exit(1);
	}
	printf("#-----------------------------------------------\n");
	printf("# HP mask: 0x%02x\n", hp_mask);
	printf("#-----------------------------------------------\n");
}

static void default_vlan_config(void)
{
	int val;
	/* Create VLAN 0 reserved for untagged packets */
	minipc_call(rtud_ch, MINIPC_TIMEOUT, &rtud_export_vlan_entry,
		    &val, 0, 0, 0xffffffff, 0, 0, 0, 0);
}

static int clear_all(void)
{
	uint32_t r;
	int val, i;
	int ep;

	for (i = 0; i < NUM_VLANS; i++) {
		if ((vlan_tab_shm[i].drop != 0)
		    && (vlan_tab_shm[i].port_mask == 0x0))
			continue;
		minipc_call(rtud_ch, MINIPC_TIMEOUT,
				    &rtud_export_vlan_entry, &val, i,
				    vlan_tab_shm[i].fid, 0, 1, 0, 0, 0);
		}

	/* cancel tagging/untagging in all endpoints*/
	for (ep = 0; ep < NPORTS; ep++) {
		r = offsetof(struct EP_WB, VCR0);
		ep_write(ep, r, 0x3 /* QMODE */);

		r = offsetof(struct EP_WB, VCR1);
		for (i = 0;i < 4096/16; i++) {
			ep_write(ep, r, (0x0000 << 10) | i); /* no untag */
		}
	}
	return 0;
}

static int set_rtu_vlan(int vid, int fid, int pmask, int drop, int prio,
			int del, int flags)
{
	struct rtu_vlans_t *cur = rtu_vlans;;

	if (!rtu_vlans && vid < 0) {
		pr_error("missing \"--rvid <vid>\" before rtu cmd\n");
		return -1;
	}

	if (vid >= 0) {
		cur = calloc(1, sizeof(*cur));
		if (!cur) {
			pr_error("%s\n", strerror(errno));
			return -1;
		}
		cur->vid = vid;
		cur->fid = vid;
		cur->flags |= VALID_VID;

		/* link to the list, next time head is "cur" */
		cur->next = rtu_vlans;
		rtu_vlans = cur;
	}
	if(flags & VALID_FID)
		cur->fid = fid;
	if(flags & VALID_PMASK)
		cur->pmask = pmask;
	if(flags & VALID_DROP)
		cur->drop = drop;
	if(flags & VALID_PRIO && prio >= 0) {
		cur->prio = prio;
		cur->has_prio = 1;
		cur->prio_override = 1;
	}
	else if(flags & VALID_PRIO) {
		cur->prio = prio;
		cur->has_prio = 0;
		cur->prio_override = 0;
	}
	if(del) {
		/*delete this vlan*/
		cur->pmask = 0;
		cur->drop = 1;
		cur->prio = 0;
		cur->has_prio = 0;
		cur->prio_override = 0;
		flags |= (VALID_PMASK | VALID_DROP | VALID_PRIO);
	}

	cur->flags |= flags;

	return 0;
}

static void free_rtu_vlans(struct rtu_vlans_t *ptr)
{
	struct rtu_vlans_t *next = NULL;

	while(ptr) {
		next = ptr->next;
		free(ptr);
		ptr = next;
	}
}

static int rtu_find_vlan(struct rtu_vlan_table_entry *rtu_vlan_entry, int vid,
					 int fid)
{
	unsigned ii;
	unsigned retries = 0;

	/* copy data no mater if it will be used later, with the sequential
	 * lock to have all data consistent */
	while (1) {
		ii = wrs_shm_seqbegin(rtu_port_shmem);
		memcpy(rtu_vlan_entry, &vlan_tab_shm[vid],
			sizeof(*rtu_vlan_entry));
		retries++;
		if (retries > 100) {
			pr_error("couldn't read consistent data from RTU's "
				 "shmem. Use inconsistent\n");
			break; /* use inconsistent data */
			}
		if (!wrs_shm_seqretry(rtu_port_shmem, ii))
			break; /* consistent read */
		usleep(1000);
	}

	/* Ignore entires that are not active */
	if ((rtu_vlan_entry->drop != 0)
	    && (rtu_vlan_entry->port_mask == 0x0))
		return 0;

	if ((fid == rtu_vlan_entry->fid) || (fid == -1))
		return 1;
	return 0;
}

static int read_dot_config(char *dot_config_file)
{
	int line;
	int port;
	char *ret;
	char buff[60];
	char *val_ch;
	int i;
	int mode;
	int vlan0_port_mask = 0;

	if (access(dot_config_file, R_OK)) {
		pr_error("Unable to read dot-config file %s\n",
			 dot_config_file);
		return -1;
	}

	line = libwr_cfg_read_file(dot_config_file);

	if (line == -1) {
		pr_error("Unable to read dot-config file %s or error in "
			"line 1\n", dot_config_file);
		return -1;
	} else if (line) {
		pr_error("Error in dot-config file %s, error in line %d\n",
			 dot_config_file, -line);
		return -1;
	}

	ret = libwr_cfg_get("RTU_HP_MASK_ENABLE");
	if (ret && !strcmp(ret, "y")) {
		if (wrs_msg_level >= LOG_DEBUG)
			printf("Setting HP mask\n");

		ret = libwr_cfg_get("RTU_HP_MASK_VAL");
		if (ret) {
			if (wrs_msg_level >= LOG_DEBUG)
				printf("Set RTU_HP_MASK_VAL %s\n", ret);
			set_hp_mask(ret);
		} else {
			pr_error("Unable to get RTU_HP_MASK_VAL\n");
			exit(1);
		}
	}

	/* clear ports and VLANs configuration */
	clear_all();

	/* read VLANs related configuration */
	ret = libwr_cfg_get("VLANS_ENABLE");
	if (!ret || strcmp(ret, "y")) {
		if (wrs_msg_level >= LOG_DEBUG)
			printf("VLANS not enabled\n");
		/* All VLANs were removed, since VLANs are disabled set the
		 * default configuration */
		default_vlan_config();
		return -2;
	}

	for (port = 1; port <= NPORTS; port++) {
		portmask = portmask | (1 << (port - 1));
		mode = QMODE_INVALID;

		sprintf(buff, "VLANS_PORT%02d_MODE_ACCESS", port);
		ret = libwr_cfg_get(buff);
		if (ret && !strcmp(ret, "y")) {
			if (wrs_msg_level >= LOG_DEBUG)
				printf("Found %s\n", buff);
			set_p_pmode(port - 1, QMODE_ACCESS);
			mode = QMODE_ACCESS;
		}
		sprintf(buff, "VLANS_PORT%02d_MODE_TRUNK", port);
		ret = libwr_cfg_get(buff);
		if (ret && !strcmp(ret, "y")) {
			if (wrs_msg_level >= LOG_DEBUG)
				printf("Found %s\n", buff);
			set_p_pmode(port - 1, QMODE_TRUNK);
			mode = QMODE_TRUNK;
		}
		sprintf(buff, "VLANS_PORT%02d_MODE_DISABLED", port);
		ret = libwr_cfg_get(buff);
		if (ret && !strcmp(ret, "y")) {
			if (wrs_msg_level >= LOG_DEBUG)
				printf("Found %s\n", buff);
			set_p_pmode(port - 1, QMODE_DISABLED);
			mode = QMODE_DISABLED;
		}
		sprintf(buff, "VLANS_PORT%02d_MODE_UNQUALIFIED", port);
		ret = libwr_cfg_get(buff);
		if (ret && !strcmp(ret, "y")) {
			if (wrs_msg_level >= LOG_DEBUG)
				printf("Found %s\n", buff);
			set_p_pmode(port - 1, QMODE_UNQ);
			mode = QMODE_UNQ;
		}
		
		/* check UNTAG all or none only for ACCESS
		 * for other modes use untag none by default */
		if (mode == QMODE_ACCESS) {
			sprintf(buff, "VLANS_PORT%02d_UNTAG_ALL", port);
			ret = libwr_cfg_get(buff);
			if (ret && !strcmp(ret, "y")) {
				if (wrs_msg_level >= LOG_DEBUG)
					printf("Found %s\n", buff);
				set_p_untag(port - 1, 1);
			}

			sprintf(buff, "VLANS_PORT%02d_UNTAG_NONE", port);
			ret = libwr_cfg_get(buff);
			if (ret && !strcmp(ret, "y")) {
				if (wrs_msg_level >= LOG_DEBUG)
					printf("Found %s\n", buff);
				set_p_untag(port - 1, 0);
			}
		} else {
			/* for other modes "untag none" by default */
			if (wrs_msg_level >= LOG_DEBUG)
				printf("Setting port %d to untag none\n",
				       port);
			set_p_untag(port - 1, 0);
		}

		/* update a mask for vlan0 */
		if (mode != QMODE_ACCESS && mode != QMODE_TRUNK) {
			vlan0_port_mask |= 1 << (port - 1);
		}

		sprintf(buff, "VLANS_PORT%02d_PRIO", port);
		val_ch = libwr_cfg_get(buff);
		if (val_ch) {
			if (wrs_msg_level >= LOG_DEBUG)
				printf("Found %s=%s\n", buff, val_ch);
			set_p_prio(port - 1, val_ch);
		}
		if (mode == QMODE_ACCESS) {
			sprintf(buff, "VLANS_PORT%02d_VID", port);
			val_ch = libwr_cfg_get(buff);
			if (val_ch) {
				if (wrs_msg_level >= LOG_DEBUG)
					printf("Found %s=%s\n", buff, val_ch);
				set_p_vid(port - 1, val_ch);
			} else {
				pr_error("VID not defined for the port (%d) in"
					 " ACCESS mode!\n", port);
				exit(1);
			}
		}
	}

	/* If VLANS_VLAN0000 is empty => ports mask is not provided for VLAN0
	 * then add to VLAN0 ports that are not ACCESS nor TRUNK */
	val_ch = libwr_cfg_get("VLANS_VLAN0000");
	if (!val_ch || !strnlen(val_ch, 10)) {
		if (wrs_msg_level >= LOG_DEBUG)
			printf("Vlan0 not configured: Using port mask based on"
			       " configured port modes (0x%05x)\n",
			       vlan0_port_mask);
		set_rtu_vlan(0, 0, vlan0_port_mask, -1, -1, 0,
			     VALID_VID | VALID_FID | VALID_PMASK);
	}

	for (i = 0; dot_config_vlan_sets[i].name; i++) {
		ret = libwr_cfg_get(dot_config_vlan_sets[i].name);
		if (ret && !strcmp(ret, "y")) {
			read_dot_config_vlans(dot_config_vlan_sets[i].min,
					      dot_config_vlan_sets[i].max);
		}
	}
	return 0;
}

static void read_dot_config_vlans(int vlan_min, int vlan_max)
{
	int fid, prio, drop;
	unsigned long pmask;
	int vlan_flags;
	int vlan;
	char buff[60];

	for (vlan = vlan_min; vlan <= vlan_max; vlan++) {
		vlan_flags = 0;
		fid = -1;
		prio = -1;
		drop = -1;
		pmask = 0;
		if (!libwr_cfg_convert2("VLANS_VLAN%04d", "fid", LIBWR_STRING,
					buff, vlan)) {
			fid = check_rtu("rtu vid", buff, RTU_FID_MIN,
					RTU_FID_MAX);
			if (fid < 0)
				exit(1);
			if (wrs_msg_level >= LOG_DEBUG)
				printf("Vlan %4d: Found fid=%d\n",
				       vlan, fid);
			vlan_flags |= VALID_FID;
		}

		if (!libwr_cfg_convert2("VLANS_VLAN%04d", "prio", LIBWR_STRING,
					buff, vlan)) {
			prio = check_rtu_prio(buff);
			if (prio < RTU_PRIO_DISABLE)
				exit(1);
			vlan_flags |= VALID_PRIO;
			if (wrs_msg_level >= LOG_DEBUG)
				printf("Vlan %4d: Found prio=%d\n",
				       vlan, prio);
		}
		if (!libwr_cfg_convert2("VLANS_VLAN%04d", "drop", LIBWR_STRING,
					buff, vlan)) {
			if (!strcmp(buff, "y") /* "y", "yes" or "1" */
			    || !strcmp(buff, "yes")
			    || !strcmp(buff, "1"))
				sprintf(buff, "1");
			else if (!strcmp(buff, "n") /* "n", "no" or "0" */
				 || !strcmp(buff, "no")
				 || !strcmp(buff, "0"))
				sprintf(buff, "0");
			else {
				pr_error("invalid drop parameter \"%s\" in "
					 "VLANS_VLAN%04d\n", buff, vlan);
				exit(1);
			}
			drop = check_rtu("rtu drop", buff, 0, 1);
			if (drop < 0)
				exit(1);
			if (wrs_msg_level >= LOG_DEBUG)
				printf("Vlan %4d: Found drop=%d\n",
				       vlan, drop);
			vlan_flags |= VALID_DROP;
		}
		if (!libwr_cfg_convert2("VLANS_VLAN%04d", "ports",
					LIBWR_STRING, buff, vlan)) {
			parse_mask(buff, &pmask);
			if (pmask < RTU_PMASK_MIN || pmask > RTU_PMASK_MAX) {
				pr_error("invalid port mask 0x%lx (\"%s\") for"
					 " vlan %4d\n", pmask, buff, vlan);
				exit(1);
			}
			if (wrs_msg_level >= LOG_DEBUG)
				printf("Vlan %4d: Port mask 0x%05lx\n",
				       vlan, pmask);
			vlan_flags |= VALID_PMASK;
		}
		if (vlan_flags) {
			/* at least one parameter is present, so trigger
			 * the fill */
			vlan_flags |= VALID_VID;
			set_rtu_vlan(vlan, fid, pmask, drop, prio, 0,
				     vlan_flags);
		}
	}
}
