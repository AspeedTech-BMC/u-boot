// SPDX-License-Identifier: GPL-2.0+
/*
 * Motorcomm YT8521S/YT8531S PHY driver.
 *
 * Copyright (C) 2024 Motorcomm Technology Co., Ltd.
 */
#include <common.h>
#include <malloc.h>
#include <phy.h>
#include <linux/bitfield.h>
#include <linux/errno.h>
#include <dm/ofnode.h>

/* Extended Register's Address Offset Register */
#define YTPHY_PAGE_SELECT			0x1E

/* Extended Register's Data Register */
#define YTPHY_PAGE_DATA				0x1F

#define YTPHY_DTS_OUTPUT_CLK_DIS		0
#define YTPHY_DTS_OUTPUT_CLK_25M		25000000
#define YTPHY_DTS_OUTPUT_CLK_125M		125000000

#define YTPHY_SYNCE_CFG_REG			0xA012
#define YT8521S_SCR_SYNCE_ENABLE		BIT(5)
#define YT8531S_SCR_SYNCE_ENABLE		BIT(6)
/* 1b0 output 25m clock   *default*
 * 1b1 output 125m clock
 */
#define YT8521S_SCR_CLK_FRE_SEL_125M		BIT(3)
#define YT8521S_SCR_CLK_SRC_MASK		GENMASK(2, 1)
#define YT8521S_SCR_CLK_SRC_PLL_125M		0
#define YT8521S_SCR_CLK_SRC_UTP_RX		1
#define YT8521S_SCR_CLK_SRC_SDS_RX		2
#define YT8521S_SCR_CLK_SRC_REF_25M		3
/* 1b0 output 25m clock   *default*
 * 1b1 output 125m clock
 */
#define YT8531S_SCR_CLK_FRE_SEL_125M		BIT(4)
#define YT8531S_SCR_CLK_SRC_MASK		GENMASK(3, 1)
#define YT8531S_SCR_CLK_SRC_PLL_125M		0
#define YT8531S_SCR_CLK_SRC_UTP_RX		1
#define YT8531S_SCR_CLK_SRC_SDS_RX		2
#define YT8531S_SCR_CLK_SRC_CLOCK_FROM_DIGITAL	3
#define YT8531S_SCR_CLK_SRC_REF_25M		4
#define YT8531S_SCR_CLK_SRC_SSC_25M		5

#define YT8531S_RGMII_CONFIG1_REG		0xA003
#define YT8531S_RC1R_RX_DELAY_MASK		GENMASK(13, 10)
#define YT8531S_RC1R_FE_TX_DELAY_MASK		GENMASK(7, 4)
#define YT8531S_RC1R_GE_TX_DELAY_MASK		GENMASK(3, 0)
#define YT8531S_RC1R_RGMII_0_000_PS		0
#define YT8531S_RC1R_RGMII_0_150_PS		1
#define YT8531S_RC1R_RGMII_0_300_PS		2
#define YT8531S_RC1R_RGMII_0_450_PS		3
#define YT8531S_RC1R_RGMII_0_600_PS		4
#define YT8531S_RC1R_RGMII_0_750_PS		5
#define YT8531S_RC1R_RGMII_0_900_PS		6
#define YT8531S_RC1R_RGMII_1_050_PS		7
#define YT8531S_RC1R_RGMII_1_200_PS		8
#define YT8531S_RC1R_RGMII_1_350_PS		9
#define YT8531S_RC1R_RGMII_1_500_PS		10
#define YT8531S_RC1R_RGMII_1_650_PS		11
#define YT8531S_RC1R_RGMII_1_800_PS		12
#define YT8531S_RC1R_RGMII_1_950_PS		13
#define YT8531S_RC1R_RGMII_2_100_PS		14
#define YT8531S_RC1R_RGMII_2_250_PS		15

/* Phy gmii clock gating Register */
#define YT8531S_CLOCK_GATING_REG		0xC
#define YT8531S_CGR_RX_CLK_EN			BIT(12)

/* Specific Status Register */
#define YTPHY_SPECIFIC_STATUS_REG		0x11
#define YTPHY_SSR_DUPLEX_MASK			BIT(13)
#define YTPHY_SSR_DUPLEX_SHIFT			13
#define YTPHY_SSR_SPEED_MASK			((0x3 << 14) | BIT(9))
#define YTPHY_SSR_SPEED_10M			((0x0 << 14))
#define YTPHY_SSR_SPEED_100M			((0x1 << 14))
#define YTPHY_SSR_SPEED_1000M			((0x2 << 14))

