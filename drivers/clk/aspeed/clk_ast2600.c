// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) ASPEED Technology Inc.
 */

#include <common.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/iopoll.h>
#include <clk-uclass.h>
#include <dm.h>
#include <asm/io.h>
#include <dm/lists.h>
#include <asm/arch/scu_ast2600.h>
#include <dt-bindings/clock/ast2600-clock.h>
#include <dt-bindings/reset/ast2600-reset.h>

/*
 * SCU 80 & 90 clock stop control for MAC controllers
 */
#define SCU_CLKSTOP_MAC1			(20)
#define SCU_CLKSTOP_MAC2			(21)
#define SCU_CLKSTOP_MAC3			(20)
#define SCU_CLKSTOP_MAC4			(21)

/*
 * MAC Clock Delay settings
 */
#define MAC_CLK_RGMII_125M_SRC_SEL		BIT(31)
#define   MAC_CLK_RGMII_125M_SRC_PAD_RGMIICK	0
#define   MAC_CLK_RGMII_125M_SRC_PLL		1
#define MAC_CLK_RMII2_50M_RCLK_O_CTRL		BIT(30)
#define   MAC_CLK_RMII2_50M_RCLK_O_DIS		0
#define   MAC_CLK_RMII2_50M_RCLK_O_EN		1
#define MAC_CLK_RMII1_50M_RCLK_O_CTRL		BIT(29)
#define   MAC_CLK_RMII1_5M_RCLK_O_DIS		0
#define   MAC_CLK_RMII1_5M_RCLK_O_EN		1
#define MAC_CLK_RGMIICK_PAD_DIR			BIT(28)
#define   MAC_CLK_RGMIICK_PAD_DIR_INPUT		0
#define   MAC_CLK_RGMIICK_PAD_DIR_OUTPUT	1
#define MAC_CLK_RMII_TXD_FALLING_2		BIT(27)
#define MAC_CLK_RMII_TXD_FALLING_1		BIT(26)
#define MAC_CLK_RXCLK_INV_2			BIT(25)
#define MAC_CLK_RXCLK_INV_1			BIT(24)
#define MAC_CLK_1G_INPUT_DELAY_2		GENMASK(23, 18)
#define MAC_CLK_1G_INPUT_DELAY_1		GENMASK(17, 12)
#define MAC_CLK_1G_OUTPUT_DELAY_2		GENMASK(11, 6)
#define MAC_CLK_1G_OUTPUT_DELAY_1		GENMASK(5, 0)

#define MAC_CLK_100M_10M_RESERVED		GENMASK(31, 26)
#define MAC_CLK_100M_10M_RXCLK_INV_2		BIT(25)
#define MAC_CLK_100M_10M_RXCLK_INV_1		BIT(24)
#define MAC_CLK_100M_10M_INPUT_DELAY_2		GENMASK(23, 18)
#define MAC_CLK_100M_10M_INPUT_DELAY_1		GENMASK(17, 12)
#define MAC_CLK_100M_10M_OUTPUT_DELAY_2		GENMASK(11, 6)
#define MAC_CLK_100M_10M_OUTPUT_DELAY_1		GENMASK(5, 0)

#define RGMII12_CLK_OUTPUT_DELAY_PS		1000
#define RGMII34_CLK_OUTPUT_DELAY_PS		1600

#define MAC_DEF_DELAY_1G			FIELD_PREP(MAC_CLK_1G_OUTPUT_DELAY_1, 16) |        \
						FIELD_PREP(MAC_CLK_1G_INPUT_DELAY_1, 10) |         \
						FIELD_PREP(MAC_CLK_1G_OUTPUT_DELAY_2, 16) |        \
						FIELD_PREP(MAC_CLK_1G_INPUT_DELAY_2, 10)
#define MAC_DEF_DELAY_100M			FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_1, 16) |  \
						FIELD_PREP(MAC_CLK_100M_10M_INPUT_DELAY_1, 16) |   \
						FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_2, 16) |  \
						FIELD_PREP(MAC_CLK_100M_10M_INPUT_DELAY_2, 16)
#define MAC_DEF_DELAY_10M			FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_1, 16) |  \
						FIELD_PREP(MAC_CLK_100M_10M_INPUT_DELAY_1, 16) |   \
						FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_2, 16) |  \
						FIELD_PREP(MAC_CLK_100M_10M_INPUT_DELAY_2, 16)
#define MAC34_DEF_DELAY_1G			FIELD_PREP(MAC_CLK_1G_OUTPUT_DELAY_1, 8) |         \
						FIELD_PREP(MAC_CLK_1G_INPUT_DELAY_1, 4) |          \
						FIELD_PREP(MAC_CLK_1G_OUTPUT_DELAY_2, 8) |         \
						FIELD_PREP(MAC_CLK_1G_INPUT_DELAY_2, 4)
#define MAC34_DEF_DELAY_100M			FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_1, 8) |   \
						FIELD_PREP(MAC_CLK_100M_10M_INPUT_DELAY_1, 4) |    \
						FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_2, 8) |   \
						FIELD_PREP(MAC_CLK_100M_10M_INPUT_DELAY_2, 4)
#define MAC34_DEF_DELAY_10M			FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_1, 8) |   \
						FIELD_PREP(MAC_CLK_100M_10M_INPUT_DELAY_1, 4) |    \
						FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_2, 8) |   \
						FIELD_PREP(MAC_CLK_100M_10M_INPUT_DELAY_2, 4)

/*
 * SCU 320 & 330 Frequency counters
 */
#define FREQC_CTRL_RESERVED			GENMASK(31, 30)
#define FREQC_CTRL_RESULT			GENMASK(29, 16)
#define FREQC_CTRL_RING_STAGE			GENMASK(15, 9)
#define FREQC_CTRL_PIN_O_CTRL			BIT(8)
#define   FREQC_CTRL_PIN_O_DIS			0
#define   FREQC_CTRL_PIN_O_EN			1
#define FREQC_CTRL_CMP_RESULT			BIT(7)
#define   FREQC_CTRL_CMP_RESULT_FAIL		0
#define   FREQC_CTRL_CMP_RESULT_PASS		1
#define FREQC_CTRL_STATUS			BIT(6)
#define   FREQC_CTRL_STATUS_NOT_FINISHED	0
#define   FREQC_CTRL_STATUS_FINISHED		1
#define FREQC_CTRL_SRC_SEL			GENMASK(5, 2)
#define   FREQC_CTRL_SRC_SEL_HCLK_DIE0		9
#define   FREQC_CTRL_SRC_SEL_DLY32_DIE0		3
#define   FREQC_CTRL_SRC_SEL_HCLK_DIE1		1
#define   FREQC_CTRL_SRC_SEL_DLY32_DIE1		7
#define FREQC_CTRL_OSC_CTRL			BIT(1)
#define   FREQC_CTRL_OSC_DIS			0
#define   FREQC_CTRL_OSC_EN			1
#define FREQC_CTRL_RING_CTRL			BIT(0)
#define   FREQC_CTRL_RING_DIS			0
#define   FREQC_CTRL_RING_EN			1

#define FREQC_RANGE_RESERVED0			GENMASK(31, 30)
#define FREQC_RANGE_LOWER			GENMASK(29, 16)
#define FREQC_RANGE_RESERVED1			GENMASK(15, 14)
#define FREQC_RANGE_UPPER			GENMASK(13, 0)

#define DLY32_NUM_OF_TAPS			32
#define DLY32_AVERAGE_COUNT_LOG2		4
#define DLY32_AVERAGE_COUNT			BIT(DLY32_AVERAGE_COUNT_LOG2)

/*
 * TGMII Clock Duty constants, taken from Aspeed SDK
 */
#define RGMII2_TXCK_DUTY		0x66
#define RGMII1_TXCK_DUTY		0x64
#define D2PLL_DEFAULT_RATE		(250 * 1000 * 1000)
#define CHIP_REVISION_ID		GENMASK(23, 16)

DECLARE_GLOBAL_DATA_PTR;

/*
 * Clock divider/multiplier configuration struct.
 * For H-PLL and M-PLL the formula is
 * (Output Frequency) = CLKIN * ((M + 1) / (N + 1)) / (P + 1)
 * M - Numerator
 * N - Denumerator
 * P - Post Divider
 * They have the same layout in their control register.
 *
 * D-PLL and D2-PLL have extra divider (OD + 1), which is not
 * yet needed and ignored by clock configurations.
 */
