// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) ASPEED Technology Inc.
 * Chia-Wei Wang <chiawei_wang@aspeedtech.com>
 */

#include <config.h>
#include <asm/armv7.h>
#include <linux/linkage.h>

#define AST_SCU_BASE		(0x1E6E2000)
#define AST_SCU_HW_STRAP1	(AST_SCU_BASE + 0x500)
#define AST_SCU_HW_STRAP2	(AST_SCU_BASE + 0x510)

#define AST_FMC_BASE		(0x1E620000)
#define AST_FMC_INT_CTRL_STAT	(AST_FMC_BASE + 0x008)
#define AST_FMC_DMA_CTRL	(AST_FMC_BASE + 0x080)
#define AST_FMC_DMA_FLASH_ADDR	(AST_FMC_BASE + 0x084)
#define AST_FMC_DMA_DRAM_ADDR	(AST_FMC_BASE + 0x088)
#define AST_FMC_DMA_LENGTH	(AST_FMC_BASE + 0x08C)

/*
 * void aspeed_bootmode(void)
 *
 * return the boot mode according to the HW strap information
 */
ENTRY(aspeed_bootmode)
	ldr	r1, =AST_SCU_HW_STRAP2
	ldr	r0, [r1]
	tst	r0, #0x100
	movne	r0, #0x2	@; AST_BOOTMODE_UART
	bne	0f

	ldr	r1, =AST_SCU_HW_STRAP1
	ldr	r0, [r1]
	tst	r0, #0x4
	moveq	r0, #0x0	@; AST_BOOTMODE_SPI
	movne	r0, #0x1	@; AST_BOOTMODE_EMMC
0:
	mov	pc, lr
ENDPROC(aspeed_bootmode)

/*
 * void aspeed_spi_fastcpy(u32 mem_addr, u32 spi_addr, u32 count)
 *
 * perform FMC SPI DMA to speed up flash copy.
 * @dst: destination memory address
 * @src: source SPI address
 * @count: number of bytes to be copied, 4-byte aligned
 *
 * NOTE that the caller must ensure the validity of parameters.
 */
ENTRY(aspeed_spi_fastcpy)
	ldr	r3, =AST_FMC_DMA_DRAM_ADDR
	str	r0, [r3]

	ldr	r3, =AST_FMC_DMA_FLASH_ADDR
	str	r1, [r3]

	ldr	r3, =AST_FMC_DMA_LENGTH
	sub	r2, #1
	str	r2, [r3]

	ldr	r0, =AST_FMC_DMA_CTRL
	mov	r1, #1
	str	r1, [r0]

	ldr	r0, =AST_FMC_INT_CTRL_STAT
polling:
	ldr	r1, [r0]
	tst	r1, #(1 << 11)
	beq	polling

	ldr	r0, =AST_FMC_DMA_CTRL
	mov	r1, #0
	str	r1, [r0]

	mov	pc, lr
ENDPROC(aspeed_spi_fastcpy)
