// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) ASPEED Technology Inc.
 * Ryan Chen <ryan_chen@aspeedtech.com>
 */

#include <common.h>
#include <errno.h>
#include <asm/io.h>
#include <asm/arch/aspeed_scu_info.h>

/* SoC mapping Table */
#define SOC_ID(str, rev) { .name = str, .rev_id = rev, }

struct soc_id {
	const char *name;
	u64 rev_id;
};

static struct soc_id soc_map_table[] = {
	SOC_ID("AST2600-A0", 0x0500030305000303),
	SOC_ID("AST2600-A1", 0x0501030305010303),
	SOC_ID("AST2620-A1", 0x0501020305010203),
	SOC_ID("AST2600-A2", 0x0502030305010303),
	SOC_ID("AST2620-A2", 0x0502020305010203),
	SOC_ID("AST2605-A2", 0x0502010305010103),
	SOC_ID("AST2600-A3", 0x0503030305030303),
	SOC_ID("AST2620-A3", 0x0503020305030203),
	SOC_ID("AST2605-A3", 0x0503010305030103),
	SOC_ID("AST2625-A3", 0x0503040305030403),
};

void aspeed_print_soc_id(void)
{
	int i;
	u64 rev_id;

	rev_id = readl(ASPEED_REVISION_ID0);
	rev_id = ((u64)readl(ASPEED_REVISION_ID1) << 32) | rev_id;

	for (i = 0; i < ARRAY_SIZE(soc_map_table); i++) {
		if (rev_id == soc_map_table[i].rev_id)
			break;
	}
	if (i == ARRAY_SIZE(soc_map_table))
		printf("UnKnow-SOC: %llx\n", rev_id);
	else
		printf("SOC: %4s \n",soc_map_table[i].name);
}

int aspeed_get_mac_phy_interface(u8 num)
{
	u32 strap1 = readl(ASPEED_HW_STRAP1);
#ifdef ASPEED_HW_STRAP2
	u32 strap2 = readl(ASPEED_HW_STRAP2);
#endif
	switch(num) {
		case 0:
			if(strap1 & BIT(6)) {
				return 1;
			} else {
				return 0;
			}
			break;
		case 1:
			if(strap1 & BIT(7)) {
				return 1;
			} else {
				return 0;
			}
			break;
#ifdef ASPEED_HW_STRAP2
		case 2:
			if(strap2 & BIT(0)) {
				return 1;
			} else {
				return 0;
			}
			break;
		case 3:
			if(strap2 & BIT(1)) {
				return 1;
			} else {
				return 0;
			}
			break;
#endif
	}
	return -1;
}

void aspeed_print_security_info(void)
{
	u32 qsr = readl(ASPEED_OTP_QSR);
	u32 sb_sts = readl(ASPEED_SB_STS);
	u32 hash;
	u32 rsa;

	if (!(sb_sts & BIT(6)))
		return;
	printf("Secure Boot: ");
	if (qsr & BIT(7)) {
		hash = (qsr >> 10) & 3;
		rsa = (qsr >> 12) & 3;

		printf("Mode_2, ");

		if (qsr & BIT(27)) {
			printf("AES_");
		}
		switch (rsa) {
		case 0:
			printf("RSA1024_");
			break;
		case 1:
			printf("RSA2048_");
			break;
		case 2:
			printf("RSA3072_");
			break;
		default:
			printf("RSA4096_");
			break;
		}
		switch (hash) {
		case 0:
			printf("SHA224\n");
			break;
		case 1:
			printf("SHA256\n");
			break;
		case 2:
			printf("SHA384\n");
			break;
		default:
			printf("SHA512\n");
			break;
		}
	} else {
		printf("Mode_GCM\n");
		return;
	}
}

