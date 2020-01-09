/*
 * fan.c
 *
 * Fan speed servo driver. Monitors temperature of I2C onboard sensors and drives the fan accordingly.
 *
 *  Created on: Jul 29, 2012
 *  Authors:
 *  	- Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 * 		- Benoit RAT <benoit|AT|sevensols.com>
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
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <libwr/wrs-msg.h>

#include <libwr/pio.h>
#include <libwr/fan.h>
#include <libwr/hal_shmem.h>
#include <libwr/config.h>

#include "i2c.h"
#include "i2c_io.h"
#include "i2c_fpga_reg.h"
#include "fpga_io.h"
#include <libwr/shw_io.h>
#include "spwm-regs.h"
#include <libwr/util.h>

#define TEMP_SENSOR_ADDR_FPGA	0x4A /* (7bits addr) IC19 Below FPGA */
#define TEMP_SENSOR_ADDR_PLL	0x4C /* (7bits addr) IC18 PLLs */
#define TEMP_SENSOR_ADDR_PSL	0x49 /* (7bits addr) IC20 Power supply left */
#define TEMP_SENSOR_ADDR_PSR	0x4D /* (7bits addr) IC17 Power supply right */

#define DESIRED_TEMPERATURE 55.0

static int fan_hysteresis = 0;
static int fan_hysteresis_t_disable = 0;
static int fan_hysteresis_t_enable = 0;
static int fan_hysteresis_pwm_val = 0;

static i2c_fpga_reg_t fpga_sensors_bus_master = {
	.base_address = FPGA_I2C_ADDRESS,
	.if_num = FPGA_I2C_SENSORS_IFNUM,
	.prescaler = 500,
};

static struct i2c_bus fpga_sensors_i2c = {
	.name = "fpga_sensors",
	.type = I2C_BUS_TYPE_FPGA_REG,
	.type_specific = &fpga_sensors_bus_master,
};

#define PI_FRACBITS 8

/* PI regulator state */
typedef struct {
	float ki, kp;		/* integral and proportional gains (1<<PI_FRACBITS == 1.0f) */
	float integrator;	/* current integrator value */
	float bias;		/* DC offset always added to the output */
	int anti_windup;	/* when non-zero, anti-windup is enabled */
	float y_min;		/* min/max output range, used by clapming and antiwindup algorithms */
	float y_max;
	float x, y;		/* Current input (x) and output value (y) */
} pi_controller_t;
static pi_controller_t fan_pi;

//-----------------------------------------
//-- New FPGA PWM system
static volatile struct SPWM_WB *spwm_wbr;
//----------------------------------------

/* Processes a single sample (x) with Proportional Integrator control algorithm (pi). Returns the value (y) to
	 drive the actuator. */
static inline float pi_update(pi_controller_t * pi, float x)
{
	float i_new, y;
	pi->x = x;
	i_new = pi->integrator + x;

	y = ((i_new * pi->ki + x * pi->kp)) + pi->bias;

	/* clamping (output has to be in <y_min, y_max>) and anti-windup:
	   stop the integrator if the output is already out of range and the output
	   is going further away from y_min/y_max. */
	if (y < pi->y_min) {
		y = pi->y_min;
		if ((pi->anti_windup && (i_new > pi->integrator))
		    || !pi->anti_windup)
			pi->integrator = i_new;
	} else if (y > pi->y_max) {
		y = pi->y_max;
		if ((pi->anti_windup && (i_new < pi->integrator))
		    || !pi->anti_windup)
			pi->integrator = i_new;
	} else			/* No antiwindup/clamping? */
		pi->integrator = i_new;

	pi->y = y;
	return y;
}

/* initializes the PI controller state. Currently almost a stub. */
static inline void pi_init(pi_controller_t * pi)
{
	pi->integrator = 0;
}


/* Configures a PWM output on gpio pin (pin). Rate accepts range from 0 (0%) to 1000 (100%) */
static void pwm_configure_fpga(int enmask, float rate)
{
	uint8_t u8speed = (uint8_t) ((rate >= 1) ? 0xff : (rate * 255.0));

	if ((enmask & 0x1) > 0)
		spwm_wbr->DR0 = u8speed;
	if ((enmask & 0x2) > 0)
		spwm_wbr->DR1 = u8speed;
}

/* Configures a PWM output. Rate accepts range is from 0 (0%) to 1 (100%) */
static void shw_pwm_speed(int enmask, float rate)
{
	//pr_info("%x %f\n",enmask,rate);
	pwm_configure_fpga(enmask, rate);
}

/* Texas Instruments TMP100 temperature sensor driver */
static uint32_t tmp100_read_reg(int dev_addr, uint8_t reg_addr, int n_bytes)
{
	uint8_t data[8];
	uint32_t rv = 0, i;

	data[0] = reg_addr;
	i2c_write(&fpga_sensors_i2c, dev_addr, 1, data);
	i2c_read(&fpga_sensors_i2c, dev_addr, n_bytes, data);

	for (i = 0; i < n_bytes; i++) {
		rv <<= 8;
		rv |= data[i];
	}

	return rv;
}

/* Interpret the readout from registers of TMP100 as temperature
 * According to TMP100, TMP101 datasheet, the temperature is encoded
 * on 12 bits in binary twos completment format, e.g.:
 * temp =  128 Celsius degree for reg_val=b'0111 1111 1111
 * temp =   25 Celsius degree for reg_val=b'0001 1001 0000
 * temp =    0 Celsius degree for reg_val=b'0000 0000 0000
 * temp =  -25 Celsius degree for reg_val=b'1110 0111 0000
 * temp = -128 Celsius degree for reg_val=b'1000 0000 0000
 *
 * We read 2 bytes (16 bits) and the four LSB bits are 0x0. Thus,
 * the value is interpreted as 16 bit twos complement.
 */
