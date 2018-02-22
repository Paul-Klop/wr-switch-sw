/*
 * rtu_stat:
 *
 * Give detail on the Routing-Table-Unit (RTU) and permit to modify it
 *
 *  Modified on: Oct 30, 2012
 *  Authors:
 *  	- Tomasz Wlostowski (tomasz.wlostowski@cern.ch)
 * 		- Benoit RAT (benoit<AT>sevensols.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libwr/util.h>

#include <libwr/shmem.h>
#include <libwr/hal_shmem.h>
#include <libwr/rtu_shmem.h>
#include <libwr/mac.h>

#include <minipc.h>
#include <rtud_exports.h>
#include <errno.h>

#define MINIPC_TIMEOUT 200

static struct minipc_ch *rtud_ch;

struct wrs_shm_head *rtu_port_shmem;
static struct rtu_vlan_table_entry vlan_tab_local[NUM_VLANS];
static struct rtu_filtering_entry rtu_htab_local[RTU_BUCKETS * HTAB_ENTRIES];

int rtudexp_clear_entries(int port, int type)
{
	int val, ret;
	ret = minipc_call(rtud_ch, MINIPC_TIMEOUT, &rtud_export_clear_entries,
			  &val, port, type);
	return (ret < 0) ? ret : val;
}

int rtudexp_add_entry(const char *mac, int port_mask, int type, int vid)
{
	int val, ret;
	ret = minipc_call(rtud_ch, MINIPC_TIMEOUT, &rtud_export_add_entry,
			  &val, mac, port_mask, type, vid);
	return (ret < 0) ? ret : val;
}

int rtudexp_remove_entry(const char *mac, int port_mask, int type)
{
	int val, ret;
	ret = minipc_call(rtud_ch, MINIPC_TIMEOUT, &rtud_export_remove_entry,
			  &val, mac, port_mask, type);
	return (ret < 0) ? ret : val;
}

int rtudexp_learning_process(int operation, int enable, int port_mask)
{
	int val, ret;
	ret = minipc_call(rtud_ch, MINIPC_TIMEOUT,
			  &rtud_export_learning_process, &val, operation,
			  enable, port_mask);
	return (ret < 0) ? ret : val;
}

int rtudexp_unrec(int operation, int enable, int port_mask)
{
	int val, ret;
	ret = minipc_call(rtud_ch, MINIPC_TIMEOUT, &rtud_export_unrec, &val,
			  operation, enable, port_mask);
	return (ret < 0) ? ret : val;
}

int rtudexp_vlan_entry(int vid, int fid, const char *ch_mask, int drop,
		       int prio, int has_prio, int prio_override)
{
	int val, ret;
	int mask;
	sscanf(ch_mask,"%x", &mask);
	ret = minipc_call(rtud_ch, MINIPC_TIMEOUT, &rtud_export_vlan_entry,
			  &val, vid, fid, mask, drop, prio, has_prio,
			  prio_override);
	return (ret < 0) ? ret : val;
}

static int cmp_rtu_entries(const void *p1, const void *p2)
{
	const struct rtu_filtering_entry *e1 = p1;
	const struct rtu_filtering_entry *e2 = p2;

	return memcmp(e1->mac, e2->mac, 6);
}

char *decode_ports(int dpm, int nports)
{
	static char str[80],str2[20];
	int i;

	if ((dpm & ((1<<nports)-1)) == ((1<<nports)-1))
	{
		strcpy(str,"ALL");
		return str;
	}
	strcpy(str,"");

	for (i = 0; i < nports; i++)
	{
		sprintf(str2, "%d ", i + 1);
		if(dpm&(1<<i)) strcat(str,str2);
	}

	if (dpm & (1<<nports))
		strcat(str, "CPU");

	return str;
}

void show_help(char *prgname)
{
	fprintf(stderr, "usage: %s <command> <values>\n", prgname);
	fprintf(stderr, "   help:             Show this message\n");
	fprintf(stderr, "   list:             List the routing table (same as empty command)\n");
	fprintf(stderr, "   remove all <port> [<type>]: Remove all RTU entries for the given port\n"
			"                               with an optional type (default %d-dynamic)\n",
			RTU_ENTRY_TYPE_DYNAMIC);
	fprintf(stderr, "   remove <mac> <port> [<type>]: Add a RTU entry for a specific\n"
			"                                 MAC address on a given port with an optional\n"
			"                                 type (default %d-static)\n",
			RTU_ENTRY_TYPE_STATIC);
	fprintf(stderr, "   remove mask <mac> <port_mask> [<type>]: Add a RTU entry for a specific\n"
			"                                           MAC address on a given port mask with\n"
			"                                           an optional type (default %d-static)\n",
			RTU_ENTRY_TYPE_STATIC);
	fprintf(stderr, "   add <mac> <port> [<type> <VID>]: Add entry for a specific MAC address with an\n"
			"                              optional type  (default %d-static) and VID (default %d);\n"
			"                              if VID is specified, the type also has to be specified;\n"
			"                              in order to add MAC to a VID, this VID must exist in the\n"
			"                              VLAN Table\n",
			RTU_ENTRY_TYPE_STATIC, RTU_ENTRY_VID_DEFAULT);
	fprintf(stderr, "   add mask <mac> <port_mask> [<type> <VID>]: Add an entry for a specific \n"
			"                                        MAC address and multiple ports with\n"
			"                                        an optional type  (default %d-static)\n"
			"                                        and VID (default %d); if VID is specified,\n"
			"                                        type also has to be specified;in order to\n"
			"                                        add MAC to a VID, this VID must exist in the\n"
			"                                        VLAN Table\n",
			RTU_ENTRY_TYPE_STATIC, RTU_ENTRY_VID_DEFAULT);
	fprintf(stderr, "   learning:         Read status of learning process in RTU\n");
	fprintf(stderr, "   learning <enable|disable> [<port>]: Enable/disable learning process in RTU\n"
			"                                       for optional port, otherwise for all\n"
			"                                       ports\n");
	fprintf(stderr, "   learning <enable|disable> mask <port_mask>: Enable/disable learning process\n"
			"                                               in RTU for ports given as a mask\n");
	fprintf(stderr, "   unrec:            Status of broadcasting/dropping frames when destination MAC\n"
			"                     is not matched\n");
	fprintf(stderr, "   unrec <enable|disable> [<port>]: Enable/disable broadcasting frames when\n"
			"                                    the destination MAC is not matched.\n"
			"                                    Apply setting on a given <port> or all ports\n"
			"                                    when <port> is not provided.\n");
	fprintf(stderr, "   unrec mask <enable|disable> <port_mask>: Enable/disable broadcasting frames when\n"
			"                                            the destination MAC is not matched.\n"
			"                                            Apply setting on ports given as a mask\n");
	fprintf(stderr, "   vlan <vid> <fid> <port_mask> [<drop>, <prio>, <has_prio>, <prio_override>]:\n"
			"                                Add VLAN entry with vid, fid, mask and drop flag;\n"
			"                                write mask=0x0 and drop=1 to remove the VLAN\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Where:\n");
	fprintf(stderr, "   <mac>           MAC address to be used in the RTU rule;\n"
			"                   it is in the format XX:XX:XX:XX:XX:XX\n");
	fprintf(stderr, "   <port>          port number on which RTU rule is registered; it is a port\n"
			"                   number from 1 to 18, or the CPU (19)\n");
	fprintf(stderr, "   <port_mask>     Mask of ports on which RTU rule is registered;\n"
			"                   it is a hex mask of ports in the format 0xXXXXX\n");
	fprintf(stderr, "   <type>          Numerical representation of rule's type %d=dynamic or %d=static\n",
			RTU_ENTRY_TYPE_DYNAMIC, RTU_ENTRY_TYPE_STATIC);
	fprintf(stderr, "   <vid>           VLAN ID\n");
	fprintf(stderr, "   <fid>           Filtering Database ID\n");
	fprintf(stderr, "   <drop>          Set to 1 to drop frames with <vid>; 0 don't drop\n");
	fprintf(stderr, "   <prio>          Priority to override the VLAN-tagged received frames\n"
			"                   (if <prio_override> true)\n");
	fprintf(stderr, "   <has_prio>      Indicates there is valid <prio>\n");
	fprintf(stderr, "   <prio_override> Use the <prio> in the VLAN Table to override the frame's\n"
			"                   PRIO in the tag\n");

	exit(1);
}

int get_nports_from_hal(void)
{
	struct hal_shmem_header *h;
	struct wrs_shm_head *hal_head = NULL;
	int hal_nports_local; /* local copy of number of ports */
	int ii;
	int n_wait = 0;
	int ret;

	/* wait for HAL */
	while ((ret = wrs_shm_get_and_check(wrs_shm_hal, &hal_head)) != 0) {
		n_wait++;
		if (n_wait > 10) {
			if (ret == WRS_SHM_OPEN_FAILED) {
				fprintf(stderr, "rtu_stat: Unable to open "
					"HAL's shm !\n");
			}
			if (ret == WRS_SHM_WRONG_VERSION) {
				fprintf(stderr, "rtu_stat: Unable to read "
					"HAL's version!\n");
			}
			if (ret == WRS_SHM_INCONSISTENT_DATA) {
				fprintf(stderr, "rtu_stat: Unable to read "
					"consistent data from HAL's shmem!\n");
			}
			exit(1);
		}
		sleep(1);
	}

	h = (void *)hal_head + hal_head->data_off;

	n_wait = 0;
	while (1) { /* wait for 10 sec for HAL to produce consistent nports */
		n_wait++;
		ii = wrs_shm_seqbegin(hal_head);
		/* Assume number of ports does not change in runtime */
		hal_nports_local = h->nports;
		if (!wrs_shm_seqretry(hal_head, ii))
			break;
		fprintf(stderr, "rtu_stat: Wait for HAL.\n");
		if (n_wait > 10) {
			exit(1);
		}
		sleep(1);
	}

	/* check hal's shm version */
	if (hal_head->version != HAL_SHMEM_VERSION) {
		fprintf(stderr, "rtu_stat: unknown HAL's shm version %i "
			"(known is %i)\n",
			hal_head->version, HAL_SHMEM_VERSION);
		exit(-1);
	}

	if (hal_nports_local > HAL_MAX_PORTS) {
		fprintf(stderr, "rtu_stat: Too many ports reported by HAL. "
			"%d vs %d supported\n",
			hal_nports_local, HAL_MAX_PORTS);
		exit(-1);
	}
	return hal_nports_local;
}

