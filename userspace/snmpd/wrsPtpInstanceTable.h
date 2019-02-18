#ifndef WRS_PTP_INSTANCE_TABLE_H
#define WRS_PTP_INSTANCE_TABLE_H

#define WRSPTPINSTANCETABLE_CACHE_TIMEOUT 5
#define WRSPTPINSTANCETABLE_OID WRS_OID, 7, 8

/* Maximum lenth of vlans list. The worst case is the maximum number of vlans
 * for the instance times the maximum lenght of a vlan (4) + 1 for coma
 */
#define WRSPTPINSTANCEVLANLISTSTRLEN (CONFIG_VLAN_ARRAY_SIZE * 5)


struct wrsPtpInstanceTable_s {
	uint32_t wrsPtpInstancePortIndex;		/* not reported, index fields has t o be marked
				 * as not-accessible in MIB */
	uint32_t wrsPtpInstanceOnPortIndex;	/* port on which ptp instance is running (index+1) */
	char wrsPtpInstanceName[16];	/* Instance name */
	int wrsPtpInstancePort;	/* port on which ptp instance is running (index+1) */
	int wrsPtpInstancePortInstance; /* serial of instance running on a given port  */
	char wrsPtpInstancePortName[16];/* port name on which ptp instance is running (wriX) */
	int wrsPtpInstanceState;
	int wrsPtpInstanceStateNext;
	int wrsPtpInstanceRole;
	int wrsPtpInstanceMechanism;
	int wrsPtpInstanceProto;
	int wrsPtpInstanceExt;
	char wrsPtpInstancePeerMac[ETH_ALEN];
	int wrsPtpInstancePeerVid;
	int wrsPtpInstanceVlanNum;
	/* wrsPtpInstanceVlanListStr is implemented as a comma separated list
	 * because SNMP does not allow table within table */
	char wrsPtpInstanceVlanListStr[WRSPTPINSTANCEVLANLISTSTRLEN];
};

extern struct wrsPtpInstanceTable_s wrsPtpInstanceTable_array[PP_MAX_LINKS];

time_t wrsPtpInstanceTable_data_fill(unsigned int *rows);
void init_wrsPtpInstanceTable(void);

#endif /* WRS_PTP_INSTANCE_TABLE_H */
