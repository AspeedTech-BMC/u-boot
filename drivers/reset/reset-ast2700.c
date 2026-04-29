// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 ASPEED Technology Inc.
 */

#include <dm.h>
#include <reset.h>
#include <asm/io.h>
#include <linux/err.h>
#include <reset-uclass.h>

/*
 * Reset model
 */
enum ast2700_reset_model {
	ASPEED_RESET_MODEL_AST2700,
	ASPEED_RESET_MODEL_AST2705,
};

struct ast2700_reset_priv {
	void __iomem *base;
	u32 ctrl1;		/* System Reset Control Register 1 */
	u32 clr1;		/* System Reset Clear Register 1 */
	u32 ctrl2;		/* System Reset Control Register 2 */
	u32 clr2;		/* System Reset Clear Register 2 */
};

static int ast2700_reset_assert(struct reset_ctl *reset_ctl)
{
	struct ast2700_reset_priv *priv = dev_get_priv(reset_ctl->dev);

	if (reset_ctl->id < 32)
		writel(BIT(reset_ctl->id), priv->base + priv->ctrl1);
	else
		writel(BIT(reset_ctl->id - 32), priv->base + priv->ctrl2);

	return 0;
}

static int ast2700_reset_deassert(struct reset_ctl *reset_ctl)
{
	struct ast2700_reset_priv *priv = dev_get_priv(reset_ctl->dev);

	if (reset_ctl->id < 32)
		writel(BIT(reset_ctl->id), priv->base + priv->clr1);
	else
		writel(BIT(reset_ctl->id - 32), priv->base + priv->clr2);

	return 0;
}

static int ast2700_reset_status(struct reset_ctl *reset_ctl)
{
	struct ast2700_reset_priv *priv = dev_get_priv(reset_ctl->dev);
	int status;

	if (reset_ctl->id < 32)
		status = BIT(reset_ctl->id) & readl(priv->base + priv->ctrl1);
	else
		status = BIT(reset_ctl->id - 32) & readl(priv->base + priv->ctrl2);

	return !!status;
}

static int ast2700_reset_of_to_plat(struct udevice *dev)
{
	struct ast2700_reset_priv *priv = dev_get_priv(dev);
	ulong data = dev_get_driver_data(dev);

	priv->ctrl1 = 0x00;
	priv->clr1 = 0x04;

	switch (data) {
	case ASPEED_RESET_MODEL_AST2700:
		priv->ctrl2 = 0x20;
		priv->clr2 = 0x24;
		break;
	case ASPEED_RESET_MODEL_AST2705:
		priv->ctrl2 = 0x18;
		priv->clr2 = 0x1c;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ast2700_reset_probe(struct udevice *dev)
{
	struct ast2700_reset_priv *priv = dev_get_priv(dev);

	priv->base = dev_read_addr_ptr(dev);
	if (!priv->base)
		return -ENOMEM;

	return 0;
}

static const struct udevice_id ast2700_reset_ids[] = {
	{ .compatible = "aspeed,ast2700-reset", .data = (ulong)ASPEED_RESET_MODEL_AST2700 },
	{ .compatible = "aspeed,ast2705-reset", .data = (ulong)ASPEED_RESET_MODEL_AST2705 },
	{ }
};

struct reset_ops ast2700_reset_ops = {
	.rst_assert = ast2700_reset_assert,
	.rst_deassert = ast2700_reset_deassert,
	.rst_status = ast2700_reset_status,
};

U_BOOT_DRIVER(ast2700_reset) = {
	.name = "ast2700_reset",
	.id = UCLASS_RESET,
	.of_match = ast2700_reset_ids,
	.of_to_plat = ast2700_reset_of_to_plat,
	.probe = ast2700_reset_probe,
	.ops = &ast2700_reset_ops,
	.priv_auto = sizeof(struct ast2700_reset_priv),
};
