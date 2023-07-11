// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright ASPEED Technology Inc.
 */

#include <common.h>
#include <clk.h>
#include <dm.h>
#include <errno.h>
#include <fdtdec.h>
#include <asm/io.h>
#include <reset.h>
#include "ast2600_i2c_global.h"

enum global_version {
	ASPEED_I2C_GLOBAL = 0,
	AST2600_I2C_GLOBAL,
};

struct ast2600_i2c_global_priv {
	void __iomem *regs;
	struct reset_ctl reset;
	int version;
};

/*
 * APB clk : 100Mhz
 * div  : scl       : baseclk [APB/((div/2) + 1)] : tBuf [1/bclk * 16]
 * I2CG10[31:24] base clk4 for i2c auto recovery timeout counter (0xC6)
 * I2CG10[23:16] base clk3 for Standard-mode (100Khz) min tBuf 4.7us
 * 0x3c : 100.8Khz  : 3.225Mhz                    : 4.96us
 * 0x3d : 99.2Khz   : 3.174Mhz                    : 5.04us
 * 0x3e : 97.65Khz  : 3.125Mhz                    : 5.12us
 * 0x40 : 97.75Khz  : 3.03Mhz                     : 5.28us
 * 0x41 : 99.5Khz   : 2.98Mhz                     : 5.36us (default)
 * I2CG10[15:8] base clk2 for Fast-mode (400Khz) min tBuf 1.3us
 * 0x12 : 400Khz    : 10Mhz                       : 1.6us
 * I2CG10[7:0] base clk1 for Fast-mode Plus (1Mhz) min tBuf 0.5us
 * 0x08 : 1Mhz      : 20Mhz                       : 0.8us
 */

static int aspeed_i2c_global_probe(struct udevice *dev)
{
	struct ast2600_i2c_global_priv *i2c_global = dev_get_priv(dev);
	int ret = 0;

	i2c_global->regs = devfdt_get_addr_ptr(dev);
	if (IS_ERR(i2c_global->regs))
		return PTR_ERR(i2c_global->regs);

	debug("%s(dev=%p)\n", __func__, dev);

	ret = reset_get_by_index(dev, 0, &i2c_global->reset);
	if (ret) {
		printf("%s(): Failed to get reset signal\n", __func__);
		return ret;
	}

	i2c_global->version = dev_get_driver_data(dev);

	reset_deassert(&i2c_global->reset);

	if (IS_ENABLED(CONFIG_SYS_I2C_AST2600) &&
	    i2c_global->version == AST2600_I2C_GLOBAL) {
		writel(AST2600_GLOBAL_INIT, i2c_global->regs +
			AST2600_I2CG_CTRL);
		writel(I2CCG_DIV_CTRL, i2c_global->regs +
			AST2600_I2CG_CLK_DIV_CTRL);
	}

	return 0;
}

static const struct udevice_id aspeed_i2c_global_ids[] = {
	{
		.compatible = "aspeed,ast2500-i2c-ic",
		.data = ASPEED_I2C_GLOBAL
	},
	{
		.compatible = "aspeed,ast2600-i2c-global",
		.data = AST2600_I2C_GLOBAL
	},
	{ }
};

U_BOOT_DRIVER(aspeed_i2c_global) = {
	.name		= "aspeed_i2c_global",
	.id			= UCLASS_MISC,
	.of_match	= aspeed_i2c_global_ids,
	.probe		= aspeed_i2c_global_probe,
	.priv_auto_alloc_size = sizeof(struct ast2600_i2c_global_priv),
};
