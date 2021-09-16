/*
	i2c_bitbang.h
	2012 CERN B.Bielawski
*/

#ifndef I2C_CPU_BB_H
#define I2C_CPU_BB_H

#include <libwr/pio.h>
#include "i2c.h"

struct i2c_bitbang {
	pio_pin_t *scl;
	pio_pin_t *sda;
	int udelay;
	int timeout;
};

int i2c_bitbang_init_bus(struct i2c_bus *bus);
void i2c_slave_soft_reset(struct i2c_bus *i2c_bus, int port);

#endif //I2C_CPU_BB_H
