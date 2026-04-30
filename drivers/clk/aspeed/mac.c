// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) ASPEED Technology Inc.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/iopoll.h>
#include <asm/arch/scu_ast2700.h>
#include <asm/global_data.h>
#include <common.h>
#include <fdt_support.h>
#include <linux/libfdt.h>
#include <phy_interface.h>

DECLARE_GLOBAL_DATA_PTR;

#define IER 0x04
#define TXPD 0x18
#define TXR_BADR 0x20
#define RXR_BADR 0x24
#define APTC 0x34
#define APTC_RPOLL_CNT GENMASK(3, 0)
#define DBLAC 0x38
#define DBLAC_RDES_SIZE(x) (((x) & 0xf) << 12)
#define DBLAC_TDES_SIZE(x) (((x) & 0xf) << 16)
#define DBLAC_DESC_UINT 8
#define FEAR 0x40
#define RBSR 0x4c
#define MACCR 0x50
#define MACCR_TXDMA_EN BIT(0)
#define MACCR_RXDMA_EN BIT(1)
#define MACCR_TXMAC_EN BIT(2)
#define MACCR_RXMAC_EN BIT(3)
#define MACCR_FULLDUP BIT(8)
#define MACCR_GMAC_MODE BIT(9)
#define MACCR_CRC_APD BIT(10)
#define MACCR_RX_RUNT BIT(12)
#define MACCR_RX_BROADPKT_EN BIT(17)
#define MACCR_SW_RST BIT(31)

#define TXR_BADR_HI 0x17c
#define RXR_BADR_HI 0x18c

#define MAC_TXDES0_TXDMA_OWN BIT(31)
#define MAC_TXDES0_EDOTR BIT(30)
#define MAC_TXDES0_FTS BIT(29)
#define MAC_TXDES0_LTS BIT(28)
#define MAC_TXDES0_TXBUF_SIZE(x) ((x) & 0x3fff)

#define MAC_TXDES2_TXBUF_BADR_HI GENMASK(18, 16)

#define MAC_RXDES0_RXPKT_RDY BIT(31)
#define MAC_RXDES0_EDORR BIT(30)
#define MAC_RXDES0_RX_ODD_NB BIT(22)
#define MAC_RXDES0_RUNT BIT(21)
#define MAC_RXDES0_FTL BIT(20)
#define MAC_RXDES0_CRC_ERR BIT(19)
#define MAC_RXDES0_RX_ERR BIT(18)

#define MAC_RXDES0_ANY_ERROR                                                         \
	(MAC_RXDES0_RX_ERR | MAC_RXDES0_CRC_ERR | MAC_RXDES0_FTL | MAC_RXDES0_RUNT | \
	 MAC_RXDES0_RX_ODD_NB)

#define MAC_RXDES2_RXBUF_BADR_HI GENMASK(18, 16)

#define MAC_TX_POLL_INTERVAL_US	10
#define MAC_TX_TIMEOUT_US	10000

#define TX_DELAY_1 GENMASK(5, 0)
#define TX_DELAY_2 GENMASK(11, 6)
#define RX_DELAY_1 GENMASK(17, 12)
#define RX_DELAY_2 GENMASK(23, 18)

#define SCU_HW_REVISION_ID	GENMASK(23, 16)
#define SCU_FREQ_RING_ENABLE	BIT(0)
#define SCU_FREQ_OSC_ENABLE	BIT(1)
#define SCU_FREQ_SELECT_DLY32	FIELD_PREP(GENMASK(5, 2), 0x5)
#define SCU_FREQ_SELECT_RGMII	FIELD_PREP(GENMASK(5, 2), 0xd)
#define SCU_FREQ_DONE		BIT(6)
#define SCU_FREQ_RING_STG(x)	FIELD_PREP(GENMASK(14, 9), x)
#define SCU_FREQ_COUNTER_MASK	GENMASK(29, 16)
#define SCU_FREQ_COUNTER(x)	FIELD_GET(SCU_FREQ_COUNTER_MASK, x)

#define SCU_DBGSEL_RING_SEL_MASK	GENMASK(15, 8)
#define SCU_DBGSEL_RING_SEL(x)		FIELD_PREP(SCU_DBGSEL_RING_SEL_MASK, x)
#define   SCU_DBGSEL_RING_SEL_RGMII0_TX	44
#define   SCU_DBGSEL_RING_SEL_RGMII1_TX	45
#define   SCU_DBGSEL_RING_SEL_RGMII0_RX	46
#define   SCU_DBGSEL_RING_SEL_RGMII1_RX	47

