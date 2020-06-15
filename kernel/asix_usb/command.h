/*
 *********************************************************************************
 *     Copyright (c) 2005 ASIX Electronic Corporation      All rights reserved.
 *
 *     This is unpublished proprietary source code of ASIX Electronic Corporation
 *
 *     The copyright notice above does not evidence any actual or intended
 *     publication of such source code.
 *********************************************************************************
 */
 
#ifndef __COMMAND_H__
#define __COMMAND_H__

/*

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/mii.h>

#include <linux/in.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>
*/
/* NAMING CONSTANT DECLARATIONS */
#define AX88772B_SIGNATURE	"AX88772B"
#define AX88772B_DRV_NAME	"AX88772B"

/* ioctl Command Definition */
#define AX_PRIVATE		SIOCDEVPRIVATE

/* private Command Definition */
#define AX_SIGNATURE			0
#define AX_READ_EEPROM			1
#define AX_WRITE_EEPROM			2

typedef struct _AX_IOCTL_COMMAND {
	unsigned short	ioctl_cmd;
	unsigned char	sig[16];
	unsigned short *buf;
	unsigned short size;
	unsigned char delay;
}AX_IOCTL_COMMAND;

#endif /* end of command.h */
