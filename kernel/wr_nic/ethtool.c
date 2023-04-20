/*
 * Ethtool operations for White-Rabbit switch network interface
 *
 * Copyright (C) 2010-2023 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 * Author: Adam Wujek
 * Partly from previous work by Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 * Partly from previous work by  Emilio G. Cota <cota@braap.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/spinlock.h>

#include "wr-nic.h"

/* Define flag in generic MII register.
 * In linux/mii.h this bit is defined under reserved/unused (BMCR_RESV) */
#define BMCR_UNI_EN 0x0020
#define FRAME_SIZE_JUMBO 9216
#define FRAME_SIZE_STANDARD 0x800

static int wrn_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct wrn_ep *ep = netdev_priv(dev);
	int ret;

	spin_lock_irq(&ep->lock);
	ret = mii_ethtool_gset(&ep->mii, cmd);
	spin_unlock_irq(&ep->lock);

	cmd->supported=
		SUPPORTED_FIBRE | /* FIXME: copper sfp? */
		SUPPORTED_Autoneg |
		SUPPORTED_1000baseKX_Full;
	cmd->advertising =
		ADVERTISED_1000baseKX_Full |
		ADVERTISED_Autoneg;
	cmd->port = PORT_FIBRE;
	cmd->speed = SPEED_1000;
	cmd->duplex = DUPLEX_FULL;
	return ret;
}

static int wrn_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct wrn_ep *ep = netdev_priv(dev);
	u32 bmcr;
	u32 tmp;

	spin_lock_irq(&ep->lock);
	bmcr = wrn_phy_read(dev, 0, MII_BMCR);
	if (cmd->autoneg == AUTONEG_ENABLE) {
		bmcr |= (BMCR_ANENABLE | BMCR_ANRESTART);
		ep->mii.force_media = 0;
	} else {
		bmcr &= ~BMCR_ANENABLE;
		ep->mii.force_media = 1;
	}
	wrn_phy_write(dev, 0, MII_BMCR, bmcr);
	tmp = wrn_phy_read(dev, 0, MII_BMCR);
	spin_unlock_irq(&ep->lock);

	/* Ignore BMCR_ANRESTART when checking write */
	if (tmp != (bmcr & ~BMCR_ANRESTART)) {
		printk(KERN_ERR KBUILD_MODNAME ": %s: Unable to set BMCR "
		       "register for port wri%d. Wrote 0x%08x, read 0x%08x\n",
		       __func__, ep->ep_number + 1, bmcr, tmp);
		return -EIO;
	}

	return 0;
}

static int wrn_nwayreset(struct net_device *dev)
{
	struct wrn_ep *ep = netdev_priv(dev);
	int ret;

	spin_lock_irq(&ep->lock);
	ret = mii_nway_restart(&ep->mii);
	spin_unlock_irq(&ep->lock);

	return ret;
}

static void wrn_get_drvinfo(struct net_device *dev,
			      struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, KBUILD_MODNAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, dev_name(dev->dev.parent),
		sizeof(info->bus_info));
}


enum {
	WRN_PRI_FLAG_UNI_EN,
	WRN_PRI_FLAG_ACCEPT_RX_JUMBO,
	WRN_PRI_FLAG_LEN,
};

static const char wrn_private_arr[WRN_PRI_FLAG_LEN][ETH_GSTRING_LEN] = {
	/* "Enable transmit regardless of whether a valid link has been
	 * established" */
	"Unidirectional Enable",
	"Accept RX Jumbo Frames",
};

static int wrn_get_sset_count(struct net_device *dev, int stringset)
{
	switch (stringset) {
	case ETH_SS_PRIV_FLAGS:
		return WRN_PRI_FLAG_LEN;

	default:
		return -EINVAL;
	}
}