#define SCU_MULTI_CTRL18	0x444	/* GPIO Group R */
#define SCU_MULTI_CTRL19	0x448	/* GPIO Group S */
#define SCU_MULTI_CTRL20	0x44C	/* GPIO Group T */
#define SCU_MULTI_CTRL21	0x450	/* GPIO Group U */

#ifndef DMA_ALIGNED
#define DMA_ALIGNED __aligned(CONFIG_SYS_CACHELINE_SIZE)
#endif
#ifndef ROUNDUP_DMA_SIZE
#define ROUNDUP_DMA_SIZE(x)                                                    \
	((((x) + CONFIG_SYS_CACHELINE_SIZE - 1) / CONFIG_SYS_CACHELINE_SIZE) * \
	 CONFIG_SYS_CACHELINE_SIZE)
#endif
struct mac_des_s {
	u32 des0;
	u32 des1;
	u32 des2;
	u32 des3;
} DMA_ALIGNED;

union mac_dma_addr {
	struct {
		u32 lo;
		u32 hi;
	};
};

static struct mac_des_s txdes;
static struct mac_des_s rxdes;
static u8 tx_pkt_buf[ROUNDUP_DMA_SIZE(1514)] DMA_ALIGNED;
static u8 rx_pkt_buf[ROUNDUP_DMA_SIZE(1514)] DMA_ALIGNED;

static u32 calculate_freq(u32 value)
{
	uint64_t freq = (uint64_t)25000000 * (value + 1) * (8);

	freq /= 512;
	return (u32)freq;
}

static u32 cal_delay32_ring(u8 revision, u8 rgmii_chain)
{
	struct ast2700_scu1 *scu = (struct ast2700_scu1 *)ASPEED_IO_SCU_BASE;
	void *base = (void *)&scu->freq_counter_ctrl;
	uint64_t time_ps;
	u32 reg, dbgsel;
	int ret;

	writel(0x1c, base);
	ret = readl_poll_timeout(base, reg, ((reg & SCU_FREQ_COUNTER_MASK) == 0), 50);
	if (ret < 0)
		return 0;
	reg = SCU_FREQ_RING_ENABLE |  SCU_FREQ_RING_STG(31);
	if (revision == 1) {
		reg |= SCU_FREQ_SELECT_DLY32;
	} else {
		reg |= SCU_FREQ_SELECT_RGMII;
		dbgsel = SCU_DBGSEL_RING_SEL(rgmii_chain);
		clrsetbits_le32(&scu->rsv_0xC4, SCU_DBGSEL_RING_SEL_MASK, dbgsel);
	}
	writel(reg, base);

	mdelay(1);

	reg |= SCU_FREQ_OSC_ENABLE;
	writel(reg, base);
	ret = readl_poll_timeout(base, reg, (reg & SCU_FREQ_DONE), 1000);
	if (ret < 0)
		return 0;
	reg = SCU_FREQ_COUNTER(readl(base));
	reg = calculate_freq(reg);
	time_ps = 1000000000000ULL / reg;

	writel(0, base);
	clrsetbits_le32(&scu->rsv_0xC4, SCU_DBGSEL_RING_SEL_MASK, 0);

	return (u32)time_ps;
}

static void mac_reset_assert(u32 index)
{
	struct ast2700_scu1 *scu = (struct ast2700_scu1 *)ASPEED_IO_SCU_BASE;

	writel(BIT(5 + index), &scu->modrst1_ctrl);
}

static void mac_reset_deassert(u32 index)
{
	struct ast2700_scu1 *scu = (struct ast2700_scu1 *)ASPEED_IO_SCU_BASE;

	writel(BIT(5 + index), &scu->modrst1_clr);
}

static void mac_clk_enable(u32 index)
{
	struct ast2700_scu1 *scu = (struct ast2700_scu1 *)ASPEED_IO_SCU_BASE;

	writel(BIT(8 + index), &scu->clkgate_clr1);
}

static void mac_clk_disable(u32 index)
{
	struct ast2700_scu1 *scu = (struct ast2700_scu1 *)ASPEED_IO_SCU_BASE;

	writel(BIT(9 + index), &scu->clkgate_ctrl1);
}

static void mac_init_rx_desc_only_desc0(void)
{
	dma_addr_t des_start, des_end;

	rxdes.des0 = MAC_RXDES0_EDORR;

	des_start = (dma_addr_t)&rxdes;
	des_end = (dma_addr_t)&rxdes + sizeof(struct mac_des_s);
	flush_dcache_range(des_start, des_end);
}

