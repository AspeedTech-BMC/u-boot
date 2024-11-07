// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2016 Fuzhou Rockchip Electronics Co., Ltd
 *
 * Rockchip SD Host Controller Interface
 */

#include <common.h>
#include <dm.h>
#include <dt-structs.h>
#include <linux/libfdt.h>
#include <malloc.h>
#include <mapmem.h>
#include <mmc.h>
#include <sdhci.h>
#include <clk.h>

/* 400KHz is max freq for card ID etc. Use that as min */
#define EMMC_MIN_FREQ	400000

#include <power/regulator.h>

#define ASPEED_SDC_PHASE		0xf4
#define   ASPEED_SDC_S1_PHASE_IN	GENMASK(25, 21)
#define   ASPEED_SDC_S0_PHASE_IN	GENMASK(20, 16)
#define   ASPEED_SDC_S0_PHASE_IN_SHIFT	16
#define   ASPEED_SDC_S0_PHASE_OUT_SHIFT 3
#define   ASPEED_SDC_S1_PHASE_OUT	GENMASK(15, 11)
#define   ASPEED_SDC_S1_PHASE_IN_EN	BIT(10)
#define   ASPEED_SDC_S1_PHASE_OUT_EN	GENMASK(9, 8)
#define   ASPEED_SDC_S0_PHASE_OUT	GENMASK(7, 3)
#define   ASPEED_SDC_S0_PHASE_IN_EN	BIT(2)
#define   ASPEED_SDC_S0_PHASE_OUT_EN	GENMASK(1, 0)
#define   ASPEED_SDC_PHASE_MAX		31

#define ASPEED_SDHCI_TAP_PARAM_INVERT_CLK	BIT(4)
#define ASPEED_SDHCI_NR_TAPS		15

#define SDHCI140_SLOT_0_MIRROR_OFFSET 0x10
#define SDHCI240_SLOT_0_MIRROR_OFFSET 0x20
#define SDHCI140_SLOT_0_CAP_REG_1_OFFSET 0x140
#define SDHCI240_SLOT_0_CAP_REG_1_OFFSET 0x240

struct aspeed_sdhci_plat {
	struct mmc_config cfg;
	struct mmc mmc;
};

struct aspeed_sdhci_priv {
	struct sdhci_host *host;
	struct clk clk;
};

#ifdef MMC_SUPPORTS_TUNING
static int aspeed_execute_tuning(struct mmc *mmc, u8 opcode)
{
	struct sdhci_host *host = mmc->priv;
	u32 val, left, right, edge;
	u32 window, oldwindow = 0, center = 0;
	u32 in_phase, out_phase, enable_mask, inverted = 0;

	out_phase = sdhci_readl(host, ASPEED_SDC_PHASE) & ASPEED_SDC_S0_PHASE_OUT;

	enable_mask = ASPEED_SDC_S0_PHASE_OUT_EN | ASPEED_SDC_S0_PHASE_IN_EN;

	/*
	 * There are two window upon clock rising and falling edge.
	 * Iterate each tap delay to find the valid window and choose the
	 * bigger one, set the tap delay at the middle of window.
	 */
	for (edge = 0; edge < 2; edge++) {
		if (edge == 1)
			inverted = ASPEED_SDHCI_TAP_PARAM_INVERT_CLK;

		val = (out_phase | enable_mask | (inverted << ASPEED_SDC_S0_PHASE_IN_SHIFT));

		/* find the left boundary */
		for (left = 0; left < ASPEED_SDHCI_NR_TAPS + 1; left++) {
			in_phase = val | (left << ASPEED_SDC_S0_PHASE_IN_SHIFT);
			sdhci_writel(host, in_phase, ASPEED_SDC_PHASE);
			if (!mmc_send_tuning(mmc, opcode, NULL))
				break;
		}

		/* find the right boundary */
		for (right = left + 1; right < ASPEED_SDHCI_NR_TAPS + 1; right++) {
			in_phase = val | (right << ASPEED_SDC_S0_PHASE_IN_SHIFT);
			sdhci_writel(host, in_phase, ASPEED_SDC_PHASE);
			if (mmc_send_tuning(mmc, opcode, NULL))
				break;
		}

		window = right - left;
		pr_debug("tuning window[%d][%d~%d] = %d\n", edge, left, right, window);

		if (window > oldwindow) {
			oldwindow = window;
			center = (((right - 1) + left) / 2) | inverted;
		}
	}

	val = (out_phase | enable_mask | (center << ASPEED_SDC_S0_PHASE_IN_SHIFT));
	sdhci_writel(host, val, ASPEED_SDC_PHASE);

	pr_debug("input tuning result=%x\n", val);

	inverted = 0;
	out_phase = val & ~ASPEED_SDC_S0_PHASE_OUT;
	in_phase = out_phase;
	oldwindow = 0;

	for (edge = 0; edge < 2; edge++) {
		if (edge == 1)
			inverted = ASPEED_SDHCI_TAP_PARAM_INVERT_CLK;

		val = (in_phase | enable_mask | (inverted << ASPEED_SDC_S0_PHASE_OUT_SHIFT));

		/* find the left boundary */
		for (left = 0; left < ASPEED_SDHCI_NR_TAPS + 1; left++) {
			out_phase = val | (left << ASPEED_SDC_S0_PHASE_OUT_SHIFT);
			sdhci_writel(host, out_phase, ASPEED_SDC_PHASE);

			if (!mmc_send_tuning(mmc, opcode, NULL))
				break;
		}

		/* find the right boundary */
		for (right = left + 1; right < ASPEED_SDHCI_NR_TAPS + 1; right++) {
			out_phase = val | (right << ASPEED_SDC_S0_PHASE_OUT_SHIFT);
			sdhci_writel(host, out_phase, ASPEED_SDC_PHASE);

			if (mmc_send_tuning(mmc, opcode, NULL))
				break;
		}

		window = right - left;
		pr_debug("tuning window[%d][%d~%d] = %d\n", edge, left, right, window);

		if (window > oldwindow) {
			oldwindow = window;
			center = (((right - 1) + left) / 2) | inverted;
		}
	}

	val = (in_phase | enable_mask | (center << ASPEED_SDC_S0_PHASE_OUT_SHIFT));
	sdhci_writel(host, val, ASPEED_SDC_PHASE);

	pr_debug("output tuning result=%x\n", val);

	return mmc_send_tuning(mmc, opcode, NULL);
}

