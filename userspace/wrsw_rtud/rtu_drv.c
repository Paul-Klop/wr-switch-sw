/*
 * White Rabbit RTU (Routing Table Unit)
 * Copyright (C) 2010, CERN.
 *
 * Version:     wrsw_rtud v1.0
 *
 * Authors:     Juan Luis Manas (juan.manas@integrasys.es)
 *              Miguel Baizan   (miguel.baizan@integrasys.es)
 *              Maciej Lipinski (maciej.lipinski@cern.ch)
 *
 * Description: RTU driver module in user space. Provides read/write access
 *              to RTU_at_HW  components including:
 *              - UFIFO
 *              - MFIFO
 *              - Aging RAM for Main Hashtable
 *              - VLAN Table
 *              - RTU Global Control Register
 *              - RTU Port settings
 *
 * Fixes:
 *              Alessandro Rubini
 *              Tomasz Wlostowski
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include <libwr/switch_hw.h>
#include <libwr/shmem.h>
#include <libwr/hal_shmem.h>
#include <libwr/rtu_shmem.h>
#include <libwr/wrs-msg.h>

#include <fpga_io.h>
#include <regs/rtu-regs.h>
#include <regs/endpoint-regs.h>

#include "rtu_drv.h"
#include "wr_rtu.h"


static void write_mfifo_addr(uint32_t zbt_addr);
static void write_mfifo_data(uint32_t word);

static uint32_t mac_entry_word0_w(struct rtu_filtering_entry *ent);
static uint32_t mac_entry_word1_w(struct rtu_filtering_entry *ent);
static uint32_t mac_entry_word2_w(struct rtu_filtering_entry *ent);
static uint32_t mac_entry_word3_w(struct rtu_filtering_entry *ent);
static uint32_t mac_entry_word4_w(struct rtu_filtering_entry *ent);

static char *qmode_names[] =   {"ACCESS     ",
				"TRUNK      ",
				"disabled   ",
				"unqualified"};
static char *yes_no[] ={"no  ",
			"yes "};

/*
 * Used to communicate to RTU UFIFO IRQ handler device at kernel space
 */
static int fd;

#define HAL_SHMEM_TOTAL_US (20000000) // 20sec
#define HAL_SHMEM_SLEEP_US (250000) // 250ms
#define HAL_SHMEM_RETRIES  (HAL_SHMEM_TOTAL_US/HAL_SHMEM_SLEEP_US)

void init_shm(void)
{
	struct hal_shmem_header *hal_shem_header;
	int n_wait = 0;
	int ret;

	/* wait for HAL */
	while ((ret = wrs_shm_get_and_check(wrs_shm_hal, &hal_head)) != 0) {
		n_wait++;
		if (n_wait > HAL_SHMEM_RETRIES) {
			/* print if waiting more than 10 seconds, some wait
			 * is expected since hal requires few seconds to start
			 */
			if (ret == WRS_SHM_OPEN_FAILED) {
				pr_error("Unable to open HAL's shared memory!\n");
			}
			if (ret == WRS_SHM_WRONG_VERSION) {
				pr_error("Unable to read HAL's shared memory version!\n");
			}
			exit(1);
		}
		usleep(HAL_SHMEM_SLEEP_US);
	}

	hal_shem_header = (void *)hal_head + hal_head->data_off;

	/* check hal's shm version */
	if (hal_head->version != HAL_SHMEM_VERSION) {
		pr_error("Unexpected HAL's shared memory version %i (expected is %i)\n",
			hal_head->version, HAL_SHMEM_VERSION);
		exit(-1);
	}

	// Wait end of HAL initialization to produce consistent nports
	n_wait=0;
	while (1) {
		if ( hal_shem_header->shmemState==HAL_SHMEM_STATE_INITITALIZED )
			break;
		if (n_wait > HAL_SHMEM_RETRIES) {
			pr_error("HAL initialization waiting failure\n");
			exit(1);
		}
		n_wait++;
		usleep(HAL_SHMEM_SLEEP_US);
	}

	/* Even after HAL restart, HAL will place structures at the same
	 * addresses. No need to re-dereference pointer at each read. */
	hal_nports_local = hal_shem_header->nports;
	hal_ports = wrs_shm_follow(hal_head, hal_shem_header->ports);
	if (hal_nports_local > HAL_MAX_PORTS) {
		pr_error("Too many ports reported by HAL. %d vs %d supported\n",
			hal_nports_local, HAL_MAX_PORTS);
		exit(-1);
	}
	pr_info("Connected to HAL's share memory.\n");
}