void mac_init_tx_desc(void)
{
	dma_addr_t des_start, des_end;

	txdes.des3 = 0;
	txdes.des1 = 0;
	txdes.des0 = 0;
	txdes.des0 = MAC_TXDES0_EDOTR;

	des_start = (dma_addr_t)&txdes;
	des_end = (dma_addr_t)&txdes + sizeof(struct mac_des_s);
	flush_dcache_range(des_start, des_end);
}

static void mac_init_rx_desc(void)
{
	dma_addr_t des_start, des_end;

	rxdes.des2 = FIELD_PREP(MAC_RXDES2_RXBUF_BADR_HI, ((dma_addr_t)rx_pkt_buf) >> 32);
	rxdes.des3 = ((dma_addr_t)rx_pkt_buf) & 0xFFFFFFFF;
	rxdes.des0 = MAC_RXDES0_EDORR;
	rxdes.des1 = 0;

	des_start = (dma_addr_t)&rxdes;
	des_end = (dma_addr_t)&rxdes + sizeof(struct mac_des_s);
	flush_dcache_range(des_start, des_end);
}

void mac_set_loopback(u32 index, bool enable)
{
	void *base;
	u32 fear;

	if (index)
		base = (void *)ASPEED_IO_MAC1_BASE;
	else
		base = (void *)ASPEED_IO_MAC0_BASE;

	fear = readl(base + FEAR);
	if (enable)
		fear |= BIT(30);
	else
		fear &= ~BIT(30);

	writel(fear, base + FEAR);
}

static void mac_rgmii_pin(u32 index)
{
	void *scu = (void *)ASPEED_IO_SCU_BASE;

	if (index) {
		/* Configure MAC1 pin to GPIO */
		writel(0, scu + SCU_MULTI_CTRL20);
		clrsetbits_le32(scu + SCU_MULTI_CTRL21, GENMASK(14, 0), 0);
	} else {
		/* Configure MAC0 pin to GPIO */
		writel(0, scu + SCU_MULTI_CTRL18);
		clrsetbits_le32(scu + SCU_MULTI_CTRL19, GENMASK(14, 0), 0);
	}
}

static void mac_init(u32 index)
{
	void *base;
	u32 reg, dblac, desc_size;

	if (index)
		base = (void *)ASPEED_IO_MAC1_BASE;
	else
		base = (void *)ASPEED_IO_MAC0_BASE;

	mac_rgmii_pin(index);
	mac_reset_deassert(index);
	mac_clk_enable(index);

	// writel(0x01, base + LADR);

	/* disable interrupt */
	writel(0, base + IER);

	writel(((dma_addr_t)&txdes) & 0xFFFFFFFF, base + TXR_BADR);
	writel(((dma_addr_t)&txdes) >> 32, base + TXR_BADR_HI);

	writel(((dma_addr_t)&rxdes) & 0xFFFFFFFF, base + RXR_BADR);
	writel(((dma_addr_t)&rxdes) >> 32, base + RXR_BADR_HI);

	mac_init_tx_desc();
	mac_init_rx_desc();

	/* set RX polling */
	writel(FIELD_PREP(APTC_RPOLL_CNT, 0x1), base + APTC);

	/* default receive buffer size = 0x600 (1536) */;
	writel(0x600, base + RBSR);

	/* set decriptor size */
	desc_size = CONFIG_SYS_CACHELINE_SIZE / DBLAC_DESC_UINT;
	/* The descriptor size is at least 2 descriptor units. */
	if (desc_size < 2)
		desc_size = 2;
	/* Clear the TX/RX DESC size field*/
	dblac = readl(base + DBLAC) & ~GENMASK(19, 12);
	dblac |= DBLAC_RDES_SIZE(desc_size) | DBLAC_TDES_SIZE(desc_size);
	writel(dblac, base + DBLAC);

	/* start HW */
	reg = FIELD_PREP(MACCR_RXDMA_EN, 1) | FIELD_PREP(MACCR_RXMAC_EN, 1) |
	      FIELD_PREP(MACCR_TXDMA_EN, 1) | FIELD_PREP(MACCR_TXMAC_EN, 1) |
	      FIELD_PREP(MACCR_CRC_APD, 1) | FIELD_PREP(MACCR_FULLDUP, 1) |
	      FIELD_PREP(MACCR_RX_RUNT, 1) | FIELD_PREP(MACCR_RX_BROADPKT_EN, 1) |
	      FIELD_PREP(MACCR_GMAC_MODE, 1);
	writel(reg, base + MACCR);
}