union ast2600_pll_reg {
	u32 w;
	struct {
		unsigned int m : 13;		/* bit[12:0]	*/
		unsigned int n : 6;		/* bit[18:13]	*/
		unsigned int p : 4;		/* bit[22:19]	*/
		unsigned int off : 1;		/* bit[23]	*/
		unsigned int bypass : 1;	/* bit[24]	*/
		unsigned int reset : 1;		/* bit[25]	*/
		unsigned int reserved : 6;	/* bit[31:26]	*/
	} b;
};

struct ast2600_pll_cfg {
	union ast2600_pll_reg reg;
	u32 ext_reg;
};

struct ast2600_pll_desc {
	u32 in;
	u32 out;
	struct ast2600_pll_cfg cfg;
};

static const struct ast2600_pll_desc ast2600_pll_lookup[] = {
	{
		.in = AST2600_CLK_IN,
		.out = 400000000,
		.cfg.reg.b.m = 95,
		.cfg.reg.b.n = 2,
		.cfg.reg.b.p = 1,
		.cfg.ext_reg = 0x31,
	}, {
		.in = AST2600_CLK_IN,
		.out = 200000000,
		.cfg.reg.b.m = 127,
		.cfg.reg.b.n = 0,
		.cfg.reg.b.p = 15,
		.cfg.ext_reg = 0x3f,
	}, {
		.in = AST2600_CLK_IN,
		.out = 334000000,
		.cfg.reg.b.m = 667,
		.cfg.reg.b.n = 4,
		.cfg.reg.b.p = 9,
		.cfg.ext_reg = 0x14d,
	}, {
		.in = AST2600_CLK_IN,
		.out = 1000000000,
		.cfg.reg.b.m = 119,
		.cfg.reg.b.n = 2,
		.cfg.reg.b.p = 0,
		.cfg.ext_reg = 0x3d,
	}, {
		.in = AST2600_CLK_IN,
		.out = 50000000,
		.cfg.reg.b.m = 95,
		.cfg.reg.b.n = 2,
		.cfg.reg.b.p = 15,
		.cfg.ext_reg = 0x31,
	},
};

struct mac_delay_config {
	u32 tx_delay_1000;
	u32 rx_delay_1000;
	u32 tx_delay_100;
	u32 rx_delay_100;
	u32 tx_delay_10;
	u32 rx_delay_10;
};

extern u32 ast2600_get_pll_rate(struct ast2600_scu *scu, int pll_idx)
{
	u32 clkin = AST2600_CLK_IN;
	u32 pll_reg = 0;
	unsigned int mult, div = 1;

	switch (pll_idx) {
	case ASPEED_CLK_HPLL:
		pll_reg = readl(&scu->h_pll_param);
		break;
	case ASPEED_CLK_MPLL:
		pll_reg = readl(&scu->m_pll_param);
		break;
	case ASPEED_CLK_DPLL:
		pll_reg = readl(&scu->d_pll_param);
		break;
	case ASPEED_CLK_EPLL:
		pll_reg = readl(&scu->e_pll_param);
		break;
	}
	if (pll_reg & BIT(24)) {
		/* Pass through mode */
		mult = 1;
		div = 1;
	} else {
		union ast2600_pll_reg reg;
		/* F = 25Mhz * [(M + 2) / (n + 1)] / (p + 1)
		 * HPLL Numerator (M) = fix 0x5F when SCU500[10]=1
		 * Fixed 0xBF when SCU500[10]=0 and SCU500[8]=1
		 * SCU200[12:0] (default 0x8F) when SCU510[10]=0 and SCU510[8]=0
		 * HPLL Denumerator (N) =	SCU200[18:13] (default 0x2)
		 * HPLL Divider (P)	 =	SCU200[22:19] (default 0x0)
		 * HPLL Bandwidth Adj (NB) =  fix 0x2F when SCU500[10]=1
		 * Fixed 0x5F when SCU500[10]=0 and SCU500[8]=1
		 * SCU204[11:0] (default 0x31) when SCU500[10]=0 and SCU500[8]=0
		 */
		reg.w = pll_reg;
		if (pll_idx == ASPEED_CLK_HPLL) {
			u32 hwstrap1 = readl(&scu->hwstrap1.hwstrap);

			if (hwstrap1 & BIT(10)) {
				reg.b.m = 0x5F;
			} else {
				if (hwstrap1 & BIT(8))
					reg.b.m = 0xBF;
				/* Otherwise keep default 0x8F */
			}
		}
		mult = (reg.b.m + 1) / (reg.b.n + 1);
		div = (reg.b.p + 1);
	}

	return ((clkin * mult) / div);
}

extern u32 ast2600_get_apll_rate(struct ast2600_scu *scu)
{
	u32 hw_rev = readl(&scu->chip_id1);
	u32 clkin = AST2600_CLK_IN;
	u32 apll_reg = readl(&scu->a_pll_param);
	unsigned int mult, div = 1;

	if (((hw_rev & CHIP_REVISION_ID) >> 16) >= 2) {
		//after A2 version
		if (apll_reg & BIT(24)) {
			/* Pass through mode */
			mult = 1;
			div = 1;
		} else {
			/* F = 25Mhz * [(m + 1) / (n + 1)] / (p + 1) */
			u32 m = apll_reg & 0x1fff;
			u32 n = (apll_reg >> 13) & 0x3f;
			u32 p = (apll_reg >> 19) & 0xf;

			mult = (m + 1);
			div = (n + 1) * (p + 1);
		}
	} else {
		if (apll_reg & BIT(20)) {
			/* Pass through mode */
			mult = 1;
			div = 1;
		} else {
			/* F = 25Mhz * (2-od) * [(m + 2) / (n + 1)] */
			u32 m = (apll_reg >> 5) & 0x3f;
			u32 od = (apll_reg >> 4) & 0x1;
			u32 n = apll_reg & 0xf;

			mult = (2 - od) * (m + 2);
			div = n + 1;
		}
	}

	return ((clkin * mult) / div);
}

static u32 ast2600_a0_axi_ahb_div_table[] = {
	2,
	2,
	3,
	4,
};

static u32 ast2600_a1_axi_ahb_div0_table[] = {
	3,
	2,
	3,
	4,
};

static u32 ast2600_a1_axi_ahb_div1_table[] = {
	3,
	4,
	6,
	8,
};

static u32 ast2600_a1_axi_ahb_default_table[] = {
	3, 4, 3, 4, 2, 2, 2, 2,
};

static u32 ast2600_get_hclk(struct ast2600_scu *scu)
{
	u32 hw_rev = readl(&scu->chip_id1);
	u32 hwstrap1 = readl(&scu->hwstrap1.hwstrap);
	u32 axi_div = 1;
	u32 ahb_div = 0;
	u32 rate = 0;

	if ((hw_rev & CHIP_REVISION_ID) >> 16) {
		//After A0
		if (hwstrap1 & BIT(16)) {
			ast2600_a1_axi_ahb_div1_table[0] =
				ast2600_a1_axi_ahb_default_table[(hwstrap1 >> 8) &
								 0x7] * 2;
			axi_div = 1;
			ahb_div =
				ast2600_a1_axi_ahb_div1_table[(hwstrap1 >> 11) &
							      0x3];
		} else {
			ast2600_a1_axi_ahb_div0_table[0] =
				ast2600_a1_axi_ahb_default_table[(hwstrap1 >> 8) &
								 0x7];
			axi_div = 2;
			ahb_div =
				ast2600_a1_axi_ahb_div0_table[(hwstrap1 >> 11) &
							      0x3];
		}
	} else {
		//A0 : fix axi = hpll / 2
		axi_div = 2;
		ahb_div = ast2600_a0_axi_ahb_div_table[(hwstrap1 >> 11) & 0x3];
	}
	rate = ast2600_get_pll_rate(scu, ASPEED_CLK_HPLL);

	return (rate / axi_div / ahb_div);
}

static u32 ast2600_get_bclk_rate(struct ast2600_scu *scu)
{
	u32 rate;
	u32 bclk_sel = (readl(&scu->clk_sel1) >> 20) & 0x7;

	rate = ast2600_get_pll_rate(scu, ASPEED_CLK_EPLL);

	return (rate / ((bclk_sel + 1) * 4));
}

static u32 ast2600_hpll_pclk1_div_table[] = {
	4, 8, 12, 16, 20, 24, 28, 32,
};

static u32 ast2600_hpll_pclk2_div_table[] = {
	2, 4, 6, 8, 10, 12, 14, 16,
};

