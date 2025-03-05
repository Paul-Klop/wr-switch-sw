#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <arpa/inet.h>

#include <libwr/switch_hw.h>
#include <libwr/wrs-msg.h>
#include <libwr/shw_io.h>
/* for shw_sfp_buses_init and shw_sfp_print_header*/
#include "../libwr/i2c_sfp.h"
#include <libwr/shmem.h>
#include <libwr/hal_shmem.h>
#include <libwr/hal_client.h>


#define SFP_EEPROM_READ 1
#define SFP_EEPROM_WRITE 2
#define HAL_PROCESS_NAME "/wr/bin/wrsw_hal"
#define PROCESS_COMMAND_HAL "/bin/ps axo command"
#define MONIT_PROCESS_NAME "/usr/bin/monit"
#define PROCESS_COMMAND_MONIT "/bin/ps axo stat o command"

#define READ_HAL 1
#define READ_I2C 2

#define HAL_CONNECT_RETRIES 5
#define HAL_CONNECT_TIMEOUT 2000000 /* us */


static struct wrs_shm_head *hal_head;
static struct hal_port_state *hal_ports;
static struct shw_sfp_caldata *shw_sfp_cal_list_local;
static int hal_nports_local;

void print_info(char *prgname)
{
	printf("usage: %s <-I|-L> [parameters]\n", prgname);
	printf(""
		"Select the source of SFP eeprom data:\n"
		"   -L                 Use eeprom data read by HAL at SFP insertion time (default)\n"
		"   -I                 Use read eeprom data directly from SFP via I2C\n"
		"                      NOTE: Be carefull! All reads (i2c transfers) are in race with HAL!\n"
		"                      May corrupt SFPs EEPROM if HAL is running!\n"
		"Optional parameters:\n"
		"   -p <num>           Dump sfp header for specific port (1-18); dump sfp header info for all\n"
		"                      ports if no <-p> specified\n"
		"   -a <READ|WRITE>    Read/write SFP's eeprom; works only with <-I>;\n"
		"                      before READs/WRITEs disable HAL and monit!\n"
		"   -f <file>          File to READ/WRITE SFP's eeprom\n"
		"   -H <dir>           Open shmem dumps from the given directory; works only with <-L>\n"
		"   -d                 Dump sfp DOM data page\n"
		"   -x                 Dump sfp/DOM header also in hex\n"
		"   -b                 Dump SFP database from HAL\n"
		"   -m                 Dump SFP basic parameters with matching information (marked with \"+\") from HAL\n"
		"   -s                 Dump SFP (with DOM) summary as table.\n"
		"   -A                 Dump information about alarms. Implies -s parameter. Note: Output is very wide.\n"
		"   -t <on|off|1|0|s>  Enable(1), disable(0) or check status of SFP's TX pin; Use with -L or -I\n"
		"   -q                 Decrease verbosity\n"
		"   -v                 Increase verbosity\n"
		"   -V                 Print version\n"
		"   -h                 Show this message\n"
		"\n"
	);

}

static int check_monit(void)
{
	FILE *f;
	char command[41]; /* 1 for null char */
	char stat[5]; /* 1 for null char */
	int ret = 0;

	f = popen(PROCESS_COMMAND_MONIT, "r");
	if (!f) {
		pr_error("Error while checking the presence of HAL!\n");
		exit(1);
	}
	while (ret != EOF) {
		/* read first word from line (process name) ignore rest of
		 * the line */
		ret = fscanf(f, "%4s %40s%*[^\n]", stat, command);

		if (ret != 2)
			continue; /* error... or EOF */
		if (!strcmp(MONIT_PROCESS_NAME, command)) {
			if (strcmp(stat, "T")) {
				/* if monit in "T" then not really running */
				pclose(f);
				return 1;
			}
		}
	}
	pclose(f);
	return 0;
}

