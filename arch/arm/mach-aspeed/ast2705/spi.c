// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) Aspeed Technology Inc.
 */

#include <asm/arch/platform.h>
#include <asm/arch-aspeed/spi.h>
#include <asm/io.h>
#include <env.h>
#include <env_internal.h>
#include <malloc.h>

#define SCU_SPI_ABR_REG		(ASPEED_IO_SCU_BASE + 0x030)
#define SPI_ABR_MODE		BIT(29)
#define SPI_AUX_EN		BIT(16)
#define SPI_ABR_EN		BIT(0)

#define SCU1_PINMUX_GRP_P	(ASPEED_IO_SCU_BASE + 0x43c)
#define   SCU1_PINMUX_PIN7	GENMASK(31, 28)

#define GPIOP7_REG		(ASPEED_IO_GPIO_BASE + 0x37c)
#define GPIO_DATA_IN		BIT(13)
#define GPIO_DIRECT		BIT(1)

#define WDT_ABR_CTRL		(ASPEED_WDTA_BASE + 0x04c)
#define WDT_ABR_INDICATOR	BIT(1)

bool spi_abr_enabled(void)
{
	u32 abr_val;

	abr_val = readl((void *)SCU_SPI_ABR_REG) & SPI_ABR_EN;

	return (abr_val != 0) ? true : false;
}

bool spi_aux_bit_enabled(void)
{
	u32 spi_aux;

	spi_aux = readl((void *)SCU_SPI_ABR_REG) & SPI_AUX_EN;

	return (spi_aux != 0) ? true : false;
}

u32 spi_ext_abr_signal(void)
{
	uint32_t reg;

	/* force to change to GPIO mode */
	reg = readl(SCU1_PINMUX_GRP_P);
	reg &= ~(SCU1_PINMUX_PIN7);
	writel(reg, SCU1_PINMUX_GRP_P);

	/* input direction */
	reg = readl(GPIOP7_REG);
	reg &= ~(GPIO_DIRECT);
	writel(reg, GPIOP7_REG);

	return !!(readl(GPIOP7_REG) & GPIO_DATA_IN);
}

u32 spi_get_abr_indictor(void)
{
	u32 wdt_abr_ctrl;

	wdt_abr_ctrl = readl((void *)WDT_ABR_CTRL);

	return (wdt_abr_ctrl & WDT_ABR_INDICATOR) >> 1;
}

enum spi_abr_mode get_spi_flash_abr_mode(void)
{
	u32 abr_mode;

	abr_mode = readl((void *)SCU_SPI_ABR_REG) & SPI_ABR_MODE;

	return (abr_mode != 0) ? SINGLE_FLASH_ABR : DUAL_FLASH_ABR;
}

/*
 * SCU flash size: SCU030[15:13]
 * 7: 512MB
 * 6: 256MB
 * 5: 128MB
 * 4: 64MB
 * 3: 32MB
 * 2: 16MB
 * 1: 8MB
 * 0: disabled
 */
u32 spi_get_flash_sz_strap(void)
{
	u32 scu_flash_sz;
	u32 flash_sz_phy;

	scu_flash_sz = (readl((void *)SCU_SPI_ABR_REG) >> 13) & 0x7;
	switch (scu_flash_sz) {
	case 0x7:
		flash_sz_phy = SNOR_SZ_512MB;
		break;
	case 0x6:
		flash_sz_phy = SNOR_SZ_256MB;
		break;
	case 0x5:
		flash_sz_phy = SNOR_SZ_128MB;
		break;
	case 0x4:
		flash_sz_phy = SNOR_SZ_64MB;
		break;
	case 0x3:
		flash_sz_phy = SNOR_SZ_32MB;
		break;
	case 0x2:
		flash_sz_phy = SNOR_SZ_16MB;
		break;
	case 0x1:
		flash_sz_phy = SNOR_SZ_8MB;
		break;
	case 0x0:
		flash_sz_phy = SNOR_SZ_UNSET;
		break;
	default:
		flash_sz_phy = SNOR_SZ_UNSET;
		break;
	}

	return flash_sz_phy;
}

u32 aspeed_spi_abr_offset(void)
{
	/* no need for dual flash ABR */
	if (get_spi_flash_abr_mode() == DUAL_FLASH_ABR)
		return 0;

	/* no need when ABR is not triggerred */
	if (spi_get_abr_indictor() == 0)
		return 0;

	return (spi_get_flash_sz_strap() / 2);
}

void spi_bootarg_config(void)
{
	char *bootargs = NULL;
	char *bootargs_rofs = NULL;
	u32 bootargs_len;

	env_set_hex("fdtspiaddr",
		    CONFIG_SPI_KERNEL_FIT_ADDR + aspeed_spi_abr_offset());

	if (get_spi_flash_abr_mode() == SINGLE_FLASH_ABR &&
	    (spi_abr_enabled() || spi_aux_bit_enabled())) {
		bootargs = env_get("bootargs");
		if (!bootargs) {
			printf("fail to get bootargs!\n");
			return;
		}

		bootargs_len = strlen(bootargs) + 1 + 15;
		bootargs_rofs = malloc(bootargs_len);
		memset(bootargs_rofs, '\0', bootargs_len);
		strlcpy(bootargs_rofs, bootargs, strlen(bootargs) + 1);

		if (spi_get_abr_indictor() == 0) {
			env_set("rootfs", "rofs-a");
			strlcat(bootargs_rofs, " rootfs=rofs-a", bootargs_len);
		} else {
			env_set("rootfs", "rofs-b");
			strlcat(bootargs_rofs, " rootfs=rofs-b", bootargs_len);
		}

		env_set("bootargs", bootargs_rofs);

		free(bootargs_rofs);
	} else {
		env_set("rootfs", "rofs");
	}
}

size_t aspeed_memmove_dma_op(void *dest, const void *src, size_t count)
{
	u32 dma_busy = 0;
	u64 dma_dest = (u64)dest;
	u64 dma_src = (u64)src;
	u32 remaining = (u32)count;
	u32 dma_len = (u32)count;
	u32 dma_addr_hi;

	if (dma_dest == dma_src)
		return 0;

	if (dma_dest % 4 != 0 || dma_src % 4 != 0)
		return 0;

	if (dma_src >= ASPEED_FMC_CS0_BASE &&
	    dma_src + dma_len < (ASPEED_FMC_CS0_BASE + ASPEED_FMC_CS0_SIZE) &&
	    dma_dest >= ASPEED_DRAM_BASE) {
		while (remaining > 0) {
			if (remaining > SPI_DMA_MAX_LEN)
				dma_len = SPI_DMA_MAX_LEN;
			else
				dma_len = remaining;

			dma_addr_hi = 0;
			if (dma_dest >= ASPEED_DRAM_BASE)
				dma_addr_hi = (u32)((dma_dest) >> 32) & 0xff;

			writel(dma_addr_hi, (void *)DRAM_HI_ADDR);

			writel((u32)(dma_dest - ASPEED_DRAM_BASE),
			       (void *)DMA_RAM_ADDR);
			writel((u32)(dma_src - ASPEED_FMC_CS0_BASE),
			       (void *)DMA_FLASH_ADDR);
			writel(dma_len - 1, (void *)DMA_LEN);

			writel(DMA_ENABLE, (void *)DMA_CTRL);

			do {
				dma_busy = readl((void *)INTR_CTRL) & SPI_DMA_DONE;
			} while (dma_busy == 0);

			writel(0x0, (void *)DMA_CTRL);
			dma_dest += dma_len;
			dma_src += dma_len;
			remaining -= dma_len;
		}

		return count;
	}
	return 0;
}

