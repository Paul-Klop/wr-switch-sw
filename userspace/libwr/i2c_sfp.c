/*
 * i2c_sfc.c
 * CERN 2012 Manohar Vanga
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stddef.h>
#include <math.h>
#include <arpa/inet.h>

#include <libwr/pio.h>
#include <libwr/wrs-msg.h>
#include <libwr/util.h>
#include <libwr/config.h>

#include "i2c.h"
#include "i2c_sfp.h"
#include "i2c_bitbang.h"
#include "i2c_fpga_reg.h"

#include "libshw_i2c.h"

#define ARRAY_SIZE(a)                               \
  (sizeof(a) / sizeof(*(a)))

/* The rest of the buses are normal */
#define WR_SFP2_BUS	2
#define WR_SFP3_BUS	3
#define WR_SFP4_BUS	4
#define WR_SFP5_BUS	5
#define WR_SFP6_BUS	6
#define WR_SFP7_BUS	7
#define WR_SFP8_BUS	8
#define WR_SFP9_BUS	9
#define WR_SFP10_BUS	10
#define WR_SFP11_BUS	11
#define WR_SFP12_BUS	12
#define WR_SFP13_BUS	13
#define WR_SFP14_BUS	14
#define WR_SFP15_BUS	15
#define WR_SFP16_BUS	16
#define WR_SFP17_BUS	17

#define SFP_LED_SYNCED_MASK(t)	((t) ? (1 << 6) : (1 << 0))
#define SFP_LED_LINK_MASK(t)	((t) ? (1 << 4) : (1 << 1))
#define SFP_LED_WRMODE_MASK(t)	((t) ? (1 << 5) : (1 << 3))
#define SFP_TX_DISABLE_MASK(t)	((t) ? (1 << 7) : (1 << 2))

/* Either 8 or 16 byte pages, so we use the smaller */
#define SFP_PAGE_SIZE 8

/*
 * We need these tables because the schematics are messed up
 * The first one is for figuring out the masks in the pca9548's
 * The second table is for the connections of the pca9554's
 */
uint32_t bus_masks[] = {
	[WR_SFP2_BUS] = 8,
	[WR_SFP3_BUS] = 9,
	[WR_SFP4_BUS] = 10,
	[WR_SFP5_BUS] = 11,
	[WR_SFP6_BUS] = 12,
	[WR_SFP7_BUS] = 13,
	[WR_SFP8_BUS] = 14,
	[WR_SFP9_BUS] = 15,
	[WR_SFP10_BUS] = 0,
	[WR_SFP11_BUS] = 1,
	[WR_SFP12_BUS] = 2,
	[WR_SFP13_BUS] = 3,
	[WR_SFP14_BUS] = 4,
	[WR_SFP15_BUS] = 5,
	[WR_SFP16_BUS] = 6,
	[WR_SFP17_BUS] = 7,
};

uint32_t pca9554_masks[] = {
	[WR_SFP2_BUS] = 14,
	[WR_SFP3_BUS] = 15,
	[WR_SFP4_BUS] = 12,
	[WR_SFP5_BUS] = 13,
	[WR_SFP6_BUS] = 10,
	[WR_SFP7_BUS] = 11,
	[WR_SFP8_BUS] = 8,
	[WR_SFP9_BUS] = 9,
	[WR_SFP10_BUS] = 6,
	[WR_SFP11_BUS] = 7,
	[WR_SFP12_BUS] = 4,
	[WR_SFP13_BUS] = 5,
	[WR_SFP14_BUS] = 2,
	[WR_SFP15_BUS] = 3,
	[WR_SFP16_BUS] = 0,
	[WR_SFP17_BUS] = 1,
};

/* The two FPGA i2c masters */
i2c_fpga_reg_t fpga_bus0_reg = {
	.base_address = FPGA_I2C_ADDRESS,
	.if_num = FPGA_I2C0_IFNUM,
	.prescaler = 500,
};