/**
 * \brief Initialize HW RTU memory map
 */

int rtu_init(void)
{
	int err;

	init_shm();
	// Used to 'get' RTU IRQs from kernel
	fd = open(RTU_DEVNAME, O_RDWR);
	if (fd < 0) {
		pr_error("Can't open %s: is the RTU kernel driver loaded?\n",
		      RTU_DEVNAME);
		return errno;
	}
	// init IO memory map
	err = shw_fpga_mmap_init();
	if (err)
		return err;

	pr_debug("module initialized\n");

	return 0;
}

void rtu_exit(void)
{
	if (fd >= 0)
		close(fd);

	pr_debug("module cleanup\n");
}

#define rtu_rd(reg) \
	 _fpga_readl(FPGA_BASE_RTU + offsetof(struct RTU_WB, reg))

#define rtu_wr(reg, val) \
	 _fpga_writel(FPGA_BASE_RTU + offsetof(struct RTU_WB, reg), val)

static inline void write_pcr(int port, uint32_t pcr)
{
	rtu_wr(PSR, RTU_PSR_PORT_SEL_W(port));
	rtu_wr(PCR, pcr);
}

static inline uint32_t read_pcr(int port)
{
	rtu_wr(PSR, RTU_PSR_PORT_SEL_W(port));
	return rtu_rd(PCR);
}

// UFIFO

/**
 * \brief Returns the UFIFO empty flag.
 * @return Value of UFIFO empty flag.
 */
int rtu_ufifo_is_empty(void)
{
	uint32_t csr = rtu_rd(UFIFO_CSR);
	return RTU_UFIFO_CSR_EMPTY & csr;
}

/**
 * \brief Returns the current learning queue length.
 * @return Number of unrecognised requests currently in the learning queue.
 */
int rtu_read_learning_queue_cnt(void)
{
	// Get counter from UFIFO Control-Status Register
	// Fixme: USEDW returns 0 (FIFO overflow?)
	uint32_t csr = rtu_rd(UFIFO_CSR);
	return RTU_UFIFO_CSR_USEDW_R(csr);
}

/**
 * \brief Reads unrecognised request from UFIFO.
 * Blocks on read if learning queue is empty.
 * @param req pointer to unrecognised request data. Memory handled by callee.
 * @return error code
 */

static int irq_disabled = 1;