#define YT8531S_EXTREG_SLEEP_CONTROL1_REG	0x27
#define YT8531S_ESC1R_SLEEP_SW			BIT(15)
#define YT8531S_ESC1R_PLLON_SLP			BIT(14)

#define YT8531S_CHIP_CONFIG_REG			0xA001
#define YT8531S_CCR_SW_RST			BIT(15)
/* 1b0 disable 1.9ns rxc clock delay  *default*
 * 1b1 enable 1.9ns rxc clock delay
 */
#define YT8531S_CCR_RXC_DLY_EN			BIT(8)
#define YT8531S_CCR_RXC_DLY_1_900_PS		1900

/* bits in struct ytphy_plat_priv->flag */
#define AUTO_SLEEP_DISABLED			BIT(0)
#define KEEP_PLL_ENABLED			BIT(1)

struct ytphy_plat_priv {
	u32 rx_delay_ps;
	u32 tx_delay_ps;
	u32 clk_out_frequency;
	u32 flag;
};

/**
 * struct ytphy_cfg_reg_map - map a config value to a register value
 * @cfg: value in device configuration
 * @reg: value in the register
 */
struct ytphy_cfg_reg_map {
	u32 cfg;
	u32 reg;
};

static const struct ytphy_cfg_reg_map ytphy_rgmii_delays[] = {
	/* for tx delay / rx delay with YT8531S_CCR_RXC_DLY_EN is not set. */
	{ 0,	YT8531S_RC1R_RGMII_0_000_PS },
	{ 150,	YT8531S_RC1R_RGMII_0_150_PS },
	{ 300,	YT8531S_RC1R_RGMII_0_300_PS },
	{ 450,	YT8531S_RC1R_RGMII_0_450_PS },
	{ 600,	YT8531S_RC1R_RGMII_0_600_PS },
	{ 750,	YT8531S_RC1R_RGMII_0_750_PS },
	{ 900,	YT8531S_RC1R_RGMII_0_900_PS },
	{ 1050,	YT8531S_RC1R_RGMII_1_050_PS },
	{ 1200,	YT8531S_RC1R_RGMII_1_200_PS },
	{ 1350,	YT8531S_RC1R_RGMII_1_350_PS },
	{ 1500,	YT8531S_RC1R_RGMII_1_500_PS },
	{ 1650,	YT8531S_RC1R_RGMII_1_650_PS },
	{ 1800,	YT8531S_RC1R_RGMII_1_800_PS },
	{ 1950,	YT8531S_RC1R_RGMII_1_950_PS },	/* default tx/rx delay */
	{ 2100,	YT8531S_RC1R_RGMII_2_100_PS },
	{ 2250,	YT8531S_RC1R_RGMII_2_250_PS },

	/* only for rx delay with YT8531S_CCR_RXC_DLY_EN is set. */
	{ 0    + YT8531S_CCR_RXC_DLY_1_900_PS,	YT8531S_RC1R_RGMII_0_000_PS },
	{ 150  + YT8531S_CCR_RXC_DLY_1_900_PS,	YT8531S_RC1R_RGMII_0_150_PS },
	{ 300  + YT8531S_CCR_RXC_DLY_1_900_PS,	YT8531S_RC1R_RGMII_0_300_PS },
	{ 450  + YT8531S_CCR_RXC_DLY_1_900_PS,	YT8531S_RC1R_RGMII_0_450_PS },
	{ 600  + YT8531S_CCR_RXC_DLY_1_900_PS,	YT8531S_RC1R_RGMII_0_600_PS },
	{ 750  + YT8531S_CCR_RXC_DLY_1_900_PS,	YT8531S_RC1R_RGMII_0_750_PS },
	{ 900  + YT8531S_CCR_RXC_DLY_1_900_PS,	YT8531S_RC1R_RGMII_0_900_PS },
	{ 1050 + YT8531S_CCR_RXC_DLY_1_900_PS,	YT8531S_RC1R_RGMII_1_050_PS },
	{ 1200 + YT8531S_CCR_RXC_DLY_1_900_PS,	YT8531S_RC1R_RGMII_1_200_PS },
	{ 1350 + YT8531S_CCR_RXC_DLY_1_900_PS,	YT8531S_RC1R_RGMII_1_350_PS },
	{ 1500 + YT8531S_CCR_RXC_DLY_1_900_PS,	YT8531S_RC1R_RGMII_1_500_PS },
	{ 1650 + YT8531S_CCR_RXC_DLY_1_900_PS,	YT8531S_RC1R_RGMII_1_650_PS },
	{ 1800 + YT8531S_CCR_RXC_DLY_1_900_PS,	YT8531S_RC1R_RGMII_1_800_PS },
	{ 1950 + YT8531S_CCR_RXC_DLY_1_900_PS,	YT8531S_RC1R_RGMII_1_950_PS },
	{ 2100 + YT8531S_CCR_RXC_DLY_1_900_PS,	YT8531S_RC1R_RGMII_2_100_PS },
	{ 2250 + YT8531S_CCR_RXC_DLY_1_900_PS,	YT8531S_RC1R_RGMII_2_250_PS }
};

