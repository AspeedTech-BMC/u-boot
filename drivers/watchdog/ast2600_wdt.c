// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2020 Aspeed Technology, Inc
 */

#include <common.h>
#include <dm.h>
#include <errno.h>
#include <log.h>
#include <wdt.h>
#include <asm/io.h>
#include <asm/arch/wdt_ast2600.h>
#include <linux/delay.h>
#include <linux/err.h>

/*
 * Control register of the AST2700 ABR watchdog. Bit 0 is its enable bit,
 * same layout as the regular watchdog instances.
 */
#define AST2700_WDT_ABR_CTRL		((void __iomem *)0x14c3740cUL)

struct aspeed_wdt_data {
	void (*wdt_writel)(u32 val, void __iomem *addr);
	/* ABR watchdog control register, NULL if the SoC has no ABR watchdog */
	void __iomem *abr_ctrl;
};

struct ast2600_wdt_priv {
	struct ast2600_wdt *regs;
	struct aspeed_wdt_data *data;
	/* the ABR watchdog is already supervising the boot */
	bool abr_armed;
};

static void wdt_writel_normal(u32 val, void __iomem *addr);
static void wdt_writel_delay(u32 val, void __iomem *addr);

static const struct aspeed_wdt_data ast2600_wdt_data = {
	.wdt_writel = wdt_writel_normal,
};

static const struct aspeed_wdt_data ast2700_wdt_data = {
	.wdt_writel = wdt_writel_delay,
	.abr_ctrl = AST2700_WDT_ABR_CTRL,
};

static void wdt_writel_normal(u32 val, void __iomem *addr)
{
	writel(val, addr);
}

static void wdt_writel_delay(u32 val, void __iomem *addr)
{
	writel(val, addr);
	udelay(5);
}

static void ast2600_wdt_arm(struct udevice *dev, u64 timeout_ms)
{
	struct ast2600_wdt_priv *priv = dev_get_priv(dev);
	struct ast2600_wdt *wdt = priv->regs;

	/* WDT counts in the 1MHz frequency, namely 1us */
	priv->data->wdt_writel((u32)(timeout_ms * 1000), &wdt->counter_reload_val);
	priv->data->wdt_writel(WDT_COUNTER_RESTART_VAL, &wdt->counter_restart);
	priv->data->wdt_writel(WDT_CTRL_EN | WDT_CTRL_RESET_SYS |
			       WDT_CTRL_RESET_WDT, &wdt->ctrl);
}

static int ast2600_wdt_start(struct udevice *dev, u64 timeout_ms, ulong flags)
{
	struct ast2600_wdt_priv *priv = dev_get_priv(dev);

	/*
	 * The ABR watchdog is already supervising the boot. Leave the reset
	 * handling to it rather than arming a second watchdog. expire_now()
	 * bypasses this, so sysreset keeps working.
	 */
	if (priv->abr_armed)
		return -EBUSY;

	ast2600_wdt_arm(dev, timeout_ms);

	return 0;
}

static int ast2600_wdt_stop(struct udevice *dev)
{
	struct ast2600_wdt_priv *priv = dev_get_priv(dev);
	struct ast2600_wdt *wdt = priv->regs;

	clrbits_le32(&wdt->ctrl, WDT_CTRL_EN);

	return 0;
}

static int ast2600_wdt_reset(struct udevice *dev)
{
	struct ast2600_wdt_priv *priv = dev_get_priv(dev);
	struct ast2600_wdt *wdt = priv->regs;

	priv->data->wdt_writel(WDT_COUNTER_RESTART_VAL, &wdt->counter_restart);

	return 0;
}

static int ast2600_wdt_expire_now(struct udevice *dev, ulong flags)
{
	struct ast2600_wdt_priv *priv = dev_get_priv(dev);
	struct ast2600_wdt *wdt = priv->regs;

	ast2600_wdt_arm(dev, 1);

	while (readl(&wdt->ctrl) & WDT_CTRL_EN)
		;

	return ast2600_wdt_stop(dev);
}

static int ast2600_wdt_of_to_plat(struct udevice *dev)
{
	struct ast2600_wdt_priv *priv = dev_get_priv(dev);

	priv->regs = dev_read_addr_ptr(dev);
	if (!priv->regs)
		return -EINVAL;

	return 0;
}

static const struct wdt_ops ast2600_wdt_ops = {
	.start = ast2600_wdt_start,
	.reset = ast2600_wdt_reset,
	.stop = ast2600_wdt_stop,
	.expire_now = ast2600_wdt_expire_now,
};

static const struct udevice_id ast2600_wdt_ids[] = {
	{ .compatible = "aspeed,ast2600-wdt", .data = (ulong)&ast2600_wdt_data },
	{ .compatible = "aspeed,ast2700-wdt", .data = (ulong)&ast2700_wdt_data },
	{ }
};

static int ast2600_wdt_probe(struct udevice *dev)
{
	struct ast2600_wdt_priv *priv = dev_get_priv(dev);

	priv->data = (struct aspeed_wdt_data *)dev_get_driver_data(dev);

	if (priv->data->abr_ctrl && (readl(priv->data->abr_ctrl) & WDT_CTRL_EN)) {
		printf("WDT:   ABR watchdog armed, not starting %s\n",
		       dev->name);
		priv->abr_armed = true;
	}

	debug("%s() wdt%u\n", __func__, dev_seq(dev));
	ast2600_wdt_stop(dev);

	return 0;
}

U_BOOT_DRIVER(ast2600_wdt) = {
	.name = "ast2600_wdt",
	.id = UCLASS_WDT,
	.of_match = ast2600_wdt_ids,
	.probe = ast2600_wdt_probe,
	.priv_auto = sizeof(struct ast2600_wdt_priv),
	.of_to_plat = ast2600_wdt_of_to_plat,
	.ops = &ast2600_wdt_ops,
};
