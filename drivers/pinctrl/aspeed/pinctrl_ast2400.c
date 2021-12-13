// SPDX-License-Identifier: GPL-2.0+

#include <common.h>
#include <dm.h>
#include <errno.h>
#include <asm/io.h>
#include <asm/arch/pinctrl.h>
#include <asm/arch/scu_ast2400.h>
#include <dm/pinctrl.h>
#include "pinctrl-aspeed.h"
/*
 * This driver works with very simple configuration that has the same name
 * for group and function. This way it is compatible with the Linux Kernel
 * driver.
 */

struct ast2400_pinctrl_priv {
	struct ast2400_scu *scu;
};

static int ast2400_pinctrl_probe(struct udevice *dev)
{
	struct ast2400_pinctrl_priv *priv = dev_get_priv(dev);
	struct udevice *clk_dev;
	int ret = 0;

	/* find SCU base address from clock device */
	ret = uclass_get_device_by_driver(UCLASS_CLK, DM_GET_DRIVER(aspeed_scu),
                                          &clk_dev);
        if (ret) {
		debug("clock device not found\n");
		return ret;
        }

	priv->scu = devfdt_get_addr_ptr(clk_dev);
	if (IS_ERR(priv->scu)) {
		debug("%s(): can't get SCU\n", __func__);
		return PTR_ERR(priv->scu);
	}

	return 0;
}

static struct aspeed_sig_desc mac1_link[] = {
	{ 0x80, BIT(0), 0	},
};

static struct aspeed_sig_desc mac2_link[] = {
	{ 0x80, BIT(1), 0	},
};

static struct aspeed_sig_desc mdio1_link[] = {
	{ 0x88, BIT(31) | BIT(30), 0	},
};

static struct aspeed_sig_desc mdio2_link[] = {
	{ 0x90, BIT(2), 0	},
};

static struct aspeed_sig_desc i2c3_link[] = {
	{ 0x90, BIT(16), 0	},
};

static struct aspeed_sig_desc i2c4_link[] = {
	{ 0x90, BIT(17), 0	},
};

static struct aspeed_sig_desc i2c5_link[] = {
	{ 0x90, BIT(18), 0	},
};

static struct aspeed_sig_desc i2c6_link[] = {
	{ 0x90, BIT(19), 0	},
};

static struct aspeed_sig_desc i2c7_link[] = {
	{ 0x90, BIT(20), 0	},
};

static struct aspeed_sig_desc i2c8_link[] = {
	{ 0x90, BIT(21), 0	},
};

static struct aspeed_sig_desc i2c9_link[] = {
	{ 0x90, BIT(22), 0	},
};

static struct aspeed_sig_desc i2c10_link[] = {
	{ 0x90, BIT(23), 0	},
	{ 0x90, BIT(0), 1	},	
};

static struct aspeed_sig_desc i2c11_link[] = {
	{ 0x90, BIT(24), 0	},
	{ 0x90, BIT(0), 1	},	
};

static struct aspeed_sig_desc i2c12_link[] = {
	{ 0x90, BIT(25), 0	},
	{ 0x90, BIT(0), 1	},	
};

static struct aspeed_sig_desc i2c13_link[] = {
	{ 0x90, BIT(26), 0	},
	{ 0x90, BIT(0), 1	},	
};

static struct aspeed_sig_desc i2c14_link[] = {
	{ 0x90, BIT(27), 0	},
};

static struct aspeed_sig_desc sdio1_link[] = {
	{ 0x90, BIT(0), 0	},
};	

static struct aspeed_sig_desc sdio2_link[] = {
	{ 0x90, BIT(1), 0	},
};

static struct aspeed_sig_desc spi1cs1_link[] = {
	{ 0x80, BIT(15), 0},
};

static struct aspeed_sig_desc spi1_link[] = {
	{ 0x70, BIT(12), 0},
};

static struct aspeed_sig_desc txd3_link[] = {
	{ 0x80, BIT(22), 0},
};

static struct aspeed_sig_desc rxd3_link[] = {
	{ 0x80, BIT(23), 0},
};

static const struct aspeed_group_config ast2400_groups[] = {
	{ "MAC1LINK", 1, mac1_link },
	{ "MAC2LINK", 1, mac2_link },
	{ "MDIO1", 1, mdio1_link },
	{ "MDIO2", 1, mdio2_link },
	{ "I2C3", 1, i2c3_link },
	{ "I2C4", 1, i2c4_link },
	{ "I2C5", 1, i2c5_link },
	{ "I2C6", 1, i2c6_link },
	{ "I2C7", 1, i2c7_link },
	{ "I2C8", 1, i2c8_link },
	{ "I2C9", 1, i2c9_link },
	{ "I2C10", 2, i2c10_link },
	{ "I2C11", 2, i2c11_link },
	{ "I2C12", 2, i2c12_link },
	{ "I2C13", 2, i2c13_link },
	{ "I2C14", 1, i2c14_link },
	{ "SD2", 1, sdio2_link },
	{ "SD1", 1, sdio1_link },
	{ "SPI1", 1, spi1_link},
	{ "SPI1CS1", 1, spi1cs1_link},
	{ "TXD3", 1, txd3_link },
	{ "RXD3", 1, rxd3_link },
};

static int ast2400_pinctrl_get_groups_count(struct udevice *dev)
{
	debug("PINCTRL: get_(functions/groups)_count\n");

	return ARRAY_SIZE(ast2400_groups);
}

static const char *ast2400_pinctrl_get_group_name(struct udevice *dev,
						  unsigned selector)
{
	debug("PINCTRL: get_(function/group)_name %u\n", selector);

	return ast2400_groups[selector].group_name;
}

static int ast2400_pinctrl_group_set(struct udevice *dev, unsigned selector,
				     unsigned func_selector)
{
	struct ast2400_pinctrl_priv *priv = dev_get_priv(dev);
	const struct aspeed_group_config *config;
	const struct aspeed_sig_desc *descs;
	u32 *ctrl_reg = (u32*)priv->scu;
	int i;

	debug("PINCTRL: group_set <%u, %u> \n", selector, func_selector);
	if (selector >= ARRAY_SIZE(ast2400_groups))
		return -EINVAL;

	config = &ast2400_groups[selector];

	for( i = 0; i < config->ndescs; i++) {
		descs = &config->descs[i];
		if(descs->clr) {
			clrbits_le32((u32)ctrl_reg + descs->offset, descs->reg_set);
		} else {
			setbits_le32((u32)ctrl_reg + descs->offset, descs->reg_set);
		}
	}

	return 0;
}

static struct pinctrl_ops ast2400_pinctrl_ops = {
	.set_state = pinctrl_generic_set_state,
	.get_groups_count = ast2400_pinctrl_get_groups_count,
	.get_group_name = ast2400_pinctrl_get_group_name,
	.get_functions_count = ast2400_pinctrl_get_groups_count,
	.get_function_name = ast2400_pinctrl_get_group_name,
	.pinmux_group_set = ast2400_pinctrl_group_set,
};

static const struct udevice_id ast2400_pinctrl_ids[] = {
	{ .compatible = "aspeed,g4-pinctrl" },
	{ }
};

U_BOOT_DRIVER(pinctrl_aspeed) = {
	.name = "aspeed_ast2400_pinctrl",
	.id = UCLASS_PINCTRL,
	.of_match = ast2400_pinctrl_ids,
	.priv_auto_alloc_size = sizeof(struct ast2400_pinctrl_priv),
	.ops = &ast2400_pinctrl_ops,
	.probe = ast2400_pinctrl_probe,
};