static int check_hal(void)
{
	FILE *f;
	char key[41]; /* 1 for null char */
	int ret = 0;

	f = popen(PROCESS_COMMAND_HAL, "r");
	if (!f) {
		pr_error("Error while checking the presence of HAL!\n");
		exit(1);
	}
	while (ret != EOF) {
		/* read first word from line (process name) ignore rest of
		 * the line */
		ret = fscanf(f, "%40s%*[^\n]", key);
		if (ret != 1)
			continue; /* error... or EOF */
		if (!strcmp(HAL_PROCESS_NAME, key)) {
			pclose(f);
			return 1;
		}
	}
	pclose(f);
	return 0;
}

static void sfp_eeprom_read(char *eeprom_file, int port)
{
	struct shw_sfp_header sfp_header;
	FILE *fp;
	int ret;

	if (!eeprom_file) {
		pr_error("Please specify file to READ!\n");
		exit(1);
	}
	memset(&sfp_header, 0, sizeof(struct shw_sfp_header));
	if (check_hal() > 0) {
		/* HAL may disturb sfp's eeprom read! */
		pr_warning("HAL is running! It may disturb SFP's eeprom read"
			   "\n");
	}
	if (check_monit() > 0) {
		/* Monit may restart, which may disturb sfp's eeprom read! */
		pr_warning("Monit is running! It may restart HAL\n");
	}
	ret = shw_sfp_read(port - 1, I2C_SFP_ADDRESS, 0x0,
			   sizeof(struct shw_sfp_header),
			   (uint8_t *) &sfp_header);
	if (ret == I2C_DEV_NOT_FOUND) {
		pr_error("Unable to read SFP header for port %d\n", port);
		return;
	}

	fp = fopen(eeprom_file, "wb");
	if (!fp) {
		pr_error("Unable to open file %s!\n", eeprom_file);
		return;
	}

	ret = fwrite(&sfp_header, 1, sizeof(struct shw_sfp_header), fp);

	pr_info("Written %d bytes to file \"%s\" for port %d\n", ret,
		eeprom_file, port);
	fclose(fp);
}

static void sfp_eeprom_write(char *eeprom_file, int port)
{
	struct shw_sfp_header sfp_header;
	FILE *fp;
	int ret;

	if (!eeprom_file) {
		pr_error("Please specify file to WRITE!\n");
		exit(1);
	}
	memset(&sfp_header, 0, sizeof(struct shw_sfp_header));
	if (check_hal() > 0) {
		/* HAL may disturb sfp's eeprom write! */
		pr_error("HAL is running! It may disturb SFP's eeprom write\n");
		exit(1);
	}
	if (check_monit() > 0) {
		/* Monit may restart, which may disturb sfp's eeprom write! */
		pr_error("Monit is running! It may restart HAL\n");
		exit(1);
	}

	fp = fopen(eeprom_file, "rb");
	if (!fp) {
		pr_error("Unable to open file %s!\n", eeprom_file);
		exit(1);
	}

	ret = fread(&sfp_header, 1, sizeof(struct shw_sfp_header), fp);

	if (ret != sizeof(struct shw_sfp_header)) {
		pr_error("Wrong number of bytes read. Expected %zu, read %d\n",
			 sizeof(struct shw_sfp_header), ret);
		exit(1);
	}
	fclose(fp);

	ret = shw_sfp_write(port - 1, I2C_SFP_ADDRESS, 0x0,
			   sizeof(struct shw_sfp_header),
			   (uint8_t *) &sfp_header);
	if (ret == I2C_DEV_NOT_FOUND) {
		pr_error("Unable to write SFP header for port %d\n", port);
		return;
	}
	pr_info("Written %d bytes to SFP's eeprom from file \"%s\" for port "
		"%d\n", ret, eeprom_file, port);
}


void print_version(char *prgname)
{
	printf("%s version %s, build by %s\n", prgname, __GIT_VER__,
	       __GIT_USR__);
}

