// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) ASPEED Technology Inc.
 * Ryan Chen <ryan_chen@aspeedtech.com>
 */

#include <errno.h>
#include <asm/io.h>
#include <asm/arch/platform.h>
#include <asm/arch/scu_ast2700.h>
#include <asm/arch/spi.h>

int aspeed_get_mac_phy_interface(u8 num)
{
	return 0;
}

void aspeed_print_security_info(void)
{
}

void aspeed_print_dram_initializer(void)
{
}

void aspeed_print_2nd_wdt_mode(void)
{
	struct ast2700_scu0 *scu0 = (struct ast2700_scu0 *)ASPEED_CPU_SCU_BASE;
	struct ast2700_scu1 *scu1 = (struct ast2700_scu1 *)ASPEED_IO_SCU_BASE;
	u32 scu1_hwstrap1 = readl(&scu1->hwstrap1); /* SCU1_010 */
	u32 scu1_hwstrap2 = readl(&scu1->hwstrap2); /* SCU1_030 */
	u32 boot_indicator = readl(ASPEED_WDTA_BASE + 0x4c) & BIT(1);

	if (readl(&scu0->sysrest_log2) & BIT(31)) {
		printf("RST: WDTA\n");
		writel(BIT(31), &scu0->sysrest_log2);
	}

	/* ABR enable */
	if (spi_abr_enabled()) {
		/* boot from eMMC */
		if (scu1_hwstrap1 & BIT(11)) {
			if (scu1_hwstrap1 & BIT(23))
				printf("UFS 2nd Boot (ABR): Enable");
			else
				printf("eMMC 2nd Boot (ABR): Enable");

			printf(", boot partition: %s",
			       !!boot_indicator ? "2" : "1");
		} else { /* boot from SPI */
			printf("FMC 2nd Boot (ABR): Enable");
			if (scu1_hwstrap2 & BIT(29))
				printf(", Single flash");
			else
				printf(", Dual flashes");

			printf(", Source: %s",
			       !!boot_indicator ? "Alternate" : "Primary");

			if (spi_get_flash_sz_strap()) {
				printf(", BSPI_size: %d MB",
				       spi_get_flash_sz_strap() / 0x100000);
			}
		}

		if (scu1_hwstrap2 & BIT(27))
			printf(", infinite ABR\n");
		else
			printf("\n");
	}
}

void aspeed_print_fmc_aux_ctrl(void)
{
	struct ast2700_scu1 *scu1 = (struct ast2700_scu1 *)ASPEED_IO_SCU_BASE;
	u32 scu1_hwstrap2 = readl(&scu1->hwstrap2);
	u32 boot_indicator = readl(ASPEED_WDTA_BASE + 0x4c) & BIT(1);

	if (spi_aux_bit_enabled()) {
		printf("FMC aux control: Enable");
		/* gpioY6 : BSPI_ABR */
		if (spi_ext_abr_signal())
			printf(", Force Alt boot");
		else
			printf(", Default mode");

		if (scu1_hwstrap2 & BIT(29))
			printf(", Single flash");
		else
			printf(", Dual flashes");

		printf(", Source: %s",
		       !!boot_indicator ? "Alternate" : "Primary");

		if (spi_get_flash_sz_strap()) {
			printf(", BSPI_size: %d MB",
			       spi_get_flash_sz_strap() / 0x100000);
		}

		printf("\n");
	}
}

void aspeed_print_spi_misc_func(void)
{
	struct ast2700_scu1 *scu1 = (struct ast2700_scu1 *)ASPEED_IO_SCU_BASE;
	u32 scu1_hwstrap1 = readl(&scu1->hwstrap1); /* SCU1_010 */
	u32 scu1_hwstrap2 = readl(&scu1->hwstrap2); /* SCU1_030 */

	if ((scu1_hwstrap1 & BIT(24)) || (scu1_hwstrap1 & BIT(25)) ||
	    (scu1_hwstrap2 & BIT(25))) {
		printf("SPI: force");
		if (scu1_hwstrap1 & BIT(25))
			printf(", 4-byte addr mode");
		if (scu1_hwstrap1 & BIT(24))
			printf(", wait for ready");
		if (scu1_hwstrap2 & BIT(25))
			printf(", check SFDP");
		printf("\n");
	}
}

void aspeed_print_espi_mode(void)
{
}

void aspeed_print_mac_info(void)
{
}
