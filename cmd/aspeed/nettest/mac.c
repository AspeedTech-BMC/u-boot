// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) ASPEED Technology Inc.
 */
#include "platform.h"
#include "internal.h"
#include "phy.h"

#define log_warn			printf

#define ISR				0x00
#define IER				0x04
#define MADR				0x08
#define LADR				0x0c
#define TXPD				0x18
#define TXR_BADR			0x20
#define RXR_BADR			0x24
#define APTC				0x34
#define   APTC_RPOLL_CNT		GENMASK(3, 0)
#define   APTC_RPOLL_TIME_SEL		BIT(4)
#define   APTC_TPOLL_CNT		GENMASK(11, 8)
#define   APTC_TPOLL_TIME_SEL		BIT(12)
#define   DISABLE_SLIM_RESP		BIT(24)
#define DBLAC				0x38
#define   DBLAC_RBST_SIZE		GENMASK(9, 8)
#define   DBLAC_TBST_SIZE		GENMASK(11, 10)
#define   DBLAC_RDES_SIZE(x)		(((x) & 0xf) << 12)
#define   DBLAC_RDES_SIZE_MASK		GENMASK(15, 12)
#define   DBLAC_TDES_SIZE(x)		(((x) & 0xf) << 16)
#define   DBLAC_TDES_SIZE_MASK		GENMASK(19, 16)
#define   DBLAC_DESC_UINT		8
#define   DBLAC_IFG_CNT			GENMASK(22, 20)
#define   DBLAC_IFG_INC			GENMASK(23, 23)
#define FEAR				0x40
#define RBSR				0x4c
#define MAHT0				0x10
#define MAHT1				0x14
#define MACCR				0x50
#define   MACCR_TXDMA_EN		BIT(0)
#define   MACCR_RXDMA_EN		BIT(1)
#define   MACCR_TXMAC_EN		BIT(2)
#define   MACCR_RXMAC_EN		BIT(3)
#define   MACCR_RM_VLAN			BIT(4)
#define   MACCR_HPTXR_EN		BIT(5)
#define   MACCR_PHY_LINK_STS_DTCT	BIT(6)
#define   MACCR_ENRX_IN_HALFTX		BIT(7)
#define   MACCR_FULLDUP			BIT(8)
#define   MACCR_GMAC_MODE		BIT(9)
#define   MACCR_CRC_APD			BIT(10)
#define   MACCR_RX_RUNT			BIT(12)
#define   MACCR_JUMBO_LF		BIT(13)
#define   MACCR_RX_ALLADR		BIT(14)
#define   MACCR_RX_HT_EN		BIT(15)
#define   MACCR_RX_MULTIPKT_EN		BIT(16)
#define   MACCR_RX_BROADPKT_EN		BIT(17)
#define   MACCR_DISCARD_CRCERR		BIT(18)
#define   MACCR_SPEED_100		BIT(19)
#define   MACCR_RMII_EN			BIT(20) /* AST2700 */
#define   MACCR_SW_RST			BIT(31)

#define TXR_BADR_HI			0x17c
#define RXR_BADR_HI			0x18c

#define RGMII_DLY_SEL_1G	0x1d0
#define TX_CLK_IO_DLY_SEL		GENMASK(5, 0)
#define RX_CLK_IO_DLY_SEL		GENMASK(13, 8)
#define RMII_TX_ALIGN_CLK_FALL		BIT(20)
#define RGMII_DLY_SEL_100M	0x1d4
#define RGMII_DLY_SEL_10M	0x1d8

/* descriptors */
#define TO_PHY_ADDR(x)			(x)
#if defined(ASPEED_AST2600)
#define TO_VIR_ADDR(x)			((x) | 0x80000000UL)
#else
#define TO_VIR_ADDR(x)			((x) | 0x400000000ULL)
#endif

#define MAC_TXDES0_TXDMA_OWN		BIT(31)
#define MAC_TXDES0_EDOTR		BIT(30)
#define MAC_TXDES0_FTS			BIT(29)
#define MAC_TXDES0_LTS			BIT(28)
#define MAC_TXDES0_CRC_ERR		BIT(19)
#define MAC_TXDES0_TXBUF_SIZE(x)	((x) & 0x3fff)