static void prepare_tx_packet(u8 *pkt)
{
	int j;
	u8 *ptr;

	ptr = pkt;
	/* DA: broadcast */
	for (j = 0; j < 6; j++)
		*ptr++ = 0xff;

	/* SA: skip */
	ptr += 6;

	/* length */
	*ptr++ = 0x55;
	*ptr++ = 0xAA;

	/* payload */
	for (j = 0x55; j < 40; j++)
		*ptr++ = j;
}

static int mac_txpkt_add(void *packet)
{
	dma_addr_t des_start, des_end;

	des_start = (dma_addr_t)&txdes;
	des_end = (dma_addr_t)&txdes + sizeof(struct mac_des_s);
	invalidate_dcache_range(des_start, des_end);

	txdes.des2 = FIELD_PREP(MAC_TXDES2_TXBUF_BADR_HI, ((dma_addr_t)packet) >> 32);
	txdes.des3 = ((dma_addr_t)packet) & 0xFFFFFFFF;
	txdes.des0 |= MAC_TXDES0_FTS | MAC_TXDES0_LTS | MAC_TXDES0_TXBUF_SIZE(60) |
		      MAC_TXDES0_TXDMA_OWN;
	txdes.des1 = 0;

	flush_dcache_range((dma_addr_t)packet, (dma_addr_t)packet + 64);

	flush_dcache_range(des_start, des_end);

	return 0;
}

static void mac_init_tx_desc_only_desc0(void)
{
	dma_addr_t des_start, des_end;

	des_start = (dma_addr_t)&txdes;
	des_end = (dma_addr_t)&txdes + sizeof(struct mac_des_s);
	invalidate_dcache_range(des_start, des_end);

	txdes.des0 |= MAC_TXDES0_TXDMA_OWN;

	flush_dcache_range(des_start, des_end);
}

static void set_rgmii_delay(u32 tx, u32 rx, u32 index, bool freq_set)
{
	struct ast2700_scu1 *scu = (struct ast2700_scu1 *)ASPEED_IO_SCU_BASE;
	void *target_reg = (freq_set) ? &scu->mac_10m_delay : &scu->mac_delay;
	u32 reg;

	reg = readl(target_reg);
	if (index) {
		reg &= ~(TX_DELAY_2 | RX_DELAY_2);
		reg |= FIELD_PREP(TX_DELAY_2, tx) | FIELD_PREP(RX_DELAY_2, rx);
	} else {
		reg &= ~(TX_DELAY_1 | RX_DELAY_1);
		reg |= FIELD_PREP(TX_DELAY_1, tx) | FIELD_PREP(RX_DELAY_1, rx);
	}
	writel(reg, target_reg);
	// printf("reg=0x%08x\n", readl(target_reg));
}

#define SCU1_SCRATCH_TX_DELAY_STEP(x)	FIELD_PREP(GENMASK(15, 0), (x))
#define SCU1_SCRATCH_RX_DELAY_STEP(x)	FIELD_PREP(GENMASK(31, 16), (x))

static void record_rgmii_delay(u32 index, u8 tx_dis, u8 tx_en, u8 rx_dis, u8 rx_en,
			       u32 tx_average_delay, u32 rx_average_delay)
{
	struct ast2700_scu1 *scu = (struct ast2700_scu1 *)ASPEED_IO_SCU_BASE;
	u32 scu0, scu1;

	scu0 = SCU1_SCRATCH_TX_DELAY_STEP(tx_average_delay) |
	       SCU1_SCRATCH_RX_DELAY_STEP(rx_average_delay);
	scu1 = FIELD_PREP(GENMASK(7, 0), tx_dis) |
	       FIELD_PREP(GENMASK(15, 8), tx_en) |
	       FIELD_PREP(GENMASK(23, 16), rx_dis) |
	       FIELD_PREP(GENMASK(31, 24), rx_en);
	if (index) {
		writel(scu0, &scu->scratch[6]);	/* SCU1_198 */
		writel(scu1, &scu->scratch[7]);	/* SCU1_19c */
	} else {
		writel(scu0, &scu->scratch[4]);	/* SCU1_190 */
		writel(scu1, &scu->scratch[5]);	/* SCU1_194 */
	}
}

static u32 mac_read_tx_desc0(dma_addr_t des_start, dma_addr_t des_end)
{
	invalidate_dcache_range(des_start, des_end);

	return txdes.des0;
}