static u32 ytphy_get_delay_reg_value(struct phy_device *phydev,
				     u32 val,
				     u16 *rxc_dly_en)
{
	int tb_size = ARRAY_SIZE(ytphy_rgmii_delays);
	int tb_size_half = tb_size / 2;
	int i;

	/* when rxc_dly_en is NULL, it is get the delay for tx, only half of
	 * tb_size is valid.
	 */
	if (!rxc_dly_en)
		tb_size = tb_size_half;

	for (i = 0; i < tb_size; i++) {
		if (ytphy_rgmii_delays[i].cfg == val) {
			if (rxc_dly_en && i < tb_size_half)
				*rxc_dly_en = 0;
			return ytphy_rgmii_delays[i].reg;
		}
	}

	pr_warn("Unsupported value %d, using default (%u)\n",
		val, YT8531S_RC1R_RGMII_1_950_PS);

	/* when rxc_dly_en is not NULL, it is get the delay for rx.
	 * The rx default in dts and ytphy_rgmii_clk_delay_config is 1950 ps,
	 * so YT8531S_CCR_RXC_DLY_EN should not be set.
	 */
	if (rxc_dly_en)
		*rxc_dly_en = 0;

	return YT8531S_RC1R_RGMII_1_950_PS;
}

static int ytphy_write_ext(struct phy_device *phydev, u16 regnum, u16 val)
{
	int ret;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, YTPHY_PAGE_SELECT, regnum);
	if (ret < 0)
		return ret;

	return phy_write(phydev, MDIO_DEVAD_NONE, YTPHY_PAGE_DATA, val);
}

static int ytphy_read_ext(struct phy_device *phydev, u16 regnum)
{
	int ret;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, YTPHY_PAGE_SELECT, regnum);
	if (ret < 0)
		return ret;

	return phy_read(phydev, MDIO_DEVAD_NONE, YTPHY_PAGE_DATA);
}

static int ytphy_rgmii_clk_delay_config(struct phy_device *phydev)
{
	struct ytphy_plat_priv *priv = phydev->priv;
	u16 rxc_dly_en = YT8531S_CCR_RXC_DLY_EN;
	u32 rx_reg, tx_reg;
	u16 mask, val = 0;
	u16 reg_val;
	int ret;

	rx_reg = ytphy_get_delay_reg_value(phydev, priv->rx_delay_ps,
					   &rxc_dly_en);
	tx_reg = ytphy_get_delay_reg_value(phydev, priv->tx_delay_ps,
					   NULL);

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		rxc_dly_en = 0;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		val |= FIELD_PREP(YT8531S_RC1R_RX_DELAY_MASK, rx_reg);
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		rxc_dly_en = 0;
		val |= FIELD_PREP(YT8531S_RC1R_GE_TX_DELAY_MASK, tx_reg);
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		val |= FIELD_PREP(YT8531S_RC1R_RX_DELAY_MASK, rx_reg) |
		       FIELD_PREP(YT8531S_RC1R_GE_TX_DELAY_MASK, tx_reg);
		break;
	default: /* do not support other modes */
		return -EOPNOTSUPP;
	}

	reg_val = ytphy_read_ext(phydev, YT8531S_CHIP_CONFIG_REG);
	reg_val &= ~YT8531S_CCR_RXC_DLY_EN;
	reg_val |= rxc_dly_en;
	ret = ytphy_write_ext(phydev, YT8531S_CHIP_CONFIG_REG, reg_val);
	if (ret < 0)
		return ret;

	/* Generally, it is not necessary to adjust YT8531S_RC1R_FE_TX_DELAY */
	mask = YT8531S_RC1R_RX_DELAY_MASK | YT8531S_RC1R_GE_TX_DELAY_MASK;
	reg_val = ytphy_read_ext(phydev, YT8531S_RGMII_CONFIG1_REG);
	reg_val &= ~mask;
	reg_val |= val;

	return ytphy_write_ext(phydev, YT8531S_RGMII_CONFIG1_REG, reg_val);
}

