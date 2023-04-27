// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) ASPEED Technology Inc.
 */
#include <common.h>
#include <asm/io.h>

#define SCU_BASE	0x1e6e2000
#define ESPI_BASE	0x1e6ee000
#define LPC_BASE	0x1e789000
#define   LPC_HICR5		(LPC_BASE + 0x80)
#define   LPC_HICR6		(LPC_BASE + 0x84)
#define   LPC_SNPWADR	(LPC_BASE + 0x90)
#define   LPC_HICRB		(LPC_BASE + 0x100)
#define GPIO_BASE	0x1e780000

/* HICR5 Bits */
#define HICR5_EN_SIOGIO (1 << 31)		/* Enable SIOGIO */
#define HICR5_EN80HGIO (1 << 30)		/* Enable 80hGIO */
#define HICR5_SEL80HGIO (0x1f << 24)		/* Select 80hGIO */
#define SET_SEL80HGIO(x) ((x & 0x1f) << 24)	/* Select 80hGIO Offset */
#define HICR5_UNKVAL_MASK 0x1FFF0000		/* Bits with unknown values on reset */
#define HICR5_ENINT_SNP0W (1 << 1)		/* Enable Snooping address 0 */
#define HICR5_EN_SNP0W (1 << 0)			/* Enable Snooping address 0 */

/* HRCR6 Bits */
#define HICR6_STR_SNP0W (1 << 0)	/* Interrupt Status Snoop address 0 */
#define HICR6_STR_SNP1W (1 << 1)	/* Interrupt Status Snoop address 1 */

/* HICRB Bits */
#define HICRB_EN80HSGIO (1 << 13)	/* Enable 80hSGIO */

static void __maybe_unused port80h_snoop_init(void)
{
	u32 value;
	/* enable port80h snoop and sgpio */
	/* set lpc snoop #0 to port 0x80 */
	value = readl(LPC_SNPWADR) & 0xffff0000;
	writel(value | 0x80, LPC_SNPWADR);

	/* clear interrupt status */
	value = readl(LPC_HICR6);
	value |= HICR6_STR_SNP0W | HICR6_STR_SNP1W;
	writel(value, LPC_HICR6);

	/* enable lpc snoop #0 and SIOGIO */
	value = readl(LPC_HICR5) & ~(HICR5_UNKVAL_MASK);
	value |= HICR5_EN_SIOGIO | HICR5_EN_SNP0W;
	writel(value, LPC_HICR5);

	/* enable port80h snoop on SGPIO */
	value = readl(LPC_HICRB) | HICRB_EN80HSGIO;
	writel(value, LPC_HICRB);
}

static void __maybe_unused sgpio_init(void)
{
#define SGPIO_CLK_DIV(N)	((N) << 16)
#define SGPIO_BYTES(N)		((N) << 6)
#define SGPIO_ENABLE		1
#define GPIO554			0x554
#define SCU_414			0x414 /* Multi-function Pin Control #5 */
#define SCU_414_SGPM_MASK	GENMASK(27, 24)

	u32 value;
	/* set the sgpio clock to pclk/(2*(5+1)) or ~2 MHz */
	value = SGPIO_CLK_DIV(256) | SGPIO_BYTES(10) | SGPIO_ENABLE;
	writel(value, GPIO_BASE + GPIO554);
	writel(readl(SCU_BASE | SCU_414) | SCU_414_SGPM_MASK,
		SCU_BASE | SCU_414);
}

static void __maybe_unused espi_init(void)
{
	u32 reg;

	/* skip eSPI init if LPC mode is selected */
	reg = readl(SCU_BASE + 0x510);
	if (reg & BIT(6))
		return;

	/*
	 * Aspeed STRONGLY NOT recommend to use eSPI early init.
	 *
	 * This eSPI early init sequence merely set OOB_FREE. It
	 * is NOT able to actually handle OOB requests from PCH.
	 *
	 * During the power on stage, PCH keep waiting OOB_FREE
	 * to continue its booting. In general, OOB_FREE is set
	 * when BMC firmware is ready. That is, the eSPI kernel
	 * driver is mounted and ready to serve eSPI. However,
	 * it means that PCH must wait until BMC kernel ready.
	 *
	 * For customers that request PCH booting as soon as
	 * possible. You may use this early init to set OOB_FREE
	 * to prevent PCH from blocking by OOB_FREE before BMC
	 * kernel ready.
	 *
	 * If you are not sure what you are doing, DO NOT use it.
	 */
	reg = readl(ESPI_BASE + 0x000);
	reg |= 0xef;
	writel(reg, ESPI_BASE + 0x000);

	writel(0x0, ESPI_BASE + 0x110);
	writel(0x0, ESPI_BASE + 0x114);

	reg = readl(ESPI_BASE + 0x00c);
	reg |= 0x80000000;
	writel(reg, ESPI_BASE + 0x00c);

	writel(0xffffffff, ESPI_BASE + 0x094);
	writel(0x1, ESPI_BASE + 0x100);
	writel(0x1, ESPI_BASE + 0x120);

	reg = readl(ESPI_BASE + 0x080);
	reg |= 0x50;
	writel(reg, ESPI_BASE + 0x080);

	reg = readl(ESPI_BASE + 0x000);
	reg |= 0x10;
	writel(reg, ESPI_BASE + 0x000);
}

#ifdef CONFIG_ASPEED_DCSCM_CARD_A2
/**
 * @brief	Reset PHY for Aspeed AST2600 DCSCM Card A2 version
 *
 * Aspeed AST2600 DCSCM Card A2 requires to reset Eth PHY by GPIOC1
 * Where GPIOC1 controls the reset signal of RMII PHY 2
*/
void reset_eth_phy_dcscm_card_a2(void)
{
#define GRP_C		16
#define PHY_RESET_MASK  (BIT(GRP_C + 1))
	u32 value = readl(0x1e780000);
	u32 direction = readl(0x1e780004);

	debug("AST2600-DCSCM A2 card reset phy\n");

	direction |= PHY_RESET_MASK;
	value &= ~PHY_RESET_MASK;
	writel(direction, 0x1e780004);
	writel(value, 0x1e780000);
	while((readl(0x1e780000) & PHY_RESET_MASK) != 0);

        /* Tgap = 10ms */
	udelay(10000);

	value |= PHY_RESET_MASK;
	writel(value, 0x1e780000);
	while((readl(0x1e780000) & PHY_RESET_MASK) != PHY_RESET_MASK);
}
#endif

int board_early_init_f(void)
{
#if 0
	port80h_snoop_init();
	sgpio_init();
#endif
	espi_init();
#ifdef CONFIG_ASPEED_DCSCM_CARD_A2
	reset_eth_phy_dcscm_card_a2();
#endif
	return 0;
}