/*	ASPEED_SYS_RESET_CTRL	: System reset contrl/status register*/
#define SYS_WDT8_SW_RESET	BIT(15)
#define SYS_WDT8_ARM_RESET	BIT(14)
#define SYS_WDT8_FULL_RESET	BIT(13)
#define SYS_WDT8_SOC_RESET	BIT(12)
#define SYS_WDT7_SW_RESET	BIT(11)
#define SYS_WDT7_ARM_RESET	BIT(10)
#define SYS_WDT7_FULL_RESET	BIT(9)
#define SYS_WDT7_SOC_RESET	BIT(8)
#define SYS_WDT6_SW_RESET	BIT(7)
#define SYS_WDT6_ARM_RESET	BIT(6)
#define SYS_WDT6_FULL_RESET	BIT(5)
#define SYS_WDT6_SOC_RESET	BIT(4)
#define SYS_WDT5_SW_RESET	BIT(3)
#define SYS_WDT5_ARM_RESET	BIT(2)
#define SYS_WDT5_FULL_RESET	BIT(1)
#define SYS_WDT5_SOC_RESET	BIT(0)

#define SYS_WDT4_SW_RESET	BIT(31)
#define SYS_WDT4_ARM_RESET	BIT(30)
#define SYS_WDT4_FULL_RESET	BIT(29)
#define SYS_WDT4_SOC_RESET	BIT(28)
#define SYS_WDT3_SW_RESET	BIT(27)
#define SYS_WDT3_ARM_RESET	BIT(26)
#define SYS_WDT3_FULL_RESET	BIT(25)
#define SYS_WDT3_SOC_RESET	BIT(24)
#define SYS_WDT2_SW_RESET	BIT(23)
#define SYS_WDT2_ARM_RESET	BIT(22)
#define SYS_WDT2_FULL_RESET	BIT(21)
#define SYS_WDT2_SOC_RESET	BIT(20)
#define SYS_WDT1_SW_RESET	BIT(19)
#define SYS_WDT1_ARM_RESET	BIT(18)
#define SYS_WDT1_FULL_RESET	BIT(17)
#define SYS_WDT1_SOC_RESET	BIT(16)

#define SYS_CM3_EXT_RESET	BIT(6)
#define SYS_PCI2_RESET		BIT(5)
#define SYS_PCI1_RESET		BIT(4)
#define SYS_DRAM_ECC_RESET	BIT(3)
#define SYS_FLASH_ABR_RESET	BIT(2)
#define SYS_EXT_RESET		BIT(1)
#define SYS_PWR_RESET_FLAG	BIT(0)

#define BIT_WDT_SOC(x)	SYS_WDT ## x ## _SOC_RESET
#define BIT_WDT_FULL(x)	SYS_WDT ## x ## _FULL_RESET
#define BIT_WDT_ARM(x)	SYS_WDT ## x ## _ARM_RESET
#define BIT_WDT_SW(x)	SYS_WDT ## x ## _SW_RESET

#define HANDLE_WDTx_RESET(x, event_log, event_log_reg) \
	if (event_log & (BIT_WDT_SOC(x) | BIT_WDT_FULL(x) | BIT_WDT_ARM(x) | BIT_WDT_SW(x))) { \
		printf("RST: WDT%d ", x); \
		if (event_log & BIT_WDT_SOC(x)) { \
			printf("SOC "); \
			writel(BIT_WDT_SOC(x), event_log_reg); \
		} \
		if (event_log & BIT_WDT_FULL(x)) { \
			printf("FULL "); \
			writel(BIT_WDT_FULL(x), event_log_reg); \
		} \
		if (event_log & BIT_WDT_ARM(x)) { \
			printf("ARM "); \
			writel(BIT_WDT_ARM(x), event_log_reg); \
		} \
		if (event_log & BIT_WDT_SW(x)) { \
			printf("SW "); \
			writel(BIT_WDT_SW(x), event_log_reg); \
		} \
		printf("\n"); \
	} \
	(void)(x)

