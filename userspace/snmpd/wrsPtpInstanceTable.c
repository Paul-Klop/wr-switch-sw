#include "wrsSnmp.h"
#include "snmp_shmem.h"
#include "wrsPtpInstanceTable.h"

struct wrsPtpInstanceTable_s wrsPtpInstanceTable_array[PP_MAX_LINKS];

static struct pickinfo wrsPtpInstanceTable_pickinfo[] = {
	/* Warning: strings are a special case for snmp format */
	FIELD(wrsPtpInstanceTable_s, ASN_UNSIGNED, wrsPtpInstanceIndex), /* not reported */
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER, wrsPtpInstancePort),
	FIELD(wrsPtpInstanceTable_s, ASN_OCTET_STR, wrsPtpInstancePortName),
	FIELD(wrsPtpInstanceTable_s, ASN_OCTET_STR, wrsPtpInstanceName),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER, wrsPtpInstanceState),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER, wrsPtpInstanceStateNext),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER, wrsPtpInstanceRole),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER, wrsPtpInstanceMechanism),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER, wrsPtpInstanceProto),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER, wrsPtpInstanceExt),
	FIELD(wrsPtpInstanceTable_s, ASN_OCTET_STR, wrsPtpInstancePeerMac),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER, wrsPtpInstancePeerVid),
};

static inline struct hal_port_state *pp_wrs_lookup_port(char *name)
{
	int i;

	for (i = 0; i < hal_nports_local; i++)
		if (hal_ports[i].in_use &&!strcmp(name, hal_ports[i].name))
                        return hal_ports + i;
	return NULL;
}

time_t wrsPtpInstanceTable_data_fill(unsigned int *n_rows)
{
	unsigned ii, i;
	unsigned retries = 0;
	static time_t time_update;
	time_t time_cur;
	static int n_rows_local = 0;
	struct wrsPtpInstanceTable_s *i_a;
	struct pp_instance *ppsi_i;
	char *tmp_name;
	struct hal_port_state *p;

	/* number of rows does not change for wrsPortStatusTable */
	if (n_rows)
		*n_rows = n_rows_local;

	time_cur = get_monotonic_sec();
	if (time_update
	    && time_cur - time_update < WRSPTPINSTANCETABLE_CACHE_TIMEOUT) {
		/* cache not updated, return last update time */
		return time_update;
	}
	time_update = time_cur;

	memset(&wrsPtpInstanceTable_array, 0, sizeof(wrsPtpInstanceTable_array));

	i_a = wrsPtpInstanceTable_array;

	/* check whether shmem is available */
	if (!shmem_ready_ppsi() && !ppsi_ppi_nlinks) {
		snmp_log(LOG_ERR, "%s: Unable to read PPSI's shmem\n", __func__);
		/* If set to 0 all PPSI related OIDs disappear */
		n_rows_local = 0;
		return time_update;
	} else {
		n_rows_local = *ppsi_ppi_nlinks;
	}

	if (n_rows)
		*n_rows = n_rows_local;

	while (1) {
		ii = wrs_shm_seqbegin(ppsi_head);
		for (i = 0; i < *ppsi_ppi_nlinks; i++) {

			ppsi_i = ppsi_ppi + i;
			/* (ppsi_ppi + i)->iface_name is a pointer in
			 * shmem, so we have to follow it
			 * NOTE: ppsi_i->cfg.port_name cannot be used instead,
			 * because it is not used when ppsi is configured from
			 * cmdline */

			tmp_name = (char *) wrs_shm_follow(ppsi_head,
					       ppsi_i->port_name);
			strncpy(i_a[i].wrsPtpInstanceName, tmp_name, 12);
			i_a[i].wrsPtpInstanceName[11] = '\0';

			tmp_name = (char *) wrs_shm_follow(ppsi_head,
					       ppsi_i->iface_name);
			strncpy(i_a[i].wrsPtpInstancePortName, tmp_name, 12);
			i_a[i].wrsPtpInstancePortName[11] = '\0';

			p = pp_wrs_lookup_port(tmp_name);
			if (p)
				i_a[i].wrsPtpInstancePort = p->hw_index + 1;

			i_a[i].wrsPtpInstanceState = ppsi_i->state;
			i_a[i].wrsPtpInstanceStateNext = ppsi_i->next_state;
			i_a[i].wrsPtpInstanceRole = ppsi_i->role + 1;
			i_a[i].wrsPtpInstanceMechanism = ppsi_i->mech + 1;
			i_a[i].wrsPtpInstanceProto = ppsi_i->proto + 1;
			i_a[i].wrsPtpInstanceExt = ppsi_i->cfg.ext + 1;

			memcpy(i_a[i].wrsPtpInstancePeerMac, ppsi_i->peer, ETH_ALEN);
			i_a[i].wrsPtpInstancePeerVid = ppsi_i->peer_vid;
		}

		retries++;
		if (retries > 100) {
			snmp_log(LOG_ERR, "%s: Unable to read PPSI, too many retries\n",
					   __func__);
			retries = 0;
			break;
			}
		if (!wrs_shm_seqretry(ppsi_head, ii))
			break; /* consistent read */
		usleep(1000);
	}

	/* there was an update, return current time */
	return time_cur;
}

#define TT_OID WRSPTPINSTANCETABLE_OID
#define TT_PICKINFO wrsPtpInstanceTable_pickinfo
#define TT_DATA_FILL_FUNC wrsPtpInstanceTable_data_fill
#define TT_DATA_ARRAY wrsPtpInstanceTable_array
#define TT_GROUP_NAME "wrsPtpInstanceTable"
#define TT_INIT_FUNC init_wrsPtpInstanceTable
#define TT_CACHE_TIMEOUT WRSPTPINSTANCETABLE_CACHE_TIMEOUT

#include "wrsTableTemplate.h"