int rtu_read_learning_queue(struct rtu_request *req)
{
	int err;
	int errno_local;

	if (irq_disabled) {
		ioctl(fd, WR_RTU_IRQENA);
		irq_disabled = 0;
	}
	// If learning queue is empty, wait for UFIFO IRQ
	if (rtu_ufifo_is_empty()) {
		err = ioctl(fd, WR_RTU_IRQWAIT);
		errno_local = errno;
		/* Check if ioctl was interrupted by a signal. Please note that
		 * the driver's function returns -ERESTARTSYS, but userspace
		 * gets -1 (and errno == EINTR) from the ioctl call. */
		if (err == -1 && errno_local == EINTR)
			return 1;

		/* IRQ disabled, driver/ioctl sets errno to EAGAIN */
		if (err == -1 && errno_local == EAGAIN)
			return 2;

		/* Other error */
		if (err) {
			pr_error("%s: error %d errno %s (%d)\n", __func__, err, strerror(errno_local), errno_local);
			return err;
		}
	}

	// read data from mapped IO memory
	uint32_t r0 = rtu_rd(UFIFO_R0);
	uint32_t r1 = rtu_rd(UFIFO_R1);
	uint32_t r2 = rtu_rd(UFIFO_R2);
	uint32_t r3 = rtu_rd(UFIFO_R3);
	uint32_t r4 = rtu_rd(UFIFO_R4);

	// Once read: if learning queue becomes empty again, enable UFIFO IRQ

	// unmarshall data and populate request
	uint32_t dmac_lo = RTU_UFIFO_R0_DMAC_LO_R(r0);
	uint32_t dmac_hi = RTU_UFIFO_R1_DMAC_HI_R(r1);
	uint32_t smac_lo = RTU_UFIFO_R2_SMAC_LO_R(r2);
	uint32_t smac_hi = RTU_UFIFO_R3_SMAC_HI_R(r3);

	req->port_id = RTU_UFIFO_R4_PID_R(r4);
	req->has_prio = RTU_UFIFO_R4_HAS_PRIO & r4;
	req->prio = RTU_UFIFO_R4_PRIO_R(r4);
	req->has_vid = RTU_UFIFO_R4_HAS_VID & r4;
	req->vid = RTU_UFIFO_R4_VID_R(r4);
	// destination mac
	req->dst[5] = 0xFF & dmac_lo;
	req->dst[4] = 0xFF & (dmac_lo >> 8);
	req->dst[3] = 0xFF & (dmac_lo >> 16);
	req->dst[2] = 0xFF & (dmac_lo >> 24);
	req->dst[1] = 0xFF & dmac_hi;
	req->dst[0] = 0xFF & (dmac_hi >> 8);
	// source mac
	req->src[5] = 0xFF & smac_lo;
	req->src[4] = 0xFF & (smac_lo >> 8);
	req->src[3] = 0xFF & (smac_lo >> 16);
	req->src[2] = 0xFF & (smac_lo >> 24);
	req->src[1] = 0xFF & smac_hi;
	req->src[0] = 0xFF & (smac_hi >> 8);

	ioctl(fd, WR_RTU_IRQENA);

	return 0;
}

// MFIFO -> HTAB

/**
 * \brief Returns the current main hashtable CPU access FIFO (MFIFO) length.
 * @return Number of MAC entries currently in the MFIFO.
 */
int rtu_read_mfifo_cnt(void)
{
	// Get counter from MFIFO Control-Status Register
	uint32_t csr = rtu_rd(MFIFO_CSR);
	return RTU_MFIFO_CSR_USEDW_R(csr);
}

/**
 * \brief Checks whether the main hashtable CPU access FIFO (MFIFO) is full.
 * @return 1 if MFIFO is full. 0 otherwise.
 */
int rtu_mfifo_is_full(void)
{
	uint32_t csr = rtu_rd(MFIFO_CSR);
	return RTU_MFIFO_CSR_FULL & csr;
}

/**
 * \brief Checks whether the main hashtable CPU access FIFO (MFIFO) is empty.
 * @return 1 if MFIFO is empty. 0 otherwise.
 */
int rtu_mfifo_is_empty(void)
{
	uint32_t csr = rtu_rd(MFIFO_CSR);
	return RTU_MFIFO_CSR_EMPTY & csr;
}

static void flush_mfifo(void)
{
	uint32_t gcr = rtu_rd(GCR);
	rtu_wr(GCR, gcr | RTU_GCR_MFIFOTRIG);

	while (!rtu_rd(GCR) & RTU_GCR_MFIFOTRIG) ;	/* wait while the RTU is busy flushing the MFIFO */
}

/**
 * \brief Writes one MAC entry into main hash table at the given address.
 * @param ent MAC table entry to be written to MFIFO.
 * @param zbt_addr ZBT SRAM memory address in which MAC entry shoud be added.
 */
void rtu_write_htab_entry(uint16_t zbt_addr, struct rtu_filtering_entry *ent,
			  int flush)
{
	write_mfifo_addr(zbt_addr);
	write_mfifo_data(mac_entry_word0_w(ent));
	write_mfifo_data(mac_entry_word1_w(ent));
	write_mfifo_data(mac_entry_word2_w(ent));
	write_mfifo_data(mac_entry_word3_w(ent));
	write_mfifo_data(mac_entry_word4_w(ent));

	if (flush)
		flush_mfifo();

	pr_debug("write htab entry [with flush]: "
		 "addr %x ent %08x %08x %08x %08x %08x\n",
		  zbt_addr,
		  mac_entry_word0_w(ent),
		  mac_entry_word1_w(ent),
		  mac_entry_word2_w(ent),
		  mac_entry_word3_w(ent), mac_entry_word4_w(ent)
	    );
}