void aspeed_print_sysrst_info(void)
{
	u32 rest = readl(ASPEED_SYS_RESET_CTRL);
	u32 rest3 = readl(ASPEED_SYS_RESET_CTRL3);

	if (rest & SYS_PWR_RESET_FLAG) {
		printf("RST: Power On \n");
		writel(rest, ASPEED_SYS_RESET_CTRL);
	} else {
		HANDLE_WDTx_RESET(8, rest3, ASPEED_SYS_RESET_CTRL3);
		HANDLE_WDTx_RESET(7, rest3, ASPEED_SYS_RESET_CTRL3);
		HANDLE_WDTx_RESET(6, rest3, ASPEED_SYS_RESET_CTRL3);
		HANDLE_WDTx_RESET(5, rest3, ASPEED_SYS_RESET_CTRL3);
		HANDLE_WDTx_RESET(4, rest, ASPEED_SYS_RESET_CTRL);
		HANDLE_WDTx_RESET(3, rest, ASPEED_SYS_RESET_CTRL);
		HANDLE_WDTx_RESET(2, rest, ASPEED_SYS_RESET_CTRL);
		HANDLE_WDTx_RESET(1, rest, ASPEED_SYS_RESET_CTRL);

		if (rest & SYS_CM3_EXT_RESET) {
			printf("RST: SYS_CM3_EXT_RESET \n");
			writel(SYS_CM3_EXT_RESET, ASPEED_SYS_RESET_CTRL);
		}

		if (rest & (SYS_PCI1_RESET | SYS_PCI2_RESET)) {
			printf("PCI RST: ");
			if (rest & SYS_PCI1_RESET) {
				printf("#1 ");
				writel(SYS_PCI1_RESET, ASPEED_SYS_RESET_CTRL);
			}

			if (rest & SYS_PCI2_RESET) {
				printf("#2 ");
				writel(SYS_PCI2_RESET, ASPEED_SYS_RESET_CTRL);
			}
			printf("\n");
		}

		if (rest & SYS_DRAM_ECC_RESET) {
			printf("RST: DRAM_ECC_RESET \n");
			writel(SYS_DRAM_ECC_RESET, ASPEED_SYS_RESET_CTRL);
		}

		if (rest & SYS_FLASH_ABR_RESET) {
			printf("RST: SYS_FLASH_ABR_RESET \n");
			writel(SYS_FLASH_ABR_RESET, ASPEED_SYS_RESET_CTRL);
		}
		if (rest & SYS_EXT_RESET) {
			printf("RST: External \n");
			writel(SYS_EXT_RESET, ASPEED_SYS_RESET_CTRL);
		}
	}
}

#define SOC_FW_INIT_DRAM		BIT(7)

void aspeed_print_dram_initializer(void)
{
	if(readl(ASPEED_VGA_HANDSHAKE0) & SOC_FW_INIT_DRAM)
		printf("[init by SOC]\n");
	else
		printf("[init by VBIOS]\n");
}

void aspeed_print_2nd_wdt_mode(void)
{
	/* ABR enable */
	if (readl(ASPEED_HW_STRAP2) & BIT(11)) {
		/* boot from eMMC */
		if (readl(ASPEED_HW_STRAP1) & BIT(2)) {
			printf("eMMC 2nd Boot (ABR): Enable");
			printf(", boot partition: %s", \
				readl(ASPEED_EMMC_WDT_CTRL) & BIT(4) ? "2" : "1");
			printf("\n");
		} else { /* boot from SPI */
			printf("FMC 2nd Boot (ABR): Enable");
			if (readl(ASPEED_HW_STRAP2) & BIT(12))
				printf(", Single flash");
			else
				printf(", Dual flashes");

			printf(", Source: %s", \
					readl(ASPEED_FMC_WDT2) & BIT(4) ? "Alternate" : "Primary");

			if (readl(ASPEED_HW_STRAP2) & GENMASK(15, 13))
				printf(", bspi_size: %ld MB", \
					BIT((readl(ASPEED_HW_STRAP2) >> 13) & 0x7));

			printf("\n");
		}
	}
}

