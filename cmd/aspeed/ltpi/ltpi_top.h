/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (c) 2023 ASPEED Technology Inc.
 */
#ifndef _LPTI_TOP_H_
#define _LPTI_TOP_H_

#include <linux/bitops.h>

/* LTPI PHY control registers */
#define LTPI_PHY_CTRL				0x000
#define   REG_LTPI_PHY_MODE			GENMASK(3, 0)
#define     LTPI_PHY_MODE_CDR_HI_SP		0b1000
#define     LTPI_PHY_MODE_CDR_LO_SP		0b0100
#define     LTPI_PHY_MODE_DDR			0b0010
#define     LTPI_PHY_MODE_SDR			0b0001
#define     LTPI_PHY_MODE_OFF			0b0000
#define LTPI_PLL_CTRL				0x004
#define   REG_LTPI_RX_PHY_CLK_INV		BIT(9)
#define   REG_LTPI_TX_PHY_CLK_INV		BIT(8)
#define   REG_LTPI_PLL_SET			BIT(4)
#define   REG_LTPI_RX_PLL_DIV2			BIT(3)
#define   REG_LTPI_PLL_SELECT			GENMASK(2, 0)
#define     REG_LTPI_PLL_25M			0
/* AST2700A0 */
#define     REG_LTPI_PLL_50M			1
#define     REG_LTPI_PLL_100M			2
#define     REG_LTPI_PLL_200M			3
#define     REG_LTPI_PLL_250M			4
#define     REG_LTPI_PLL_400M			5
#define     REG_LTPI_PLL_800M			6
#define     REG_LTPI_PLL_1G			7
/* AST2700A1 */
#define     REG_LTPI_PLL_LPLL			7

#define LTPI_PHY_ALIGN_CTRL			0x008
#define LTPI_DLL_CTRL				0x00C
#define   REG_LTPI_SW_DLL_RST			BIT(26)
#define   REG_LTPI_FORCE_DLL_RST		BIT(25)
#define   REG_LTPI_DLL_PD			BIT(24)
#define   REG_LTPI_DLL_TSTCTRL			GENMASK(23, 16)
#define   REG_LTPI_DLL_RST_TIMEOUT_A0		GENMASK(15, 0)
#define   REG_LTPI_DLL_CLK_2X			BIT(8)
#define   REG_LTPI_DLL_RST_TIMEOUT		GENMASK(7, 0)
#define LTPI_PHY_HI_SP_CDR_CTRL			0x010
#define   REG_LTPI_SW_HI_SP_CDR_EN		BIT(10)
#define   REG_LTPI_FORCE_HI_SP_CDR_EN		BIT(9)
#define   REG_LTPI_HI_SP_CDR_EN_TIMEOUT_EN	BIT(8)
#define	  LTPI_HI_SP_CDR_EN_TIMEOUT		GENMASK(7, 0)

#define LTPI_PROT_KEY				0x34
#define   LTPI_PROT_KEY_UNLOCK			0x1728aacc

/* LVDS TOP registers */
#define LTPI_LVDS_TX_CTRL			0x000
#define   REG_LTPI_LVDS_TX1_DS1			BIT(22)
#define   REG_LTPI_LVDS_TX1_DS0			BIT(21)
#define   REG_LTPI_LVDS_TX1_IPREE		BIT(20)
#define   REG_LTPI_LVDS_TX1_IPREE_EN		BIT(19)
#define   REG_LTPI_LVDS_TX1_PD			BIT(18)
#define   REG_LTPI_LVDS_TX1_PU			BIT(17)
#define   REG_LTPI_LVDS_TX1_OE			BIT(16)
#define   REG_LTPI_LVDS_TX0_DS1			BIT(6)
#define   REG_LTPI_LVDS_TX0_DS0			BIT(5)
#define   REG_LTPI_LVDS_TX0_IPREE		BIT(4)
#define   REG_LTPI_LVDS_TX0_IPREE_EN		BIT(3)
#define   REG_LTPI_LVDS_TX0_PD			BIT(2)
#define   REG_LTPI_LVDS_TX0_PU			BIT(1)
#define   REG_LTPI_LVDS_TX0_OE			BIT(0)
#define LTPI_LVDS_RX_CTRL			0x004
#define   REG_LTPI_LVDS_RX1_BIAS_EN		BIT(18)
#define   REG_LTPI_LVDS_RX1_ST			BIT(17)
#define   REG_LTPI_LVDS_RX1_IE			BIT(16)
#define   REG_LTPI_LVDS_RX0_BIAS_EN		BIT(2)
#define   REG_LTPI_LVDS_RX0_ST			BIT(1)
#define   REG_LTPI_LVDS_RX0_IE			BIT(0)
#define LTPI_SW_RST				0x008
#define   REG_LTPI_ALL_SW_RST			BIT(31)
#define   REG_LTPI1_SW_RST			BIT(17)
#define   REG_LTPI0_SW_RST			BIT(16)
#define   REG_LTPI_DLL_CTRL_SW_RST		BIT(9)
#define   REG_LTPI_REF_SW_RST			BIT(8)
#define   REG_LTPI1_RX_PHY_SW_RST		BIT(7)
#define   REG_LTPI1_TX_PHY_SW_RST		BIT(6)
#define   REG_LTPI1_RX_MAC_SW_RST		BIT(5)
#define   REG_LTPI1_TX_MAC_SW_RST		BIT(4)
#define   REG_LTPI0_RX_PHY_SW_RST		BIT(3)
#define   REG_LTPI0_TX_PHY_SW_RST		BIT(2)
#define   REG_LTPI0_RX_MAC_SW_RST		BIT(1)
#define   REG_LTPI0_TX_MAC_SW_RST		BIT(0)
#define LTPI_STRAP_VAL				0x00c
#define   REG_LTPI_STRAP_2LTPI_EN		BIT(1)
#define   REG_LTPI_STRAP_1700_EN		BIT(0)
#define LTPI_SW_FORCE_EN			0x010
#define LTPI_SW_FORCE_VAL			0x014
#define   REG_LTPI_SW_FORCE_LVDS_TX_DS_EN	BIT(1)
#define   REG_LTPI_SW_FORCE_1700_EN		BIT(0)
#endif	/* _LPTI_TOP_H_ */