static u32 ast2600_get_pclk1(struct ast2600_scu *scu)
{
	u32 clk_sel1 = readl(&scu->clk_sel1);
	u32 apb_div = ast2600_hpll_pclk1_div_table[((clk_sel1 >> 23) & 0x7)];
	u32 rate = ast2600_get_pll_rate(scu, ASPEED_CLK_HPLL);

	return (rate / apb_div);
}

static u32 ast2600_get_pclk2(struct ast2600_scu *scu)
{
	u32 clk_sel4 = readl(&scu->clk_sel4);
	u32 apb_div = ast2600_hpll_pclk2_div_table[((clk_sel4 >> 9) & 0x7)];
	u32 rate = ast2600_get_hclk(scu);

	return (rate / apb_div);
}

static u32 ast2600_get_uxclk_in_rate(struct ast2600_scu *scu)
{
	u32 clk_in = 0;
	u32 uxclk_sel = readl(&scu->clk_sel5);

	uxclk_sel &= 0x3;
	switch (uxclk_sel) {
	case 0:
		clk_in = ast2600_get_apll_rate(scu) / 4;
		break;
	case 1:
		clk_in = ast2600_get_apll_rate(scu) / 2;
		break;
	case 2:
		clk_in = ast2600_get_apll_rate(scu);
		break;
	case 3:
		clk_in = ast2600_get_hclk(scu);
		break;
	}

	return clk_in;
}

static u32 ast2600_get_huxclk_in_rate(struct ast2600_scu *scu)
{
	u32 clk_in = 0;
	u32 huclk_sel = readl(&scu->clk_sel5);

	huclk_sel = ((huclk_sel >> 3) & 0x3);
	switch (huclk_sel) {
	case 0:
		clk_in = ast2600_get_apll_rate(scu) / 4;
		break;
	case 1:
		clk_in = ast2600_get_apll_rate(scu) / 2;
		break;
	case 2:
		clk_in = ast2600_get_apll_rate(scu);
		break;
	case 3:
		clk_in = ast2600_get_hclk(scu);
		break;
	}

	return clk_in;
}

static u32 ast2600_get_uart_uxclk_rate(struct ast2600_scu *scu)
{
	u32 clk_in = ast2600_get_uxclk_in_rate(scu);
	u32 div_reg = readl(&scu->uart_24m_ref_uxclk);
	unsigned int mult, div;

	u32 n = (div_reg >> 8) & 0x3ff;
	u32 r = div_reg & 0xff;

	mult = r;
	div = (n * 2);
	return (clk_in * mult) / div;
}

static u32 ast2600_get_uart_huxclk_rate(struct ast2600_scu *scu)
{
	u32 clk_in = ast2600_get_huxclk_in_rate(scu);
	u32 div_reg = readl(&scu->uart_24m_ref_huxclk);

	unsigned int mult, div;

	u32 n = (div_reg >> 8) & 0x3ff;
	u32 r = div_reg & 0xff;

	mult = r;
	div = (n * 2);
	return (clk_in * mult) / div;
}

static u32 ast2600_get_sdio_clk_rate(struct ast2600_scu *scu)
{
	u32 clkin = 0;
	u32 clk_sel = readl(&scu->clk_sel4);
	u32 div = (clk_sel >> 28) & 0x7;
	u32 hw_rev = readl(&scu->chip_id1);

	if (clk_sel & BIT(8))
		clkin = ast2600_get_apll_rate(scu);
	else
		clkin = ast2600_get_hclk(scu);

	div = (1 + div) * 2;
	if (((hw_rev & GENMASK(23, 16)) >> 16) >= 2)
		div = (div & 0xf) ? div : 1;

	return (clkin / div);
}

static u32 ast2600_get_emmc_clk_rate(struct ast2600_scu *scu)
{
	u32 mmc_clk_src = readl(&scu->clk_sel1);
	u32 clkin;
	u32 clk_sel = readl(&scu->clk_sel1);
	u32 div = (clk_sel >> 12) & 0x7;

	if (mmc_clk_src & BIT(11)) {
		/* emmc clock comes from MPLL */
		clkin = ast2600_get_pll_rate(scu, ASPEED_CLK_MPLL);
		div = (div + 1) * 2;
	} else {
		clkin = ast2600_get_pll_rate(scu, ASPEED_CLK_HPLL);
		div = (div + 1) << 2;
	}

	return (clkin / div);
}

#ifdef CONFIG_SPL_BUILD
static void ast2600_enable_uart_pinmux(struct ast2600_scu *scu, int uart_idx)
{
	switch(uart_idx) {
	case 1:
		scu->pinmux_ctrl7 |= (BIT(7) | BIT(6));
		break;
	case 2:
		scu->pinmux_ctrl7 |= (BIT(14) | BIT(15));
		break;
	case 3:
		scu->pinmux_ctrl6 |= (BIT(28) | BIT(29));
		break;
	case 4:
		scu->pinmux_ctrl4 |= (BIT(14) | BIT(15));
		break;
	case 5:
		/* do nothing*/
		break;
	case 6:
		scu->pinmux_ctrl5 |= (BIT(16) | BIT(17));
		break;
	case 7:
		scu->pinmux_ctrl5 |= (BIT(18) | BIT(19));
		break;
	case 8:
		scu->pinmux_ctrl5 |= (BIT(20) | BIT(21));
		break;
	case 9:
		scu->pinmux_ctrl5 |= (BIT(22) | BIT(23));
		break;
	case 10:
		scu->pinmux_ctrl8 |= (BIT(20) | BIT(21));
		break;
	case 11:
		scu->pinmux_ctrl8 |= (BIT(22) | BIT(23));
		break;
	case 12:
		scu->pinmux_ctrl19 |= (BIT(0) | BIT(1));
		scu->pinmux_ctrl6 &= ~(BIT(0) | BIT(1));
		break;
	case 13:
		scu->pinmux_ctrl19 |= (BIT(2) | BIT(3));
		scu->pinmux_ctrl6 &= ~(BIT(2) | BIT(3));
		break;
	default:
		break;
	}
}
#endif

static u32 ast2600_get_uart_clk_rate(struct ast2600_scu *scu, int uart_idx)
{
	u32 hicr9 = readl(0x1e789098);
	u32 uart_sel = readl(&scu->clk_sel4);
	u32 uart_sel5 = readl(&scu->clk_sel5);
	ulong uart_clk = 0;

#ifdef CONFIG_SPL_BUILD
	ast2600_enable_uart_pinmux(scu, uart_idx);
#endif

	switch (uart_idx) {
	case 1:
	case 2:
	case 3:
	case 4:
		hicr9 &= ~(BIT(uart_idx + 3));
		writel(hicr9, 0x1e789098);
	case 6:
		if (uart_sel & BIT(uart_idx - 1))
			uart_clk = ast2600_get_uart_huxclk_rate(scu);
		else
			uart_clk = ast2600_get_uart_uxclk_rate(scu);
		break;
	case 5: //24mhz is come form usb phy 48Mhz
	{
		u8 uart5_clk_sel = 0;
		//high bit
		if (readl(&scu->misc_ctrl1) & BIT(12))
			uart5_clk_sel = 0x2;
		else
			uart5_clk_sel = 0x0;

		if (readl(&scu->clk_sel2) & BIT(14))
			uart5_clk_sel |= 0x1;

		switch (uart5_clk_sel) {
		case 0:
			uart_clk = 24000000;
			break;
		case 1:
			uart_clk = 192000000;
			break;
		case 2:
			uart_clk = 24000000 / 13;
			break;
		case 3:
			uart_clk = 192000000 / 13;
			break;
		}
	} break;
	case 7:
	case 8:
	case 9:
	case 10:
	case 11:
	case 12:
	case 13:
		if (uart_sel5 & BIT(uart_idx - 1))
			uart_clk = ast2600_get_uart_huxclk_rate(scu);
		else
			uart_clk = ast2600_get_uart_uxclk_rate(scu);
		break;
	}

	return uart_clk;
}