static int mac_xmit(u32 index)
{
	dma_addr_t des_start, des_end;
	void *base;
	u32 des0;

	if (index)
		base = (void *)ASPEED_IO_MAC1_BASE;
	else
		base = (void *)ASPEED_IO_MAC0_BASE;

	writel(1, base + TXPD);

	des_start = (dma_addr_t)&txdes;
	des_end = des_start + sizeof(txdes);

	return read_poll_timeout(mac_read_tx_desc0, des0,
				 !(des0 & MAC_TXDES0_TXDMA_OWN),
				 MAC_TX_POLL_INTERVAL_US, MAC_TX_TIMEOUT_US,
				 des_start, des_end);
}

static int mac_recv_no_data(void)
{
	dma_addr_t des_start, des_end;
	int i = 50;

	des_start = (dma_addr_t)&rxdes;
	des_end = des_start + sizeof(rxdes);
	do {
		invalidate_dcache_range(des_start, des_end);
		if (i-- < 0)
			return -1;
	} while (!(rxdes.des0 & MAC_RXDES0_RXPKT_RDY));

	if (rxdes.des0 & MAC_RXDES0_ANY_ERROR)
		return -1;

	return 0;
}

static int packet_check(u32 index)
{
	int ret;

	mac_init_rx_desc_only_desc0();
	mac_init_tx_desc_only_desc0();
	ret = mac_xmit(index);
	if (ret)
		return -1;

	return mac_recv_no_data();
}

static const char *get_mac_phy_mode(u32 index)
{
	const void *fdt = gd->fdt_blob;
	char alias_name[16];
	int alias_node = fdt_path_offset(fdt, "/aliases");
	const char *target_path;
	int nodeoffset;

	if (alias_node < 0) {
		printf("No /aliases node\n");
		return NULL;
	}

	snprintf(alias_name, sizeof(alias_name), "ethernet%d", index);

	target_path = fdt_getprop(fdt, alias_node, alias_name, NULL);
	if (!target_path) {
		printf("Alias '%s' not found\n", alias_name);
		return NULL;
	}

	nodeoffset = fdt_path_offset(fdt, target_path);
	if (nodeoffset < 0) {
		printf("Node at path '%s' not found\n", target_path);
		return NULL;
	}

	if (strcmp("okay", (const char *)fdt_getprop(fdt, nodeoffset, "status", NULL)) != 0)
		return NULL;

	return fdt_getprop(fdt, nodeoffset, "phy-mode", NULL);
}

static u32 find_rx_center(u8 *data)
{
	int max_len = 0;
	int max_start = -1;
	int max_end = -1;
	int current_start = -1;
	int i;

	for (i = 0; i < 32; i++) {
		if (data[i] == 0) {
			if (current_start == -1)
				current_start = i;
		} else {
			if (current_start != -1) {
				int current_len = i - current_start;

				if (current_len > max_len) {
					max_len = current_len;
					max_start = current_start;
					max_end = i - 1;
				}
				current_start = -1;
			}
		}
	}

	if (current_start != -1) {
		int current_len = i - current_start;

		if (current_len > max_len) {
			max_len = current_len;
			max_start = current_start;
			max_end = i - 1;
		}
	}

	return max_start + (max_len - 1) / 2;
}

static bool check_calibration_delay(u32 index)
{
	struct ast2700_scu1 *scu = (struct ast2700_scu1 *)ASPEED_IO_SCU_BASE;

	if (index) {
		if ((readl(&scu->scratch[6]) & SCU1_SCRATCH_TX_DELAY_STEP(0xFFFF)) == 0)
			return false;
	} else {
		if ((readl(&scu->scratch[4]) & SCU1_SCRATCH_TX_DELAY_STEP(0xFFFF)) == 0)
			return false;
	}

	return true;
}

