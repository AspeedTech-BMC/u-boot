// SPDX-License-Identifier: GPL-2.0+
#include <common.h>
#include <dm.h>
#include <reset.h>
#include <fdtdec.h>
#include <pci.h>
#include <asm/io.h>
#include <asm/arch/ahbc_aspeed.h>
#include "pcie_aspeed.h"

DECLARE_GLOBAL_DATA_PTR;

/* PCI Host Controller registers */
#define ASPEED_PCIE_CLASS_CODE		0x04
#define ASPEED_PCIE_GLOBAL			0x30
#define ASPEED_PCIE_CFG_DIN			0x50
#define ASPEED_PCIE_CFG3			0x58
#define ASPEED_PCIE_LOCK			0x7C
#define ASPEED_PCIE_LINK			0xC0
#define ASPEED_PCIE_INT				0xC4
#define ASPEED_PCIE_LINK_STS		0xD0

/* AST_PCIE_CFG2	0x04 */
#define PCIE_CFG_CLASS_CODE(x)	((x) << 8)
#define PCIE_CFG_REV_ID(x)		(x)

/* AST_PCIE_GLOBAL	0x30 */
#define ROOT_COMPLEX_ID(x)		((x) << 4)

/* AST_PCIE_LOCK	0x7C */
#define PCIE_UNLOCK				0xa8

/* AST_PCIE_LINK	0xC0 */
#define PCIE_LINK_STS			BIT(5)

/* ASPEED_PCIE_LINK_STS	0xD0 */
#define PCIE_LINK_5G			BIT(17)
#define PCIE_LINK_2_5G			BIT(16)

extern int aspeed_pcie_phy_link_status(struct udevice *dev)
{
	struct aspeed_rc_bridge *rc_bridge = dev_get_priv(dev);
	u32 pcie_link = readl(rc_bridge->reg + ASPEED_PCIE_LINK);
	int ret = 0;

	printf("RC Bridge %s : ", dev->name);
	if (pcie_link & PCIE_LINK_STS) {
		printf("Link up\n");
		ret = 1;
	} else {
		printf("Link down\n");
		ret = 0;
	}

	return ret;
}

static int aspeed_pcie_phy_probe(struct udevice *dev)
{
	struct reset_ctl reset_ctl;
	int ret = 0;
	struct aspeed_rc_bridge *rc_bridge = dev_get_priv(dev);

	debug("%s(dev=%p)\n", __func__, dev);

	ret = reset_get_by_index(dev, 0, &reset_ctl);

	if (ret) {
		printf("%s(): Failed to get reset signal\n", __func__);
		return ret;
	}

	rc_bridge->reg = devfdt_get_addr_ptr(dev);
	if (IS_ERR(rc_bridge->reg))
		return PTR_ERR(rc_bridge->reg);

	writel(PCIE_UNLOCK, rc_bridge->reg + ASPEED_PCIE_LOCK);
	writel(ROOT_COMPLEX_ID(0x3), rc_bridge->reg + ASPEED_PCIE_GLOBAL);

	return 0;
}

static const struct udevice_id aspeed_pcie_phy_ids[] = {
	{ .compatible = "aspeed,ast2600-pcie_phy" },
	{ }
};

U_BOOT_DRIVER(aspeed_rc_bridge) = {
	.name		= "aspeed_pcie_phy",
	.id			= UCLASS_MISC,
	.of_match	= aspeed_pcie_phy_ids,
	.probe		= aspeed_pcie_phy_probe,
	.priv_auto_alloc_size	= sizeof(struct aspeed_rc_bridge),
};

