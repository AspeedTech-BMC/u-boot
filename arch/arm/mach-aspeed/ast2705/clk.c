// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) ASPEED Technology Inc.
 */

#include <clk.h>
#include <dm.h>
#include <init.h>
#include <malloc.h>
#include <asm/global_data.h>
#include <dt-bindings/clock/ast2700-clock.h>

DECLARE_GLOBAL_DATA_PTR;

struct aspeed_clks {
	ulong id;
	const char *name;
};

static struct aspeed_clks ast2700_clk0_names[] = {
	{ SCU0_CLK_PSP, "cpu clk" },     { SCU0_CLK_HPLL, "hpll" },
	{ SCU0_CLK_DPLL, "dpll" },     { SCU0_CLK_MPLL, "mpll" },
	{ SCU0_CLK_AXI1, "axi1" },     { SCU0_CLK_AHB, "hclk" },
	{ SCU0_CLK_APB, "pclk" },
};

static struct aspeed_clks ast2700_clk1_names[] = {
	{ SCU1_CLK_HPLL, "hpll" },     { SCU1_CLK_APLL, "apll" },
	{ SCU1_CLK_DPLL, "dpll" },     { SCU1_CLK_AHB, "hclk" },
	{ SCU1_CLK_APB, "pclk" },
};

/**
 * soc_clk_dump() - Print clock frequencies
 * Returns zero on success
 *
 * Implementation for the clk dump command.
 */
int soc_clk_dump(void)
{
	struct udevice *dev;
	struct clk clk;
	unsigned long rate;
	int i, ret;

	ret = uclass_get_device_by_driver(UCLASS_CLK, DM_DRIVER_GET(aspeed_ast2700_soc0_clk), &dev);
	if (ret)
		return ret;

	printf("soc0 clk\tfrequency\n");
	for (i = 0; i < ARRAY_SIZE(ast2700_clk0_names); i++) {
		clk.id = ast2700_clk0_names[i].id;
		ret = clk_request(dev, &clk);
		if (ret < 0)
			return ret;

		rate = clk_get_rate(&clk);

		clk_free(&clk);

		if (ret == -ENOTSUPP) {
			printf("clk ID %lu not supported yet\n",
			       ast2700_clk0_names[i].id);
			continue;
		}
		if (ret < 0) {
			printf("%s %lu: get_rate err: %d\n", __func__,
			       ast2700_clk0_names[i].id, ret);
			continue;
		}
		printf("%s(%3lu):\t%lu\n", ast2700_clk0_names[i].name,
		       ast2700_clk0_names[i].id, rate);
	}

	ret = uclass_get_device_by_driver(UCLASS_CLK, DM_DRIVER_GET(aspeed_ast2700_soc1_clk), &dev);
	if (ret)
		return ret;

	printf("soc1 clk\tfrequency\n");
	for (i = 0; i < ARRAY_SIZE(ast2700_clk1_names); i++) {
		clk.id = ast2700_clk1_names[i].id;
		ret = clk_request(dev, &clk);
		if (ret < 0)
			return ret;

		rate = clk_get_rate(&clk);

		clk_free(&clk);

		if (ret == -ENOTSUPP) {
			printf("clk ID %lu not supported yet\n",
			       ast2700_clk1_names[i].id);
			continue;
		}
		if (ret < 0) {
			printf("%s %lu: get_rate err: %d\n", __func__,
			       ast2700_clk1_names[i].id, ret);
			continue;
		}

		printf("%s(%3lu):\t%lu\n", ast2700_clk1_names[i].name,
		       ast2700_clk1_names[i].id, rate);
	}

	return 0;
}