static void aspeed_set_ios_post(struct sdhci_host *host)
{
	struct mmc *mmc = host->mmc;
	u32 reg;
	u32 drv;

	drv = dev_read_u32_default(mmc->dev, "sdhci-drive-type", 0);

	reg = sdhci_readw(host, SDHCI_HOST_CONTROL_2);
	reg &= ~SDHCI_DRIVER_STRENGTH_MASK;

	switch (mmc->selected_mode) {
	case SD_HS:
	case UHS_SDR50:
	case UHS_DDR50:
	case UHS_SDR104:
		reg |= ((drv & 0x3) << SDHCI_DRIVER_STRENGTH_SHIFT);
		break;
	default:
		break;
	}

	sdhci_writew(host, reg, SDHCI_HOST_CONTROL_2);
}

static struct sdhci_ops aspeed_sdhci_ops = {
	.set_control_reg = sdhci_set_control_reg,
	.set_ios_post = aspeed_set_ios_post,
	.platform_execute_tuning = aspeed_execute_tuning,
};
#endif

static int aspeed_sdhci_probe(struct udevice *dev)
{
	struct mmc_uclass_priv *upriv = dev_get_uclass_priv(dev);
	struct aspeed_sdhci_plat *plat = dev_get_platdata(dev);
	struct aspeed_sdhci_priv *prv = dev_get_priv(dev);
	struct sdhci_host *host = prv->host;
	void *sdhci_reg;
	unsigned long clock;
	u32 max_freq;
	u32 reg_val;
	struct clk clk;
	int ret;

	max_freq = dev_read_u32_default(dev, "max-frequency", 4);

	ret = clk_get_by_index(dev, 0, &clk);
	if (ret < 0) {
		pr_debug("%s: Can't get clock for %s: %d\n", __func__, dev->name,
		      ret);
	}

	clock = clk_get_rate(&clk);
	if (IS_ERR_VALUE(clock)) {
		dev_err(dev, "failed to get clock\n");
		return ret;
	}

	debug("%s: CLK %ld\n", __func__, clock);

	if (dev_read_bool(dev, "sdhci_hs200")) {
		sdhci_reg = dev_read_addr_ptr(dev);
		if (!sdhci_reg)
			return -EINVAL;

		reg_val = readl(sdhci_reg + SDHCI140_SLOT_0_CAP_REG_1_OFFSET);
		/* support 1.8V */
		reg_val |= BIT(26);
		writel(reg_val, sdhci_reg + SDHCI140_SLOT_0_MIRROR_OFFSET);
		reg_val = readl(sdhci_reg + SDHCI240_SLOT_0_CAP_REG_1_OFFSET);
		/* support 1.8V */
		reg_val |= BIT(26);
		writel(reg_val, sdhci_reg + SDHCI240_SLOT_0_MIRROR_OFFSET);
	}

#ifdef MMC_SUPPORTS_TUNING
	host->ops = &aspeed_sdhci_ops;
#endif
	host->max_clk = clock;
	host->mmc = &plat->mmc;
	host->mmc->dev = dev;
	host->mmc->priv = host;
	upriv->mmc = host->mmc;
	host->mmc->drv_type = dev_read_u32_default(dev, "sdhci-drive-type", 0);

	host->bus_width = dev_read_u32_default(dev, "bus-width", 4);

	if (host->bus_width == 8)
		host->host_caps |= MMC_MODE_8BIT;

	ret = sdhci_setup_cfg(&plat->cfg, host, max_freq, 400000);
	if (ret)
		return ret;

	return sdhci_probe(dev);
}

static int aspeed_sdhci_ofdata_to_platdata(struct udevice *dev)
{
	struct aspeed_sdhci_priv *priv = dev_get_priv(dev);

	priv->host = calloc(1, sizeof(struct sdhci_host));
	if (!priv->host)
			return -1;

	priv->host->name = dev->name;
	priv->host->ioaddr = (void *)dev_read_addr(dev);

	return 0;
}

static int aspeed_sdhci_bind(struct udevice *dev)
{
	struct aspeed_sdhci_plat *plat = dev_get_platdata(dev);

	return sdhci_bind(dev, &plat->mmc, &plat->cfg);
}

static const struct udevice_id aspeed_sdhci_ids[] = {
	{ .compatible = "aspeed,sdhci-ast2500" },
	{ .compatible = "aspeed,sdhci-ast2600" },
	{ .compatible = "aspeed,emmc-ast2600" },
	{ }
};

U_BOOT_DRIVER(aspeed_sdhci_drv) = {
	.name		= "aspeed_sdhci",
	.id		= UCLASS_MMC,
	.of_match	= aspeed_sdhci_ids,
	.ofdata_to_platdata = aspeed_sdhci_ofdata_to_platdata,
	.ops		= &sdhci_ops,
	.bind		= aspeed_sdhci_bind,
	.probe		= aspeed_sdhci_probe,
	.priv_auto_alloc_size = sizeof(struct aspeed_sdhci_priv),
	.platdata_auto_alloc_size = sizeof(struct aspeed_sdhci_plat),
};
