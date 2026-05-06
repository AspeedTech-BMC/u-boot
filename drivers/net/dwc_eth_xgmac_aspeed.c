// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2026 Aspeed Technology Inc.
 */
#include <clk.h>
#include <cpu_func.h>
#include <dm.h>
#include <errno.h>
#include <eth_phy.h>
#include <log.h>
#include <malloc.h>
#include <memalign.h>
#include <miiphy.h>
#include <net.h>
#include <netdev.h>
#include <phy.h>
#include <reset.h>
#include <wait_bit.h>
#include <regmap.h>
#include <syscon.h>
#include <asm/cache.h>
#include <asm/gpio.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <dm/device_compat.h>
#include "dwc_eth_xgmac.h"

static int xgmac_probe_resources_aspeed(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	int ret;

	ret = reset_get_bulk(dev, &xgmac->reset_bulk);
	if (ret) {
		pr_err("xgmac reset request failed: %d\n", ret);
		return ret;
	}

	ret = clk_get_by_name(dev, "stmmaceth", &xgmac->clk_common);
	if (ret) {
		pr_err("xgmac clock request failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int xgmac_remove_resources_aspeed(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);

	reset_release_bulk(&xgmac->reset_bulk);
	clk_free(&xgmac->clk_common);

	return 0;
}

static int xgmac_stop_resets_aspeed(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	int ret;

	ret = reset_assert_bulk(&xgmac->reset_bulk);
	if (ret < 0)
		pr_err("xgmac reset assert failed: %d\n", ret);

	return ret;
}

static int xgmac_start_resets_aspeed(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	int ret;

	ret = reset_assert_bulk(&xgmac->reset_bulk);
	if (ret < 0) {
		pr_err("xgmac reset assert failed: %d", ret);
		return ret;
	}

	udelay(2);

	ret = reset_deassert_bulk(&xgmac->reset_bulk);
	if (ret < 0) {
		pr_err("xgmac reset de-assert failed: %d", ret);
		return ret;
	}

	return 0;
}

static int xgmac_start_clks_aspeed(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	int ret;

	ret = clk_enable(&xgmac->clk_common);
	if (!ret)
		xgmac->clk_ck_enabled = true;
	else
		pr_err("xgmac clock enable failed: %d\n", ret);

	return ret;
}

static int xgmac_stop_clks_aspeed(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);

	if (xgmac->clk_ck_enabled) {
		clk_disable(&xgmac->clk_common);
		xgmac->clk_ck_enabled = false;
	}

	return 0;
}

static struct xgmac_ops xgmac_aspeed_ops = {
	.xgmac_inval_desc = xgmac_inval_desc_generic,
	.xgmac_flush_desc = xgmac_flush_desc_generic,
	.xgmac_inval_buffer = xgmac_inval_buffer_generic,
	.xgmac_flush_buffer = xgmac_flush_buffer_generic,
	.xgmac_probe_resources = xgmac_probe_resources_aspeed,
	.xgmac_remove_resources = xgmac_remove_resources_aspeed,
	.xgmac_stop_resets = xgmac_stop_resets_aspeed,
	.xgmac_start_resets = xgmac_start_resets_aspeed,
	.xgmac_stop_clks = xgmac_stop_clks_aspeed,
	.xgmac_start_clks = xgmac_start_clks_aspeed,
	.xgmac_calibrate_pads = xgmac_null_ops,
	.xgmac_disable_calibration = xgmac_null_ops,
	.xgmac_get_enetaddr = xgmac_null_ops,
};

struct xgmac_config __maybe_unused xgmac_aspeed_config = {
	.reg_access_always_ok = false,
	.swr_wait = 50,
	.config_mac = XGMAC_MAC_RXQ_CTRL0_RXQ0EN_NOT_ENABLED,
	.config_mac_mdio = XGMAC_MAC_MDIO_ADDRESS_CR_300_350,
	.axi_bus_width = XGMAC_AXI_WIDTH_128,
	.interface = dev_read_phy_mode,
	.ops = &xgmac_aspeed_ops
};
