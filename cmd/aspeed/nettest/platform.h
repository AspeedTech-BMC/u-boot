/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef _PLATFORM_H_
#define _PLATFORM_H_

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <asm/io.h>
#include <asm/unaligned.h>
#include <asm/barriers.h>
#include <asm/types.h>
#include <string.h>
#include <time.h>
#include <log.h>
#include <stdbool.h>
#include <vsprintf.h>
#include <getopt.h>
#include <hexdump.h>
#include <console.h>
#include <asm/arch/scu_ast2700.h>

#define AST_SGMII_BASE		0X014C01000 //HS034
#define AST_IO_SCU_BASE		0X014C02000 //HS035
#define AST_PMI_BASE		0X014040000 //HS016
#define   AST_MDIO0_BASE	AST_PMI_BASE
#define   AST_MDIO1_BASE	(AST_PMI_BASE + 0x8)
#define   AST_MDIO2_BASE	(AST_PMI_BASE + 0x10)
#define AST_MAC0_BASE		0X014050000 //HS017
#define AST_MAC1_BASE		0X014060000 //HS018
#define AST_MAC2_BASE		0X014070000 //HS019
#define AST_IO_PLDA1_BASE	0X014C1C000 //HS094

#ifndef DMA_ALIGNED
#define DMA_ALIGNED	__aligned(CONFIG_SYS_CACHELINE_SIZE)
#endif

#ifndef ROUNDUP_DMA_SIZE
#define ROUNDUP_DMA_SIZE(x)                                                    \
	((((x) + CONFIG_SYS_CACHELINE_SIZE - 1) / CONFIG_SYS_CACHELINE_SIZE) * \
	 CONFIG_SYS_CACHELINE_SIZE)
#endif

#define U_BOOT
#define ASPEED_AST2700

#ifndef EOF
#define EOF	-1
#endif

#define CLRBITS clrbits_le32
#define CLRSETBITS clrsetbits_le32
#define SETBITS setbits_le32

#endif /* _PLATFORM_H_ */
