// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) ASPEED Technology Inc.
 */

#include <linux/bitfield.h>
#include <common.h>
#include <clk.h>
#include <dm.h>
#include <errno.h>
#include <regmap.h>
#include <syscon.h>
#include <reset.h>
#include <fdtdec.h>
#include <asm/io.h>

#define MCU_CTRL                        0x00e0
#define  MCU_CTRL_AHBS_IMEM_EN          BIT(0)
#define  MCU_CTRL_AHBS_SW_RST           BIT(4)
#define  MCU_CTRL_AHBM_SW_RST           BIT(8)
#define  MCU_CTRL_CORE_SW_RST           BIT(12)
#define  MCU_CTRL_DMEM_SHUT_DOWN        BIT(16)
#define  MCU_CTRL_DMEM_SLEEP            BIT(17)
#define  MCU_CTRL_DMEM_CLK_OFF          BIT(18)
#define  MCU_CTRL_IMEM_SHUT_DOWN        BIT(20)
#define  MCU_CTRL_IMEM_SLEEP            BIT(21)
#define  MCU_CTRL_IMEM_CLK_OFF          BIT(22)
#define  MCU_CTRL_IMEM_SEL              BIT(24)
#define  MCU_CTRL_CONFIG                BIT(28)

#define MCU_INTR_CTRL                   0x00e8
#define  MCU_INTR_CTRL_CLR              GENMASK(7, 0)
#define  MCU_INTR_CTRL_MASK             GENMASK(15, 8)
#define  MCU_INTR_CTRL_EN               GENMASK(23, 16)

struct aspeed_dp_priv {
	void *ctrl_base;
	void *mcud_base;	// mcu data
	void *mcuc_base;	// mcu ctrl regs
	void *mcui_base;	// mcu instruction mem
	struct regmap *scu;
};

static void _redriver_cfg(struct udevice *dev)
{
	struct aspeed_dp_priv *dp = dev_get_priv(dev);
	const u32 *cell;
	int i, len;
	u32 tmp;

	// update configs to dmem for re-driver
	writel(0x0000dead, dp->mcud_base + 0x0e00);	// mark re-driver cfg not ready
	cell = dev_read_prop(dev, "eq-table", &len);
	if (cell) {
		for (i = 0; i < len / sizeof(u32); ++i)
			writel(fdt32_to_cpu(cell[i]), dp->mcud_base + 0x0e04 + i * 4);
	} else {
		debug("%s(): Failed to get eq-table for re-driver\n", __func__);
		return;
	}

	tmp = dev_read_s32_default(dev, "i2c-base-addr", -1);
	if (tmp == -1) {
		debug("%s(): Failed to get i2c port's base address\n", __func__);
		return;
	}
	writel(tmp, dp->mcud_base + 0x0e28);

	tmp = dev_read_s32_default(dev, "i2c-buf-addr", -1);
	if (tmp == -1) {
		debug("%s(): Failed to get i2c port's buf address\n", __func__);
		return;
	}
	writel(tmp, dp->mcud_base + 0x0e2c);

	tmp = dev_read_s32_default(dev, "dev-addr", -1);
	if (tmp == -1)
		tmp = 0x70;
	writel(tmp, dp->mcud_base + 0x0e30);
	writel(0x0000cafe, dp->mcud_base + 0x0e00);	// mark re-driver cfg ready
}