i2c_fpga_reg_t fpga_bus1_reg = {
	.base_address = FPGA_I2C_ADDRESS,
	.if_num = FPGA_I2C1_IFNUM,
	.prescaler = 500,
};

/* I2C pins are set to input to avoid wrong transfers (which can lead to
 * e.g. overwrite of SFP's eeprom) on i2c buses if HAL was killed in
 * the middle of a transfer (by e.g. system reset). */

/* The Bit-Banged I2C bus connected to the PCA9548A Multiplexers. WORKS */
pio_pin_t wr_mux_scl = {
	.port = PIOB,
	.pin = 25,
	.mode = PIO_MODE_GPIO,
	.dir = PIO_IN, /* Set as input to avoid toggle after reset */
};

pio_pin_t wr_mux_sda = {
	.port = PIOB,
	.pin = 27,
	.mode = PIO_MODE_GPIO,
	.dir = PIO_IN, /* Set as input to avoid toggle after reset */
};

struct i2c_bitbang wr_mux_bus_reg = {
	.scl = &wr_mux_scl,
	.sda = &wr_mux_sda,
};

/* The Bit-Banged I2C bus connected to the SFP 0 (Link 0). WORKS */
pio_pin_t wr_link0_sda = {
	.port = PIOB,
	.pin = 23,
	.mode = PIO_MODE_GPIO,
	.dir = PIO_IN, /* Set as input to avoid toggle after reset */
};

pio_pin_t wr_link0_scl = {
	.port = PIOB,
	.pin = 26,
	.mode = PIO_MODE_GPIO,
	.dir = PIO_IN, /* Set as input to avoid toggle after reset */
};

struct i2c_bitbang wr_link0_reg = {
	.scl = &wr_link0_scl,
	.sda = &wr_link0_sda,
};

/* The Bit-Banged I2C bus connected to the SFP 1 (Link 1). WORKS */
pio_pin_t wr_link1_sda = {
	.port = PIOB,
	.pin = 22,
	.mode = PIO_MODE_GPIO,
	.dir = PIO_IN, /* Set as input to avoid toggle after reset */
};

pio_pin_t wr_link1_scl = {
	.port = PIOB,
	.pin = 21,
	.mode = PIO_MODE_GPIO,
	.dir = PIO_IN, /* Set as input to avoid toggle after reset */
};

struct i2c_bitbang wr_link1_reg = {
	.scl = &wr_link1_scl,
	.sda = &wr_link1_sda,
};

struct i2c_bus i2c_buses[] = {
	{
		.name = "fpga_bus0",
		.type = I2C_BUS_TYPE_FPGA_REG,
		.type_specific = &fpga_bus0_reg,
	},
	{
		.name = "fpga_bus1",
		.type = I2C_BUS_TYPE_FPGA_REG,
		.type_specific = &fpga_bus1_reg,
	},
	{
		.name = "wr_mux_bus",
		.type = I2C_TYPE_BITBANG,
		.type_specific = &wr_mux_bus_reg,
	},
	{
		.name = "wr_sfp0_link0",
		.type = I2C_TYPE_BITBANG,
		.type_specific = &wr_link0_reg,
	},
	{
		.name = "wr_sfp0_link1",
		.type = I2C_TYPE_BITBANG,
		.type_specific = &wr_link1_reg,
	},
};

int shw_sfp_buses_init(void)
{
	int i;
	uint8_t byte1, byte2;
	struct i2c_bus* mux_bus;

	pr_info("Initializing SFP I2C busses...\n");
	for (i = 0; i < ARRAY_SIZE(i2c_buses); i++) {
		if (i2c_init_bus(&i2c_buses[i]) < 0) {
			pr_error("I2C SFP init failed for bus %s\n",
				i2c_buses[i].name);
			return -1;
		}
//              printf("init: success: %s\n", i2c_buses[i].name);
	}
	mux_bus = &i2c_buses[WR_MUX_BUS];
	for (i = WR_SFP2_BUS; i <= WR_SFP17_BUS; i++) {
		/* Set the mask in the PCA9548 */
		byte1 = (1 << bus_masks[i]) & 0xff;
		byte2 = ((1 << bus_masks[i]) >> 8) & 0xff;
		i2c_transfer(mux_bus, 0x70, 1, 0, &byte1);
		i2c_transfer(mux_bus, 0x71, 1, 0, &byte2);
		i2c_slave_soft_reset(mux_bus, i + 1);
	}
	return 0;
}