static u32 wrn_get_private_flags(struct net_device *dev)
{
	struct wrn_ep *ep = netdev_priv(dev);
	u32 flags = 0;
	u32 bmcr;
	u32 rfcr;

	spin_lock_irq(&ep->lock);
	bmcr = wrn_phy_read(dev, 0, MII_BMCR);
	rfcr = wrn_ep_read(ep, RFCR);
	spin_unlock_irq(&ep->lock);

	flags |= (!!(bmcr & BMCR_UNI_EN)) << WRN_PRI_FLAG_UNI_EN;
	flags |= (!!(rfcr & EP_RFCR_A_GIANT)) << WRN_PRI_FLAG_ACCEPT_RX_JUMBO;

	return flags;
}


static int wrn_set_private_flags(struct net_device *dev, u32 flags)
{
	struct wrn_ep *ep = netdev_priv(dev);
	u32 bmcr;
	u32 rfcr;
	u32 tmp;

	spin_lock_irq(&ep->lock);
	bmcr = wrn_phy_read(dev, 0, MII_BMCR);
	bmcr &= ~BMCR_UNI_EN;
	bmcr |= (flags & (1 << WRN_PRI_FLAG_UNI_EN)) ? BMCR_UNI_EN : 0;
	wrn_phy_write(dev, 0, MII_BMCR, bmcr);
	tmp = wrn_phy_read(dev, 0, MII_BMCR);
	spin_unlock_irq(&ep->lock);
	if (tmp != bmcr) {
		printk(KERN_ERR KBUILD_MODNAME ": %s: Unable to set BMCR "
		       "register for port wri%d. Wrote 0x%08x, read 0x%08x\n",
		       __func__, ep->ep_number + 1, bmcr, tmp);
		return -EIO;
	}

	spin_lock_irq(&ep->lock);
	rfcr = wrn_ep_read(ep, RFCR);
	rfcr &= ~(EP_RFCR_A_GIANT | EP_RFCR_MRU_MASK);
	if (flags & (1 << WRN_PRI_FLAG_ACCEPT_RX_JUMBO)) {
		rfcr |= EP_RFCR_A_GIANT;
		rfcr |= FRAME_SIZE_JUMBO << EP_RFCR_MRU_SHIFT;
	} else {
		rfcr |= FRAME_SIZE_STANDARD << EP_RFCR_MRU_SHIFT;
	}
	wrn_ep_write(ep, RFCR, rfcr);
	tmp = wrn_ep_read(ep, RFCR);
	spin_unlock_irq(&ep->lock);
	if (tmp != rfcr) {
		printk(KERN_ERR KBUILD_MODNAME ": %s: Unable to set RFCR "
		       "register for port wri%d. Wrote 0x%08x, read 0x%08x\n",
		       __func__, ep->ep_number + 1, rfcr, tmp);
		return -EIO;
	}

	return 0;
}

static void wrn_get_strings(struct net_device *dev, u32 stringset, u8 *buf)
{
	switch (stringset) {
	case ETH_SS_PRIV_FLAGS:
		memcpy(buf, wrn_private_arr,
		       ETH_GSTRING_LEN * WRN_PRI_FLAG_LEN);
		break;
	}
}

/*
 * These are the operations we support. No coalescing is there since
 * most of the traffic will just happen within the FPGA switching core.
 * Similarly, other funcionality like ringparam are not used.
 * get_eeprom/set_eeprom may be useful for a simple MAC address management.
 */
static const struct ethtool_ops wrn_ethtool_ops = {
	.get_settings	= wrn_get_settings,
	.set_settings	= wrn_set_settings,
	.get_drvinfo	= wrn_get_drvinfo,
	.nway_reset	= wrn_nwayreset,
	.get_sset_count	= wrn_get_sset_count,
	.get_priv_flags	= wrn_get_private_flags,
	.set_priv_flags	= wrn_set_private_flags,
	.get_strings	= wrn_get_strings,
	/* Some of the default methods apply for us */
	.get_link	= ethtool_op_get_link,
	/* FIXME: get_regs_len and get_regs may be useful for debugging */
};

int wrn_ethtool_init(struct net_device *netdev)
{
	netdev->ethtool_ops = &wrn_ethtool_ops;
	return 0;
}
