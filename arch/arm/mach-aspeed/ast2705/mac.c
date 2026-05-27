// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) ASPEED Technology Inc.
 */
#include <linux/bitfield.h>
#include <asm/arch-aspeed/platform.h>
#include <asm/arch-aspeed/scu_ast2700.h>
#include <linux/iopoll.h>
#include <stdint.h>

#include "mac.h"

#ifndef DMA_ALIGNED
#define DMA_ALIGNED __aligned(CONFIG_SYS_CACHELINE_SIZE)
#endif

struct mac_des_s {
	uint32_t des0;
	uint32_t des1;
	uint32_t des2;
	uint32_t des3;
} DMA_ALIGNED;

static struct mac_des_s txdes;
static struct mac_des_s rxdes;
static uint8_t tx_pkt_buf[1600] DMA_ALIGNED;
static uint8_t rx_pkt_buf[1600] DMA_ALIGNED;

static uintptr_t mac_base(uint32_t index)
{
	return index ? ASPEED_IO_MAC1_BASE : ASPEED_IO_MAC0_BASE;
}

static uint32_t counter_to_delay_ps(uint32_t value)
{
	return 2560000U / (value + 1);
}

static uint32_t cal_delay32_ring(struct ast2705_scu1 *scu, uint8_t rgmii_chain)
{
	uintptr_t base = (uintptr_t)&scu->freq_counter_ctrl;
	uint32_t reg, dbgsel;
	int ret;

	writel(0x1c, base);
	ret = readl_poll_timeout(base, reg, ((reg & SCU_FREQ_COUNTER_MASK) == 0), 50);
	if (ret < 0)
		return 0;

	reg = SCU_FREQ_RING_ENABLE | SCU_FREQ_RING_STG(31);
	if (rgmii_chain) {
		reg |= SCU_FREQ_SELECT_RGMII;
		dbgsel = readl(&scu->rsv_0xC4) & ~SCU_DBGSEL_RING_SEL_MASK;
		dbgsel |= SCU_DBGSEL_RING_SEL(rgmii_chain);
		writel(dbgsel, &scu->rsv_0xC4);
	} else {
		reg |= SCU_FREQ_SELECT_DLY32;
	}
	writel(reg, base);
	mdelay(1);

	reg |= SCU_FREQ_OSC_ENABLE;
	writel(reg, base);
	ret = readl_poll_timeout(base, reg, (reg & SCU_FREQ_DONE), 1000);
	if (ret < 0)
		return 0;

	writel(0, base);
	dbgsel = readl(&scu->rsv_0xC4) & ~SCU_DBGSEL_RING_SEL_MASK;
	writel(dbgsel, &scu->rsv_0xC4);

	return counter_to_delay_ps(SCU_FREQ_COUNTER(reg));
}

static void mac_reset_assert(struct ast2705_scu1 *scu, uint32_t index)
{
	writel(BIT(5 + index), (uintptr_t)&scu->modrst1_ctrl);
}

static void mac_reset_deassert(struct ast2705_scu1 *scu, uint32_t index)
{
	writel(BIT(5 + index), (uintptr_t)&scu->modrst1_clr);
}

static void mac_clk_enable(struct ast2705_scu1 *scu, uint32_t index)
{
	writel(BIT(8 + index), (uintptr_t)&scu->clkgate_clr1);
}

static void mac_set_freq(struct ast2705_scu1 *scu)
{
	uint32_t data = readl((uintptr_t)&scu->clk_sel1);

	data &= ~(SCU_CLK_SEL1_RGMIICLK_MASK | SCU_CLK_SEL1_MHCLK_MASK);
	data |= SCU_CLK_SEL1_RGMIICLK | SCU_CLK_SEL1_RMHCLK;
	writel(data, (uintptr_t)&scu->clk_sel1);
}

static void mac_clk_disable(struct ast2705_scu1 *scu, uint32_t index)
{
	writel(BIT(9 + index), (uintptr_t)&scu->clkgate_ctrl1);
}

static void mac_init_rx_desc_only_desc0(void)
{
	dma_addr_t des_start, des_end;

	rxdes.des0 = MAC_RXDES0_EDORR;

	des_start = (dma_addr_t)&rxdes;
	des_end = (dma_addr_t)&rxdes + sizeof(struct mac_des_s);
	flush_dcache_range(des_start, des_end);
}

static void mac_init_tx_desc(void)
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

static void mac_set_loopback(uint32_t index, bool enable)
{
	uintptr_t base = mac_base(index);
	uint32_t fear = readl(base + FEAR);

	if (enable)
		fear |= BIT(30);
	else
		fear &= ~BIT(30);

	writel(fear, base + FEAR);
}

