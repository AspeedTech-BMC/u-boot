// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 ASPEED Technology Inc.
 */

#include <dm.h>
#include <reset.h>
#include <asm/io.h>
#include <linux/err.h>
#include <reset-uclass.h>

struct ast2700_reset_priv {
	void __iomem *base;
};

static int ast2700_reset_assert(struct reset_ctl *reset_ctl)
{
	struct ast2700_reset_priv *priv = dev_get_priv(reset_ctl->dev);

	if (reset_ctl->id < 32)
		writel(BIT(reset_ctl->id), priv->base);
	else
		writel(BIT(reset_ctl->id - 32), priv->base + 0x20);

	return 0;
}

static int ast2700_reset_deassert(struct reset_ctl *reset_ctl)
{
	struct ast2700_reset_priv *priv = dev_get_priv(reset_ctl->dev);

	if (reset_ctl->id < 32)
		writel(BIT(reset_ctl->id), priv->base + 0x04);
	else
		writel(BIT(reset_ctl->id - 32), priv->base + 0x24);

	return 0;
}

static int ast2700_reset_status(struct reset_ctl *reset_ctl)
{
	struct ast2700_reset_priv *priv = dev_get_priv(reset_ctl->dev);
	int status;

	if (reset_ctl->id < 32)
		status = BIT(reset_ctl->id) & readl(priv->base);
	else
		status = BIT(reset_ctl->id - 32) & readl(priv->base + 0x20);

	return !!status;
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
	{ .compatible = "aspeed,ast2700-reset" },
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
	.probe = ast2700_reset_probe,
	.ops = &ast2700_reset_ops,
	.priv_auto = sizeof(struct ast2700_reset_priv),
};