#define MAC_TXDES2_TXBUF_BADR_HI	GENMASK(18, 16)

#define MAC_RXDES0_RXPKT_RDY		BIT(31)
#define MAC_RXDES0_EDORR		BIT(30)
#define MAC_RXDES0_FRS			BIT(29)
#define MAC_RXDES0_LRS			BIT(28)
#define MAC_RXDES0_PAUSE_FRAME		BIT(25)
#define MAC_RXDES0_PAUSE_OPCODE		BIT(24)
#define MAC_RXDES0_FIFO_FULL		BIT(23)
#define MAC_RXDES0_RX_ODD_NB		BIT(22)
#define MAC_RXDES0_RUNT			BIT(21)
#define MAC_RXDES0_FTL			BIT(20)
#define MAC_RXDES0_CRC_ERR		BIT(19)
#define MAC_RXDES0_RX_ERR		BIT(18)
#define MAC_RXDES0_BROADCAST		BIT(17)
#define MAC_RXDES0_MULTICAST		BIT(16)
#define MAC_RXDES0_VDBC(x)		((x) & 0x3fff)

#define MAC_RXDES0_ANY_ERROR                                                   \
	(MAC_RXDES0_RX_ERR | MAC_RXDES0_CRC_ERR | MAC_RXDES0_FTL |             \
	 MAC_RXDES0_RUNT | MAC_RXDES0_RX_ODD_NB)

#define MAC_RXDES2_RXBUF_BADR_HI	GENMASK(18, 16)

#define ASPEED_MAC_INTERVAL_US		100
#define ASPEED_MAC_TIMEOUT_US		(10 * ASPEED_MAC_INTERVAL_US)

/* AST2700 SGMII */
#define SGMII_CFG			0x00
#define   SGMII_LOOPBACK	BIT(17)
#define   RESET			BIT(15)
#define   PHY_LOOPBACK		BIT(14)
#define   AN_ENABLE		BIT(12)
#define   POWER_DOWN		BIT(11)
#define   AN_START		BIT(9)
#define   SPEED_1000		BIT(5)
#define   SPEED_100		BIT(4)
#define   SPEED_10		0
#define   FIFO			BIT(0)
#define SGMII_LOCAL_CFG			0x18
#define   L_SPEED_1000		BIT(3)
#define   L_SPEED_100		BIT(2)
#define   L_SPEED_10		0
#define   L_DUPLEX_F		BIT(1)
#define   L_LINK_UP		BIT(0)
#define SGMII_PHY_PIPE_CTL		0x20
#define   TX_DEEMPH_3_5DB	BIT(6)
#define SGMII_PHY_PIPE_ST		0x24
#define   PIPE_PHY_ST		BIT(0)
#define SGMII_FIFO_DELAY_THREHOLD	0x28
#define SGMII_MODE			0x30
#define   LOCAL_CONF		BIT(2)
#define   PHY_SIDE		BIT(1)
#define   ENABLE		BIT(0)

union ftgmac100_dma_addr {
	dma_addr_t addr;
	struct {
		u32 lo;
		u32 hi;
	};
};

int aspeed_mac_reset(struct mac_s *obj)
{
	void *base = obj->device->base;
	u32 maccr;

	writel(MACCR_SW_RST, base + MACCR);

	return readl_poll_sleep_timeout(base + MACCR, maccr, !(maccr & MACCR_SW_RST),
					ASPEED_MAC_INTERVAL_US, ASPEED_MAC_TIMEOUT_US);
}

int aspeed_mac_init_tx_desc(struct mac_s *obj)
{
	int i;
	dma_addr_t des_start, des_end;

	obj->txptr = 0;

	for (i = 0; i < obj->n_txdes; i++) {
		obj->txdes[i].txdes3 = 0;
		obj->txdes[i].txdes1 = 0;
		obj->txdes[i].txdes0 = 0;
	}
	obj->txdes[obj->n_txdes - 1].txdes0 = MAC_TXDES0_EDOTR;

	des_start = (dma_addr_t)&obj->txdes[0].txdes0;
	des_end = (dma_addr_t)&obj->txdes[obj->n_txdes - 1].txdes0 + sizeof(struct mac_txdes_s);
	debug("%s: flush dcache %llx to %llx\n", __func__, des_start, des_end);
	flush_dcache_range(des_start, des_end);

	return 0;
}