/**
 * \brief Cleans main hash table.
 * Cleans all entries in HTAB inactive bank.
 */
void rtu_clean_htab(void)
{
	int addr;
	for (addr = 0; addr < RTU_ENTRIES; addr++) {
		write_mfifo_addr(addr * 8);
		write_mfifo_data(0x00000000);
		write_mfifo_data(0x00000000);
		write_mfifo_data(0x00000000);
		write_mfifo_data(0x00000000);
		write_mfifo_data(0x00000000);
		flush_mfifo();
	}
}

// AGING RAM - HTAB

/**
 * \brief Read word from aging HTAB.
 * Aging RAM Size: 256 32-bit words
 */

void rtu_read_aging_bitmap(uint32_t * bitmap)
{
	int i;
	for (i = 0; i < RTU_ENTRIES / 32; i++) {
		bitmap[i] = _fpga_readl(FPGA_BASE_RTU + RTU_ARAM_BASE + 4 * i);
		_fpga_writel(FPGA_BASE_RTU + RTU_ARAM_BASE + 4 * i, 0);
	}
}

// VLAN TABLE

/**
 * \brief Writes entry to vlan table.
 * VLAN table size: 4096 32-bit words.
 * @param addr entry memory address
 */
void rtu_write_vlan_entry(int vid, struct rtu_vlan_table_entry *ent)
{
	uint32_t vtr1 = 0, vtr2 = 0;

	vtr2 = ent->port_mask;
	vtr1 = RTU_VTR1_UPDATE | RTU_VTR1_VID_W(vid)
	    | (ent->drop ? RTU_VTR1_DROP : 0)
	    | (ent->prio_override ? RTU_VTR1_PRIO_OVERRIDE : 0)
	    | (ent->has_prio ? RTU_VTR1_HAS_PRIO : 0)
	    | RTU_VTR1_PRIO_W(ent->prio)
	    | RTU_VTR1_FID_W(ent->fid);

	rtu_wr(VTR2, vtr2);
	rtu_wr(VTR1, vtr1);

	if (ent->drop > 0 && ent->port_mask == 0x0) {
		pr_info("Removing VLAN: vid %d (fid %d)\n", vid, ent->fid);
	} else {
		pr_info("Adding VLAN: vid %d (fid %d) port_mask 0x%x\n",
		      vid, ent->fid, ent->port_mask);
	}

}

/**
 * \brief Cleans VLAN entry in VLAN table
 * @param addr memory address which shoud be cleaned.
 */
void rtu_clean_vlan_entry(int vid)
{
	uint32_t vtr1 = 0, vtr2 = 0;

	vtr2 = 0;
	vtr1 = RTU_VTR1_UPDATE | RTU_VTR1_VID_W(vid);

	rtu_wr(VTR2, vtr2);
	rtu_wr(VTR1, vtr1);
}

/**
 * \brief Cleans VLAN table (drop bit is set to 1)
 */
void rtu_clean_vlan(void)
{
	int addr;
	for (addr = 0; addr < NUM_VLANS; addr++)
		rtu_clean_vlan_entry(addr);
}

// RTU Global Control Register

/**
 * \brief Enables RTU operation.
 */
void rtu_enable(void)
{
	uint32_t gcr = rtu_rd(GCR);
	rtu_wr(GCR, gcr | RTU_GCR_G_ENA);
	pr_debug("updated gcr (enable): %x\n", gcr);
}

/**
 * \brief Disables RTU operation.
 */
void rtu_disable(void)
{
	uint32_t gcr = rtu_rd(GCR);
	rtu_wr(GCR, gcr & (~RTU_GCR_G_ENA));
	pr_debug("updated gcr (disable): %x\n", gcr);
}

/**
 * \brief Gets the polynomial used for hash computation in RTU at HW.
 * @return hash_poly hex representation of polynomial
 */
uint16_t rtu_read_hash_poly(void)
{
	uint32_t gcr = rtu_rd(GCR);
	return RTU_GCR_POLY_VAL_R(gcr);
}