static int tmp100_read_temp(int dev_addr)
{
	int temp;
	uint32_t rv = 0x0000FFFF & tmp100_read_reg(dev_addr, 0, 2);

	if(rv & 0x00008000) // negative
		temp = -(int)(0x0000FFFF & (~rv+1));
	else                // positive
		temp =  (int)(0x00007FFF & rv);
	return temp;
}

static void tmp100_write_reg(int dev_addr, uint8_t reg_addr, uint8_t value)
{
	uint8_t data[2];

	data[0] = reg_addr;
	data[1] = value;
	i2c_write(&fpga_sensors_i2c, dev_addr, 2, data);
}

static int shw_init_i2c_sensors(void)
{
	if (i2c_init_bus(&fpga_sensors_i2c) < 0) {
		pr_error("Can't initialize temperature sensors I2C bus.\n");
		return -1;
	}
	return 0;
}

int shw_init_fans(void)
{
	uint32_t val = 0;
	char *config_item;

	pr_info(
	      "Configuring FPGA PWMs for fans (desired temperature = %.1f "
	      "degC)\n", DESIRED_TEMPERATURE);

	//Point to the corresponding WB direction
	spwm_wbr =
	    (volatile struct SPWM_WB *)(FPGA_BASE_ADDR +
					FPGA_BASE_SPWM);

	//Configure SPWM register the 30~=(62.5MHz÷(8kHz×2^8))−1
	val = SPWM_CR_PRESC_W(30) | SPWM_CR_PERIOD_W(255);
	spwm_wbr->CR = val;

	fan_pi.ki = 1.0;
	fan_pi.kp = 4.0;
	fan_pi.y_min = 400;
	fan_pi.bias = 200;
	fan_pi.y_max = 1000;

	shw_init_i2c_sensors();

	/* set all to 12-bit resolution */
	tmp100_write_reg(TEMP_SENSOR_ADDR_FPGA, 1, 0x60);
	tmp100_write_reg(TEMP_SENSOR_ADDR_PLL, 1, 0x60);
	tmp100_write_reg(TEMP_SENSOR_ADDR_PSL, 1, 0x60);
	tmp100_write_reg(TEMP_SENSOR_ADDR_PSR, 1, 0x60);

	pi_init(&fan_pi);

	/* check wether config fields exist, atoi has to have valid string */
	config_item = libwr_cfg_get("FAN_HYSTERESIS");
	if ((config_item) && !strcmp(config_item, "y")) {
		fan_hysteresis = 1;
		pr_info("Enabling fan hysteresis\n");
		config_item = libwr_cfg_get("FAN_HYSTERESIS_T_ENABLE");
		if (config_item) {
			fan_hysteresis_t_enable = atoi(config_item);
			/* don't allow fan_hysteresis_t_enable to be higher
			 * than 80 deg */
			if (fan_hysteresis_t_enable >= 80)
				fan_hysteresis_t_enable = 80;
		}

		config_item = libwr_cfg_get("FAN_HYSTERESIS_T_DISABLE");
		if (config_item) {
			fan_hysteresis_t_disable = atoi(config_item);
		}

		config_item = libwr_cfg_get("FAN_HYSTERESIS_PWM_VAL");
		if (config_item) {
			fan_hysteresis_pwm_val = atoi(config_item);
		}
		if (fan_hysteresis_pwm_val < 4) {
			/* set minimum pwm value to 4 */
			fan_hysteresis_pwm_val = 4;
		}

		pr_info("Setting upper temperature threshold to %d for fan "
			"hysteresis\n", fan_hysteresis_t_enable);
		pr_info("Setting lower temperature threshold to %d for fan "
			"hysteresis\n", fan_hysteresis_t_disable);
		pr_info("Setting pwm value to %d for fan hysteresis\n",
			fan_hysteresis_pwm_val);
	}

	return 0;
}

/*
 * Reads out the temperature and drives the fan accordingly
 * note: This call is done by hal_main.c:hal_update()
 */
void shw_update_fans(struct hal_temp_sensors *sensors)
{
	float t_cur;
	float drive;
	static float pwm_set_old = -1.0; /* anything different than 0, to force
					    write at the beginning */
	static float pwm_set_new = 0;
	//pr_info("t=%f,pwm=%f\n",t_cur , drive);

	/* update sensor values */
	sensors->fpga = tmp100_read_temp(TEMP_SENSOR_ADDR_FPGA);
	sensors->pll  = tmp100_read_temp(TEMP_SENSOR_ADDR_PLL);
	sensors->psl  = tmp100_read_temp(TEMP_SENSOR_ADDR_PSL);
	sensors->psr  = tmp100_read_temp(TEMP_SENSOR_ADDR_PSR);

	/* drive fan based on PLL temperature */
	t_cur = ((float)(sensors->pll >> 4)) / 16.0;
	drive = pi_update(&fan_pi, t_cur - DESIRED_TEMPERATURE);


	if (fan_hysteresis) {
		if (t_cur < fan_hysteresis_t_disable) {
			/* disable fans */
			pwm_set_new = 0;
			}
		if (t_cur > fan_hysteresis_t_enable) {
			/* enable fans with given value */
			pwm_set_new = ((float) fan_hysteresis_pwm_val) / 1000;
			}
		/* pwm_set_new might be not updated. e.g. t_cur is between
		 * fan_hysteresis_t_disable and fan_hysteresis_t_enable */
	} else {
		/* use PI controller for FANs speeds */
		pwm_set_new = drive / 1000;
	}

	/* Update PWM, only when there was a change */
	if (pwm_set_new != pwm_set_old) {
		shw_pwm_speed(0xFF, pwm_set_new);
		pwm_set_old = pwm_set_new;
	}
}