static void ytphy_dt_parse(struct phy_device *phydev)
{
	struct ytphy_plat_priv *priv = phydev->priv;

	priv->clk_out_frequency =
		ofnode_read_u32_default(phydev->node,
					"motorcomm,clk-out-frequency-hz",
					YTPHY_DTS_OUTPUT_CLK_DIS);
	priv->rx_delay_ps =
		ofnode_read_u32_default(phydev->node,
					"rx-internal-delay-ps",
					YT8531S_RC1R_RGMII_1_950_PS);
	priv->tx_delay_ps =
		ofnode_read_u32_default(phydev->node,
					"tx-internal-delay-ps",
					YT8531S_RC1R_RGMII_1_950_PS);

	if (ofnode_read_bool(phydev->node, "motorcomm,auto-sleep-disabled"))
		priv->flag |= AUTO_SLEEP_DISABLED;

	if (ofnode_read_bool(phydev->node, "motorcomm,keep-pll-enabled"))
		priv->flag |= KEEP_PLL_ENABLED;
}

static int ytphy_parse_status(struct phy_device *phydev)
{
	int speed, speed_mode;
	int val;

	val = phy_read(phydev, MDIO_DEVAD_NONE, YTPHY_SPECIFIC_STATUS_REG);
	if (val < 0)
		return val;

	speed_mode = val & YTPHY_SSR_SPEED_MASK;
	switch (speed_mode) {
	case YTPHY_SSR_SPEED_1000M:
		speed = SPEED_1000;
		break;
	case YTPHY_SSR_SPEED_100M:
		speed = SPEED_100;
		break;
	case YTPHY_SSR_SPEED_10M:
		speed = SPEED_10;
		break;
	default:
		pr_warn("UNKNOWN SPEED\n");
		return -EINVAL;
	}

	phydev->speed = speed;
	phydev->duplex = (val & YTPHY_SSR_DUPLEX_MASK) >>
				YTPHY_SSR_DUPLEX_SHIFT;

	return 0;
}