/**
 * \brief Sets the polynomial used for hash computation in RTU at HW.
 * @param hash_poly hex representation of polynomial
 */
void rtu_write_hash_poly(uint16_t hash_poly)
{
	// Get current GCR
	uint32_t gcr = rtu_rd(GCR);
	// Clear previous hash poly and insert the new one
	gcr = (gcr & (~RTU_GCR_POLY_VAL_MASK)) | RTU_GCR_POLY_VAL_W(hash_poly);
	// Update GCR
	rtu_wr(GCR, gcr);
	pr_debug("updated gcr (poly): %x\n", gcr);
}

// PORT SETTINGS

/**
 * \brief Sets fixed priority of value 'prio' on indicated port.
 * It overrides the priority coming form the endpoint.
 * @param port port number (0 to 9)
 * @param prio priority value
 * @return error code.
 */
int rtu_set_fixed_prio_on_port(int port, uint8_t prio)
{
	if ((port < MIN_PORT) || (port > MAX_PORT))
		return -EINVAL;

	uint32_t pcr = read_pcr(port);
	write_pcr(port, pcr | RTU_PCR_FIX_PRIO | RTU_PCR_PRIO_VAL_W(prio));
	return 0;
}

/**
 * \brief Unsets fixed priority on indicated port.
 * Orders to use priority from the endpoint instead.
 * @param port port number (0 to 9)
 * @return error code.
 */
int rtu_unset_fixed_prio_on_port(int port)
{
	if ((port < MIN_PORT) || (port > MAX_PORT))
		return -EINVAL;

	uint32_t pcr = read_pcr(port);
	write_pcr(port,
		  pcr & (RTU_PCR_LEARN_EN | RTU_PCR_PASS_ALL | RTU_PCR_PASS_BPDU
			 | RTU_PCR_B_UNREC));

	return 0;
}

/**
 * \brief Sets the LEARN_EN flag on indicated port.
 * @param port port number (0 to 9)
 * @param flag 0 disables learning. Otherwise: enables learning porcess on this port.
 * @return error code.
 */
int rtu_learn_enable_on_port(int port, int flag)
{
	if ((port < MIN_PORT) || (port > MAX_PORT))
		return -EINVAL;

	uint32_t pcr = read_pcr(port);
	pcr = flag ? RTU_PCR_LEARN_EN | pcr : (~RTU_PCR_LEARN_EN) & pcr;

	write_pcr(port, pcr);
	return 0;
}

/**
 * \brief Gets the LEARN_EN flag on indicated port.
 * @param port port number (0 to 9)
 * @return flag status
 */
int rtu_learn_read_on_port(int port)
{
	if ((port < MIN_PORT) || (port > MAX_PORT))
		return -EINVAL;

	return read_pcr(port) & RTU_PCR_LEARN_EN ? 1 : 0;
}

/**
 * \brief Sets the PASS_BPDU flag on indicated port.
 * @param port port number (0 to 9)
 * @param flag 0: BPDU packets are passed RTU rules only if PASS_ALL is set.
 * Otherwise: BPDU packets are passed according to RTU rules.
 * @return error code.
 */
int rtu_pass_bpdu_on_port(int port, int flag)
{
	if ((port < MIN_PORT) || (port > MAX_PORT))
		return -EINVAL;

	uint32_t pcr = read_pcr(port);
	pcr = flag ? RTU_PCR_PASS_BPDU | pcr : (~RTU_PCR_PASS_BPDU) & pcr;
	write_pcr(port, pcr);
	return 0;
}

/**
 * \brief Sets the PASS_ALL flag on indicated port.
 * @param port port number (0 to 9)
 * @param flag 0: all packets are dropped. Otherwise: all packets are passed.
 * @return error code.
 */
int rtu_pass_all_on_port(int port, int flag)
{
	if ((port < MIN_PORT) || (port > MAX_PORT))
		return -EINVAL;

	uint32_t pcr = read_pcr(port);

	pcr = flag ? RTU_PCR_PASS_ALL | pcr : (~RTU_PCR_PASS_ALL) & pcr;
	write_pcr(port, pcr);
	return 0;
}

