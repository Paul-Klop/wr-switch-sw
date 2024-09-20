#include <regs/lpdc_mdio_regs.h>

#define PRIV_IOCGCALIBRATE (SIOCDEVPRIVATE + 1)
#define PRIV_IOCGGETPHASE (SIOCDEVPRIVATE + 2)
#define PRIV_IOCREADREG (SIOCDEVPRIVATE + 3)
#define PRIV_IOCPHYREG (SIOCDEVPRIVATE + 4)
#define PRIV_IOCLPDCREG (SIOCDEVPRIVATE + 5)

#define NIC_READ_PHY_CMD(addr)  (((addr) & 0xff) << 16)
#define NIC_RESULT_DATA(val) ((val) & 0xffff)
#define NIC_WRITE_PHY_CMD(addr, value)  ((((addr) & 0xff) << 16) \
 | (1 << 31) \
 | ((value) & 0xffff))

#define LPDC_MDIO_CTRL_DMTD_SOURCE_TXOUTCLK (1 << LPDC_MDIO_CTRL_DMTD_CLK_SEL_SHIFT)
#define LPDC_MDIO_CTRL_DMTD_SOURCE_RXRECCLK (0 << LPDC_MDIO_CTRL_DMTD_CLK_SEL_SHIFT)

/*
 * Address and mask to discover support for Low Phase Drift
 * Calibration, taken from endpoint-regs.h
 */
#define EP_ECR_FEAT_LPC (1 << 28)
#define EP_ECR_ADDR     0x0
