// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) ASPEED Technology Inc.
 */

#include <dm.h>
#include <asm/arch-aspeed/scu_ast2700.h>
#include <asm/arch-aspeed/spi.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <env.h>
#include <env_internal.h>

DECLARE_GLOBAL_DATA_PTR;

enum env_location env_get_location(enum env_operation op, int prio)
{
	enum env_location env_loc = ENVL_UNKNOWN;

	if (prio)
		return env_loc;

	if (IS_ENABLED(CONFIG_ENV_IS_NOWHERE)) {
		env_loc = ENVL_NOWHERE;
	} else {
		if ((readl(ASPEED_IO_HW_STRAP1) & SCU_IO_HWSTRAP_EMMC)) {
			if ((readl(ASPEED_IO_HW_STRAP1) & SCU_IO_HWSTRAP_UFS))
				env_loc =  ENVL_UFS;
			else
				env_loc =  ENVL_MMC;
		} else {
			env_loc =  ENVL_SPI_FLASH;
		}
	}

	return env_loc;
}

int arch_misc_init(void)
{
	if (IS_ENABLED(CONFIG_ARCH_MISC_INIT)) {
		if ((readl(ASPEED_IO_HW_STRAP1) & SCU_IO_HWSTRAP_EMMC)) {
			if ((readl(ASPEED_IO_HW_STRAP1) & SCU_IO_HWSTRAP_UFS))
				env_set("boot_device", "ufs");
			else
				env_set("boot_device", "mmc");
		} else {
			env_set("boot_device", "spi");
			spi_bootarg_config();
		}

		if ((readl(ASPEED_IO_HW_STRAP1) & SCU_IO_HWSTRAP_SECBOOT))
			env_set("verify", "yes");
		else
			env_set("verify", "no");
	}

	return 0;
}
