#ifndef WRS_PTP_INSTANCE_TABLE_H
#define WRS_PTP_INSTANCE_TABLE_H

#define WRSPTPINSTANCETABLE_CACHE_TIMEOUT 5
#define WRSPTPINSTANCETABLE_OID WRS_OID, 7, 8


struct wrsPtpInstanceTable_s {
	uint32_t wrsPtpInstanceIndex;		/* not reported, index fields has t o be marked
				 * as not-accessible in MIB */
	int wrsPtpInstancePort;	/* port on which ptp instance is running (index+1) */
	char wrsPtpInstancePortName[12];/* port name on which ptp instance is running (wriX) */
	char wrsPtpInstanceName[12];	/* Instance name */
	int wrsPtpInstanceState;
	int wrsPtpInstanceStateNext;
	int wrsPtpInstanceRole;
	int wrsPtpInstanceMechanism;
	int wrsPtpInstanceProto;
	int wrsPtpInstanceExt;
	char wrsPtpInstancePeerMac[ETH_ALEN];
	int wrsPtpInstancePeerVid;
	/* vlans: */
	/* Number of VLANs nvlans*/
	/* List (Table?) of VLANs? */
	/* flags? */

};

extern struct wrsPtpInstanceTable_s wrsPtpInstanceTable_array[PP_MAX_LINKS];

time_t wrsPtpInstanceTable_data_fill(unsigned int *rows);
void init_wrsPtpInstanceTable(void);

#endif /* WRS_PTP_INSTANCE_TABLE_H */