int shw_sfp_bus_scan(int num, uint8_t * dev_map)
{
	int i;
	int detect;

	if (num < 0 || num >= ARRAY_SIZE(i2c_buses))
		return -1;

	if (i2c_buses[num].err)
		return -1;

	detect = i2c_scan(&i2c_buses[num], dev_map);
	pr_debug("\ni2c_bus: %s: %d devices\n", i2c_buses[num].name, detect);
	for (i = 0; i < 128; i++)
		if (dev_map[i / 8] & (1 << (i % 8)))
			pr_debug("device at: 0x%02X\n", i);

	return detect;
}

int shw_sfp_header_verify_base(struct shw_sfp_header *head)
{
	int i;
	uint32_t sum = 0;

	for (i = 0; i < 63; i++)
		sum += ((uint8_t *) head)[i];
	sum &= 0xff;

	if (sum != head->cc_base) {
		pr_error("shw_sfp_header_verify_base: Wrong base checksum! "
			 "(expected 0x%02x, calculated 0x%02x)\n",
			 head->cc_base, sum);
		if (wrs_msg_level >= LOG_DEBUG) {
			shw_sfp_print_header(head);
			shw_sfp_header_dump(head);
		}
		return -1;
	}
	return 0;
}

int shw_sfp_header_verify_ext(struct shw_sfp_header *head)
{
	int i;
	uint32_t sum = 0;

	for (i = 64; i < 95; i++)
		sum += ((uint8_t *) head)[i];
	sum &= 0xff;

	if (sum != head->cc_ext) {
		pr_error("shw_sfp_header_verify_ext: Wrong ext checksum! "
			 "(expected 0x%02x, calculated 0x%02x)\n",
			 head->cc_ext, sum);
		if (wrs_msg_level >= LOG_DEBUG) {
			shw_sfp_print_header(head);
			shw_sfp_header_dump(head);
		}
		return -1;
	}
	return 0;
}

int shw_sfp_header_verify(struct shw_sfp_header *head)
{
	return (!shw_sfp_header_verify_base(head) &&
		!shw_sfp_header_verify_ext(head)) ? 0 : -1;
}

static inline int getSfpTxWaveLength (struct shw_sfp_header *head) {
	return (head->tx_wavelength[0] << 8)
			+ head->tx_wavelength[1];
}

void shw_sfp_print_header(struct shw_sfp_header *head)
{
	int i;

	printf("Identifier: %02X\n", head->id);
	printf("Extended Identifier: %02X\n", head->ext_id);
	printf("Connector: %02X\n", head->connector);
	printf("Transceiver: ");
	for(i = 0; i < sizeof(head->transciever); i++)
		printf("%02X", head->transciever[i]);
	printf("\n");
	printf("Encoding: %02x\n", head->encoding);
	printf("Nominal Bit Rate: %d Megabits/s\n", head->br_nom * 100);
	printf("Length (9m): %dkm\n", head->length1);
	printf("Length (9m): %dm\n", head->length2 * 100);
	printf("Length (50m): %dm\n", head->length3 * 10);
	printf("Length (62.5m): %dm\n", head->length4 * 10);
	printf("Length (copper): %dm\n", head->length5);
	printf("Vendor Name: ");
	for (i = 0; i < 16; i++)
		printf("%c", head->vendor_name[i]);
	printf("\n");
	printf("Company ID: %02X%02X%02X\n", head->vendor_oui[0],
	       head->vendor_oui[1], head->vendor_oui[2]);
	printf("Vendor Part Number: ");
	for (i = 0; i < 16; i++)
		printf("%c", head->vendor_pn[i]);
	printf("\n");
	printf("Vendor Revision: ");
	for (i = 0; i < 4; i++)
		printf("%c", head->vendor_rev[i]);
	printf("\n");
	printf("TX Wavelength: %d\n", getSfpTxWaveLength(head));
	printf("Options: %04X\n", ((uint16_t *) head->options)[0]);
	printf("Bitrate (MAX): %02X\n", head->br_max);
	printf("Bitrate (MIN): %02X\n", head->br_min);
	printf("Vendor Serial: ");
	for (i = 0; i < 16; i++)
		printf("%c", head->vendor_serial[i]);
	printf("\n");

	printf("Date Code: ");
	for (i = 0; i < 8; i++)
		printf("%c", head->date_code[i]);
	printf("\n");
}