static void find_rgmii_delay(struct ast2700_scu1 *scu, u32 index)
{
	u32 tx, rx, tx_en, tx_dis, rx_en, rx_dis, tx_average_delay, rx_average_delay;
	u32 mac_loopback_delay = 0, dly32_average_delay = 0;
	u8 revision = FIELD_GET(SCU_HW_REVISION_ID, scu->chip_id1);
	const char *phy_mode;
	u8 rgmii_chain;
	u8 result[32];

	if (check_calibration_delay(index))
		return;

	phy_mode = get_mac_phy_mode(index);
	if (!phy_mode)
		return;

	if (revision == 1) {
		/* TX delay chain */
		tx_average_delay = cal_delay32_ring(revision, 0);
		tx_average_delay /= 32;
		if (tx_average_delay == 0)
			return;
		tx_average_delay = tx_average_delay * 10 / 7;
		rx_average_delay = tx_average_delay;
	} else {
		u32 tx_start, tx_end;
#ifdef RX_DELAY_CHAIN
		u32 rx_start, rx_end;
#endif

		dly32_average_delay = cal_delay32_ring(1, 0);
		dly32_average_delay /= 32;
		if (dly32_average_delay == 0)
			return;
		/* TX delay chain */
		rgmii_chain = (index) ?
			SCU_DBGSEL_RING_SEL_RGMII1_TX :
			SCU_DBGSEL_RING_SEL_RGMII0_TX;
		set_rgmii_delay(0, 0, index, true);
		tx_start = cal_delay32_ring(revision, rgmii_chain);
		set_rgmii_delay(31, 0, index, true);
		tx_end = cal_delay32_ring(revision, rgmii_chain);

		tx_average_delay = (tx_end - tx_start) / 31;
		tx_average_delay /= 2;
		if (tx_average_delay == 0)
			return;
		/* RX delay chain */
#ifdef RX_DELAY_CHAIN
		rgmii_chain = (index) ?
			      SCU_DBGSEL_RING_SEL_RGMII1_RX :
			      SCU_DBGSEL_RING_SEL_RGMII0_RX;
		set_rgmii_delay(0, 0, index, true);
		rx_start = cal_delay32_ring(revision, rgmii_chain);
		set_rgmii_delay(0, 31, index, true);
		rx_end = cal_delay32_ring(revision, rgmii_chain);
		rx_average_delay = (rx_end - rx_start) / 31;
		rx_average_delay /= 2;
#endif
		if (index)
			rx_average_delay = (dly32_average_delay * 1778460) / 1000000;
		else
			rx_average_delay = (dly32_average_delay * 1926404) / 1000000;

		mac_loopback_delay = (index) ? 400 : 700;
	}

	mac_init(index);
	mac_set_loopback(index, true);
	prepare_tx_packet(tx_pkt_buf);
	mac_txpkt_add(tx_pkt_buf);

	if (revision == 2)
		tx_en = (10000 - mac_loopback_delay) / tx_average_delay;
	else
		tx_en = 2000 / tx_average_delay;
	for (rx = 0; rx < 32; rx++) {
		set_rgmii_delay(tx_en, rx, index, false);
		result[rx] = packet_check(index);
	};

	if (revision == 2 && index == 0) {
		tx_en = 10000 / tx_average_delay;
		tx_dis = 8000 / tx_average_delay;
	} else {
		tx_dis = 0;
	}
	rx_dis = find_rx_center(result) + 1;
	rx_en = rx_dis + 2000 / rx_average_delay;
	if (phy_mode) {
		printf(" %d: %s ", index, phy_mode);
		if (strcmp(phy_mode, "rgmii-rxid") == 0) {
			tx = tx_en;
			rx = rx_dis;
		} else if (strcmp(phy_mode, "rgmii-txid") == 0) {
			tx = tx_dis;
			rx = rx_en;
		} else if (strcmp(phy_mode, "rgmii") == 0) {
			tx = tx_en;
			rx = rx_en;
		} else if (strcmp(phy_mode, "rmii") == 0 || strcmp(phy_mode, "NC-SI") == 0) {
			tx = 0;
			rx = 0;
		} else {
			tx = tx_dis;
			rx = rx_dis;
		}
	} else {
		tx = tx_dis;
		rx = rx_dis;
	}

	if (tx > 32 || rx > 32) {
		printf("RGMII delay out of range: tx=%u, rx=%u\n", tx, rx);
		tx = 0;
		rx = 0;
	}

	set_rgmii_delay(tx, rx, index, false);
	mac_set_loopback(index, false);
	mac_clk_disable(index);
	mac_reset_assert(index);

	/* Record in SCU1 */
	record_rgmii_delay(index, tx_dis, tx_en, rx_dis, rx_en, tx_average_delay, rx_average_delay);
}

/* Because clk driver will be called twice, bypass the first time */
static bool bypass_first = true;

void ast2700_rgmii_init(struct ast2700_scu1 *scu)
{
	if (!bypass_first) {
		printf("MAC:  ");
		find_rgmii_delay(scu, 0);
		find_rgmii_delay(scu, 1);
		printf("\n");
	} else {
		bypass_first = false;
	}
}