int aspeed_mac_init_rx_desc(struct mac_s *obj)
{
	int i;
	dma_addr_t des_start, des_end;
	union ftgmac100_dma_addr dma_addr = { .hi = 0, .lo = 0 };

	obj->rxptr = 0;

	for (i = 0; i < obj->n_rxdes; i++) {
		dma_addr.addr = TO_PHY_ADDR((dma_addr_t)obj->rx_pkt_buf[i]);
		obj->rxdes[i].rxdes2 = FIELD_PREP(MAC_RXDES2_RXBUF_BADR_HI, dma_addr.hi);
		obj->rxdes[i].rxdes3 = dma_addr.lo;
		obj->rxdes[i].rxdes0 = 0;
		obj->rxdes[i].rxdes1 = 0;
	}
	obj->rxdes[obj->n_rxdes - 1].rxdes0 = MAC_RXDES0_EDORR;

	des_start = (dma_addr_t)&obj->rxdes[0].rxdes0;
	des_end = (dma_addr_t)&obj->rxdes[obj->n_rxdes - 1].rxdes0 + sizeof(struct mac_rxdes_s);
	debug("%s: flush dcache %llx to %llx\n", __func__, des_start, des_end);
	flush_dcache_range(des_start, des_end);

	return 0;
}

void aspeed_mac_set_sgmii(struct mac_s *obj)
{
#if defined(ASPEED_AST2700)
	writel(0x0000012B, AST_IO_PLDA1_BASE + 0x268);
	writel(0, AST_SGMII_BASE + SGMII_MODE);
	writel(0, AST_SGMII_BASE + SGMII_CFG);
	writel(RESET | POWER_DOWN, AST_SGMII_BASE + SGMII_CFG);
	writel(AN_ENABLE, AST_SGMII_BASE + SGMII_CFG);
	if (obj->phy->speed == 1000)
		writel(0x5, AST_SGMII_BASE + SGMII_FIFO_DELAY_THREHOLD);
	else
		writel(0xa, AST_SGMII_BASE + SGMII_FIFO_DELAY_THREHOLD);
	writel(TX_DEEMPH_3_5DB, AST_SGMII_BASE + SGMII_PHY_PIPE_CTL);
	writel(ENABLE, AST_SGMII_BASE + SGMII_MODE);
	if (obj->phy->phy_mode != PHY_LOOPBACK_OFF) {
		mdelay(500);
		net_enable_mdio_pin(obj->phy->mdio->device->dev_id - ASPEED_DEV_MDIO0);
		phy_init(obj->phy);
	}
#endif
}