static void mac_rgmii_pin(struct ast2705_scu1 *scu, uint32_t index)
{
	uintptr_t reg = (uintptr_t)(index ? &scu->pinumx20 : &scu->pinumx18);

	writel(0, reg);
	writel(readl(reg + 4) & ~GENMASK(14, 0), reg + 4);
}

static void mac_controller_init(struct ast2705_scu1 *scu, uint32_t index)
{
	uintptr_t base = mac_base(index);
	uint32_t reg, dblac, desc_size;

	mac_rgmii_pin(scu, index);
	mac_reset_deassert(scu, index);
	mac_clk_enable(scu, index);
	mac_set_freq(scu);

	writel(0, base + IER);

	writel(((dma_addr_t)&txdes) & 0xFFFFFFFF, base + TXR_BADR);
	writel(((dma_addr_t)&txdes) >> 32, base + TXR_BADR_HI);

	writel(((dma_addr_t)&rxdes) & 0xFFFFFFFF, base + RXR_BADR);
	writel(((dma_addr_t)&rxdes) >> 32, base + RXR_BADR_HI);

	mac_init_tx_desc();
	mac_init_rx_desc();

	writel(FIELD_PREP(APTC_RPOLL_CNT, 0x1), base + APTC);
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

	reg = MACCR_RXDMA_EN | MACCR_RXMAC_EN | MACCR_TXDMA_EN |
	      MACCR_TXMAC_EN | MACCR_CRC_APD | MACCR_FULLDUP |
	      MACCR_RX_RUNT | MACCR_RX_BROADPKT_EN | MACCR_GMAC_MODE;
	writel(reg, base + MACCR);
}

static void prepare_tx_packet(uint8_t *pkt)
{
	uint8_t *ptr = pkt;
	int j;

	for (j = 0; j < 6; j++)
		*ptr++ = 0xff;

	ptr += 6;

	*ptr++ = 0x55;
	*ptr++ = 0xaa;
}