static int yt8521s_config(struct phy_device *phydev)
{
	struct ytphy_plat_priv *priv = phydev->priv;
	u16 mask, val;
	u16 reg_val;
	int ret;

	ret = genphy_config_aneg(phydev);
	if (ret < 0)
		return ret;

	ytphy_dt_parse(phydev);
	switch (priv->clk_out_frequency) {
	case YTPHY_DTS_OUTPUT_CLK_DIS:
		mask = YT8521S_SCR_SYNCE_ENABLE;
		val = 0;
		break;
	case YTPHY_DTS_OUTPUT_CLK_25M:
		mask = YT8521S_SCR_SYNCE_ENABLE | YT8521S_SCR_CLK_SRC_MASK |
			   YT8521S_SCR_CLK_FRE_SEL_125M;
		val = YT8521S_SCR_SYNCE_ENABLE |
			  FIELD_PREP(YT8521S_SCR_CLK_SRC_MASK,
				     YT8521S_SCR_CLK_SRC_REF_25M);
		break;
	case YTPHY_DTS_OUTPUT_CLK_125M:
		mask = YT8521S_SCR_SYNCE_ENABLE | YT8521S_SCR_CLK_SRC_MASK |
			   YT8521S_SCR_CLK_FRE_SEL_125M;
		val = YT8521S_SCR_SYNCE_ENABLE | YT8521S_SCR_CLK_FRE_SEL_125M |
			  FIELD_PREP(YT8521S_SCR_CLK_SRC_MASK,
				     YT8521S_SCR_CLK_SRC_PLL_125M);
		break;
	default:
		pr_warn("Freq err:%u\n", priv->clk_out_frequency);
		return -EINVAL;
	}

	reg_val = ytphy_read_ext(phydev, YTPHY_SYNCE_CFG_REG);
	reg_val &= ~mask;
	reg_val |= val;
	ret = ytphy_write_ext(phydev, YTPHY_SYNCE_CFG_REG, reg_val);
	if (ret < 0)
		return ret;

	ret = ytphy_rgmii_clk_delay_config(phydev);
	if (ret < 0)
		return ret;

	if (priv->flag & AUTO_SLEEP_DISABLED) {
		/* disable auto sleep */
		reg_val = ytphy_read_ext(phydev,
					 YT8531S_EXTREG_SLEEP_CONTROL1_REG);
		reg_val &= ~YT8531S_ESC1R_SLEEP_SW;
		ret = ytphy_write_ext(phydev,
				      YT8531S_EXTREG_SLEEP_CONTROL1_REG,
				      reg_val);
		if (ret < 0)
			return ret;
	}

	if (priv->flag & KEEP_PLL_ENABLED) {
		/* enable RXC clock when no wire plug */
		reg_val = ytphy_read_ext(phydev, YT8531S_CLOCK_GATING_REG);
		reg_val &= ~YT8531S_CGR_RX_CLK_EN;
		ret = ytphy_write_ext(phydev,
				      YT8531S_CLOCK_GATING_REG,
				      reg_val);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int yt8531s_startup(struct phy_device *phydev)
{
	int ret;

	ret = genphy_update_link(phydev);
	if (ret)
		return ret;

	ret = ytphy_parse_status(phydev);
	if (ret)
		return ret;

	return 0;
}

static int yt8531s_config(struct phy_device *phydev)
{
	struct ytphy_plat_priv *priv = phydev->priv;
	u16 mask, val;
	u16 reg_val;
	int ret;

	ret = genphy_config_aneg(phydev);
	if (ret < 0)
		return ret;

	ytphy_dt_parse(phydev);
	switch (priv->clk_out_frequency) {
	case YTPHY_DTS_OUTPUT_CLK_DIS:
		mask = YT8531S_SCR_SYNCE_ENABLE;
		val = 0;
		break;
	case YTPHY_DTS_OUTPUT_CLK_25M:
		mask = YT8531S_SCR_SYNCE_ENABLE | YT8531S_SCR_CLK_SRC_MASK |
			   YT8531S_SCR_CLK_FRE_SEL_125M;
		val = YT8531S_SCR_SYNCE_ENABLE |
			  FIELD_PREP(YT8531S_SCR_CLK_SRC_MASK,
				     YT8531S_SCR_CLK_SRC_REF_25M);
		break;
	case YTPHY_DTS_OUTPUT_CLK_125M:
		mask = YT8531S_SCR_SYNCE_ENABLE | YT8531S_SCR_CLK_SRC_MASK |
			   YT8531S_SCR_CLK_FRE_SEL_125M;
		val = YT8531S_SCR_SYNCE_ENABLE | YT8531S_SCR_CLK_FRE_SEL_125M |
			  FIELD_PREP(YT8531S_SCR_CLK_SRC_MASK,
				     YT8531S_SCR_CLK_SRC_PLL_125M);
		break;
	default:
		pr_warn("Freq err:%u\n", priv->clk_out_frequency);
		return -EINVAL;
	}

	reg_val = ytphy_read_ext(phydev, YTPHY_SYNCE_CFG_REG);
	reg_val &= ~mask;
	reg_val |= val;
	ret = ytphy_write_ext(phydev, YTPHY_SYNCE_CFG_REG, reg_val);
	if (ret < 0)
		return ret;

	ret = ytphy_rgmii_clk_delay_config(phydev);
	if (ret < 0)
		return ret;

	if (priv->flag & AUTO_SLEEP_DISABLED) {
		/* disable auto sleep */
		reg_val = ytphy_read_ext(phydev,
					 YT8531S_EXTREG_SLEEP_CONTROL1_REG);
		reg_val &= ~YT8531S_ESC1R_SLEEP_SW;
		ret = ytphy_write_ext(phydev,
				      YT8531S_EXTREG_SLEEP_CONTROL1_REG,
				      reg_val);
		if (ret < 0)
			return ret;
	}

	if (priv->flag & KEEP_PLL_ENABLED) {
		/* enable RXC clock when no wire plug */
		reg_val = ytphy_read_ext(phydev, YT8531S_CLOCK_GATING_REG);
		reg_val &= ~YT8531S_CGR_RX_CLK_EN;
		ret = ytphy_write_ext(phydev,
				      YT8531S_CLOCK_GATING_REG,
				      reg_val);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int yt8531s_probe(struct phy_device *phydev)
{
	struct ytphy_plat_priv	*priv;

	priv = calloc(1, sizeof(struct ytphy_plat_priv));
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	return 0;
}

static struct phy_driver motorcomm8521S = {
	.name		= "YT8521S Gigabit Ethernet Transceiver",
	.uid		= 0x0000011a,
	.mask		= 0xffffffff,
	.features	= PHY_GBIT_FEATURES,
	.probe		= &yt8531s_probe,
	.config		= &yt8521s_config,
	.startup	= &yt8531s_startup,
	.shutdown	= &genphy_shutdown,
};

static struct phy_driver motorcomm8531S = {
	.name		= "YT8531S Gigabit Ethernet Transceiver",
	.uid		= 0x4f51e91a,
	.mask		= 0xffffffff,
	.features	= PHY_GBIT_FEATURES,
	.probe		= &yt8531s_probe,
	.config		= &yt8531s_config,
	.startup	= &yt8531s_startup,
	.shutdown	= &genphy_shutdown,
};

int phy_yt_init(void)
{
	phy_register(&motorcomm8521S);
	phy_register(&motorcomm8531S);

	return 0;
}