static int aspeed_dp_probe(struct udevice *dev)
{
	struct aspeed_dp_priv *dp = dev_get_priv(dev);
	struct reset_ctl dp_reset_ctl, dpmcu_reset_ctrl;
	int i, ret = 0;
	u32 mcu_ctrl, val;
	bool is_mcu_stop = false;
	u32 fw[0x1000];

	regmap_read(dp->scu, 0x100, &val);
	is_mcu_stop = ((val & BIT(13)) == 0);

	debug("%s(dev=%p) \n", __func__, dev);

	ret = dev_read_u32_array(dev, "aspeed,dp-fw", fw, ARRAY_SIZE(fw));
	if (ret) {
		dev_err(dev, "Can't get dp-firmware, err(%d)\n", ret);
		return ret;
	}

	ret = reset_get_by_index(dev, 0, &dp_reset_ctl);
	if (ret) {
		printf("%s(): Failed to get dp reset signal\n", __func__);
		return ret;
	}

	ret = reset_get_by_index(dev, 1, &dpmcu_reset_ctrl);
	if (ret) {
		printf("%s(): Failed to get dp mcu reset signal\n", __func__);
		return ret;
	}

	/* reset for DPTX and DPMCU if MCU isn't running */
	if (is_mcu_stop) {
		reset_assert(&dp_reset_ctl);
		reset_assert(&dpmcu_reset_ctrl);
		reset_deassert(&dp_reset_ctl);
		reset_deassert(&dpmcu_reset_ctrl);
	}

	/* select HOST or BMC as display control master
	enable or disable sending EDID to Host	*/
	writel(readl(dp->ctrl_base + 0xB8) & ~(BIT(24) | BIT(28)), dp->ctrl_base + 0xB8);

	/* DPMCU */
	/* clear display format and enable region */
	writel(0, dp->mcud_base + 0x0de0);

	_redriver_cfg(dev);

	/* load DPMCU firmware to internal instruction memory */
	if (is_mcu_stop) {
		mcu_ctrl = MCU_CTRL_CONFIG | MCU_CTRL_IMEM_CLK_OFF | MCU_CTRL_IMEM_SHUT_DOWN |
		      MCU_CTRL_DMEM_CLK_OFF | MCU_CTRL_DMEM_SHUT_DOWN | MCU_CTRL_AHBS_SW_RST;
		writel(mcu_ctrl, dp->mcuc_base + MCU_CTRL);

		mcu_ctrl &= ~(MCU_CTRL_IMEM_SHUT_DOWN | MCU_CTRL_DMEM_SHUT_DOWN);
		writel(mcu_ctrl, dp->mcuc_base + MCU_CTRL);

		mcu_ctrl &= ~(MCU_CTRL_IMEM_CLK_OFF | MCU_CTRL_DMEM_CLK_OFF);
		writel(mcu_ctrl, dp->mcuc_base + MCU_CTRL);

		mcu_ctrl |= MCU_CTRL_AHBS_IMEM_EN;
		writel(mcu_ctrl, dp->mcuc_base + MCU_CTRL);

		for (i = 0; i < ARRAY_SIZE(fw); i++)
			writel(fw[i], dp->mcui_base + (i * 4));

		/* release DPMCU internal reset */
		mcu_ctrl &= ~MCU_CTRL_AHBS_IMEM_EN;
		writel(mcu_ctrl, dp->mcuc_base + MCU_CTRL);
		mcu_ctrl |= MCU_CTRL_CORE_SW_RST | MCU_CTRL_AHBM_SW_RST;
		writel(mcu_ctrl, dp->mcuc_base + MCU_CTRL);
		//disable dp interrupt
		writel(FIELD_PREP(MCU_INTR_CTRL_EN, 0xff), dp->mcuc_base + MCU_INTR_CTRL);
	}

	//set vga ASTDP with DPMCU FW handling scratch
	regmap_update_bits(dp->scu, 0x100, 0x7 << 9, 0x7 << 9);

	return 0;
}

static int dp_aspeed_ofdata_to_platdata(struct udevice *dev)
{
	struct aspeed_dp_priv *dp = dev_get_priv(dev);

	/* Get the controller base address */
	dp->ctrl_base = (void *)devfdt_get_addr_index(dev, 0);
	if (IS_ERR(dp->ctrl_base))
		return PTR_ERR(dp->ctrl_base);
	dp->mcud_base = (void *)devfdt_get_addr_index(dev, 1);
	if (IS_ERR(dp->mcud_base))
		return PTR_ERR(dp->mcud_base);
	dp->mcuc_base = (void *)devfdt_get_addr_index(dev, 2);
	if (IS_ERR(dp->mcuc_base))
		return PTR_ERR(dp->mcuc_base);
	dp->mcui_base = (void *)devfdt_get_addr_index(dev, 3);
	if (IS_ERR(dp->mcui_base))
		return PTR_ERR(dp->mcui_base);
	dp->scu = syscon_regmap_lookup_by_phandle(dev, "aspeed,scu");
	if (IS_ERR(dp->scu)) {
		pr_err("Dev: %s - can't get regmap for scu!\n", dev->name);
		return PTR_ERR(dp->scu);
	}

	return 0;
}

static const struct udevice_id aspeed_dp_ids[] = {
	{ .compatible = "aspeed,ast2600-displayport" },
	{ }
};

U_BOOT_DRIVER(aspeed_dp) = {
	.name		= "aspeed_dp",
	.id		= UCLASS_MISC,
	.of_match	= aspeed_dp_ids,
	.probe		= aspeed_dp_probe,
	.ofdata_to_platdata   = dp_aspeed_ofdata_to_platdata,
	.priv_auto_alloc_size = sizeof(struct aspeed_dp_priv),
};