static ulong ast2600_clk_get_rate(struct clk *clk)
{
	struct ast2600_clk_priv *priv = dev_get_priv(clk->dev);
	ulong rate = 0;

	switch (clk->id) {
	case ASPEED_CLK_HPLL:
	case ASPEED_CLK_EPLL:
	case ASPEED_CLK_DPLL:
	case ASPEED_CLK_MPLL:
		rate = ast2600_get_pll_rate(priv->scu, clk->id);
		break;
	case ASPEED_CLK_AHB:
		rate = ast2600_get_hclk(priv->scu);
		break;
	case ASPEED_CLK_APB1:
		rate = ast2600_get_pclk1(priv->scu);
		break;
	case ASPEED_CLK_APB2:
		rate = ast2600_get_pclk2(priv->scu);
		break;
	case ASPEED_CLK_APLL:
		rate = ast2600_get_apll_rate(priv->scu);
		break;
	case ASPEED_CLK_GATE_UART1CLK:
		rate = ast2600_get_uart_clk_rate(priv->scu, 1);
		break;
	case ASPEED_CLK_GATE_UART2CLK:
		rate = ast2600_get_uart_clk_rate(priv->scu, 2);
		break;
	case ASPEED_CLK_GATE_UART3CLK:
		rate = ast2600_get_uart_clk_rate(priv->scu, 3);
		break;
	case ASPEED_CLK_GATE_UART4CLK:
		rate = ast2600_get_uart_clk_rate(priv->scu, 4);
		break;
	case ASPEED_CLK_GATE_UART5CLK:
		rate = ast2600_get_uart_clk_rate(priv->scu, 5);
		break;
	case ASPEED_CLK_BCLK:
		rate = ast2600_get_bclk_rate(priv->scu);
		break;
	case ASPEED_CLK_SDIO:
		rate = ast2600_get_sdio_clk_rate(priv->scu);
		break;
	case ASPEED_CLK_EMMC:
		rate = ast2600_get_emmc_clk_rate(priv->scu);
		break;
	case ASPEED_CLK_UARTX:
		rate = ast2600_get_uart_uxclk_rate(priv->scu);
		break;
	case ASPEED_CLK_HUARTX:
		rate = ast2600_get_uart_huxclk_rate(priv->scu);
		break;
	default:
		pr_debug("can't get clk rate\n");
		return -ENOENT;
	}

	return rate;
}

/**
 * @brief	lookup PLL divider config by input/output rate
 * @param[in]	*pll - PLL descriptor
 * @return	true - if PLL divider config is found, false - else
 * The function caller shall fill "pll->in" and "pll->out",
 * then this function will search the lookup table
 * to find a valid PLL divider configuration.
 */
static bool ast2600_search_clock_config(struct ast2600_pll_desc *pll)
{
	u32 i;
	bool is_found = false;

	for (i = 0; i < ARRAY_SIZE(ast2600_pll_lookup); i++) {
		const struct ast2600_pll_desc *def_cfg = &ast2600_pll_lookup[i];

		if (def_cfg->in == pll->in && def_cfg->out == pll->out) {
			is_found = true;
			pll->cfg.reg.w = def_cfg->cfg.reg.w;
			pll->cfg.ext_reg = def_cfg->cfg.ext_reg;
			break;
		}
	}
	return is_found;
}

static u32 ast2600_configure_pll(struct ast2600_scu *scu,
				 struct ast2600_pll_cfg *p_cfg, int pll_idx)
{
	u32 addr, addr_ext;
	u32 reg;

	switch (pll_idx) {
	case ASPEED_CLK_HPLL:
		addr = (u32)(&scu->h_pll_param);
		addr_ext = (u32)(&scu->h_pll_ext_param);
		break;
	case ASPEED_CLK_MPLL:
		addr = (u32)(&scu->m_pll_param);
		addr_ext = (u32)(&scu->m_pll_ext_param);
		break;
	case ASPEED_CLK_DPLL:
		addr = (u32)(&scu->d_pll_param);
		addr_ext = (u32)(&scu->d_pll_ext_param);
		break;
	case ASPEED_CLK_EPLL:
		addr = (u32)(&scu->e_pll_param);
		addr_ext = (u32)(&scu->e_pll_ext_param);
		break;
	default:
		debug("unknown PLL index\n");
		return 1;
	}

	p_cfg->reg.b.bypass = 0;
	p_cfg->reg.b.off = 0;
	p_cfg->reg.b.reset = 1;

	reg = readl(addr);
	reg &= ~GENMASK(25, 0);
	reg |= p_cfg->reg.w;
	writel(reg, addr);

	/* write extend parameter */
	writel(p_cfg->ext_reg, addr_ext);
	udelay(100);
	p_cfg->reg.b.off = 0;
	p_cfg->reg.b.reset = 0;
	reg &= ~GENMASK(25, 0);
	reg |= p_cfg->reg.w;
	writel(reg, addr);
	while (!(readl(addr_ext) & BIT(31)))
		;

	return 0;
}

static u32 ast2600_configure_ddr(struct ast2600_scu *scu, ulong rate)
{
	struct ast2600_pll_desc mpll;

	mpll.in = AST2600_CLK_IN;
	mpll.out = rate;
	if (ast2600_search_clock_config(&mpll) == false) {
		printf("error!! unable to find valid DDR clock setting\n");
		return 0;
	}
	ast2600_configure_pll(scu, &mpll.cfg, ASPEED_CLK_MPLL);

	return ast2600_get_pll_rate(scu, ASPEED_CLK_MPLL);
}

static ulong ast2600_clk_set_rate(struct clk *clk, ulong rate)
{
	struct ast2600_clk_priv *priv = dev_get_priv(clk->dev);
	ulong new_rate;

	switch (clk->id) {
	case ASPEED_CLK_MPLL:
		new_rate = ast2600_configure_ddr(priv->scu, rate);
		break;
	default:
		return -ENOENT;
	}

	return new_rate;
}

static int ast2600_calc_dly32_time(struct ast2600_scu *scu, int die_id, int stage)
{
	int ret, i;
	u64 sum = 0;
	u32 base, value, reset_sel, dly32_sel;

	if (die_id) {
		base = (u32)&scu->freq_counter_ctrl2;
		reset_sel = FREQC_CTRL_SRC_SEL_HCLK_DIE1;
		dly32_sel = FREQC_CTRL_SRC_SEL_DLY32_DIE1;
	} else {
		base = (u32)&scu->freq_counter_ctrl1;
		reset_sel = FREQC_CTRL_SRC_SEL_HCLK_DIE0;
		dly32_sel = FREQC_CTRL_SRC_SEL_DLY32_DIE0;
	}

	for (i = 0; i < DLY32_AVERAGE_COUNT; i++) {
		/* reset frequency-counter */
		writel(FIELD_PREP(FREQC_CTRL_SRC_SEL, reset_sel), base);
		ret = readl_poll_timeout(base, value, !(value & FREQC_CTRL_RESULT), 1000);
		if (ret)
			return -1;

		/* start frequency counter */
		value = FIELD_PREP(FREQC_CTRL_RING_STAGE, stage)
		      | FIELD_PREP(FREQC_CTRL_SRC_SEL, dly32_sel)
		      | FIELD_PREP(FREQC_CTRL_RING_CTRL, FREQC_CTRL_RING_EN);
		writel(value, base);

		/* delay for a while for settling down */
		udelay(100);

		/* enable osc for measurement */
		value |= FIELD_PREP(FREQC_CTRL_OSC_CTRL, FREQC_CTRL_OSC_EN);
		writel(value, base);
		ret = readl_poll_timeout(base, value, value & FREQC_CTRL_STATUS, 1000);
		if (ret)
			return -1;

		/* the result is represented in T count, will translate to pico-second later */
		sum += FIELD_GET(FREQC_CTRL_RESULT, value);
	}

	/* return the DLY32 value in pico-second */
	return (2560000 / (int)(sum >> DLY32_AVERAGE_COUNT_LOG2));
}

static void ast2600_init_dly32_lookup(struct ast2600_clk_priv *priv)
{
	struct ast2600_scu *scu = priv->scu;
	int i;

	for (i = 0; i < DLY32_NUM_OF_TAPS; i++) {
		priv->dly32_lookup[0][i] = ast2600_calc_dly32_time(scu, 0, i);
		priv->dly32_lookup[1][i] = ast2600_calc_dly32_time(scu, 1, i);
	}

#ifdef DEBUG
	for (i = 0; i < DLY32_NUM_OF_TAPS; i++)
		printf("28nm DLY32[%d] = %d ps\n", i, priv->dly32_lookup[0][i]);

	for (i = 0; i < DLY32_NUM_OF_TAPS; i++)
		printf("55nm DLY32[%d] = %d ps\n", i, priv->dly32_lookup[1][i]);
#endif
}

/**
 * @brief find the DLY32 tap number fitting the target delay time
 *
 * @param target_pico_sec target delay time in pico-second
 * @param lookup DLY32 lookup table
 * @return int DLY32 tap number
 */
static int ast2600_find_dly32_tap(int target_pico_sec, int *lookup)
{
	int i;
	bool found = false;

	for (i = 0; i < DLY32_NUM_OF_TAPS; i++) {
		if (lookup[i] >= target_pico_sec) {
			found = true;
			break;
		}
	}

	if (!found)
		return -1;

	return i;
}