void shw_sfp_print_dom(struct shw_sfp_dom * dom)
{
	float tx_pow = (ntohs(dom->tx_pow))/(float)10000;
	float rx_pow = (ntohs(dom->rx_pow))/(float)10000;
	printf("Temperature: %.3f C\n", (int16_t)ntohs(dom->temp)/(float)256);
	printf("Voltage: %.3f V\n", ntohs(dom->vcc)/(float)10000);
	printf("Bias Current:  %.3f mA\n", ntohs(dom->tx_bias)/(float)500);
	printf("TX power: %.4f mW (%.1f dBm)\n", tx_pow, 10 * log10(tx_pow));
	printf("RX power: %.4f mW (%.1f dBm)\n", rx_pow, 10 * log10(rx_pow));
}

void shw_sfp_header_dump(struct shw_sfp_header *head)
{
	int i;
	uint8_t *dump = (uint8_t *) head;
	printf("Header Dump:");
	for (i = 0; i < sizeof(struct shw_sfp_header); i++) {
		if (i % 8 == 0)
			printf("\n");
		printf("%02X ", dump[i]);
	}
	printf("\n");

}

void shw_sfp_dom_dump(struct shw_sfp_dom *dom)
{
	int i;
	uint8_t *dump = (uint8_t *) dom;
	printf("Dom Dump:");
	for (i = 0; i < sizeof(struct shw_sfp_dom); i++) {
		if (i % 8 == 0)
			printf("\n");
		printf("%02X ", dump[i]);
	}
	printf("\n");
}


/* Get the SFP ID from the SFP number (0 to 17) */
inline int shw_sfp_id(int num)
{
	if (num > 17 || num < 0)
		return -1;
	return num;
}

int32_t shw_sfp_read(int num, uint32_t addr, int off, int len, uint8_t * buf)
{
	int id;
	uint8_t byte1, byte2;
	struct i2c_bus *bus;

	id = shw_sfp_id(num);
	if (id < 0)
		return -1;

	bus = &i2c_buses[WR_MUX_BUS];
	if (id == 0 || id == 1)
		bus = &i2c_buses[WR_SFP0_BUS + id];

	if (id > 1) {
		/* Set the mask in the PCA9548 */
		byte1 = (1 << bus_masks[id]) & 0xff;
		byte2 = ((1 << bus_masks[id]) >> 8) & 0xff;
		i2c_transfer(bus, 0x70, 1, 0, &byte1);
		i2c_transfer(bus, 0x71, 1, 0, &byte2);
	}

	/* Send the offset we want to read from */
	if (off >= 0)
		i2c_transfer(bus, addr, 1, 0, (uint8_t *) & off);
	/* Do the read */
	return i2c_transfer(bus, addr, 0, len, buf);
}


