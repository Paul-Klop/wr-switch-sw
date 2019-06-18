#include <stdlib.h>
#include <unistd.h>
#include <libwr/switch_hw.h>
#include <libwr/wrs-msg.h>
#include <libwr/shw_io.h>
/* for shw_sfp_buses_init and shw_sfp_print_header*/
#include "../libwr/i2c_sfp.h"
#include <libwr/shmem.h>
#include <libwr/hal_shmem.h>


#define SFP_EEPROM_READ 1
#define SFP_EEPROM_WRITE 2
#define HAL_PROCESS_NAME "/wr/bin/wrsw_hal"
#define PROCESS_COMMAND_HAL "/bin/ps axo command"
#define MONIT_PROCESS_NAME "/usr/bin/monit"
#define PROCESS_COMMAND_MONIT "/bin/ps axo stat o command"

#define READ_HAL 1
#define READ_I2C 2

static struct wrs_shm_head *hal_head;
static struct hal_port_state *hal_ports;
static int hal_nports_local;

void print_info(char *prgname)
{
	printf("usage: %s <-I|-L> [parameters]\n", prgname);
	printf(""
		"Select the source of SFP eeprom data:\n"
		"   -L              Use eeprom data read by HAL at SFP insertion time\n"
		"   -I              Use read eeprom data directly from SFP via I2C\n"
		"Optional parameters:\n"
		"   -p <num>        Dump sfp header for specific port (1-18); dump sfp header info for all\n"
		"                   ports if no <-p> specified\n"
		"   -a <READ|WRITE> Read/write SFP's eeprom; works only with <-I>;\n"
		"                   before READs/WRITEs disable HAL and monit!\n"
		"   -f <file>       File to READ/WRITE SFP's eeprom\n"
		"   -H <dir>        Open shmem dumps from the given directory; works only with <-L>\n"
		"   -d              Dump sfp DOM data page\n"
		"   -x              Dump sfp/DOM header also in hex\n"
		"   -q              Decrease verbosity\n"
		"   -v              Increase verbosity\n"
		"   -V              Print version\n"
		"   -h              Show this message\n"
		"\n"
		"NOTE: Be carefull! All reads (i2c transfers) are in race with HAL!\n"
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
		pr_error("Wrong number of bytes read. Expected %d, read %d\n",
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

int hal_read(struct shw_sfp_header *sfp_header_local_copy,
	     struct shw_sfp_dom *sfp_dom) {
	unsigned ii;
	unsigned retries = 0;
	int port;

	/* read data, with the sequential lock to have all data consistent */
	while (1) {
		ii = wrs_shm_seqbegin(hal_head);
		for (port = 0; port < hal_nports_local; port++) {
			memcpy(&sfp_header_local_copy[port],
			       &hal_ports[port].calib.sfp_header_raw,
			       sizeof(struct shw_sfp_header));
		}
		if (sfp_dom)
			for (port = 0; port < hal_nports_local; port++) {
				memcpy(&sfp_dom[port],
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
	int sfp_data_source = 0;
	/* local copy of sfp eeprom */
	struct shw_sfp_header hal_sfp_raw_header_lc[HAL_MAX_PORTS];
	struct shw_sfp_dom hal_sfp_raw_dom_lc[HAL_MAX_PORTS];


	wrs_msg_init(argc, argv, LOG_USER);
	nports = 18;
	dump_port = 1;

	while ((c = getopt(argc, argv, "a:hqvp:xVf:LIdH:")) != -1) {
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
		case 'L':
			/* HAL mode */
			sfp_data_source = READ_HAL;
			break;
		case 'I':
			/* HAL mode */
			sfp_data_source = READ_I2C;
			break;
		case 'H':
			/* HAL mode */
			wrs_shm_set_path(optarg);
			break;
		case 'h':
		default:
			print_info(argv[0]);
			print_version(argv[0]);
			exit(1);
		}
	}

	if (sfp_data_source != READ_HAL && sfp_data_source != READ_I2C) {
		pr_error("Please specify the source of SFP eeprom data.\n"
			 "  -L for saved data in HAL at SFP plugin\n"
			 "  -I for direct access to SFPs via i2c\n");
		exit(1);
	}

	if (sfp_data_source == READ_HAL) {
		hal_init_shm();
		hal_read(hal_sfp_raw_header_lc, hal_sfp_raw_dom_lc);
		printf("Reading SFP eeprom from HAL\n");
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


	for (i = dump_port; i <= nports; i++) {
		printf("========= port %d =========\n", i);
		if (sfp_data_source == READ_I2C) {
			memset(&sfp_hdr, 0, sizeof(sfp_hdr));
			sfp_hdr_p = &sfp_hdr;
			err = shw_sfp_read_header(i - 1, sfp_hdr_p);
			memset(&sfp_dom, 0, sizeof(sfp_dom));
			sfp_dom_p = &sfp_dom;
			shw_sfp_read_dom(i - 1, sfp_dom_p);
		}
		if (sfp_data_source == READ_HAL) {
			sfp_hdr_p = &hal_sfp_raw_header_lc[i - 1];
			sfp_dom_p = &hal_sfp_raw_dom_lc[i - 1];
		}
		err = shw_sfp_header_verify(sfp_hdr_p);
		if (err == -2) {
			pr_error("SFP module not inserted in port %d. Failed "
				 "to read SFP configuration header\n", i);
		} else if (err < 0) {
			pr_error("Failed to read SFP configuration header on "
				 "port %d\n", i);
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