static u32 ast2600_configure_mac12_clk(struct ast2600_clk_priv *priv, struct udevice *dev)
{
	struct ast2600_scu *scu = priv->scu;
	struct mac_delay_config mac1_cfg, mac2_cfg;
	u32 reg[3];
	int ret;

	reg[0] = MAC_DEF_DELAY_1G;
	reg[1] = MAC_DEF_DELAY_100M;
	reg[2] = MAC_DEF_DELAY_10M;

	ret = ast2600_find_dly32_tap(RGMII12_CLK_OUTPUT_DELAY_PS, priv->dly32_lookup[0]);
	if (ret > 0) {
		debug("suggested tx delay for mac1/2: %d\n", priv->dly32_lookup[0][ret]);

		reg[0] &= ~(MAC_CLK_1G_OUTPUT_DELAY_1 | MAC_CLK_1G_OUTPUT_DELAY_2);
		reg[0] |= FIELD_PREP(MAC_CLK_1G_OUTPUT_DELAY_1, ret) |
			  FIELD_PREP(MAC_CLK_1G_OUTPUT_DELAY_2, ret);
		reg[1] &= ~(MAC_CLK_100M_10M_OUTPUT_DELAY_1 | MAC_CLK_100M_10M_OUTPUT_DELAY_2);
		reg[1] |= FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_1, ret) |
			  FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_2, ret);
		reg[2] &= ~(MAC_CLK_100M_10M_OUTPUT_DELAY_1 | MAC_CLK_100M_10M_OUTPUT_DELAY_2);
		reg[2] |= FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_1, ret) |
			  FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_2, ret);
	}
	ret = dev_read_u32_array(dev, "mac0-clk-delay", (u32 *)&mac1_cfg,
				 sizeof(mac1_cfg) / sizeof(u32));
	if (!ret) {
		reg[0] &= ~(MAC_CLK_1G_INPUT_DELAY_1 | MAC_CLK_1G_OUTPUT_DELAY_1);
		reg[0] |= FIELD_PREP(MAC_CLK_1G_INPUT_DELAY_1, mac1_cfg.rx_delay_1000) |
			  FIELD_PREP(MAC_CLK_1G_OUTPUT_DELAY_1, mac1_cfg.tx_delay_1000);

		reg[1] &= ~(MAC_CLK_100M_10M_INPUT_DELAY_1 | MAC_CLK_100M_10M_OUTPUT_DELAY_1);
		reg[1] |= FIELD_PREP(MAC_CLK_100M_10M_INPUT_DELAY_1, mac1_cfg.rx_delay_100) |
			  FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_1, mac1_cfg.tx_delay_100);

		reg[2] &= ~(MAC_CLK_100M_10M_INPUT_DELAY_1 | MAC_CLK_100M_10M_OUTPUT_DELAY_1);
		reg[2] |= FIELD_PREP(MAC_CLK_100M_10M_INPUT_DELAY_1, mac1_cfg.rx_delay_10) |
			  FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_1, mac1_cfg.tx_delay_10);
	}

	ret = dev_read_u32_array(dev, "mac1-clk-delay", (u32 *)&mac2_cfg,
				 sizeof(mac2_cfg) / sizeof(u32));
	if (!ret) {
		reg[0] &= ~(MAC_CLK_1G_INPUT_DELAY_2 | MAC_CLK_1G_OUTPUT_DELAY_2);
		reg[0] |= FIELD_PREP(MAC_CLK_1G_INPUT_DELAY_2, mac2_cfg.rx_delay_1000) |
			  FIELD_PREP(MAC_CLK_1G_OUTPUT_DELAY_2, mac2_cfg.tx_delay_1000);

		reg[1] &= ~(MAC_CLK_100M_10M_INPUT_DELAY_2 | MAC_CLK_100M_10M_OUTPUT_DELAY_2);
		reg[1] |= FIELD_PREP(MAC_CLK_100M_10M_INPUT_DELAY_2, mac2_cfg.rx_delay_100) |
			  FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_2, mac2_cfg.tx_delay_100);

		reg[2] &= ~(MAC_CLK_100M_10M_INPUT_DELAY_2 | MAC_CLK_100M_10M_OUTPUT_DELAY_2);
		reg[2] |= FIELD_PREP(MAC_CLK_100M_10M_INPUT_DELAY_2, mac2_cfg.rx_delay_10) |
			  FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_2, mac2_cfg.tx_delay_10);
	}

	reg[0] |= (readl(&scu->mac12_clk_delay) & ~GENMASK(25, 0));
	writel(reg[0], &scu->mac12_clk_delay);
	writel(reg[1], &scu->mac12_clk_delay_100M);
	writel(reg[2], &scu->mac12_clk_delay_10M);

	/* MAC AHB = HPLL / 6 */
	clrsetbits_le32(&scu->clk_sel1, GENMASK(18, 16), (0x2 << 16));

	return 0;
}

static u32 ast2600_configure_mac34_clk(struct ast2600_clk_priv *priv, struct udevice *dev)
{
	struct ast2600_scu *scu = priv->scu;
	struct mac_delay_config mac3_cfg, mac4_cfg;
	u32 reg[3];
	int ret;

	reg[0] = MAC34_DEF_DELAY_1G;
	reg[1] = MAC34_DEF_DELAY_100M;
	reg[2] = MAC34_DEF_DELAY_10M;

	ret = ast2600_find_dly32_tap(RGMII34_CLK_OUTPUT_DELAY_PS, priv->dly32_lookup[1]);
	if (ret > 0) {
		debug("suggested tx delay for mac3/4: %d\n", priv->dly32_lookup[1][ret]);

		reg[0] &= ~(MAC_CLK_1G_OUTPUT_DELAY_1 | MAC_CLK_1G_OUTPUT_DELAY_2);
		reg[0] |= FIELD_PREP(MAC_CLK_1G_OUTPUT_DELAY_1, ret) |
			  FIELD_PREP(MAC_CLK_1G_OUTPUT_DELAY_2, ret);
		reg[1] &= ~(MAC_CLK_100M_10M_OUTPUT_DELAY_1 | MAC_CLK_100M_10M_OUTPUT_DELAY_2);
		reg[1] |= FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_1, ret) |
			  FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_2, ret);
		reg[2] &= ~(MAC_CLK_100M_10M_OUTPUT_DELAY_1 | MAC_CLK_100M_10M_OUTPUT_DELAY_2);
		reg[2] |= FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_1, ret) |
			  FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_2, ret);
	}

	ret = dev_read_u32_array(dev, "mac2-clk-delay", (u32 *)&mac3_cfg, sizeof(mac3_cfg) / sizeof(u32));
	if (!ret) {
		reg[0] &= ~(MAC_CLK_1G_INPUT_DELAY_1 | MAC_CLK_1G_OUTPUT_DELAY_1);
		reg[0] |= FIELD_PREP(MAC_CLK_1G_INPUT_DELAY_1, mac3_cfg.rx_delay_1000) |
			  FIELD_PREP(MAC_CLK_1G_OUTPUT_DELAY_1, mac3_cfg.tx_delay_1000);

		reg[1] &= ~(MAC_CLK_100M_10M_INPUT_DELAY_1 | MAC_CLK_100M_10M_OUTPUT_DELAY_1);
		reg[1] |= FIELD_PREP(MAC_CLK_100M_10M_INPUT_DELAY_1, mac3_cfg.rx_delay_100) |
			  FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_1, mac3_cfg.tx_delay_100);

		reg[2] &= ~(MAC_CLK_100M_10M_INPUT_DELAY_1 | MAC_CLK_100M_10M_OUTPUT_DELAY_1);
		reg[2] |= FIELD_PREP(MAC_CLK_100M_10M_INPUT_DELAY_1, mac3_cfg.rx_delay_10) |
			  FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_1, mac3_cfg.tx_delay_10);
	}

	ret = dev_read_u32_array(dev, "mac3-clk-delay", (u32 *)&mac4_cfg, sizeof(mac4_cfg) / sizeof(u32));
	if (!ret) {
		reg[0] &= ~(MAC_CLK_1G_INPUT_DELAY_2 | MAC_CLK_1G_OUTPUT_DELAY_2);
		reg[0] |= FIELD_PREP(MAC_CLK_1G_INPUT_DELAY_2, mac4_cfg.rx_delay_1000) |
			  FIELD_PREP(MAC_CLK_1G_OUTPUT_DELAY_2, mac4_cfg.tx_delay_1000);

		reg[1] &= ~(MAC_CLK_100M_10M_INPUT_DELAY_2 | MAC_CLK_100M_10M_OUTPUT_DELAY_2);
		reg[1] |= FIELD_PREP(MAC_CLK_100M_10M_INPUT_DELAY_2, mac4_cfg.rx_delay_100) |
			  FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_2, mac4_cfg.tx_delay_100);

		reg[2] &= ~(MAC_CLK_100M_10M_INPUT_DELAY_2 | MAC_CLK_100M_10M_OUTPUT_DELAY_2);
		reg[2] |= FIELD_PREP(MAC_CLK_100M_10M_INPUT_DELAY_2, mac4_cfg.rx_delay_10) |
			  FIELD_PREP(MAC_CLK_100M_10M_OUTPUT_DELAY_2, mac4_cfg.tx_delay_10);
	}

	reg[0] |= (readl(&scu->mac34_clk_delay) & ~GENMASK(25, 0));
	reg[0] &= ~MAC_CLK_RGMII_125M_SRC_SEL;
	reg[0] |= FIELD_PREP(MAC_CLK_RGMII_125M_SRC_SEL, MAC_CLK_RGMII_125M_SRC_PAD_RGMIICK);
	writel(reg[0], &scu->mac34_clk_delay);
	writel(reg[1], &scu->mac34_clk_delay_100M);
	writel(reg[2], &scu->mac34_clk_delay_10M);

	/*
	 * clock source seletion and divider
	 * scu310[26:24] : MAC AHB bus clock = HCLK / 2
	 * scu310[18:16] : RMII 50M = HCLK_200M / 4
	 */
	clrsetbits_le32(&scu->clk_sel4, (GENMASK(26, 24) | GENMASK(18, 16)),
			((0x0 << 24) | (0x3 << 16)));

	/*
	 * set driving strength
	 * scu458[3:2] : MAC4 driving strength
	 * scu458[1:0] : MAC3 driving strength
	 */
	clrsetbits_le32(&scu->pinmux_ctrl16, GENMASK(3, 0),
			(0x3 << 2) | (0x3 << 0));

	return 0;
}