int32_t shw_sfp_write(int num, uint32_t addr, int off, int len, uint8_t * buf)
{
	int id;
	uint8_t byte1, byte2;
	int32_t counter = 0;
	struct i2c_bus *bus;
	int i = 0;
	uint8_t page[SFP_PAGE_SIZE + 1];
	int ret;

	/* The SFP eeprom only supports 8 bit addresses */
	if (len < 1 || len > 256 || off < 0 || off > 256 || off + len < 0
	    || off + len > 256)
		return -1;

	id = shw_sfp_id(num);
	if (id < 0)
		return -1;

	bus = &i2c_buses[WR_MUX_BUS];
	if (id == 0 || id == 1)
		bus = &i2c_buses[WR_SFP0_BUS + id];

	if (id != 0 && id != 1) {
		/* Set the mask in the PCA9548 */
		byte1 = (1 << bus_masks[id]) & 0xff;
		byte2 = ((1 << bus_masks[id]) >> 8) & 0xff;
		i2c_transfer(bus, 0x70, 1, 0, &byte1);
		i2c_transfer(bus, 0x71, 1, 0, &byte2);
	}

	/* Write in a paged mode, 1 byte address */
	page[0] = (counter + off) & 0xff;
	while (counter < len)
	{
		page[i + 1] = buf[counter++];
		i++;
		/* When we hit a page boundary then perform a write */
		if ((off + counter) % SFP_PAGE_SIZE == 0)
		{
			pr_debug("Writing %d bytes to EEPROM address %02x\n",
				 i, page[0]);
			ret = i2c_transfer(bus, addr, i + 1, 0, page);
			if (ret < 0) {
				pr_error("i2c_transfer error code 0x%x\n",
					 ret);
				return -1;
			}
			i = 0;
			page[0] = (counter + off) & 0xff;
			/* Sleep 10ms for eeprom to finish writing the page */
			/* XXX this should actually be done by polling the  */
			/* EEPROM and seeing if it is ready */
			usleep(10000);
		}
	}
	/* Write the last page, if not already done */
	if (i != 0)
	{
		pr_debug("Writing last %d bytes to EEPROM address %02x\n",
			i, page[0]);
		ret = i2c_transfer(bus, addr, i + 1, 0, page);
		if (ret < 0) {
			pr_error("i2c_transfer error code 0x%x\n", ret);
			return -1;
		}
		usleep(10000);
	}
	return counter;
}

uint32_t shw_sfp_module_scan(void)
{
	int i;
	int ret;
	uint32_t mask = 0;
	uint8_t test;
	for (i = 0; i < 18; i++) {
		ret = shw_sfp_read(i, 0x50, 0x0, sizeof(test), &test);
		if (ret == I2C_DEV_NOT_FOUND)
			continue;
		mask |= (1 << i);
	}
	return mask;
}

void shw_sfp_gpio_init(void)
{
	int i;
	uint8_t addr = 0x20;
	uint8_t conf_output[] = { 0x3, 0x0 };
	uint8_t set_output[] = { 0x1, 0x0 };
	struct i2c_bus *bus = &i2c_buses[WR_FPGA_BUS0];

	/* configure the pins as outputs */
	i2c_transfer(bus, addr, 2, 0, conf_output);
	i2c_transfer(bus, addr, 2, 0, set_output);

	bus = &i2c_buses[WR_FPGA_BUS1];
	for (i = 0; i < 8; i++) {
		i2c_transfer(bus, addr + i, 2, 0, conf_output);
		i2c_transfer(bus, addr + i, 2, 0, set_output);
	}

	for (i = 0; i < 18; i++) {
		shw_sfp_set_led_synced(i, 1);
		shw_udelay(7000);
		shw_sfp_set_generic(i, 1, SFP_LED_WRMODE1 | SFP_LED_WRMODE2);
		shw_udelay(7000);
	}
	for (i = 0; i < 18; i++) {
		shw_sfp_set_led_synced(i, 0);
		shw_udelay(7000);
		shw_sfp_set_generic(i, 0, SFP_LED_WRMODE1 | SFP_LED_WRMODE2);
		shw_udelay(7000);
	}

}