int aspeed_mac_init(struct mac_s *obj)
{
	void *base = obj->device->base;
	struct aspeed_mac_priv_s *priv = obj->device->private;
	union ftgmac100_dma_addr dma_addr = {.hi = 0, .lo = 0};
	u32 reg, dblac, desc_size;
	int rx_buf_size = priv->max_rx_packet_size;

	aspeed_reset_deassert(obj->device);
	aspeed_clk_enable(obj->device);

	aspeed_mdio_init(obj->phy->mdio);
	aspeed_mac_reset(obj);

	if (obj->is_sgmii)
		aspeed_mac_set_sgmii(obj);

	/* set MAC address */
	reg = obj->mac_addr[0] << 8 | obj->mac_addr[1];
	writel(reg, base + MADR);
	reg = ((u32)obj->mac_addr[2] << 24) | ((u32)obj->mac_addr[3] << 16) |
	      ((u32)obj->mac_addr[4] << 8) | ((u32)obj->mac_addr[5]);
	writel(reg, base + LADR);

	/* disable interrupt */
	writel(0, base + IER);

	/* TODO: set descriptor bases */
	dma_addr.addr = TO_PHY_ADDR((dma_addr_t)obj->txdes);
	writel(dma_addr.lo, base + TXR_BADR);
	writel(dma_addr.hi, base + TXR_BADR_HI);

	dma_addr.addr = TO_PHY_ADDR((dma_addr_t)obj->rxdes);
	writel(dma_addr.lo, base + RXR_BADR);
	writel(dma_addr.hi, base + RXR_BADR_HI);

	aspeed_mac_init_tx_desc(obj);
	aspeed_mac_init_rx_desc(obj);

	/* set RX polling */
	reg = FIELD_PREP(APTC_RPOLL_CNT, 0x1);
	writel(reg, base + APTC);

	/* default receive buffer size = 0x600 (1536) */
	if (rx_buf_size == 0)
		rx_buf_size = 0x600;
	writel(rx_buf_size, base + RBSR);

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
	      FIELD_PREP(MACCR_RX_RUNT, 1) | FIELD_PREP(MACCR_JUMBO_LF, 1) |
	      FIELD_PREP(MACCR_RX_BROADPKT_EN, 1);
#if defined(ASPEED_AST2700)
	/* AST2700 SGMII ignore this bit */
	if (!obj->is_rgmii)
		reg |= FIELD_PREP(MACCR_RMII_EN, 1);
#endif
	if (obj->phy->speed == 1000) {
		reg |= FIELD_PREP(MACCR_GMAC_MODE, 1) |
		       FIELD_PREP(MACCR_SPEED_100, 1);
	} else if (obj->phy->speed == 100) {
		reg |= FIELD_PREP(MACCR_SPEED_100, 1);
	}
	if (obj->rx_vlan_remove)
		reg |= FIELD_PREP(MACCR_RM_VLAN, 1);
	writel(reg, base + MACCR);

	/* TODO: PHY start */
	obj->device->init = 1;

	return 0;
}

int aspeed_mac_set_loopback(struct mac_s *obj, u8 enable)
{
	u32 fear = readl(obj->device->base + FEAR);

	if (enable)
		fear |= BIT(30);
	else
		fear &= ~BIT(30);

	writel(fear, obj->device->base + FEAR);

	return 0;
}

int aspeed_mac_set_sgmii_loopback(struct mac_s *obj, u8 enable)
{
#if defined(ASPEED_AST2700)
	u32 cfg = readl(AST_SGMII_BASE + SGMII_CFG);

	if (!obj->is_sgmii)
		return 0;

	if (enable)
		cfg |= SGMII_LOOPBACK;
	else
		cfg &= ~SGMII_LOOPBACK;

	writel(cfg, AST_SGMII_BASE + SGMII_CFG);
#endif
	return 0;
}

int aspeed_mac_txpkt_add(struct mac_s *obj, void *packet, int length, struct test_s *test_obj)
{
	struct mac_txdes_s *curr_txdes = &obj->txdes[obj->txptr];
	union ftgmac100_dma_addr dma_addr = { .hi = 0, .lo = 0 };
	dma_addr_t des_start, des_end;

	des_start = (dma_addr_t)curr_txdes;
	des_end = des_start + sizeof(*curr_txdes);
	invalidate_dcache_range(des_start, des_end);

	/* current description is occupied */
	if (curr_txdes->txdes0 & MAC_TXDES0_TXDMA_OWN)
		return FAIL_BUSY;

	dma_addr.addr = TO_PHY_ADDR((dma_addr_t)packet);
	curr_txdes->txdes2 = FIELD_PREP(MAC_TXDES2_TXBUF_BADR_HI, dma_addr.hi);
	curr_txdes->txdes3 = dma_addr.lo;
	curr_txdes->txdes0 |= MAC_TXDES0_FTS | MAC_TXDES0_LTS |
			      MAC_TXDES0_TXBUF_SIZE(length) |
			      MAC_TXDES0_TXDMA_OWN;
	curr_txdes->txdes1 = obj->txdes1;
	if (obj->txdes1 & TXDESC1_INS_VLAN)
		curr_txdes->txdes1 = obj->txdes1 | test_obj->vlan.tci[obj->txptr];

	debug("%s: txptr=%d, obj->txdes=%p, curr_txdes->txdes0=%p\n", __func__, obj->txptr,
	      obj->txdes, curr_txdes);
	debug("packet address = %p, hi: %08x, lo: %08x\n", packet, dma_addr.hi, dma_addr.lo);
	debug("txdes0=%08x, txdes2=%08x, txdes3=%08x\n", curr_txdes->txdes0, curr_txdes->txdes2,
	      curr_txdes->txdes3);

	flush_dcache_range((dma_addr_t)packet, (dma_addr_t)packet + length);

	debug("%s: flush dcache %llx to %llx\n", __func__, des_start, des_end);
	flush_dcache_range(des_start, des_end);

	obj->txptr = (obj->txptr + 1) & (obj->n_txdes - 1);
	return 0;
}

