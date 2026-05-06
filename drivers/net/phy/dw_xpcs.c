// SPDX-License-Identifier: GPL-2.0+
/*
 * Synopsys DesignWare XPCS PHY driver
 *
 * Based on the Linux Synopsys DesignWare XPCS helpers.
 */

#include <common.h>
#include <dm/device_compat.h>
#include <phy.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/iopoll.h>

#define DW_XPCS_ID			0x7996ced0
#define DW_XPCS_ID_MASK			0xffffffff

#define DW_VENDOR			BIT(15)

/* VR_XS_PCS */
#define DW_VR_XS_PCS_DIG_STS		0x0010
#define DW_RXFIFO_ERR			GENMASK(6, 5)

static int xpcs_soft_reset(struct phy_device *phydev, int devad)
{
	int ret, val;

	ret = phy_write_mmd(phydev, devad, MII_BMCR, BMCR_RESET);
	if (ret < 0)
		return ret;

	return phy_read_mmd_poll_timeout(phydev, devad, MII_BMCR, val,
					 !(val & BMCR_RESET),
					 50000, 600000, true);
}

struct conf_5000baser {
	int dev;
	u16 reg;
	u16 mask;
	u16 val;
} conf_5000[] = {
	{ MDIO_MMD_PCS,    0x0007, 0x000f, 0x000f },  //SR_XS_PCS_CTRL2 [3:0]PCS_TYPE_SEL     5G
	{ MDIO_MMD_PMAPMD, 0x8071, 0x00ff, 0x0021 },  //VR_XS_PMA_MP_12G_16G_MPLLA_CTRL0 [7:0]MPLLA_FREQ_MULT
	{ MDIO_MMD_PMAPMD, 0x8077, 0xffff, 0xa03e },  //VR_XS_PMA_MP_16G_MPLLA_CTRL3 [15:0]MPLLA_BANDWIDTH
	{ MDIO_MMD_PMAPMD, 0x8092, 0x1fff, 0x0549 },  //VR_XS_PMA_MP_12G_16G_25G_VCO_CAL_LD0 [12:0]VCO_LD_VAL
	{ MDIO_MMD_PMAPMD, 0x8096, 0x007f, 0x0029 },  //VR_XS_PMA_MP_16G_25G_VCO_CAL_REF0 [6:0]VCO_REF_LD
	{ MDIO_MMD_PMAPMD, 0x805c, 0x0001, 0x0001 },  //VR_XS_PMA_MP_12G_16G_25G_RX_EQ_CTRL4 [0]CONT_ADAPT
	{ MDIO_MMD_PMAPMD, 0x8034, 0x0007, 0x0001 },  //VR_XS_PMA_MP_12G_16G_25G_TX_RATE_CTRL [2:0]RATE     5G
	{ MDIO_MMD_PMAPMD, 0x8054, 0x0007, 0x0001 },  //VR_XS_PMA_MP_12G_16G_25G_RX_RATE_CTRL [2:0]RATE     5G
	{ MDIO_MMD_PMAPMD, 0x8032, 0x0300, 0x0300 },  //VR_XS_PMA_MP_12G_16G_TX_GENCTRL2 [9:8]WIDTH
	{ MDIO_MMD_PMAPMD, 0x8052, 0x0300, 0x0300 },  //VR_XS_PMA_MP_12G_16G_RX_GENCTRL2 [9:8]WIDTH
	{ MDIO_MMD_PMAPMD, 0x8073, 0x0700, 0x0600 },  //VR_XS_PMA_MP_12G_16G_MPLLA_CTRL2 [10,9,8]CLK_EN
	{ MDIO_MMD_PMAPMD, 0x8031, 0x1510, 0x1510 },  //VR_XS_PMA_MP_12G_16G_25G_TX_GENCTRL1 [4]VBOOST_EN
	{ MDIO_MMD_PMAPMD, 0x8058, 0x5550, 0x5550 },  //VR_XS_PMA_MP_16G_25G_RX_EQ_CTRL0 [4:0]CTLE_BOOST
	{ MDIO_MMD_PMAPMD, 0x8064, 0x0111, 0x0111 },  //VR_XS_PMA_MP_16G_RX_CDR_CTRL1 [9:8,4,0]RX_CDR VCO
	{ MDIO_MMD_PMAPMD, 0x8069, 0x0002, 0x0002 },  //VR_XS_PMA_MP_16G_25G_RX_MISC_CTRL0 [7:0]RX_MISC
	{ MDIO_MMD_PMAPMD, 0x8068, 0x0000, 0x0000 },  //VR_XS_PMA_MP_16G_25G_RX_GENCTRL4 [8]RX_DEF_BYP
	{ MDIO_MMD_PMAPMD, 0x806b, 0x0300, 0x0300 },  //VR_XS_PMA_MP_16G_25G_RX_IQ_CTRL0 [11:8]RX_DELTA_IQ
	{ MDIO_MMD_PMAPMD, 0x805d, 0x0030, 0x0030 },  //VR_XS_PMA_MP_16G_25G_RX_EQ_CTRL5 [5:4]RX_ADPT_MODE
	{ MDIO_MMD_PCS,    0x8000, 0xa000, 0xa000 },  //SR_XS_PCS_DIG_CTRL1 [15]VR_RST
};