void shw_sfp_gpio_set(int num, uint8_t state)
{
	int id;
	int top;
	struct i2c_bus *bus;
	uint8_t addr = 0x20;
	uint8_t send[2];
	uint8_t curr;

	id = shw_sfp_id(num);
	bus = &i2c_buses[WR_FPGA_BUS1];
	if (id < 2)
		bus = &i2c_buses[WR_FPGA_BUS0];

	if (id > 1) {
		addr += pca9554_masks[id] / 2;
		top = pca9554_masks[id] % 2;
	} else {
		top = id % 2;
	}

	send[0] = 0x1;
	/* Read current state of pins */

	i2c_transfer(bus, addr, 1, 0, send);
	i2c_transfer(bus, addr, 0, 1, &curr);

	//pr_info("%d: 0x%x 0x%x s=%d, send=%d,%d c=%d, \n",num,bus,addr,state,send[0],send[1],&curr);

	if (top)
		curr &= 0xf;
	else
		curr &= 0xf0;

	if (state & SFP_LED_WRMODE1)
		curr |= SFP_LED_LINK_MASK(top);
	if (state & SFP_LED_WRMODE2)
		curr |= SFP_LED_WRMODE_MASK(top);
	if (state & SFP_LED_SYNCED)
		curr |= SFP_LED_SYNCED_MASK(top);
	if (state & SFP_TX_DISABLE)
		curr |= SFP_TX_DISABLE_MASK(top);

	send[1] = curr;
	i2c_transfer(bus, addr, 2, 0, send);

	//pr_info("%d: 0x%x 0x%x s=%d, send=%d,%d c=%d, \n",num,bus,addr,state,send[0],send[1],curr);
}

uint8_t shw_sfp_gpio_get(int num)
{
	int id;
	int top;
	struct i2c_bus *bus;
	uint8_t addr = 0x20;
	uint8_t send[2];
	uint8_t out = 0;
	uint8_t curr;

	id = shw_sfp_id(num);
	bus = &i2c_buses[WR_FPGA_BUS1];
	if (id < 2)
		bus = &i2c_buses[WR_FPGA_BUS0];

	if (id > 1) {
		addr += pca9554_masks[id] / 2;
		top = pca9554_masks[id] % 2;
	} else {
		top = id % 2;
	}

	send[0] = 0x1;
	/* Read current state of pins */
	i2c_transfer(bus, addr, 1, 0, send);
	i2c_transfer(bus, addr, 0, 1, &curr);

	if (curr & SFP_LED_LINK_MASK(top))
		out |= SFP_LED_WRMODE1;
	if (curr & SFP_LED_WRMODE_MASK(top))
		out |= SFP_LED_WRMODE2;
	if (curr & SFP_LED_SYNCED_MASK(top))
		out |= SFP_LED_SYNCED;
	if (curr & SFP_TX_DISABLE_MASK(top))
		out |= SFP_TX_DISABLE;

	return out;
}

int shw_sfp_read_header(int num, struct shw_sfp_header *head)
{
	int ret;

	if (shw_sfp_id(num) < 0) {
		pr_error("shw_sfp_read_header: wrong SFP num %d\n", num + 1);
		return -1;
	}

	ret = shw_sfp_module_scan();
	if (!(ret & (1 << num))) {
		pr_error("shw_sfp_read_header: SFP not present %d\n", num + 1);
		return -2;
	}

	ret =
	    shw_sfp_read(num, I2C_SFP_ADDRESS, 0x0,
			 sizeof(struct shw_sfp_header), (uint8_t *) head);
	if (ret == I2C_DEV_NOT_FOUND) {
		pr_error("shw_sfp_read_header: I2C_DEV_NOT_FOUND\n");
		return -ENODEV;
	}

	return 0;
}

