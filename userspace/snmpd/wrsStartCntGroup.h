#ifndef WRS_START_CNT_GROUP_H
#define WRS_START_CNT_GROUP_H

#define WRSSTARTCNT_CACHE_TIMEOUT 5
#define WRSSTARTCNT_OID WRS_OID, 7, 2

struct wrsStartCnt_s {
	uint32_t wrsStartCntHAL;
	uint32_t wrsStartCntPTP;
	uint32_t wrsStartCntRTUd;
	uint32_t wrsStartCntSshd;
	uint32_t wrsStartCntHttpd;
	uint32_t wrsStartCntSnmpd;
	uint32_t wrsStartCntSyslogd;
	uint32_t wrsStartCntWrsWatchdog;
	uint32_t wrsStartCntLldpd;
};

extern struct wrsStartCnt_s wrsStartCnt_s;
time_t wrsStartCnt_data_fill(void);

void init_wrsStartCntGroup(void);
#endif /* WRS_START_CNT_GROUP_H */