int read_vlans(void)
{
	unsigned ii;
	unsigned retries = 0;
	struct rtu_vlan_table_entry *vlan_tab_shm;
	struct rtu_shmem_header *rtu_hdr;

	rtu_hdr = (void *)rtu_port_shmem + rtu_port_shmem->data_off;
	vlan_tab_shm = wrs_shm_follow(rtu_port_shmem, rtu_hdr->vlans);
	if (!vlan_tab_shm)
		return -2;
	/* read data, with the sequential lock to have all data consistent */
	while (1) {
		ii = wrs_shm_seqbegin(rtu_port_shmem);
		memcpy(&vlan_tab_local, vlan_tab_shm,
		       NUM_VLANS * sizeof(*vlan_tab_shm));
		retries++;
		if (retries > 100)
			return -1;
		if (!wrs_shm_seqretry(rtu_port_shmem, ii))
			break; /* consistent read */
		usleep(1000);
	}

	return 0;
}

/* Read filtes from rtud's shm, convert hashtable to regular table */
int read_htab(int *read_entries)
{
	unsigned ii;
	unsigned retries = 0;
	struct rtu_filtering_entry *htab_shm;
	struct rtu_shmem_header *rtu_hdr;
	struct rtu_filtering_entry *empty;

	rtu_hdr = (void *)rtu_port_shmem + rtu_port_shmem->data_off;
	htab_shm = wrs_shm_follow(rtu_port_shmem, rtu_hdr->filters);
	if (!htab_shm)
		return -2;

	/* Read data, with the sequential lock to have all data consistent */
	while (1) {
		ii = wrs_shm_seqbegin(rtu_port_shmem);
		memcpy(&rtu_htab_local, htab_shm,
		       RTU_BUCKETS * HTAB_ENTRIES * sizeof(*htab_shm));
		retries++;
		if (retries > 100)
			return -1;
		if (!wrs_shm_seqretry(rtu_port_shmem, ii))
			break; /* consistent read */
		usleep(1000);
	}

	/* Convert hash table to ordered table. Table will be qsorted later,
	 * no need to qsort entire table */
	*read_entries = 0;
	empty = rtu_htab_local;
	for (ii = 0; ii < RTU_BUCKETS * HTAB_ENTRIES; ii++) {
		if (rtu_htab_local[ii].valid) {
			memcpy(empty, &rtu_htab_local[ii], sizeof(*htab_shm));
			empty++;
			(*read_entries)++;
		}
	}

	return 0;
}