/**
 * ast2600 RGMII clock source tree
 * 125M from external PAD -------->|\
 * HPLL -->|\                      | |---->RGMII 125M for MAC#1 & MAC#2
 *         | |---->| divider |---->|/                             +
 * EPLL -->|/                                                     |
 *                                                                |
 * +---------<-----------|RGMIICK PAD output enable|<-------------+
 * |
 * +--------------------------->|\
 *                              | |----> RGMII 125M for MAC#3 & MAC#4
 * HCLK 200M ---->|divider|---->|/
 * To simplify the control flow:
 * 1. RGMII 1/2 always use EPLL as the internal clock source
 * 2. RGMII 3/4 always use RGMIICK pad as the RGMII 125M source
 * 125M from external PAD -------->|\
 *                                 | |---->RGMII 125M for MAC#1 & MAC#2
 *         EPLL---->| divider |--->|/                             +
 *                                                                |
 * +<--------------------|RGMIICK PAD output enable|<-------------+
 * |
 * +--------------------------->RGMII 125M for MAC#3 & MAC#4
 */
#define RGMIICK_SRC_PAD		0
#define RGMIICK_SRC_EPLL	1 /* recommended */
#define RGMIICK_SRC_HPLL	2

#define RGMIICK_DIV2	1
#define RGMIICK_DIV3	2
#define RGMIICK_DIV4	3
#define RGMIICK_DIV5	4
#define RGMIICK_DIV6	5
#define RGMIICK_DIV7	6
#define RGMIICK_DIV8	7 /* recommended */

#define RMIICK_DIV4		0
#define RMIICK_DIV8		1
#define RMIICK_DIV12	2
#define RMIICK_DIV16	3
#define RMIICK_DIV20	4 /* recommended */
#define RMIICK_DIV24	5
#define RMIICK_DIV28	6
#define RMIICK_DIV32	7

struct ast2600_mac_clk_div {
	u32 src; /* 0=external PAD, 1=internal PLL */
	u32 fin; /* divider input speed */
	u32 n; /* 0=div2, 1=div2, 2=div3, 3=div4,...,7=div8 */
	u32 fout; /* fout = fin / n */
};

struct ast2600_mac_clk_div rgmii_clk_defconfig = {
	.src = ASPEED_CLK_EPLL,
	.fin = 1000000000,
	.n = RGMIICK_DIV8,
	.fout = 125000000,
};

struct ast2600_mac_clk_div rmii_clk_defconfig = {
	.src = ASPEED_CLK_EPLL,
	.fin = 1000000000,
	.n = RMIICK_DIV20,
	.fout = 50000000,
};

static void ast2600_init_mac_pll(struct ast2600_scu *p_scu,
				 struct ast2600_mac_clk_div *p_cfg)
{
	struct ast2600_pll_desc pll;

	pll.in = AST2600_CLK_IN;
	pll.out = p_cfg->fin;
	if (ast2600_search_clock_config(&pll) == false) {
		pr_err("unable to find valid ETHNET MAC clock setting\n");
		debug("%s: pll cfg = 0x%08x 0x%08x\n", __func__, pll.cfg.reg.w,
		      pll.cfg.ext_reg);
		debug("%s: pll cfg = %02x %02x %02x\n", __func__,
		      pll.cfg.reg.b.m, pll.cfg.reg.b.n, pll.cfg.reg.b.p);
		return;
	}
	ast2600_configure_pll(p_scu, &pll.cfg, p_cfg->src);
}

static void ast2600_init_rgmii_clk(struct ast2600_scu *p_scu,
				   struct ast2600_mac_clk_div *p_cfg)
{
	u32 reg_304 = readl(&p_scu->clk_sel2);
	u32 reg_340 = readl(&p_scu->mac12_clk_delay);
	u32 reg_350 = readl(&p_scu->mac34_clk_delay);

	reg_340 &= ~(MAC_CLK_RGMII_125M_SRC_SEL | MAC_CLK_RMII2_50M_RCLK_O_CTRL |
		     MAC_CLK_RMII1_50M_RCLK_O_CTRL | MAC_CLK_RGMIICK_PAD_DIR);
	/* RGMIICK PAD output enable (to MAC 3/4) */
	reg_340 |= FIELD_PREP(MAC_CLK_RGMIICK_PAD_DIR, MAC_CLK_RGMIICK_PAD_DIR_OUTPUT);
	if (p_cfg->src == ASPEED_CLK_EPLL || p_cfg->src == ASPEED_CLK_HPLL) {
		/*
		 * re-init PLL if the current PLL output frequency doesn't match
		 * the divider setting
		 */
		if (p_cfg->fin != ast2600_get_pll_rate(p_scu, p_cfg->src))
			ast2600_init_mac_pll(p_scu, p_cfg);
		/* select RGMII 125M from internal source */
		reg_340 |= FIELD_PREP(MAC_CLK_RGMII_125M_SRC_SEL, MAC_CLK_RGMII_125M_SRC_PLL);
	}

	reg_304 &= ~GENMASK(23, 20);

	/* set clock divider */
	reg_304 |= (p_cfg->n & 0x7) << 20;

	/* select internal clock source */
	if (p_cfg->src == ASPEED_CLK_HPLL)
		reg_304 |= BIT(23);

	/* RGMII 3/4 clock source select */
	reg_350 &= ~BIT(31);

	writel(reg_304, &p_scu->clk_sel2);
	writel(reg_340, &p_scu->mac12_clk_delay);
	writel(reg_350, &p_scu->mac34_clk_delay);
}

/**
 * ast2600 RMII/NCSI clock source tree
 * HPLL -->|\
 *         | |---->| divider |----> RMII 50M for MAC#1 & MAC#2
 * EPLL -->|/
 * HCLK(SCLICLK)---->| divider |----> RMII 50M for MAC#3 & MAC#4
 */
static void ast2600_init_rmii_clk(struct ast2600_scu *p_scu,
				  struct ast2600_mac_clk_div *p_cfg)
{
	u32 reg_304;
	u32 reg_310;

	if (p_cfg->src == ASPEED_CLK_EPLL || p_cfg->src == ASPEED_CLK_HPLL) {
		/*
		 * re-init PLL if the current PLL output frequency doesn't match
		 * the divider setting
		 */
		if (p_cfg->fin != ast2600_get_pll_rate(p_scu, p_cfg->src))
			ast2600_init_mac_pll(p_scu, p_cfg);
	}

	reg_304 = readl(&p_scu->clk_sel2);
	reg_310 = readl(&p_scu->clk_sel4);

