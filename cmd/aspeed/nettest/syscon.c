// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) ASPEED Technology Inc.
 */
#include "platform.h"
#include "internal.h"

#define log_warn	printf

#define TX_DELAY_1			GENMASK(5, 0)
#define TX_DELAY_2			GENMASK(11, 6)
#define RX_DELAY_1			GENMASK(17, 12)
#define RX_DELAY_2			GENMASK(23, 18)
#define RX_CLK_INV_1			BIT(24)
#define RX_CLK_INV_2			BIT(25)
#define RMII_TX_DATA_AT_FALLING_1	BIT(26)
#define RMII_TX_DATA_AT_FALLING_2	BIT(27)
#define RGMIICK_PAD_DIR			BIT(28)
#define RMII_50M_OE_1			BIT(29)
#define RMII_50M_OE_2			BIT(30)
#define RGMII_125M_O_SEL		BIT(31)

struct mii_reg_info_s {
	u32 group;
	void __iomem *addr;
};

int aspeed_reset_assert(struct aspeed_device_s *device)
{
	struct aspeed_reset_s *reset = device->reset;

	if (reset->reg_assert)
		writel(reset->bits, reset->reg_assert);

	return 0;
}

int aspeed_reset_deassert(struct aspeed_device_s *device)
{
	struct aspeed_reset_s *reset = device->reset;

	if (reset->reg_deassert)
		writel(reset->bits, reset->reg_deassert);

	return 0;
}

int aspeed_clk_enable(struct aspeed_device_s *device)
{
	struct aspeed_clk_s *clk = device->clk;

	if (clk->reg_enable)
		writel(clk->bits, clk->reg_enable);

	return 0;
}

int aspeed_clk_disable(struct aspeed_device_s *device)
{
	struct aspeed_clk_s *clk = device->clk;

	if (clk->reg_disable)
		writel(clk->bits, clk->reg_disable);

	return 0;
}

static int aspeed_clk_get_mii_reg_info(enum aspeed_dev_id macdev, int speed,
				       struct mii_reg_info_s *reg)
{
	u32 offset, group;
	void __iomem *addr;

#if defined(ASPEED_AST2700)
	switch (macdev) {
	case ASPEED_DEV_MAC0:
		group = 0;
		break;
	case ASPEED_DEV_MAC1:
		group = 1;
		break;
	default:
		return -1;
	}
	addr = (void __iomem *)AST_IO_SCU_BASE + 0x390;

	switch (speed) {
	case 10:
		offset = 0x8;
		break;
	case 100:
		offset = 0x4;
		break;
	case 1000:
	default:
		offset = 0;
		break;
	}
#else
	switch (macdev) {
	case ASPEED_DEV_MAC0:
		addr = (void __iomem *)AST_SCU_BASE + 0x340;
		group = 0;
		break;
	case ASPEED_DEV_MAC1:
		addr = (void __iomem *)AST_SCU_BASE + 0x340;
		group = 1;
		break;
	case ASPEED_DEV_MAC2:
		addr = (void __iomem *)AST_SCU_BASE + 0x350;
		group = 0;
		break;
	case ASPEED_DEV_MAC3:
		addr = (void __iomem *)AST_SCU_BASE + 0x350;
		group = 1;
		break;
	default:
		return -1;
	}

	switch (speed) {
	case 10:
		offset = 0xc;
		break;
	case 100:
		offset = 0x8;
		break;
	case 1000:
	default:
		offset = 0;
		break;
	}
#endif
	reg->addr = addr + offset;
	reg->group = group;

	return 0;
}

void aspeed_clk_set_rgmii_delay(struct mac_s *obj, int speed, u32 tx, u32 rx)
{
	enum aspeed_dev_id macdev = obj->device->dev_id;
	struct mii_reg_info_s info;
	u32 reg;
	int ret;

	ret = aspeed_clk_get_mii_reg_info(macdev, speed, &info);
	if (ret)
		return;

	reg = readl(info.addr);
	if (info.group) {
		reg &= ~(TX_DELAY_2 | RX_DELAY_2);
		reg |= FIELD_PREP(TX_DELAY_2, tx) | FIELD_PREP(RX_DELAY_2, rx);
	} else {
		reg &= ~(TX_DELAY_1 | RX_DELAY_1);
		reg |= FIELD_PREP(TX_DELAY_1, tx) | FIELD_PREP(RX_DELAY_1, rx);
	}
	writel(reg, info.addr);
}

void aspeed_clk_get_rgmii_delay(struct mac_s *obj, int speed, u32 *tx, u32 *rx)
{
	enum aspeed_dev_id macdev = obj->device->dev_id;
	struct mii_reg_info_s info;
	u32 reg;
	int ret;

	ret = aspeed_clk_get_mii_reg_info(macdev, speed, &info);
	if (ret)
		return;

	reg = readl(info.addr);

	if (info.group) {
		*tx = FIELD_GET(TX_DELAY_2, reg);
		*rx = FIELD_GET(RX_DELAY_2, reg);
	} else {
		*tx = FIELD_GET(TX_DELAY_1, reg);
		*rx = FIELD_GET(RX_DELAY_1, reg);
	}
}

void aspeed_clk_set_rmii_delay(struct mac_s *obj, int speed, u32 tx, u32 rx)
{
	enum aspeed_dev_id macdev = obj->device->dev_id;
	struct mii_reg_info_s info;
	u32 reg;
	int ret;

	ret = aspeed_clk_get_mii_reg_info(macdev, speed, &info);
	if (ret)
		return;

	info.addr = (void __iomem *)((uintptr_t)info.addr & ~0xF);
	reg = readl(info.addr);
	if (info.group) {
		reg &= ~(RMII_TX_DATA_AT_FALLING_2 | RX_DELAY_2);
		reg |= FIELD_PREP(RX_DELAY_2, rx);
		if (tx)
			reg |= RMII_TX_DATA_AT_FALLING_2;
	} else {
		reg &= ~(RMII_TX_DATA_AT_FALLING_1 | RX_DELAY_1);
		reg |= FIELD_PREP(RX_DELAY_1, rx);
		if (tx)
			reg |= RMII_TX_DATA_AT_FALLING_1;
	}
	writel(reg, info.addr);
}

void aspeed_clk_get_rmii_delay(struct mac_s *obj, int speed, u32 *tx, u32 *rx)
{
	enum aspeed_dev_id macdev = obj->device->dev_id;
	struct mii_reg_info_s info;
	u32 reg;
	int ret;

	ret = aspeed_clk_get_mii_reg_info(macdev, speed, &info);
	if (ret)
		return;

	info.addr = (void __iomem *)((uintptr_t)info.addr & ~0xF);
	reg = readl(info.addr);
	if (info.group) {
		*tx = !!(reg & RMII_TX_DATA_AT_FALLING_2);
		*rx = FIELD_GET(RX_DELAY_2, reg);
	} else {
		*tx = !!(reg & RMII_TX_DATA_AT_FALLING_1);
		*rx = FIELD_GET(RX_DELAY_1, reg);
	}
}