int open_rtu_shm(void)
{
	int n_wait = 0;
	int ret;
	/* open rtu shm */
	while ((ret = wrs_shm_get_and_check(wrs_shm_rtu, &rtu_port_shmem)) != 0) {
		n_wait++;
		if (n_wait > 10) {
			if (ret == WRS_SHM_OPEN_FAILED) {
				fprintf(stderr, "rtu_stat: Unable to open "
					"RTUd's shm !\n");
			}
			if (ret == WRS_SHM_WRONG_VERSION) {
				fprintf(stderr, "rtu_stat: Unable to read "
					"RTUd's version!\n");
			}
			if (ret == WRS_SHM_INCONSISTENT_DATA) {
				fprintf(stderr, "rtu_stat: Unable to read "
					"consistent data from RTUd's shmem!\n"
				       );
			}
			exit(1);
		}
		sleep(1);
	}

	/* check rtu shm version */
	if (rtu_port_shmem->version != RTU_SHMEM_VERSION) {
		fprintf(stderr, "rtu_stat: unknown rtud's version %i "
			"(known is %i)\n", rtu_port_shmem->version,
			RTU_SHMEM_VERSION);
		return -1;
	}
	return 0;
}

int read_port(char *port, int nports)
{
	int i;
	if (!strcmp(port, "CPU") || !strcmp(port, "cpu")) {
		return 1 + nports; /* CPU is usually a 19th port */
	}

	i = strtol(port, NULL, 0);
	/* interface number 1..18, CPU is 19 */
	if (0 < i && i <= (nports + 1)) { /* 18 ports + CPU */
		return i;
	}
	return -1;
}