int aspeed_mac_xmit(struct mac_s *obj)
{
	struct mac_txdes_s *last_txdes;
	u32 last;
	dma_addr_t des_start, des_end;

	writel(1, obj->device->base + TXPD);

	/* polling the last desc */
	last = (obj->txptr - 1) & (obj->n_txdes - 1);
	last_txdes = &obj->txdes[last];
	des_start = (dma_addr_t)last_txdes;
	des_end = des_start + sizeof(*last_txdes);
	debug("%s: invalidate dcache %llx to %llx\n", __func__, des_start, des_end);
	do {
		mdelay(10);
		invalidate_dcache_range(des_start, des_end);
	} while (last_txdes->txdes0 & MAC_TXDES0_TXDMA_OWN);

	return 0;
}

int aspeed_mac_recv(struct mac_s *obj, void **packet, u32 *rxlen)
{
	struct mac_rxdes_s *curr_rxdes = &obj->rxdes[obj->rxptr];
	union ftgmac100_dma_addr dma_addr = { .lo = 0, .hi = 0 };
	dma_addr_t des_start, des_end;

	des_start = (dma_addr_t)curr_rxdes;
	des_end = des_start + sizeof(*curr_rxdes);
	debug("%s: invalidate dcache %llx to %llx\n", __func__, des_start, des_end);
	invalidate_dcache_range(des_start, des_end);

	if (!(curr_rxdes->rxdes0 & MAC_RXDES0_RXPKT_RDY)) {
		debug("MAC RX timeout, ISR %08x\n", readl(obj->device->base + ISR));
		writel(readl(obj->device->base + ISR), obj->device->base + ISR);
		return FAIL_TIMEOUT;
	}

	debug("Got RX packet %08x, ISR %08x\n", curr_rxdes->rxdes0, readl(obj->device->base + ISR));
	writel(readl(obj->device->base + ISR), obj->device->base + ISR);

	if (curr_rxdes->rxdes0 & MAC_RXDES0_ANY_ERROR) {
		debug("MAC RX error %08x\n", curr_rxdes->rxdes0);
		return FAIL_MAC_RX_ERROR;
	}

	dma_addr.hi = FIELD_GET(MAC_RXDES2_RXBUF_BADR_HI, curr_rxdes->rxdes2);
	dma_addr.lo = curr_rxdes->rxdes3;
	*rxlen = MAC_RXDES0_VDBC(curr_rxdes->rxdes0);
	*packet = (u8 *)TO_VIR_ADDR(dma_addr.addr);

	des_start = (dma_addr_t)*packet;
	des_end = des_start + *rxlen;
	debug("%s: invalidate dcache %llx to %llx\n", __func__, des_start, des_end);
	invalidate_dcache_range(des_start, des_end);

	obj->rxptr = (obj->rxptr + 1) & (obj->n_rxdes - 1);

	return 0;
}

int aspeed_mac_set_speed(struct mac_s *obj, u32 speed)
{
	u32 maccr = readl(obj->device->base + MACCR);

	maccr &= ~(MACCR_GMAC_MODE | MACCR_SPEED_100);

	if (speed == 1000)
		maccr |= MACCR_GMAC_MODE | MACCR_SPEED_100;
	else if (speed == 100)
		maccr |= MACCR_SPEED_100;

	writel(maccr, obj->device->base + MACCR);

	return 0;
}