/**
 * \brief Sets the B_UNREC flag on indicated port.
 * @param port port number (0 to 9)
 * @param flag 0: packet is dropped. Otherwise: packet is broadcast.
 * @return error code.
 */
int rtu_set_unrecognised_behaviour_on_port(int port, int flag)
{
	if ((port < MIN_PORT) || (port > MAX_PORT))
		return -EINVAL;

	uint32_t pcr = read_pcr(port);

	pcr = flag ? RTU_PCR_B_UNREC | pcr : (~RTU_PCR_B_UNREC) & pcr;

	write_pcr(port, pcr);
	return 0;
}

/**
 * \brief Gets the B_UNREC flag on indicated port.
 * @param port port number (0 to 9)
 * @return error code.
 */
int rtu_read_unrecognised_behaviour_on_port(int port)
{
	if ((port < MIN_PORT) || (port > MAX_PORT))
		return -EINVAL;

	return read_pcr(port) & RTU_PCR_B_UNREC ? 1 : 0;
}


/**
 * \brief Enables/disables port mirroring
 * @param ena Enable flag
 * @return error code.
 */
int rtu_enable_mirroring(int ena)
{
	uint32_t ctr = rtu_rd(RX_CTR);

	if (ena == 1)
		ctr = ctr | RTU_RX_CTR_MR_ENA;
	else
		ctr = ctr & (~RTU_RX_CTR_MR_ENA);

	rtu_wr(RX_CTR, ctr);
	return 0;
}

/**
 * \brief Configures port mirroring
 * @param imask Ingress port mask
 * @param emask Egress port mask
 * @param dmask Destination port mask
 * @return error code.
 */
int rtu_cfg_mirroring(struct rtu_mirror_info *cfg)
{
	/* if mirroring disabled (en == 0) zero all masks */
	/* if mirroring enabled (en == 1) write only non-zero masks */

	/* write destination port mask - to which port/-s mirrored traffic will
	 * be sent
	 */
	if (cfg->en == 0 || cfg->dmask != 0) {
		rtu_wr(RX_MP_R0, 0);
		rtu_wr(RX_MP_R1, RTU_RX_MP_R1_MASK_W(cfg->dmask));
	}
	/* write ingress port mask - source of mirrored traffic */
	if (cfg->en == 0 || cfg->imask != 0) {
		rtu_wr(RX_MP_R0, RTU_RX_MP_R0_DST_SRC);
		rtu_wr(RX_MP_R1, RTU_RX_MP_R1_MASK_W(cfg->imask));
	}
	/* write egress port mask - source of mirrored traffic */
	if (cfg->en == 0 || cfg->emask != 0) {
		rtu_wr(RX_MP_R0, RTU_RX_MP_R0_DST_SRC | RTU_RX_MP_R0_RX_TX);
		rtu_wr(RX_MP_R1, RTU_RX_MP_R1_MASK_W(cfg->emask));
	}

	return 0;
}

/**
 * \brief Gets port mirroring configuration
 * @param en Enabled flag mirroring pointer
 * @param imask Ingress port mask pointer
 * @param emask Egress port mask pointer
 * @param dmask Destination port mask pointer
 * @return error code.
 */
int rtu_get_mirroring(int *en, uint32_t *imask, uint32_t *emask, uint32_t *dmask)
{
	/* Enabled flag */
	if (rtu_rd(RX_CTR) & RTU_RX_CTR_MR_ENA != 0)
		*en = 1;
	else
		*en = 0;
	/* destination port mask */
	rtu_wr(RX_MP_R0, 0);
	*dmask = RTU_RX_MP_R1_MASK_R(rtu_rd(RX_MP_R1));
	/* ingress port mask */
	rtu_wr(RX_MP_R0, RTU_RX_MP_R0_DST_SRC);
	*imask = RTU_RX_MP_R1_MASK_R(rtu_rd(RX_MP_R1));
	/* write egress port mask - source of mirrored traffic */
	rtu_wr(RX_MP_R0, RTU_RX_MP_R0_DST_SRC | RTU_RX_MP_R0_RX_TX);
	*emask = RTU_RX_MP_R1_MASK_R(rtu_rd(RX_MP_R1));

	return 0;
}