void aspeed_print_fmc_aux_ctrl(void)
{

	if (readl(ASPEED_HW_STRAP2) & BIT(22)) {
		printf("FMC aux control: Enable");
		/* gpioY6 : BSPI_ABR */
		if (readl(ASPEED_GPIO_YZ_DATA) & BIT(6))
			printf(", Force Alt boot");

		/* gpioY7 : BSPI_WP_N */
		if (!(readl(ASPEED_GPIO_YZ_DATA) & BIT(7)))
			printf(", BSPI_WP: Enable");

		if (!(readl(ASPEED_GPIO_YZ_DATA) & BIT(7)) && \
			(readl(ASPEED_HW_STRAP2) & GENMASK(24, 23)) != 0) {
			printf(", FMC HW CRTM: Enable, size: %ld KB", \
					BIT((readl(ASPEED_HW_STRAP2) >> 23) & 0x3) * 128);
		}

		printf("\n");
	}
}

void aspeed_print_spi1_abr_mode(void)
{
	if (readl(ASPEED_HW_STRAP2) & BIT(16)) {
		printf("SPI1 ABR: Enable");
		if(readl(ASPEED_SPI1_BOOT_CTRL) & BIT(6))
			printf(", Single flash");
		else
			printf(", Dual flashes");

		printf(", Source : %s", \
				readl(ASPEED_SPI1_BOOT_CTRL) & BIT(4) ? "Alternate" : "Primary");

		if (readl(ASPEED_SPI1_BOOT_CTRL) & GENMASK(3, 1))
			printf(", hspi_size : %ld MB", \
				BIT((readl(ASPEED_SPI1_BOOT_CTRL) >> 1) & 0x7));

		printf("\n");
	}

	if (readl(ASPEED_HW_STRAP2) & BIT(17)) {
		printf("SPI1 select pin: Enable");
		/* gpioZ1 : HSPI_ABR */
		if (readl(ASPEED_GPIO_YZ_DATA) & BIT(9))
			printf(", Force Alt boot");

		printf("\n");
	}
}

void aspeed_print_spi1_aux_ctrl(void)
{
	if (readl(ASPEED_HW_STRAP2) & BIT(27)) {
		printf("SPI1 aux control: Enable");
		/* gpioZ1 : HSPI_ABR */
		if (readl(ASPEED_GPIO_YZ_DATA) & BIT(9))
			printf(", Force Alt boot");

		/* gpioZ2: BSPI_WP_N */
		if (!(readl(ASPEED_GPIO_YZ_DATA) & BIT(10)))
			printf(", HPI_WP: Enable");

		if (!(readl(ASPEED_GPIO_YZ_DATA) & BIT(10)) && \
			(readl(ASPEED_HW_STRAP2) & GENMASK(26, 25)) != 0) {
			printf(", SPI1 HW CRTM: Enable, size: %ld KB", \
					BIT((readl(ASPEED_HW_STRAP2) >> 25) & 0x3) * 128);
		}

		printf("\n");
	}
}

void aspeed_print_spi_strap_mode(void)
{
	if(readl(ASPEED_HW_STRAP2) & BIT(10))
		printf("SPI: 3/4 byte mode auto detection \n");
}

void aspeed_print_espi_mode(void)
{
	int espi_mode = 0;
	int sio_disable = 0;
	u32 sio_addr = 0x2e;

	if (readl(ASPEED_HW_STRAP2) & BIT(6))
		espi_mode = 0;
	else
		espi_mode = 1;

	if (readl(ASPEED_HW_STRAP2) & BIT(2))
		sio_addr = 0x4e;

	if (readl(ASPEED_HW_STRAP2) & BIT(3))
		sio_disable = 1;

	if (espi_mode)
		printf("eSPI Mode: SIO:%s ", sio_disable ? "Disable" : "Enable");
	else
		printf("LPC Mode: SIO:%s ", sio_disable ? "Disable" : "Enable");

	if (!sio_disable)
		printf(": SuperIO-%02x\n", sio_addr);
	else
		printf("\n");
}

void aspeed_print_mac_info(void)
{
	int i;
	printf("Eth: ");
	for (i = 0; i < ASPEED_MAC_COUNT; i++) {
		printf("MAC%d: %s", i,
				aspeed_get_mac_phy_interface(i) ? "RGMII" : "RMII/NCSI");
		if (i != (ASPEED_MAC_COUNT -1))
			printf(", ");
	}
	printf("\n");
}
