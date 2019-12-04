
#define PRIV_IOCGCALIBRATE (SIOCDEVPRIVATE + 1)
#define PRIV_IOCGGETPHASE (SIOCDEVPRIVATE + 2)
#define PRIV_IOCREADREG (SIOCDEVPRIVATE + 3)
#define PRIV_IOCPHYREG (SIOCDEVPRIVATE + 4)

#define NIC_READ_PHY_CMD(addr)  (((addr) & 0xff) << 16)
#define NIC_RESULT_DATA(val) ((val) & 0xffff)
#define NIC_WRITE_PHY_CMD(addr, value)  ((((addr) & 0xff) << 16) \
 | (1 << 31) \
 | ((value) & 0xffff))

/*
 * MDIO registers used in Low Phase Drift Calibration
 * See kernel/wbgen-regs/endpoint-mdio.h
 */

// address of status and control registers
#define MDIO_LPC_STAT 18
#define MDIO_LPC_CTRL 19

// flags for status and control registers
#define MDIO_LPC_STAT_RESET_TX_DONE (1 << 0)
#define MDIO_LPC_STAT_LINK_UP       (1 << 1)
#define MDIO_LPC_STAT_LINK_ALIGNED  (1 << 2)
#define MDIO_LPC_STAT_RESET_RX_DONE (1 << 3)

#define MDIO_LPC_CTRL_RESET_TX      (1 << 0)
#define MDIO_LPC_CTRL_TX_ENABLE     (1 << 1)
#define MDIO_LPC_CTRL_RX_ENABLE     (1 << 2)
#define MDIO_LPC_CTRL_RESET_RX      (1 << 3)

#define MDIO_LPC_CTRL_DMTD_SOURCE_TXOUTCLK (1 << 14)
#define MDIO_LPC_CTRL_DMTD_SOURCE_RXRECCLK (0 << 14)
#define MDIO_LPC_STAT_DBG_DATA      (1 << 4)

#define MDIO_LPC_CTRL_DBG_SHIFT_EN  (1 << 8)
#define MDIO_LPC_CTRL_DBG_TRIG      (1 << 10)
/*
 * Address and mask to discover support for Low Phase Drift
 * Calibration, taken from endpoint-regs.h
 */
#define EP_ECR_FEAT_LPC (1 << 28)
#define EP_ECR_ADDR     0x0