static void print_config_rtu(int port, uint8_t valid_mask, struct rtu_port_entry *rtu_ports)
{
	int i;

        i=port;
		pr_debug("Updates applied for port: %4d   ", i+1);
		pr_debug("pmode: ");
		if (valid_mask & VALID_QMODE)
			pr_debug("%s ", qmode_names[rtu_ports[i].qmode & 0x3]);
		else
			pr_debug("[No update] ");

		pr_debug("fix_prio: ");
		if (valid_mask & VALID_PRIO)
			pr_debug("%s        ", yes_no[rtu_ports[i].fix_prio & 0x1]);
		else
			pr_debug("[No update] ");

		pr_debug("prio_val: ");
		if (valid_mask & VALID_PRIO)
			pr_debug("%2d          ", rtu_ports[i].prio);
		else
			pr_debug("[No update] ");

		pr_debug("vid: ");
		if (valid_mask & VALID_VID)
			pr_debug("%4d        ", rtu_ports[i].pvid);
		else
			pr_debug("[No update] ");

		pr_debug("untag: ");
		if (valid_mask & VALID_UNTAG)
			pr_debug("%s        ", yes_no[rtu_ports[i].untag]);
		else
			pr_debug("[No update] ");

		pr_debug("\n");
}

static uint32_t ep_read(int ep, int offset)
{
	return _fpga_readl(0x30000 + ep * 0x400 + offset);
}

static void ep_write(int ep, int offset, uint32_t value)
{
	_fpga_writel(0x30000 + ep * 0x400 + offset, value);
}

static void rtu_write_port_cfg_hw(uint8_t hw_index)
{
	uint32_t v, r;
	int i;

	r = offsetof(struct EP_WB, VCR0);
	v = ep_read(hw_index, r);
	v = (v & ~EP_VCR0_QMODE_MASK) | EP_VCR0_QMODE_W(ports_cfg[hw_index].qmode);
	if (ports_cfg[hw_index].fix_prio)
		v |= EP_VCR0_FIX_PRIO;
	else
		v &= ~EP_VCR0_FIX_PRIO;
	v = (v & ~EP_VCR0_PRIO_VAL_MASK) | EP_VCR0_PRIO_VAL_W(ports_cfg[hw_index].prio);
	v = (v & ~EP_VCR0_PVID_MASK) | EP_VCR0_PVID_W(ports_cfg[hw_index].pvid);
	ep_write(hw_index, r, v);

	/* VCR1: loop over the whole bitmask */
	r = offsetof(struct EP_WB, VCR1);
	for (i = 0;i < 4096/16; i++) {
		if (ports_cfg[hw_index].untag)
			ep_write(hw_index, r, (0xffff << 10) | i);
		else
			ep_write(hw_index, r, (0x0000 << 10) | i);
	}
}

void rtu_write_port_config(uint8_t hw_index,	/* indexed from 0 to 17 */
			   uint8_t valid_mask, /* mask of valid settings */
			   uint8_t qmode,	/* q mode of a port */
			   uint8_t fix_prio,	/* is fix priority set */
			   uint8_t prio,	/* VLAN priority */
			   uint16_t pvid,	/* PVID  */
			   uint8_t untag	/* untag */
			   )
{
	wrs_shm_write(rtu_shmem_p, WRS_SHM_WRITE_BEGIN);

	/* update masks in SHM only for fields included in the valid_mask */

	if (valid_mask & VALID_QMODE)
		ports_cfg[hw_index].qmode = qmode;

	if (valid_mask & VALID_PRIO) {
		ports_cfg[hw_index].fix_prio = fix_prio;
		ports_cfg[hw_index].prio = prio;
	}

	if (valid_mask & VALID_VID)
		ports_cfg[hw_index].pvid = pvid;

	if (valid_mask & VALID_UNTAG)
		ports_cfg[hw_index].untag = untag;

        print_config_rtu(hw_index, valid_mask, ports_cfg);
	rtu_write_port_cfg_hw(hw_index);

	wrs_shm_write(rtu_shmem_p, WRS_SHM_WRITE_END);
}