void aspeed_mac_reg_dump(struct mac_s *obj)
{
	u32 reg[64] = { 0 };

	for (int i = 0; i < 64; i++)
		reg[i] = readl(obj->device->base + (i * 4));

	print_hex_dump("", DUMP_PREFIX_OFFSET, 16, 4, reg, sizeof(u32) * 64, false);
}

void ast2705_clk_set_rmii_delay(struct mac_s *obj, int speed, u32 tx, u32 rx)
{
	void __iomem *base = obj->device->base + RGMII_DLY_SEL_1G;
	u32 reg;

	reg = readl(base);
	reg &= ~(RMII_TX_ALIGN_CLK_FALL | RX_CLK_IO_DLY_SEL);
	if (tx)
		reg |= RMII_TX_ALIGN_CLK_FALL;
	reg |= FIELD_PREP(RX_CLK_IO_DLY_SEL, rx);
	writel(reg, base);
}

void ast2705_clk_get_rmii_delay(struct mac_s *obj, int speed, u32 *tx, u32 *rx)
{
	void __iomem *base = obj->device->base + RGMII_DLY_SEL_1G;
	u32 reg;

	reg = readl(base);
	*tx = !!(reg & RMII_TX_ALIGN_CLK_FALL);
	*rx = FIELD_GET(RX_CLK_IO_DLY_SEL, reg);
}

void ast2705_clk_set_rgmii_delay(struct mac_s *obj, int speed, u32 tx, u32 rx)
{
	void __iomem *base = obj->device->base;
	u32 reg;

	switch (speed) {
	case 100:
		base += RGMII_DLY_SEL_100M;
		break;
	case 10:
		base += RGMII_DLY_SEL_10M;
		break;
	case 1000:
	default:
		base += RGMII_DLY_SEL_1G;
		break;
	}

	reg = readl(base);
	reg &= ~(TX_CLK_IO_DLY_SEL | RX_CLK_IO_DLY_SEL);
	reg |= FIELD_PREP(TX_CLK_IO_DLY_SEL, tx);
	reg |= FIELD_PREP(RX_CLK_IO_DLY_SEL, rx);
	writel(reg, base);
}

void ast2705_clk_get_rgmii_delay(struct mac_s *obj, int speed, u32 *tx, u32 *rx)
{
	void __iomem *base = obj->device->base;
	u32 reg;

	switch (speed) {
	case 100:
		base += RGMII_DLY_SEL_100M;
		break;
	case 10:
		base += RGMII_DLY_SEL_10M;
		break;
	case 1000:
	default:
		base += RGMII_DLY_SEL_1G;
		break;
	}

	reg = readl(base);
	*tx = FIELD_GET(TX_CLK_IO_DLY_SEL, reg);
	*rx = FIELD_GET(RX_CLK_IO_DLY_SEL, reg);
}

void aspeed_mac_set_mcast_hash(struct mac_s *obj, u32 ht0, u32 ht1)
{
	writel(ht0, obj->device->base + MAHT0);
	writel(ht1, obj->device->base + MAHT1);
}

void aspeed_mac_enable_all_mcast(struct mac_s *obj, bool enable)
{
	u32 maccr = readl(obj->device->base + MACCR);

	maccr &= ~(MACCR_RX_HT_EN | MACCR_RX_MULTIPKT_EN);
	if (enable)
		maccr |= MACCR_RX_MULTIPKT_EN;
	writel(maccr, obj->device->base + MACCR);
}

void aspeed_mac_enable_hash_mcast(struct mac_s *obj, bool enable)
{
	u32 maccr = readl(obj->device->base + MACCR);

	maccr &= ~(MACCR_RX_HT_EN | MACCR_RX_MULTIPKT_EN);
	if (enable)
		maccr |= MACCR_RX_HT_EN;
	writel(maccr, obj->device->base + MACCR);
}