static int xpcs_config_5gbaser(struct phy_device *phydev)
{
	int ret = 0, i;

	for (i = 0; i < ARRAY_SIZE(conf_5000); i++) {
		ret = phy_modify_mmd(phydev, conf_5000[i].dev, conf_5000[i].reg,
				     conf_5000[i].mask, conf_5000[i].val);
		if (ret < 0)
			return ret;
	}

	return ret;
}

static int xpcs_config_10gbaser(struct phy_device *phydev)
{
	int ret;

	ret = xpcs_soft_reset(phydev, MDIO_MMD_PCS);
	if (ret < 0)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_PCS, MDIO_CTRL2,
			     MDIO_PCS_CTRL2_TYPE, MDIO_PCS_CTRL2_10GBR);
	if (ret < 0)
		return ret;

	return phy_modify_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1,
			      MDIO_CTRL1_SPEEDSEL, MDIO_CTRL1_SPEED10G);
}

static int xpcs_config(struct phy_device *phydev)
{
	phydev->autoneg = AUTONEG_DISABLE;
	phydev->speed = SPEED_10000;
	phydev->duplex = DUPLEX_FULL;
	phydev->supported = SUPPORTED_10000baseT_Full | SUPPORTED_FIBRE;
	phydev->advertising = phydev->supported;

	if (phydev->interface == PHY_INTERFACE_MODE_10GBASER)
		xpcs_config_10gbaser(phydev);
	else
		xpcs_config_5gbaser(phydev);

	return 0;
}

static int xpcs_startup(struct phy_device *phydev)
{
	int ret, stat;

	phydev->speed = SPEED_10000;
	phydev->duplex = DUPLEX_FULL;

	/* Link status is latched low, so read it twice for current state. */
	phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_STAT1);
	stat = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_STAT1);
	if (stat < 0)
		return stat;

	phydev->link = !!(stat & MDIO_STAT1_LSTATUS);
	if (!phydev->link)
		return 0;

	ret = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_PCS_10GBRT_STAT1);
	if (ret >= 0 && !(ret & MDIO_PCS_10GBRT_STAT1_BLKLK))
		phydev->link = 0;

	ret = phy_read_mmd(phydev, MDIO_MMD_PCS,
			   DW_VENDOR | DW_VR_XS_PCS_DIG_STS);
	if (ret >= 0 && (ret & DW_RXFIFO_ERR))
		phydev->link = 0;

	return 0;
}

static int xpcs_probe(struct phy_device *phydev)
{
	int id1, id2;
	u32 id;

	if (!phydev->is_c45)
		return -ENODEV;

	id1 = phy_read_mmd(phydev, MDIO_MMD_PCS, MII_PHYSID1);
	id2 = phy_read_mmd(phydev, MDIO_MMD_PCS, MII_PHYSID2);
	if (id1 < 0 || id2 < 0)
		return -ENODEV;

	id = (id1 << 16) | id2;
	if ((id & DW_XPCS_ID_MASK) != DW_XPCS_ID)
		return -ENODEV;

	phydev->mmds = MDIO_DEVS_PMAPMD | MDIO_DEVS_PCS |
		       MDIO_DEVS_AN | MDIO_DEVS_VEND2;

	/*
	 * U-Boot's generic C45 reset picks the first MMD. XPCS 10GBase-R
	 * follows the Linux driver and resets through the PCS MMD instead.
	 */
	phydev->flags |= PHY_FLAG_BROKEN_RESET;

	return 0;
}

U_BOOT_PHY_DRIVER(dw_xpcs) = {
	.name		= "Synopsys DesignWare XPCS",
	.uid		= DW_XPCS_ID,
	.mask		= DW_XPCS_ID_MASK,
	.features	= PHY_10G_FEATURES | SUPPORTED_FIBRE,
	.mmds		= MDIO_DEVS_PMAPMD | MDIO_DEVS_PCS |
			  MDIO_DEVS_AN | MDIO_DEVS_VEND2,
	.probe		= xpcs_probe,
	.config		= xpcs_config,
	.startup	= xpcs_startup,
	.shutdown	= gen10g_shutdown,
};