static void rtu_read_mac_from_ep(int hw_index, uint8_t mac[ETH_ALEN])
{
	uint32_t tmp;
	tmp = ep_read(hw_index, offsetof(struct EP_WB, MACH)),
	mac[0] = ((tmp >> 8) & 0xff);
	mac[1] = (tmp & 0xff);
	tmp = ep_read(hw_index, offsetof(struct EP_WB, MACL));
	mac[2] = ((tmp >> 24) & 0xff);
	mac[3] = ((tmp >> 16) & 0xff);
	mac[4] = ((tmp >> 8) & 0xff);
	mac[5] = (tmp & 0xff);
}

/**
 * Port config initialization.
 */
void rtu_clean_ports(int lock)
{
	uint8_t hw_index;
	if (lock & SHM_LOCK)
		wrs_shm_write(rtu_shmem_p, WRS_SHM_WRITE_BEGIN);

	memset(ports_cfg, 0, sizeof(*ports_cfg) * HAL_MAX_PORTS);

	for (hw_index = 0; hw_index < hal_nports_local; hw_index++) {
	    ports_cfg[hw_index].qmode = QMODE_UNQ;
	    ports_cfg[hw_index].fix_prio = 0;
	    ports_cfg[hw_index].prio = 0;
	    ports_cfg[hw_index].pvid = 0;
	    ports_cfg[hw_index].untag = 0;
	    rtu_write_port_cfg_hw(hw_index);
	    /* read MAC from ep */
	    rtu_read_mac_from_ep(hw_index, ports_cfg[hw_index].mac);
	}

        if (lock & SHM_LOCK)
		wrs_shm_write(rtu_shmem_p, WRS_SHM_WRITE_END);
}
//---------------------------------------------
// Private Methods
//---------------------------------------------

// to write on MFIFO

static void write_mfifo_addr(uint32_t zbt_addr)
{
	rtu_wr(MFIFO_R0, RTU_MFIFO_R0_AD_SEL);
	rtu_wr(MFIFO_R1, zbt_addr);
}

static void write_mfifo_data(uint32_t word)
{
	rtu_wr(MFIFO_R0, RTU_MFIFO_R0_DATA_SEL);
	rtu_wr(MFIFO_R1, word);
}

// to marshall MAC entries

static uint32_t mac_entry_word0_w(struct rtu_filtering_entry *ent)
{
	return
	    ((0xFF & ent->mac[0]) << 24) |
	    ((0xFF & ent->mac[1]) << 16) |
	    ((0xFF & ent->fid) << 4) |
	    ((0x1 & ent->is_bpdu) << 2) |
	    ((0x1 & ent->end_of_bucket) << 1) | ((0x1 & ent->valid));
}

static uint32_t mac_entry_word1_w(struct rtu_filtering_entry *ent)
{
	return
	    ((0xFF & ent->mac[2]) << 24) |
	    ((0xFF & ent->mac[3]) << 16) |
	    ((0xFF & ent->mac[4]) << 8) | ((0xFF & ent->mac[5]));
}

static uint32_t mac_entry_word2_w(struct rtu_filtering_entry *ent)
{
	return
	    ((0x1 & ent->drop_when_dest) << 28) |
	    ((0x1 & ent->prio_override_dst) << 27) |
	    ((0x7 & ent->prio_dst) << 24) |
	    ((0x1 & ent->has_prio_dst) << 23) |
	    ((0x1 & ent->drop_unmatched_src_ports) << 22) |
	    ((0x1 & ent->drop_when_source) << 21) |
	    ((0x1 & ent->prio_override_src) << 20) |
	    ((0x7 & ent->prio_src) << 17) | ((0x1 & ent->has_prio_src) << 16);
}

static uint32_t mac_entry_word3_w(struct rtu_filtering_entry *ent)
{
	return
	    ((0xFFFF & ent->port_mask_dst) << 16) |
	    ((0xFFFF & ent->port_mask_src));
}

static uint32_t mac_entry_word4_w(struct rtu_filtering_entry *ent)
{
	return
	    ((0xFFFF & (ent->port_mask_dst >> 16)) << 16) |
	    ((0xFFFF & (ent->port_mask_src >> 16)));
}