int hal_read_sfp_eeprom(struct hal_port_calibration *sfp_calib_local_copy) {
	unsigned ii;
	unsigned retries = 0;
	int port;

	/* read data, with the sequential lock to have all data consistent */
	while (1) {
		ii = wrs_shm_seqbegin(hal_head);
		for (port = 0; port < hal_nports_local; port++) {
			memcpy(&sfp_calib_local_copy[port].sfp_header_raw,
			       &hal_ports[port].calib.sfp_header_raw,
			       sizeof(struct shw_sfp_header));
		}
		for (port = 0; port < hal_nports_local; port++) {
			memcpy(&sfp_calib_local_copy[port].sfp_dom_raw,
			       &hal_ports[port].calib.sfp_dom_raw,
			       sizeof(struct shw_sfp_dom));
		}
		retries++;
		if (retries > 100)
			return -1;
		if (!wrs_shm_seqretry(hal_head, ii))
			break; /* consistent read */
		usleep(1000);
	}

	return 0;
}

int hal_read_sfp_caldata(struct hal_port_calibration *sfp_calib_local_copy,
			 int *hal_port_fiber_index_copy)
{
	unsigned ii;
	unsigned retries = 0;
	int port;

	/* read data, with the sequential lock to have all data consistent */
	while (1) {
		ii = wrs_shm_seqbegin(hal_head);
		for (port = 0; port < hal_nports_local; port++) {
			memcpy(&sfp_calib_local_copy[port].sfp,
			       &hal_ports[port].calib.sfp,
			       sizeof(struct shw_sfp_caldata));
			hal_port_fiber_index_copy[port] =
						hal_ports[port].fiber_index;
		}
		retries++;
		if (retries > 100)
			return -1;
		if (!wrs_shm_seqretry(hal_head, ii))
			break; /* consistent read */
		usleep(1000);
	}

	return 0;
}