static void mac_txpkt_add(void *packet)
{
	dma_addr_t des_start, des_end;

	des_start = (dma_addr_t)&txdes;
	des_end = (dma_addr_t)&txdes + sizeof(struct mac_des_s);
	invalidate_dcache_range(des_start, des_end);

	txdes.des2 = FIELD_PREP(MAC_TXDES2_TXBUF_BADR_HI, ((dma_addr_t)packet) >> 32);
	txdes.des3 = ((dma_addr_t)packet) & 0xFFFFFFFF;
	txdes.des0 |= MAC_TXDES0_FTS | MAC_TXDES0_LTS |
		      MAC_TXDES0_TXBUF_SIZE(60) | MAC_TXDES0_TXDMA_OWN;
	txdes.des1 = 0;

	flush_dcache_range((dma_addr_t)packet, (dma_addr_t)packet + 64);
	flush_dcache_range(des_start, des_end);
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

static void set_rgmii_delay(uint32_t tx, uint32_t rx, uint32_t index, uintptr_t target)
{
	uint32_t reg = readl(target);
	uint32_t mask = TX_CLK_IO_DLY_SEL | RX_CLK_IO_DLY_SEL;

	reg &= ~mask;
	reg |= FIELD_PREP(TX_CLK_IO_DLY_SEL, tx);
	reg |= FIELD_PREP(RX_CLK_IO_DLY_SEL, rx) << 8;

	writel(reg, target);
}

static void set_rgmii_1g_delay(uint32_t tx, uint32_t rx, uint32_t index, bool freq_set)
{
	uintptr_t base = mac_base(index);
	uintptr_t target = freq_set ? (uintptr_t)(base + RGMII_DLY_SEL_10M) :
				      (uintptr_t)(base + RGMII_DLY_SEL_1G);

	set_rgmii_delay(tx, rx, index, target);
}

static void record_rgmii_delay(struct ast2705_scu1 *scu, uint32_t index,
			       uint8_t tx_dis, uint8_t tx_en, uint8_t rx_dis,
			       uint8_t rx_en, uint32_t tx_average_delay,
			       uint32_t rx_average_delay)
{
	uint32_t scratch = index ? 6 : 4;
	uint32_t scu0 = SCU1_SCRATCH_TX_DELAY_STEP(tx_average_delay) |
			SCU1_SCRATCH_RX_DELAY_STEP(rx_average_delay);
	uint32_t scu1 = FIELD_PREP(GENMASK(7, 0), tx_dis) |
			FIELD_PREP(GENMASK(15, 8), tx_en) |
			FIELD_PREP(GENMASK(23, 16), rx_dis) |
			FIELD_PREP(GENMASK(31, 24), rx_en);

	writel(scu0, (uintptr_t)&scu->scratch[scratch]);
	writel(scu1, (uintptr_t)&scu->scratch[scratch + 1]);
}

static int mac_xmit(uint32_t index)
{
	dma_addr_t des_start, des_end;
	uintptr_t base = mac_base(index);

	writel(1, base + TXPD);

	des_start = (dma_addr_t)&txdes;
	des_end = des_start + sizeof(txdes);
	do {
		invalidate_dcache_range(des_start, des_end);
	} while (txdes.des0 & MAC_TXDES0_TXDMA_OWN);

	return 0;
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

static int packet_check(uint32_t index)
{
	int ret;

	mac_init_rx_desc_only_desc0();
	mac_init_tx_desc_only_desc0();

	ret = mac_xmit(index);
	if (ret)
		return -1;

	return mac_recv_no_data();
}

static uint32_t find_rx_center(uint8_t *data)
{
	uint32_t best_start = 0;
	uint32_t best_len = 0;
	uint32_t current_start = 0;
	uint32_t current_len = 0;

	for (uint32_t i = 0; i < 32; i++) {
		if (data[i] == 0) {
			if (!current_len)
				current_start = i;
			if (++current_len > best_len) {
				best_len = current_len;
				best_start = current_start;
			}
		} else {
			current_len = 0;
		}
	}

	if (!best_len)
		return UINT32_MAX;

	return best_start + (best_len - 1) / 2;
}

static bool check_calibration_delay(struct ast2705_scu1 *scu, uint32_t index)
{
	uint32_t scratch = index ? 6 : 4;

	return readl(&scu->scratch[scratch]) &
	       SCU1_SCRATCH_TX_DELAY_STEP(0xffff);
}

static void find_rgmii_delay(uint32_t index)
{
	struct ast2705_scu1 *scu = (struct ast2705_scu1 *)ASPEED_IO_SCU_BASE;
	uint32_t rx, tx_en, tx_dis, rx_en, rx_dis;
	uint32_t tx_start, tx_end;
	uint32_t tx_average_delay, rx_average_delay;
	uint32_t mac_loopback_delay = 0, dly32_average_delay = 0;
	uint8_t rgmii_chain;
	uint8_t result[32];

	if (check_calibration_delay(scu, index))
		return;

	rgmii_chain = index ? SCU_DBGSEL_RING_SEL_RGMII1_TX :
			      SCU_DBGSEL_RING_SEL_RGMII0_TX;
	set_rgmii_1g_delay(0, 0, index, true);
	tx_start = cal_delay32_ring(scu, rgmii_chain);
	set_rgmii_1g_delay(63, 0, index, true);
	tx_end = cal_delay32_ring(scu, rgmii_chain);

	tx_average_delay = (tx_end - tx_start) / 126;
	if (tx_average_delay == 0)
		return;

	dly32_average_delay = cal_delay32_ring(scu, 0);
	/* TODO:: */
	if (index)
		rx_average_delay = (dly32_average_delay * 1778460) / 1000000;
	else
		rx_average_delay = (dly32_average_delay * 1926404) / 1000000;
	/* TODO:: */
	mac_loopback_delay = index ? 400 : 700;

	mac_controller_init(scu, index);
	mac_set_loopback(index, true);
	prepare_tx_packet(tx_pkt_buf);
	mac_txpkt_add(tx_pkt_buf);

	/* TODO:: */
	tx_en = (10000 - mac_loopback_delay) / tx_average_delay;

	for (rx = 0; rx < 32; rx++) {
		set_rgmii_1g_delay(tx_en, rx, index, false);
		result[rx] = packet_check(index);
	}

	/* TODO:: */
	tx_en = 10000 / tx_average_delay;
	tx_dis = 8000 / tx_average_delay;

	rx_dis = find_rx_center(result) + 1;
	rx_en = rx_dis + 2000 / rx_average_delay;

	mac_set_loopback(index, false);
	mac_clk_disable(scu, index);
	mac_reset_assert(scu, index);

	set_rgmii_delay(tx_en, rx_en, index, (uintptr_t)(mac_base(index) + RGMII_DLY_SEL_10M));
	set_rgmii_delay(tx_en, rx_en, index, (uintptr_t)(mac_base(index) + RGMII_DLY_SEL_100M));
	record_rgmii_delay(scu, index, tx_dis, tx_en, rx_dis, rx_en,
			   tx_average_delay, rx_average_delay);
}

void mac_init(void)
{
	find_rgmii_delay(0);
	find_rgmii_delay(1);
}
