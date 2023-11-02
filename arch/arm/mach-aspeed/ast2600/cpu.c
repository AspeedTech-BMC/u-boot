// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) ASPEED Technology Inc.
 */

#include <common.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <environment.h>

DECLARE_GLOBAL_DATA_PTR;

enum env_location env_get_location(enum env_operation op, int prio)
{
	enum env_location env_loc = ENVL_UNKNOWN;

	if (prio)
		return env_loc;

	if (readl(ASPEED_HW_STRAP1) & BIT(2))
		env_loc =  ENVL_MMC;
	else
		env_loc =  ENVL_SPI_FLASH;

	return env_loc;
}

int arch_misc_init(void)
{
	if (IS_ENABLED(CONFIG_ARCH_MISC_INIT)) {
		if ((readl(ASPEED_HW_STRAP1) & BIT(2)))
			env_set("boot_device", "mmc");
		else
			env_set("boot_device", "spi");
	}

	return 0;
}