	reg_304 &= ~GENMASK(19, 16);

	/* set RMII 1/2 clock divider */
	reg_304 |= (p_cfg->n & 0x7) << 16;

	/* RMII clock source selection */
	if (p_cfg->src == ASPEED_CLK_HPLL)
		reg_304 |= BIT(19);

	/* set RMII 3/4 clock divider */
	reg_310 &= ~GENMASK(18, 16);
	reg_310 |= (0x3 << 16);

	writel(reg_304, &p_scu->clk_sel2);
	writel(reg_310, &p_scu->clk_sel4);
}

static u32 ast2600_configure_mac(struct ast2600_scu *scu, int index)
{
	u32 reset_bit;
	u32 clkstop_bit;

	switch (index) {
	case 1:
		reset_bit = BIT(ASPEED_RESET_MAC1);
		clkstop_bit = BIT(SCU_CLKSTOP_MAC1);
		writel(reset_bit, &scu->sysreset_ctrl1);
		udelay(100);
		writel(clkstop_bit, &scu->clk_stop_clr_ctrl1);
		mdelay(10);
		writel(reset_bit, &scu->sysreset_clr_ctrl1);
		break;
	case 2:
		reset_bit = BIT(ASPEED_RESET_MAC2);
		clkstop_bit = BIT(SCU_CLKSTOP_MAC2);
		writel(reset_bit, &scu->sysreset_ctrl1);
		udelay(100);
		writel(clkstop_bit, &scu->clk_stop_clr_ctrl1);
		mdelay(10);
		writel(reset_bit, &scu->sysreset_clr_ctrl1);
		break;
	case 3:
		reset_bit = BIT(ASPEED_RESET_MAC3 - 32);
		clkstop_bit = BIT(SCU_CLKSTOP_MAC3);
		writel(reset_bit, &scu->sysreset_ctrl2);
		udelay(100);
		writel(clkstop_bit, &scu->clk_stop_clr_ctrl2);
		mdelay(10);
		writel(reset_bit, &scu->sysreset_clr_ctrl2);
		break;
	case 4:
		reset_bit = BIT(ASPEED_RESET_MAC4 - 32);
		clkstop_bit = BIT(SCU_CLKSTOP_MAC4);
		writel(reset_bit, &scu->sysreset_ctrl2);
		udelay(100);
		writel(clkstop_bit, &scu->clk_stop_clr_ctrl2);
		mdelay(10);
		writel(reset_bit, &scu->sysreset_clr_ctrl2);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

#define SCU_CLK_ECC_RSA_FROM_HPLL_CLK	BIT(19)
#define SCU_CLK_ECC_RSA_CLK_MASK		GENMASK(27, 26)
#define SCU_CLK_ECC_RSA_CLK_DIV(x)		((x) << 26)
static void ast2600_configure_rsa_ecc_clk(struct ast2600_scu *scu)
{
	u32 clk_sel = readl(&scu->clk_sel1);

	/* Configure RSA clock = HPLL/4 */
	clk_sel |= SCU_CLK_ECC_RSA_FROM_HPLL_CLK;
	clk_sel &= ~SCU_CLK_ECC_RSA_CLK_MASK;
	clk_sel |= SCU_CLK_ECC_RSA_CLK_DIV(3);

	writel(clk_sel, &scu->clk_sel1);
}

#define SCU_CLKSTOP_SDIO 4
static ulong ast2600_enable_sdclk(struct ast2600_scu *scu)
{
	u32 reset_bit;
	u32 clkstop_bit;

	reset_bit = BIT(ASPEED_RESET_SD - 32);
	clkstop_bit = BIT(SCU_CLKSTOP_SDIO);

	writel(reset_bit, &scu->sysreset_ctrl2);

	udelay(100);
	//enable clk
	writel(clkstop_bit, &scu->clk_stop_clr_ctrl2);
	mdelay(10);
	writel(reset_bit, &scu->sysreset_clr_ctrl2);

	return 0;
}

#define SCU_CLKSTOP_EXTSD			31
#define SCU_CLK_SD_MASK				(0x7 << 28)
#define SCU_CLK_SD_DIV(x)			((x) << 28)
#define SCU_CLK_SD_FROM_APLL_CLK	BIT(8)

static ulong ast2600_enable_extsdclk(struct ast2600_scu *scu)
{
	u32 clk_sel = readl(&scu->clk_sel4);
	u32 enableclk_bit;
	u32 rate = 0;
	u32 div = 0;
	int i = 0;

	enableclk_bit = BIT(SCU_CLKSTOP_EXTSD);

	/* ast2600 sd controller max clk is 200Mhz :
	 * use apll for clock source 800/4 = 200 : controller max is 200mhz
	 */
	rate = ast2600_get_apll_rate(scu);
	for (i = 0; i < 8; i++) {
		div = (i + 1) * 2;
		if ((rate / div) <= 200000000)
			break;
	}
	clk_sel &= ~SCU_CLK_SD_MASK;
	clk_sel |= SCU_CLK_SD_DIV(i) | SCU_CLK_SD_FROM_APLL_CLK;
	writel(clk_sel, &scu->clk_sel4);

	//enable clk
	setbits_le32(&scu->clk_sel4, enableclk_bit);

	return 0;
}

#define SCU_CLKSTOP_EMMC 27
static ulong ast2600_enable_emmcclk(struct ast2600_scu *scu)
{
	u32 reset_bit;
	u32 clkstop_bit;

	reset_bit = BIT(ASPEED_RESET_EMMC);
	clkstop_bit = BIT(SCU_CLKSTOP_EMMC);

	writel(reset_bit, &scu->sysreset_ctrl1);
	udelay(100);
	//enable clk
	writel(clkstop_bit, &scu->clk_stop_clr_ctrl1);
	mdelay(10);
	writel(reset_bit, &scu->sysreset_clr_ctrl1);

	return 0;
}

#define SCU_CLKSTOP_EXTEMMC			15
#define SCU_CLK_EMMC_MASK			(0x7 << 12)
#define SCU_CLK_EMMC_DIV(x)			((x) << 12)
#define SCU_CLK_EMMC_FROM_MPLL_CLK	BIT(11)

static ulong ast2600_enable_extemmcclk(struct ast2600_scu *scu)
{
	u32 revision_id = readl(&scu->chip_id1);
	u32 clk_sel = readl(&scu->clk_sel1);
	u32 enableclk_bit = BIT(SCU_CLKSTOP_EXTEMMC);
	u32 rate = 0;
	u32 div = 0;
	int i = 0;

	/*
	 * ast2600 eMMC controller max clk is 200Mhz
	 * HPll->1/2->|\
	 *				|->SCU300[11]->SCU300[14:12][1/N] +
	 * MPLL------>|/								  |
	 * +----------------------------------------------+
	 * |
	 * +---------> EMMC12C[15:8][1/N]-> eMMC clk
	 */
	if (((revision_id & CHIP_REVISION_ID) >> 16)) {
		//AST2600A1 : use mpll to be clk source
		rate = ast2600_get_pll_rate(scu, ASPEED_CLK_MPLL);
		for (i = 0; i < 8; i++) {
			div = (i + 1) * 2;
			if ((rate / div) <= 200000000)
				break;
		}

		clk_sel &= ~SCU_CLK_EMMC_MASK;
		clk_sel |= SCU_CLK_EMMC_DIV(i) | SCU_CLK_EMMC_FROM_MPLL_CLK;
		writel(clk_sel, &scu->clk_sel1);

	} else {
		//AST2600A0 : use hpll to be clk source
		rate = ast2600_get_pll_rate(scu, ASPEED_CLK_HPLL);

		for (i = 0; i < 8; i++) {
			div = (i + 1) * 4;
			if ((rate / div) <= 200000000)
				break;
		}

		clk_sel &= ~SCU_CLK_EMMC_MASK;
		clk_sel |= SCU_CLK_EMMC_DIV(i);
		writel(clk_sel, &scu->clk_sel1);
	}
	setbits_le32(&scu->clk_sel1, enableclk_bit);

	return 0;
}

#define SCU_CLKSTOP_FSICLK 30

static ulong ast2600_enable_fsiclk(struct ast2600_scu *scu)
{
	u32 reset_bit;
	u32 clkstop_bit;

	reset_bit = BIT(ASPEED_RESET_FSI % 32);
	clkstop_bit = BIT(SCU_CLKSTOP_FSICLK);

	/* The FSI clock is shared between masters. If it's already on
	 * don't touch it, as that will reset the existing master.
	 */
	if (!(readl(&scu->clk_stop_ctrl2) & clkstop_bit)) {
		debug("%s: already running, not touching it\n", __func__);
		return 0;
	}

	writel(reset_bit, &scu->sysreset_ctrl2);
	udelay(100);
	writel(clkstop_bit, &scu->clk_stop_clr_ctrl2);
	mdelay(10);
	writel(reset_bit, &scu->sysreset_clr_ctrl2);

	return 0;
}

static ulong ast2600_enable_usbahclk(struct ast2600_scu *scu)
{
	u32 reset_bit;
	u32 clkstop_bit;

	reset_bit = BIT(ASPEED_RESET_EHCI_P1);
	clkstop_bit = BIT(14);

	writel(reset_bit, &scu->sysreset_ctrl1);
	udelay(100);
	writel(clkstop_bit, &scu->clk_stop_ctrl1);
	mdelay(20);
	writel(reset_bit, &scu->sysreset_clr_ctrl1);

	return 0;
}

static ulong ast2600_enable_usbbhclk(struct ast2600_scu *scu)
{
	u32 reset_bit;
	u32 clkstop_bit;

	reset_bit = BIT(ASPEED_RESET_EHCI_P2);
	clkstop_bit = BIT(7);

	writel(reset_bit, &scu->sysreset_ctrl1);
	udelay(100);
	writel(clkstop_bit, &scu->clk_stop_clr_ctrl1);
	mdelay(20);

	writel(reset_bit, &scu->sysreset_clr_ctrl1);

	return 0;
}

/* also known as yclk */
static ulong ast2600_enable_haceclk(struct ast2600_scu *scu)
{
	u32 reset_bit;
	u32 clkstop_bit;

	reset_bit = BIT(ASPEED_RESET_HACE);
	clkstop_bit = BIT(13);

	writel(reset_bit, &scu->sysreset_ctrl1);
	udelay(100);
	writel(clkstop_bit, &scu->clk_stop_clr_ctrl1);
	mdelay(20);

	writel(reset_bit, &scu->sysreset_clr_ctrl1);

	return 0;
}

static ulong ast2600_enable_rsaeccclk(struct ast2600_scu *scu)
{
	u32 clkstop_bit;

	clkstop_bit = BIT(24);

	writel(clkstop_bit, &scu->clk_stop_clr_ctrl1);
	mdelay(20);

	return 0;
}

static int ast2600_clk_enable(struct clk *clk)
{
	struct ast2600_clk_priv *priv = dev_get_priv(clk->dev);

	switch (clk->id) {
	case ASPEED_CLK_GATE_MAC1CLK:
		ast2600_configure_mac(priv->scu, 1);
		break;
	case ASPEED_CLK_GATE_MAC2CLK:
		ast2600_configure_mac(priv->scu, 2);
		break;
	case ASPEED_CLK_GATE_MAC3CLK:
		ast2600_configure_mac(priv->scu, 3);
		break;
	case ASPEED_CLK_GATE_MAC4CLK:
		ast2600_configure_mac(priv->scu, 4);
		break;
	case ASPEED_CLK_GATE_SDCLK:
		ast2600_enable_sdclk(priv->scu);
		break;
	case ASPEED_CLK_GATE_SDEXTCLK:
		ast2600_enable_extsdclk(priv->scu);
		break;
	case ASPEED_CLK_GATE_EMMCCLK:
		ast2600_enable_emmcclk(priv->scu);
		break;
	case ASPEED_CLK_GATE_EMMCEXTCLK:
		ast2600_enable_extemmcclk(priv->scu);
		break;
	case ASPEED_CLK_GATE_FSICLK:
		ast2600_enable_fsiclk(priv->scu);
		break;
	case ASPEED_CLK_GATE_USBPORT1CLK:
		ast2600_enable_usbahclk(priv->scu);
		break;
	case ASPEED_CLK_GATE_USBPORT2CLK:
		ast2600_enable_usbbhclk(priv->scu);
		break;
	case ASPEED_CLK_GATE_YCLK:
		ast2600_enable_haceclk(priv->scu);
		break;
	case ASPEED_CLK_GATE_RSAECCCLK:
		ast2600_enable_rsaeccclk(priv->scu);
		break;
	default:
		pr_err("can't enable clk\n");
		return -ENOENT;
	}

	return 0;
}

struct clk_ops ast2600_clk_ops = {
	.get_rate = ast2600_clk_get_rate,
	.set_rate = ast2600_clk_set_rate,
	.enable = ast2600_clk_enable,
};

static int ast2600_clk_probe(struct udevice *dev)
{
	struct ast2600_clk_priv *priv = dev_get_priv(dev);
	u32 uart_clk_source;

	priv->scu = devfdt_get_addr_ptr(dev);
	if (IS_ERR(priv->scu))
		return PTR_ERR(priv->scu);

	uart_clk_source = dev_read_u32_default(dev, "uart-clk-source", 0x0);

	if (uart_clk_source) {
		if (uart_clk_source & GENMASK(5, 0))
			setbits_le32(&priv->scu->clk_sel4,
				     uart_clk_source & GENMASK(5, 0));
		if (uart_clk_source & GENMASK(12, 6))
			setbits_le32(&priv->scu->clk_sel5,
				     uart_clk_source & GENMASK(12, 6));
	}

	ast2600_init_rgmii_clk(priv->scu, &rgmii_clk_defconfig);
	ast2600_init_rmii_clk(priv->scu, &rmii_clk_defconfig);
	ast2600_init_dly32_lookup(priv);
	ast2600_configure_mac12_clk(priv, dev);
	ast2600_configure_mac34_clk(priv, dev);
	ast2600_configure_rsa_ecc_clk(priv->scu);

	return 0;
}

static int ast2600_clk_bind(struct udevice *dev)
{
	int ret;

	/* The reset driver does not have a device node, so bind it here */
	ret = device_bind_driver(gd->dm_root, "ast_sysreset", "reset", &dev);
	if (ret)
		debug("Warning: No reset driver: ret=%d\n", ret);

	return 0;
}

struct aspeed_clks {
	ulong id;
	const char *name;
};

static struct aspeed_clks aspeed_clk_names[] = {
	{ ASPEED_CLK_HPLL, "hpll" },     { ASPEED_CLK_MPLL, "mpll" },
	{ ASPEED_CLK_APLL, "apll" },     { ASPEED_CLK_EPLL, "epll" },
	{ ASPEED_CLK_DPLL, "dpll" },     { ASPEED_CLK_AHB, "hclk" },
	{ ASPEED_CLK_APB1, "pclk1" },    { ASPEED_CLK_APB2, "pclk2" },
	{ ASPEED_CLK_BCLK, "bclk" },     { ASPEED_CLK_UARTX, "uxclk" },
	{ ASPEED_CLK_HUARTX, "huxclk" },
};

int soc_clk_dump(void)
{
	struct udevice *dev;
	struct clk clk;
	unsigned long rate;
	int i, ret;

	ret = uclass_get_device_by_driver(UCLASS_CLK, DM_GET_DRIVER(aspeed_scu),
					  &dev);
	if (ret)
		return ret;

	printf("Clk\t\tHz\n");

	for (i = 0; i < ARRAY_SIZE(aspeed_clk_names); i++) {
		clk.id = aspeed_clk_names[i].id;
		ret = clk_request(dev, &clk);
		if (ret < 0) {
			debug("%s clk_request() failed: %d\n", __func__, ret);
			continue;
		}

		ret = clk_get_rate(&clk);
		rate = ret;

		clk_free(&clk);

		if (ret == -ENOTSUPP) {
			printf("clk ID %lu not supported yet\n",
			       aspeed_clk_names[i].id);
			continue;
		}
		if (ret < 0) {
			printf("%s %lu: get_rate err: %d\n", __func__,
			       aspeed_clk_names[i].id, ret);
			continue;
		}

		printf("%s(%3lu):\t%lu\n", aspeed_clk_names[i].name,
		       aspeed_clk_names[i].id, rate);
	}

	return 0;
}

static const struct udevice_id ast2600_clk_ids[] = {
	{
		.compatible = "aspeed,ast2600-scu",
	},
	{}
};

U_BOOT_DRIVER(aspeed_scu) = {
	.name = "aspeed_scu",
	.id = UCLASS_CLK,
	.of_match = ast2600_clk_ids,
	.priv_auto_alloc_size = sizeof(struct ast2600_clk_priv),
	.ops = &ast2600_clk_ops,
	.bind = ast2600_clk_bind,
	.probe = ast2600_clk_probe,
};