int shw_sfp_read_dom(int num, struct shw_sfp_dom *dom)
{
	int ret;

	if (shw_sfp_id(num) < 0) {
		pr_error("shw_sfp_read_header: wrong SFP num %d\n", num + 1);
		return -1;
	}

	ret = shw_sfp_module_scan();
	if (!(ret & (1 << num))) {
		pr_error("shw_sfp_read_header: SFP not present %d\n", num + 1);
		return -2;
	}

	ret =
	    shw_sfp_read(num, I2C_SFP_DOM_ADDRESS, 0x0,
			 sizeof(struct shw_sfp_dom), (uint8_t *) dom);
	if (ret == I2C_DEV_NOT_FOUND) {
		pr_error("shw_sfp_read_header: I2C_DEV_NOT_FOUND\n");
		return -ENODEV;
	}

	return 0;
}

/* read only values that are updated in real-time */
int shw_sfp_read_dom_rt(int num, struct shw_sfp_dom *dom)
{
	int ret;
	int rt_size;
	size_t offset_temp;

	if (shw_sfp_id(num) < 0) {
		pr_error("shw_sfp_read_header: wrong SFP num %d\n", num + 1);
		return -1;
	}

	ret = shw_sfp_module_scan();
	if (!(ret & (1 << num))) {
		pr_error("shw_sfp_read_header: SFP not present %d\n", num + 1);
		return -2;
	}

	offset_temp = offsetof(struct shw_sfp_dom, temp);
	rt_size = offsetof(struct shw_sfp_dom, alw) - offset_temp;

	ret = shw_sfp_read(num, I2C_SFP_DOM_ADDRESS, offset_temp, rt_size,
			   (uint8_t *) dom + offset_temp);
	if (ret == I2C_DEV_NOT_FOUND) {
		pr_error("shw_sfp_read_header: I2C_DEV_NOT_FOUND\n");
		return -ENODEV;
	}

	return 0;
}


/* Function to update SFP's Diagnostic Monitoring data from SFP's eeprom */
int shw_sfp_update_dom(int num, struct shw_sfp_dom *dom)
{
	/* For now copy entire eeprom */
	return shw_sfp_read_dom(num, dom);
}

/* Update the SFP diagnostics page, only real-time values */
int shw_sfp_update_dom_rt(int num, struct shw_sfp_dom *dom)
{
	/* Copy only fields updated in real-time */
	return shw_sfp_read_dom_rt(num, dom);
}



int shw_sfp_read_verify_header(int num, struct shw_sfp_header *head)
{
	int ret;

	ret = shw_sfp_read_header(num, head);
	if (ret < 0)
		return ret;

	return shw_sfp_header_verify(head);
}

static struct shw_sfp_caldata *shw_sfp_cal_list = NULL;

/* local helper */
static void __err_msg(int index, char *pname, char *pvalue)
{
	if (pvalue)
		pr_error("Config item \"SFP%02i_PARAMS\": parameter \"%s\" "
			 "is wrong (\"%s\")\n", index, pname, pvalue);
	else
		pr_error("Config item \"SFP%02i_PARAMS\": parameter \"%s\" "
			 "is not specified\n", index, pname);
}