void hal_init_shm(void)
{
	struct hal_shmem_header *h;
	int ret;
	int n_wait = 0;
	while ((ret = wrs_shm_get_and_check(wrs_shm_hal, &hal_head)) != 0) {
		n_wait++;
		if (ret == WRS_SHM_OPEN_FAILED) {
			pr_error("Unable to open HAL's shmem !\n");
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
			exit(1);
		}
		usleep(900000);
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

	/* Get the pointer to the SFP database */
	shw_sfp_cal_list_local = wrs_shm_follow(hal_head, h->shw_sfp_cal_list);
	/* shw_sfp_cal_list may be NULL if SFP database is empty */
}

static void dump_sfp_database_from_hal(void)
{
	struct shw_sfp_caldata *sfp_db_entry = shw_sfp_cal_list_local;

	printf(" # |    Vendor Name   |    Part Number   |  Rev |   Vendor Serial  | TX WL | RX WL | Delta TX | Delta RX\n");
	printf("---+------------------+------------------+------+------------------+-------+-------+----------+---------\n");

	while (sfp_db_entry) {
		printf("%2d", sfp_db_entry->db_entry);
		printf(" | %16.16s", sfp_db_entry->vendor_name);
		printf(" | %16.16s", sfp_db_entry->part_num);
		printf(" | %4.4s", sfp_db_entry->vendor_revision);
		printf(" | %16.16s", sfp_db_entry->vendor_serial);
		printf(" | %5d", sfp_db_entry->tx_wl);
		printf(" | %5d", sfp_db_entry->rx_wl);
		printf(" | %8d", sfp_db_entry->delta_tx_ps);
		printf(" | %8d", sfp_db_entry->delta_rx_ps);
		printf("\n");

		sfp_db_entry = wrs_shm_follow(hal_head, sfp_db_entry->next);
	};
}

static void dump_sfp_database_match_reason_from_hal(int dump_port, int nports,
						    struct hal_port_calibration *hal_sfp_calib_lc,
						    int *hal_port_fiber_index_lc)
{
	int i = 0;

	printf("                                      From SFPs EEPROM                                      @         From SFP Database         @ From Fiber Database\n");
	printf(" Port |     Vendor Name     |     Part Number     |   Rev   |    Vendor Serial    |  TX WL  @ #DB | RX WL | Delta TX | Delta RX @ #DB |     Alpha\n");
	printf("------+---------------------+---------------------+---------+---------------------+---------+-----+-------+----------+----------+-----+----------------\n");

	for (i = dump_port; i <= nports; i++) {
		printf("%2d%3s", i, hal_sfp_calib_lc[i - 1].sfp.match_flags ? "(+)" : "");
		printf(" | %16.16s%3s", hal_sfp_calib_lc[i - 1].sfp.vendor_name,
		       hal_sfp_calib_lc[i - 1].sfp.match_flags & SFP_MATCH_FLAG_VN ? "(+)" : "");
		printf(" | %16.16s%3s", hal_sfp_calib_lc[i - 1].sfp.part_num,
		       hal_sfp_calib_lc[i - 1].sfp.match_flags & SFP_MATCH_FLAG_PN ? "(+)" : "");
		printf(" | %4.4s%3s", hal_sfp_calib_lc[i - 1].sfp.vendor_revision,
		       hal_sfp_calib_lc[i - 1].sfp.match_flags & SFP_MATCH_FLAG_VR ? "(+)" : "");
		printf(" | %16.16s%3s", hal_sfp_calib_lc[i - 1].sfp.vendor_serial,
		       hal_sfp_calib_lc[i - 1].sfp.match_flags & SFP_MATCH_FLAG_VS ? "(+)" : "");
		printf(" | %4d%3s", hal_sfp_calib_lc[i - 1].sfp.tx_wl,
		       hal_sfp_calib_lc[i - 1].sfp.match_flags & SFP_MATCH_FLAG_TX_WAVELENGTH ? "(+)" : "");
		if (hal_sfp_calib_lc[i - 1].sfp.match_flags) {
			printf(" @ %3d", hal_sfp_calib_lc[i - 1].sfp.db_entry);
			printf(" | %5d", hal_sfp_calib_lc[i - 1].sfp.rx_wl);
			printf(" | %8d", hal_sfp_calib_lc[i - 1].sfp.delta_tx_ps);
			printf(" | %8d", hal_sfp_calib_lc[i - 1].sfp.delta_rx_ps);
		} else {
			printf(" @ %3s | %5s | %8s | %8s", "", "", "", "");
		}
		if (hal_sfp_calib_lc[i - 1].sfp.flags & SFP_FLAG_FIBER_IN_DB) {
			printf(" @ %2d%1.1s", hal_port_fiber_index_lc[i - 1], hal_sfp_calib_lc[i - 1].sfp.flags & SFP_FLAG_FIBER_REV_IN_DB ? "R" : " ");
			printf(" | %15.12f", hal_sfp_calib_lc[i - 1].sfp.alpha);
		} else {
			printf(" @ %3s | %15s", "", "");
		}
		printf("\n");
	};
}

static void sfp_summary_print_header(void)
{
	printf("                                                                           @                      DOM\n");
	printf(" # |    Vendor Name   |   Part Number    |  Rev |   Vendor Serial  | TX WL @   Temp  |  Volt | BiasCur |     TX Power    |     RX Power\n");
	printf("   |                  |                  |      |                  |  (nm) @    (C)  |   (V) |   (mA)  |     mW (dBm)    |     mW (dBm)\n");
	printf("---+------------------+------------------+------+------------------+-------+---------+-------+---------+-----------------+----------------\n");
}


static void sfp_summary_print_entry(int i, struct shw_sfp_header *sfp_hdr_p,
				    struct shw_sfp_dom *sfp_dom_p)
{
	int tmp_i;
	double tmp_f;
	printf("%2d", i);
	printf(" | %16.16s", sfp_hdr_p->vendor_name);
	printf(" | %16.16s", sfp_hdr_p->vendor_pn);
	printf(" | %4.4s",   sfp_hdr_p->vendor_rev);
	printf(" | %16.16s", sfp_hdr_p->vendor_serial);

	/* DOM */
	if ((tmp_i = getSfpTxWaveLength(sfp_hdr_p))) {
		printf(" | %5d", getSfpTxWaveLength(sfp_hdr_p));
	} else {
		printf(" | %5s", "");
	}
	if ((tmp_f = (int16_t) ntohs(sfp_dom_p->temp) / (float) 256)) {
		printf(" @ %7.3f", tmp_f);
	} else {
		printf(" @ %7s", "");
	}
	if ((tmp_f = ntohs(sfp_dom_p->vcc) / (float) 10000)) {
		printf(" | %5.3f", tmp_f);
	} else {
		printf(" | %5s", "");
	}
	if ((tmp_f = ntohs(sfp_dom_p->tx_bias) / (float) 500)) {
		printf(" | %7.3f", tmp_f);
	} else {
		printf(" | %7s", "");
	}
	if ((tmp_f = (ntohs(sfp_dom_p->tx_pow))  /(float) 10000)) {
		printf(" | %6.4f (%6.2f)", tmp_f, 10 * log10(tmp_f));
	} else {
		printf(" | %15s", "");
	}
	if ((tmp_f = (ntohs(sfp_dom_p->rx_pow)) / (float) 10000)) {
		printf(" | %6.4f (%6.2f)", tmp_f, 10 * log10(tmp_f));
	} else {
		printf(" | %15s", "");
	}

	printf("\n");
}

float sfp_conv_temp(uint16_t temp)
{
	return (int16_t) ntohs(temp) / (float) 256;
}

float sfp_conv_voltage(uint16_t voltage)
{
	return ntohs(voltage) / (float) 10000;
}

float sfp_conv_tx_bias(uint16_t tx_bias)
{
	return (ntohs(tx_bias)) / (float) 500;
}

float sfp_conv_power(uint16_t power)
{
	return (ntohs(power)) / (float) 10000;
}

static void sfp_summary_print_header_alarms(void)
{
	printf("                                                                           @                                                                                                 DOM\n");
	printf(" # |    Vendor Name   |   Part Number    |  Rev |   Vendor Serial  | TX WL @                   Temp                  |              Voltage              |                 BiasCur                 |                   TX Power                  |                   RX Power\n");
	printf("   |                  |                  |      |                  |  (nm) @  A Hi <  W Hi {    (C)  }  W Lo >  A Hi | A Hi < W Hi {  (V)  } W Lo > A Hi |  A Hi <  W Hi {    (mA) }  W Lo >  A Hi | A Hi < W Hi {     mW (dBm)    } W Lo > A Hi | A Hi < W Hi {     mW (dBm)    } W Lo > A Hi\n");
	printf("---+------------------+------------------+------+------------------+-------+-----------------------------------------+-----------------------------------+-----------------------------------------+---------------------------------------------+--------------------------------------------\n");
}

static void sfp_summary_print_entry_alarms(int i,
					   struct shw_sfp_header *sfp_hdr_p,
					   struct shw_sfp_dom *sfp_dom_p)
{
	int tmp_i;
	double tmp_f;
	printf("%2d", i);
	printf(" | %16.16s", sfp_hdr_p->vendor_name);
	printf(" | %16.16s", sfp_hdr_p->vendor_pn);
	printf(" | %4.4s", sfp_hdr_p->vendor_rev);
	printf(" | %16.16s", sfp_hdr_p->vendor_serial);

	/* DOM */
	if ((tmp_i = getSfpTxWaveLength(sfp_hdr_p))) {
		printf(" | %5d", getSfpTxWaveLength(sfp_hdr_p));
	} else {
		printf(" | %5s", "");
	}
	if ((tmp_f = sfp_conv_temp(sfp_dom_p->temp))) {
		printf(" @ %5.1f", sfp_conv_temp(sfp_dom_p->temp_high_alarm));
		printf(" < %5.1f", sfp_conv_temp(sfp_dom_p->temp_high_warn));
		printf(" { %7.3f", tmp_f);
		printf(" } %5.1f", sfp_conv_temp(sfp_dom_p->temp_low_warn));
		printf(" > %5.1f", sfp_conv_temp(sfp_dom_p->temp_low_alarm));
	} else {
		printf(" @ %5s   %5s   %7s   %5s   %5s", "", "", "", "", "");
	}
	if ((tmp_f = sfp_conv_voltage(sfp_dom_p->vcc))) {
		printf(" | %4.2f", sfp_conv_voltage(sfp_dom_p->volt_high_alarm));
		printf(" < %4.2f", sfp_conv_voltage(sfp_dom_p->volt_high_warn));
		printf(" { %5.3f", tmp_f);
		printf(" } %4.2f", sfp_conv_voltage(sfp_dom_p->volt_low_warn));
		printf(" > %4.2f", sfp_conv_voltage(sfp_dom_p->volt_low_alarm));
	} else {
		printf(" | %4s   %4s   %5s   %4s   %4s", "", "", "", "", "");
	}
	if ((tmp_f = sfp_conv_tx_bias(sfp_dom_p->tx_bias))) {
		printf(" | %5.1f", sfp_conv_tx_bias(sfp_dom_p->bias_high_alarm));
		printf(" < %5.1f", sfp_conv_tx_bias(sfp_dom_p->bias_high_warn));
		printf(" { %7.3f", tmp_f);
		printf(" } %5.1f", sfp_conv_tx_bias(sfp_dom_p->bias_low_warn));
		printf(" > %5.1f", sfp_conv_tx_bias(sfp_dom_p->bias_low_alarm));
	} else {
		printf(" | %5s   %5s   %7s   %5s   %5s", "", "", "", "", "");
	}
	if ((tmp_f = sfp_conv_power(sfp_dom_p->tx_pow))) {
		printf(" | %4.2f", sfp_conv_power(sfp_dom_p->tx_power_high_alarm));
		printf(" < %4.2f", sfp_conv_power(sfp_dom_p->tx_power_high_warn));
		printf(" { %6.4f (%6.2f)", tmp_f, 10 * log10(tmp_f));
		printf(" } %4.2f", sfp_conv_power(sfp_dom_p->tx_power_low_warn));
		printf(" > %4.2f", sfp_conv_power(sfp_dom_p->tx_power_low_alarm));
	} else {
		printf(" | %4s   %4s   %6s %8s   %4s   %4s", "", "", "", "", "", "");
	}
	if ((tmp_f = sfp_conv_power(sfp_dom_p->rx_pow))) {
		printf(" | %4.2f", sfp_conv_power(sfp_dom_p->rx_power_high_alarm));
		printf(" < %4.2f", sfp_conv_power(sfp_dom_p->rx_power_high_warn));
		printf(" { %6.4f (%6.2f)", tmp_f, 10 * log10(tmp_f));
		printf(" } %4.2f", sfp_conv_power(sfp_dom_p->rx_power_low_warn));
		printf(" > %4.2f", sfp_conv_power(sfp_dom_p->rx_power_low_alarm));
	} else {
		printf(" | %4s   %4s   %6s %8s   %4s   %4s", "", "", "", "", "", "");
	}

	printf("\n");
}

int main(int argc, char **argv)
{
	int c;
	struct shw_sfp_header sfp_hdr;
	struct shw_sfp_header *sfp_hdr_p;
	struct shw_sfp_dom sfp_dom;
	struct shw_sfp_dom *sfp_dom_p;
	int err;
	int nports;
	int dump_port;
	int i;
	int dump_hex_header = 0;
	int dump_sfp_dom = 0;
	int operation = 0;
	char *eeprom_file = NULL;
	int sfp_data_source = READ_HAL;
	int sfp_tx_update = 0;
	int sfp_tx_enable = 0;
	int dump_sfp_database = 0;
	int dump_sfp_database_match_reason = 0;
	int dump_sfp_summary = 0;
	int dump_sfp_summary_alarms = 0;
	/* local copy of sfp eeprom */
	struct hal_port_calibration hal_sfp_calib_lc[HAL_MAX_PORTS];
	int hal_port_fiber_index_lc[HAL_MAX_PORTS];

	wrs_msg_init(argc, argv, LOG_USER);
	nports = 18;
	dump_port = 1;

	while ((c = getopt(argc, argv, "a:hqvp:xVf:LIdH:t:bmsA")) != -1) {
		switch (c) {
		case 'p':
			dump_port = atoi(optarg);
			if (dump_port) {
				nports = dump_port;
			} else {
				printf("Wrong port number!\n");
				print_info(argv[0]);
				exit(1);
			}
			break;
		case 'x':
			dump_hex_header = 1;
			break;
		case 'd':
			dump_sfp_dom = 1;
			break;
		case 'V':
			print_version(argv[0]);
			exit(0);
		case 'q': break; /* done in wrs_msg_init() */
		case 'v': break; /* done in wrs_msg_init() */
		case 'a':
			if (!strcmp(optarg, "READ"))
				operation = SFP_EEPROM_READ;
			else if (!strcmp(optarg, "WRITE"))
				operation = SFP_EEPROM_WRITE;
			else {
				operation = 0;
				pr_error("Wrong operation!\n");
				exit(1);
			}
			break;
		case 'f':
			eeprom_file = strdup(optarg);
			if (!eeprom_file) {
				pr_error("File error!\n");
				exit(1);
			}
			break;
		case 'b':
			dump_sfp_database = 1;
			break;
		case 'm':
			dump_sfp_database_match_reason = 1;
			break;
		case 's':
			dump_sfp_summary = 1;
			break;
		case 'A':
			dump_sfp_summary = 1;
			dump_sfp_summary_alarms = 1;
			break;
		case 'L':
			/* HAL mode */
			sfp_data_source = READ_HAL;
			break;
		case 'I':
			/* I2C mode */
			sfp_data_source = READ_I2C;
			break;
		case 'H':
			/* Set path for shmem files */
			wrs_shm_set_path(optarg);
			/* Ignore WRS_SHM_LOCKED flag */
			wrs_shm_ignore_flag_locked(1);
			break;
		case 't':
			/* Handle enable/disable/status of SFP's TX */
			if (!strcmp(optarg, "on") || !strcmp(optarg, "1")) {
			    sfp_tx_update = 1;
			    sfp_tx_enable = HEXP_SFP_TX_CMD_ENABLE_TX;
			}
			if (!strcmp(optarg, "off") || !strcmp(optarg, "0")) {
			    sfp_tx_update = 1;
			    sfp_tx_enable = HEXP_SFP_TX_CMD_DISABLE_TX;
			}
			if (!strcmp(optarg, "status") || !strcmp(optarg, "s")) {
			    sfp_tx_update = 1;
			    sfp_tx_enable = HEXP_SFP_TX_CMD_STATUS;
			}
			break;
		case 'h':
		default:
			print_info(argv[0]);
			print_version(argv[0]);
			exit(1);
		}
	}

	if (sfp_data_source != READ_HAL && sfp_data_source != READ_I2C) {
		pr_error("Please specify the communication way with SFP:\n"
			 "  -L for saved data in HAL at SFP insertion\n"
			 "     or TX pin enable/disable\n"
			 "  -I for direct access to SFPs via i2c\n");
		exit(1);
	}

	if (dump_sfp_database || dump_sfp_database_match_reason) {
		if (sfp_data_source != READ_HAL) {
			printf("Reading SFP database can be done only from HAL "
			       "(use -L parameter).\n");
			exit(1);
		}

		hal_init_shm();
		if (dump_sfp_database)
			dump_sfp_database_from_hal();

		if (dump_sfp_database_match_reason) {
			hal_read_sfp_caldata(hal_sfp_calib_lc,
					     hal_port_fiber_index_lc);
			dump_sfp_database_match_reason_from_hal(dump_port, nports,
								hal_sfp_calib_lc,
								hal_port_fiber_index_lc);
		}

		exit(0);
	}

	if (sfp_tx_update && sfp_data_source == READ_HAL) {
		char *msg;
		int ret;

		switch(sfp_tx_enable) {
		case HEXP_SFP_TX_CMD_ENABLE_TX:
			msg = "Enable";
			break;
		case HEXP_SFP_TX_CMD_DISABLE_TX:
			msg = "Disable";
			break;
		case HEXP_SFP_TX_CMD_STATUS:
			msg = "Check status of";
			break;
		default:
			printf("%s BUG: line %d, wrong value %d of "
			       "sfp_tx_enable\n",
			       __func__, __LINE__, sfp_tx_enable);
		}

		printf("%s TX via HAL for port %d\n", msg, dump_port);
		ret = halexp_client_try_connect(HAL_CONNECT_RETRIES,
						HAL_CONNECT_TIMEOUT);
		if (ret < 0) {
			pr_error("Can't establish WRIPC connection to the HAL "
				"daemon!\n");
			exit(1);
		}
		ret = halexp_sfp_tx_cmd(sfp_tx_enable, dump_port);
		if (sfp_tx_enable == HEXP_SFP_TX_CMD_STATUS) {
			switch(ret) {
			case HEXP_SFP_TX_CMD_ENABLE_TX:
				msg = "enabled";
				break;
			case HEXP_SFP_TX_CMD_DISABLE_TX:
				msg = "disabled";
				break;
			default:
				msg = "Error (unable to read)";
				break;
			}
			printf("SFP's TX for port %d %s\n",
			       dump_port, msg);
		}
		exit(0);
	}
	else if (sfp_data_source == READ_HAL) {
		hal_init_shm();
		hal_read_sfp_eeprom(hal_sfp_calib_lc);
		printf("Reading SFP eeprom from HAL\n");
	}

	if (sfp_tx_update && sfp_data_source == READ_I2C) {
		printf("Setting TX pin of SFP via I2C is not supported!\n"
		       "Use -L parameter to set it via HAL\n");
		exit(1);
	}

	if (sfp_data_source == READ_I2C) {
		/* init i2c, be carefull all i2c transfers are in race with
		 * hal! */
		assert_init(shw_io_init());
		assert_init(shw_fpga_mmap_init());
		assert_init(shw_sfp_buses_init());

		if (operation == SFP_EEPROM_READ) {
			sfp_eeprom_read(eeprom_file, dump_port);
			exit(0);
		}
		if (operation == SFP_EEPROM_WRITE) {
			sfp_eeprom_write(eeprom_file, dump_port);
			exit(0);
		}
		printf("Reading SFP eeprom via I2C\n");
	}


	if (dump_sfp_summary) {
		if (!dump_sfp_summary_alarms)
			sfp_summary_print_header();
		else
			sfp_summary_print_header_alarms();
	}

	for (i = dump_port; i <= nports; i++) {
		if (!dump_sfp_summary) {
			printf("========= port %d =========\n", i);
		}
		if (sfp_data_source == READ_I2C) {
			memset(&sfp_hdr, 0, sizeof(sfp_hdr));
			sfp_hdr_p = &sfp_hdr;
			err = shw_sfp_read_header(i - 1, sfp_hdr_p);
			memset(&sfp_dom, 0, sizeof(sfp_dom));
			sfp_dom_p = &sfp_dom;
			shw_sfp_read_dom(i - 1, sfp_dom_p);
		}
		if (sfp_data_source == READ_HAL) {
			sfp_hdr_p = &hal_sfp_calib_lc[i - 1].sfp_header_raw;
			sfp_dom_p = &hal_sfp_calib_lc[i - 1].sfp_dom_raw;
		}
		err = shw_sfp_header_verify(sfp_hdr_p);
		if (err == -2) {
			pr_error("SFP module not inserted in port %d. Failed "
				 "to read SFP configuration header\n", i);
		} else if (err < 0) {
			pr_error("Failed to read SFP configuration header on "
				 "port %d\n", i);
		} else if (dump_sfp_summary) {
			if (!dump_sfp_summary_alarms)
				sfp_summary_print_entry(i, sfp_hdr_p,
							sfp_dom_p);
			else
				sfp_summary_print_entry_alarms(i, sfp_hdr_p,
							       sfp_dom_p);
		} else {
			shw_sfp_print_header(sfp_hdr_p);
			if (dump_hex_header) {
				shw_sfp_header_dump(sfp_hdr_p);
			}
			if(dump_sfp_dom) {
				shw_sfp_print_dom(sfp_dom_p);
				if(dump_hex_header) {
					shw_sfp_dom_dump(sfp_dom_p);
				}
			}
		}
	}
	return 0;
}
