/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) ASPEED Technology Inc.
 *
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#include <configs/aspeed-common.h>

#define CONFIG_SYS_MEMTEST_START	(CONFIG_SYS_SDRAM_BASE + 0x300000)
#define CONFIG_SYS_MEMTEST_END		(CONFIG_SYS_MEMTEST_START + 0x5000000)

#define CONFIG_SYS_UBOOT_BASE		CONFIG_SYS_TEXT_BASE

/* Memory Info */
#define CONFIG_SYS_LOAD_ADDR		0x83000000

/* SPL */
#define CONFIG_SPL_TEXT_BASE		0x00000000
#define CONFIG_SPL_MAX_SIZE		0x00010000
#define CONFIG_SPL_STACK		0x10016000
#define CONFIG_SPL_BSS_START_ADDR	0x90000000
#define CONFIG_SPL_BSS_MAX_SIZE		0x00100000
#define CONFIG_SPL_LOAD_FIT_ADDRESS	0x00010000

#define CONFIG_SUPPORT_EMMC_BOOT

/* Extra ENV for Boot Command */
#define STR_HELPER(n)	#n
#define _STR(n)		STR_HELPER(n)

#undef CONFIG_EXTRA_ENV_SETTINGS
#define CONFIG_EXTRA_ENV_SETTINGS	\
	"loadaddr=" _STR(CONFIG_SYS_LOAD_ADDR) "\0"	\
	"bootspi=fdt addr 20160000 && fdt header get fitsize totalsize && cp.b 20160000 ${loadaddr} ${fitsize} && bootm; echo Error loading kernel FIT image\0"	\
	"bootside=a\0"	\
	"rootfs=rofs-a\0"	\
	"setmmcargs=setenv bootargs ${bootargs} rootwait root=PARTLABEL=${rootfs}\0"	\
	"boota=setenv bootpart 2; setenv rootfs rofs-a; run setmmcargs; ext4load mmc 0:${bootpart} ${loadaddr} fitImage && bootm; echo Error loading kernel FIT image\0"	\
	"bootb=setenv bootpart 3; setenv rootfs rofs-b; run setmmcargs; ext4load mmc 0:${bootpart} ${loadaddr} fitImage && bootm; echo Error loading kernel FIT image\0"	\
	"bootmmc=if test \"${bootside}\" = \"b\"; then run bootb; run boota; else run boota; run bootb; fi\0"	\
	"boot_ast2700=" \
	"echo \"Boot from ${boot_device}!\";" \
	"if test ${boot_device} = mmc; then run bootmmc; fi;" \
	"if test ${boot_device} = spi; then run bootspi; fi;" \
	"verify=yes\0"	\
	""

#endif	/* __CONFIG_H */