int read_port_mask(char *mask, int nports)
{
	int i;
	if (!strcmp(mask, "CPU") || !strcmp(mask, "cpu")) {
		return 1 << nports; /* CPU is usually at 19th bit */
	}
	if (!strcmp(mask, "ALL") || !strcmp(mask, "all")) {
		return (1 << (nports + 1)) - 1;
	}

	i = strtol(mask, NULL, 0);
	/* interface number 1..18, CPU is 19 */
	if (0 < i && i <= ((1 << (nports + 1)) - 1)) { /* 18 ports + CPU */
		return i;
	}
	return -1;
}

int main(int argc, char **argv)
{
	int i, isok;
	int nports;
	int htab_read_entries;
	int vid_active = 0;
	char mac_buf[ETH_ALEN_STR];
	int n_wait = 0;
	int ret;
	int type;
	int vid;
	int enable;
	uint32_t mask;

	nports = get_nports_from_hal();

	while (!(rtud_ch = minipc_client_create("rtud", 0)))
	{
		if (n_wait > 5) {
			/* Don't print first 5 times, it may take few seconds
			 * for rtud to start */
			fprintf(stderr, "rtu_stat: Can't connect to RTUd "
				"mini-rpc server\n");
		}
		n_wait++;
		sleep(1);
	}
	minipc_set_logfile(rtud_ch,stderr);

	/* Open rtud's shmem, it should be available after connecting rtud's
	 * minipc */
	if (open_rtu_shm()) {
		fprintf(stderr, "rtu_stat: Can't open RTUd shmem\n");
		return -1;
	}

	isok = 0;
/* ****************** remove all <port> [<type>] *************************** */
	if (argc >= 4
	    && !strcmp(argv[1], "remove")
	    && !strcmp(argv[2], "all")) {
		/* rtu_stat remove all <port> [<type>] */
		i = read_port(argv[3], nports);
		/* interface number 1..18, CPU is 19 */
		if (i < 0) {
			printf("Wrong port number %s\n", argv[3]);
			exit(1);
		}
		type = atoidef(argc, argv, 5, RTU_ENTRY_TYPE_DYNAMIC);
		if (rtu_check_type(type)) {
			fprintf(stderr, "rtu_stat: Unknown type %d\n", type);
			exit(1);
		}
		ret = rtudexp_clear_entries(i - 1, /* port */
					    type /* type */
					    );
		/* ok */
		isok = 1;
/* ****************** remove mask <mac> <port_mask> [<type>] *************** */
	} else if (argc >= 5
		   && !strcmp(argv[1], "remove")
		   && !strcmp(argv[2], "mask")) {
		/* rtu_stat remove mask <mac> <port_mask> [<type>] */
		i = read_port_mask(argv[4], nports);
		/* interface number 1..18, CPU is 19 */
		if (i < 0) {
			printf("Wrong port mask 0x%s\n", argv[4]);
			exit(1);
		}
		if (mac_to_lower(argv[3]) || mac_verify(argv[3])) {
			fprintf(stderr, "rtu_stat: Wrong MAC %s\n", argv[3]);
			exit(1);
		}
		type = atoidef(argc, argv, 5, RTU_ENTRY_TYPE_STATIC);
		if (rtu_check_type(type)) {
			fprintf(stderr, "rtu_stat: Unknown type %d\n", type);
			exit(1);
		}
		ret = rtudexp_remove_entry(argv[3], /* MAC */
					    i, /* port mask */
					    type /* type */
					  );
		if (ret > 0)
			printf("Removed %d entries for port mask 0x%x\n",
			       ret, i);
		if (ret < 0) {
			printf("Could not remove entry for port mask 0x%x\n",
			       i);
			exit(1);
		}
		isok = 1;
/* ****************** remove <mac> <port> [<type>] ************************* */
	} else if (argc >= 4
		   && !strcmp(argv[1], "remove")) {
		/* rtu_stat remove <mac> <port> [<type>] */
		i = read_port(argv[3], nports);
		/* interface number 1..18, CPU is 19 */
		if (i < 0) {
			printf("Wrong port number %s\n", argv[3]);
			exit(1);
		}
		if (mac_to_lower(argv[2]) || mac_verify(argv[2])) {
			fprintf(stderr, "rtu_stat: Wrong MAC %s\n", argv[2]);
			exit(1);
		}
		type = atoidef(argc, argv, 4, RTU_ENTRY_TYPE_STATIC);
		if (rtu_check_type(type)) {
			fprintf(stderr, "rtu_stat: Unknown type %d\n", type);
			exit(1);
		}
		ret = rtudexp_remove_entry(argv[2], /* MAC */
					1 << (i - 1), /* port mask */
					type /* type */
					);
		if (ret > 0)
			printf("Removed %d entries for port %d (wri%d)\n",
			       ret, i, i);
		if (ret < 0) {
			printf("Could not remove entry for port %d (wri%d)\n",
			       i, i);
			exit(1);
		}
		isok = 1;
/* ****************** add mask <mac> <port_mask> [<type>, <VID>] ************ */
	} else if (argc >= 5
		   && !strcmp(argv[1], "add")
		   && !strcmp(argv[2], "mask")) {
		/* rtu_stat add mask <mac> <port_mask> [<type>, <VID>] */
		i = read_port_mask(argv[4], nports);
		/* interface number 1..18, CPU is 19 */
		if (i < 0) {
			printf("Wrong port mask 0x%s\n", argv[4]);
			exit(1);
		}
		if (mac_to_lower(argv[3]) || mac_verify(argv[3])) {
			fprintf(stderr, "rtu_stat: Wrong MAC %s\n", argv[3]);
			exit(1);
		}
		type = atoidef(argc, argv, 5, RTU_ENTRY_TYPE_STATIC);
		if (rtu_check_type(type)) {
			fprintf(stderr, "rtu_stat: Unknown type %d\n", type);
			exit(1);
		}
		vid = atoidef(argc, argv, 6, RTU_ENTRY_VID_DEFAULT);
		if (0 > vid || vid > 0xFFF) {
			fprintf(stderr, "rtu_stat: Incorrect VID %d\n", vid);
			exit(1);
		}
		ret = rtudexp_add_entry(argv[3], /* MAC */
					i, /* port mask */
					type, /* type */
					vid /* VID */
					);
		if (ret > 0)
			printf("Added %d entries for port mask 0x%x\n",
			       ret, i);
		if (ret < 0) {
			printf("Could not add entry for port mask 0x%x\n", i);
			exit(1);
		}
		isok = 1;
/* ****************** add <mac> <port> [<type>, <VID>] ********************** */
	} else if (argc >= 4
		   && !strcmp(argv[1], "add")) {
		/* rtu_stat add <mac> <port> [<type>, <VID>] */
		i = read_port(argv[3], nports);
		/* interface number 1..18, CPU is 19 */
		if (i < 0) {
			printf("Wrong port number %s\n", argv[3]);
			exit(1);
		}
		if (mac_to_lower(argv[2]) || mac_verify(argv[2])) {
			fprintf(stderr, "rtu_stat: Wrong MAC %s\n", argv[2]);
			exit(1);
		}
		type = atoidef(argc, argv, 4, RTU_ENTRY_TYPE_STATIC);
		if (rtu_check_type(type)) {
			fprintf(stderr, "rtu_stat: Unknown type %d\n", type);
			exit(1);
		}
		vid = atoidef(argc, argv, 5, RTU_ENTRY_VID_DEFAULT);
		if (0 > vid || vid > 0xFFF) {
			fprintf(stderr, "rtu_stat: Incorrect VID %d\n", vid);
			exit(1);
		}
		ret = rtudexp_add_entry(argv[2], /* MAC */
					1 << (i - 1), /* port mask */
					type, /* type */
					vid /* VID */
					);
		if (ret > 0)
			printf("Added %d entries for port %d (wri%d)\n",
			       ret, i, i);
		if (ret < 0) {
			printf("Could not add entry for port %d (wri%d)\n",
			       i, i);
			exit(1);
		}
		isok = 1;
/* ****************** learning <enable|disable> mask <port_mask> *********** */
	} else if (argc >= 5
		    && strcmp(argv[1], "learning") == 0
		    && strcmp(argv[3], "mask") == 0) {
		if (!strcmp(argv[2], "enable"))
			enable = RTU_LEARNING_ENABLE;
		else if (!strcmp(argv[2], "disable"))
			enable = RTU_LEARNING_DISABLE;
		else {
			fprintf(stderr, "rtu_stat: expected \"enable\""
				" or \"disable\"\n");
			exit(1);
		}
		mask = 0;
		if (!strcmp(argv[4], "ALL")
		    || !strcmp(argv[4], "all")) {
			mask = (1 << (nports + 1)) - 1;
		} else {
			i = strtol(argv[4], NULL, 0);
			/* interface number 1..18 */
			if (0 < i && i <= ((1 << nports) - 1)) {
				mask = i;
			}
		}

		/* interface number 1..18, CPU is 19 */
		if (mask == 0) {
			printf("Wrong port mask 0x%s\n", argv[4]);
			exit(1);
		}
		ret = rtudexp_learning_process(
					RTU_SET_LEARNING, /* oper */
					enable, /* enable */
					mask /* port_mask */
					);
		if (ret > 0)
			printf("Update of learning state change successful\n");
		if (ret < 0) {
			printf("Could not change learning state 0x%x\n", ret);
			exit(1);
		}
		/* Don't do anything more */
		return 0;
/* ****************** learning <enable|disable> [<port>] ******************* */
	} else if (argc >= 3
		    && strcmp(argv[1], "learning") == 0) {
		if (!strcmp(argv[2], "enable"))
			enable = RTU_LEARNING_ENABLE;
		else if (!strcmp(argv[2], "disable"))
			enable = RTU_LEARNING_DISABLE;
		else {
			fprintf(stderr, "rtu_stat: expected \"enable\""
				" or \"disable\"\n");
			exit(1);
		}
		if (argc >= 4) { /* port defined */
			i = strtol(argv[3], NULL, 0);
			/* interface number 1..18*/
			if (1 > i || i > nports) { /* 18 ports */
				printf("Wrong port %s\n", argv[3]);
				exit(1);
			}
			mask = 1 << (i - 1);
		} else { /* port undefined, use as all */
			mask = (1 << (nports)) - 1;
		}
		ret = rtudexp_learning_process(
					RTU_SET_LEARNING, /* oper */
					enable, /* enable */
					mask /* port_mask */
					);
		if (ret > 0)
			printf("Update of learning state change successful\n");
		if (ret < 0) {
			printf("Could not change learning state 0x%x\n", ret);
			exit(1);
		}
		/* Don't do anything more */
		return 0;
/* ****************** learning ********************************************* */
	} else if (argc >= 2
		    && strcmp(argv[1], "learning") == 0) {
		ret = rtudexp_learning_process(
					RTU_GET_LEARNING, /* oper */
					0, /* enable */
					(1 << nports) - 1 /* port_mask */
					);
		if (ret < 0) {
			printf("Could not read learning state 0x%x\n", ret);
			exit(1);
		}
		printf("-------------\n");
		printf("Port | Learning\n");
		printf("-------------\n");
		for (i = 0; i < nports; i++) {
			printf(" %2d    %s\n", i + 1,
				(ret & 1) ? "enabled" : "disabled");
			ret >>= 1;
		}
		/* Don't do anything more */
		return 0;
/* ****************** unrec <enable|disable> mask <port_mask> *********** */
	} else if (argc >= 5
		    && strcmp(argv[1], "unrec") == 0
		    && strcmp(argv[3], "mask") == 0) {
		if (!strcmp(argv[2], "enable"))
			enable = RTU_UNREC_ENABLE;
		else if (!strcmp(argv[2], "disable"))
			enable = RTU_UNREC_DISABLE;
		else {
			fprintf(stderr, "rtu_stat: expected \"enable\""
				" or \"disable\"\n");
			exit(1);
		}
		mask = 0;
		if (!strcmp(argv[4], "ALL")
		    || !strcmp(argv[4], "all")) {
			mask = (1 << (nports + 1)) - 1;
		} else {
			i = strtol(argv[4], NULL, 0);
			/* interface number 1..18 */
			if (0 < i && i <= ((1 << nports) - 1)) {
				mask = i;
			}
		}

		/* interface number 1..18, CPU is 19 */
		if (mask == 0) {
			printf("Wrong port mask 0x%s\n", argv[4]);
			exit(1);
		}
		ret = rtudexp_unrec(RTU_SET_UNREC, /* oper */
				    enable, /* enable */
				    mask /* port_mask */
				    );
		if (ret > 0)
			printf("Update of unrec state change successful\n");
		if (ret < 0) {
			printf("Could not change unrec state 0x%x\n", ret);
			exit(1);
		}
		/* Don't do anything more */
		return 0;
/* ****************** unrec <enable|disable> [<port>] ******************* */
	} else if (argc >= 3
		    && strcmp(argv[1], "unrec") == 0) {
		if (!strcmp(argv[2], "enable"))
			enable = RTU_UNREC_ENABLE;
		else if (!strcmp(argv[2], "disable"))
			enable = RTU_UNREC_DISABLE;
		else {
			fprintf(stderr, "rtu_stat: expected \"enable\""
				" or \"disable\"\n");
			exit(1);
		}
		if (argc >= 4) { /* port defined */
			i = strtol(argv[3], NULL, 0);
			/* interface number 1..18*/
			if (1 > i || i > nports) { /* 18 ports */
				printf("Wrong port %s\n", argv[3]);
				exit(1);
			}
			mask = 1 << (i - 1);
		} else { /* port undefined, use as all */
			mask = (1 << (nports)) - 1;
		}
		ret = rtudexp_unrec(RTU_SET_UNREC, /* oper */
				    enable, /* enable */
				    mask /* port_mask */
				    );
		if (ret > 0)
			printf("Update of unrec state change successful\n");
		if (ret < 0) {
			printf("Could not change unrec state 0x%x\n", ret);
			exit(1);
		}
		/* Don't do anything more */
		return 0;
/* ****************** unrec ************************************************ */
	} else if (argc >= 2
		    && strcmp(argv[1], "unrec") == 0) {
		ret = rtudexp_unrec(RTU_GET_UNREC, /* oper */
				    0, /* enable */
				    (1 << nports) - 1 /* port_mask */
				    );
		if (ret < 0) {
			printf("Could not read unrec state 0x%x\n", ret);
			exit(1);
		}
		printf("Dropping frames when the destination MAC is not "
		       "matched\n");
		printf("----------------------\n");
		printf("Port | Unrec broadcast\n");
		printf("----------------------\n");
		for (i = 0; i < nports; i++) {
			printf(" %2d    %s\n", i + 1,
				(ret & 1) ? "enabled" : "disabled");
			ret >>= 1;
		}
		/* Don't do anything more */
		return 0;
/* ****************** vlan <vid> <fid> <port_mask>
 *			   [<drop>, <prio>, <has_prio>, <prio_override>] *** */
	} else if (argc >= 5
		   && !strcmp(argv[1], "vlan")) {
		if (rtudexp_vlan_entry(atoi(argv[2]),
				       atoi(argv[3]) - 1,
				       argv[4],
				       atoidef(argc, argv, 5, 0),
				       atoidef(argc, argv, 6, 0),
				       atoidef(argc, argv, 7, 0),
				       atoidef(argc, argv, 8, 0)
				       ) == 0) {
			/* ok */
			isok = 1;
		} else {
			printf("Vlan command error\n");
			exit(1);
		}
	} else if (argc >= 2
		   && !strcmp(argv[1], "list"))
		isok = 1;

	/* Does not continue */
	if (argc > 1 && !isok) {
		show_help(argv[0]);
		exit(1);
	}


	/* read filter entires from shm to local memory for data consistency */
	if (read_htab(&htab_read_entries)) {
		printf("Too many retries while reading htab entries from RTUd "
		       "shmem\n");
		return -1;
	}

	qsort(rtu_htab_local, htab_read_entries,
	      sizeof(struct rtu_filtering_entry), cmp_rtu_entries);

	printf("RTU Filtering Database Dump: %d rules\n", htab_read_entries);
	printf("\n");
	printf("MAC                     Dst.ports      FID          Type               Age [s]\n");
	printf("----------------------------------------------------------------------------------\n");

	for (i = 0; i < htab_read_entries; i++)
	{
		if (!rtu_htab_local[i].valid)
			continue;
		printf("%-25s %-12s %2d          %-7s (hash %03x:%x)   ",
			mac_to_buffer(rtu_htab_local[i].mac, mac_buf),
			decode_ports(rtu_htab_local[i].port_mask_dst, nports),
			rtu_htab_local[i].fid,
			rtu_type_to_str(rtu_htab_local[i].dynamic),
			rtu_htab_local[i].addr.hash,
			rtu_htab_local[i].addr.bucket);
		if (rtu_htab_local[i].dynamic == RTU_ENTRY_TYPE_DYNAMIC)
			printf("%d\n", rtu_htab_local[i].age);
		else
			printf("-\n");
	}
	printf("\n");

	/* read vlans from shm to local memory for data consistency */
	if (read_vlans()) {
		printf("Too many retries while reading vlans from RTUd "
		       "shmem\n");
		return -1;
	}

	printf("RTU VLAN Table Dump:\n");
	printf("\n");
	printf("  VID    FID       MASK       DROP    PRIO    PRIO_OVERRIDE\n");
	printf("-----------------------------------------------------------\n");

	for (i = 0; i < NUM_VLANS; i++) {
		if ((vlan_tab_local[i].drop != 0)
		     && (vlan_tab_local[i].port_mask == 0x0))
			continue;

		printf("%4d   %4d      0x%8x    ", i, vlan_tab_local[i].fid,
		       vlan_tab_local[i].port_mask);
		if (vlan_tab_local[i].drop == 0)
			printf("NO ");
		else
			printf("YES");
		if (vlan_tab_local[i].has_prio == 0)
			printf("     --    ");
		else
			printf("     %1d    ", vlan_tab_local[i].prio);

		if (vlan_tab_local[i].prio_override == 0)
			printf("     NO ");
		else
			printf("     YES ");
		printf("\n");
		vid_active++;
	}
	printf("\n");
	printf("%d active VIDs defined\n", vid_active);
	printf("\n");

	return 0;
}