int shw_sfp_read_db(void)
{
	struct shw_sfp_caldata *sfp;
	char s[128];
	int error, val, index, nbSfpEntries;
	char *retValue;
	char *keySfpEntries="N_SFP_ENTRIES";

	/* read dot-config values to get the number of defined fibers */
	if( (retValue=libwr_cfg_get(keySfpEntries))==NULL) {
		pr_error("Key \"%s\" is not defined\n",keySfpEntries);
		return -1;
	}
	if (sscanf(retValue, "%i", &nbSfpEntries) != 1) {
		pr_error("Invalid key \"%s\" value (%d)\n",keySfpEntries,*retValue);
		return -1;
	}

	for (index = 0; index<nbSfpEntries; index++) {
		error = libwr_cfg_convert2("SFP%02i_PARAMS", "pn",
					   LIBWR_STRING, s, index);
		if (error)
			return 0; /* no more, no error */

		sfp = calloc(1, sizeof(*sfp));
		strncpy(sfp->part_num, s, sizeof(sfp->part_num));

		error = libwr_cfg_convert2("SFP%02i_PARAMS", "vn",
					   LIBWR_STRING, s, index);
		/* copy vendor name if found */
		if (!error)
			strncpy(sfp->vendor_name, s, sizeof(sfp->vendor_name));

		sfp->vendor_serial[0] = 0;
		error = libwr_cfg_convert2("SFP%02i_PARAMS", "vs",
					   LIBWR_STRING, s, index);
		/* copy serial name if found */
		if (!error)
			strncpy(sfp->vendor_serial, s,
				sizeof(sfp->vendor_serial));

		sfp->flags = SFP_FLAG_CLASS_DATA; /* never used */

		/* These are uint32_t as I write this. So use "int val" */
		val = 0;
		error = libwr_cfg_convert2("SFP%02i_PARAMS", "tx",
					   LIBWR_INT, &val, index);
		if (error)
			__err_msg(index, "tx", NULL);
		sfp->delta_tx_ps = val;
		val = 0;
		error = libwr_cfg_convert2("SFP%02i_PARAMS", "rx",
					   LIBWR_INT, &val, index);
		if (error)
			__err_msg(index, "rx", NULL);
		sfp->delta_rx_ps = val;

		/* We also store the wavelength, used to get alpha */
		error = libwr_cfg_convert2("SFP%02i_PARAMS", "wl_txrx",
					   LIBWR_STRING, &s, index);
		if (error)
			__err_msg(index, "wl_txrx", NULL);
		if (sscanf(s, "%i+%i", &sfp->tx_wl, &sfp->rx_wl) != 2) {
			sfp->tx_wl = 0;
			sfp->rx_wl = 0;
			__err_msg(index, "wl_txrx", s);
		}

		/* Alpha is filled later, per-port, in hal_port_insert_sfp() */

		/* link and continue */
		sfp->next = shw_sfp_cal_list;
		shw_sfp_cal_list = sfp;
	}
	return 0;
}

static inline void removeTrailingSpaces(char *p, int strSize) {
	int i;

	for (i = strSize-1; i >= 0 ; i--) {
		if (p[i] != 0x20)
			break;
		p[i] = 0;
	}
}

struct shw_sfp_caldata *shw_sfp_get_cal_data(int num,
					     struct shw_sfp_header *head)
{
	struct shw_sfp_caldata *t;
	struct shw_sfp_caldata *match_pn_vn = NULL;
	struct shw_sfp_caldata *match_pn = NULL;
	char *vn = (char *)head->vendor_name;
	char *pn = (char *)head->vendor_pn;
	char *vs = (char *)head->vendor_serial;
	int txWaveLength=getSfpTxWaveLength(head);

	/* Replace spaces at the end of strings with 0 needed for
	 * string comparison inside shw_sfp_get_cal_data.
	 * String may contain spaces, standard says only about space padding */
	removeTrailingSpaces(vn,sizeof(head->vendor_name));
	removeTrailingSpaces(pn,sizeof(head->vendor_pn));
	removeTrailingSpaces(vs,sizeof(head->vendor_serial));

	t = shw_sfp_cal_list;
	/* In the first pass, look for serial number */
	while (t) {

		if ( t->tx_wl == txWaveLength ) {
			if (t->vendor_name[0] == 0
				&& strncmp(pn, t->part_num, 16) == 0
				&& t->vendor_serial[0] == 0)
				/* matched pn, but vn and vs not defined */
				match_pn = t;
			else if (strncmp(vn, t->vendor_name, 16) == 0
				&& strncmp(pn, t->part_num, 16) == 0
				&& t->vendor_serial[0] == 0 )
				/* matched vn, pn, but vs not defined */
				match_pn_vn = t;
			else if (strncmp(vn, t->vendor_name, 16) == 0
				&& strncmp(pn, t->part_num, 16) == 0
				&& strncmp(vs, t->vendor_serial, 16) == 0)
				/* matched vn, pn, vs */
				return t;
		}
		t = t->next;
	}
	if (match_pn_vn)
		return match_pn_vn;
	if (match_pn)
		return match_pn;
	return NULL;
}
