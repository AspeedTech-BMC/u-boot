// SPDX-License-Identifier: GPL-2.0+
/*
 * Synopsys DesignWare XPCS PHY driver
 *
 * Based on the Linux Synopsys DesignWare XPCS helpers.
 */

#include <common.h>
#include <dm/device_compat.h>
#include <phy.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/iopoll.h>

#define DW_XPCS_ID			0x7996ced0
#define DW_XPCS_ID_MASK			0xffffffff

#define DW_VENDOR			BIT(15)

/* XS_PMA_MMD */
#define DW_VR_XS_PMA_RX_LSTS				0x8020
#define RX_VALID_0						BIT(12)
#define DW_VR_XS_PMA_MP_12G_16G_25G_TX_GENCTRL1		0x8031
#define DW_VR_XS_PMA_MP_12G_16G_TX_GENCTRL2		0x8032
#define DW_VR_XS_PMA_MP_12G_16G_25G_TX_RATE_CTRL	0x8034
#define DW_VR_XS_PMA_MP_12G_16G_25G_TX_EQ_CTRL0		0x8036
#define TX_EQ_MAIN_MASK						GENMASK(13, 8)
#define TX_EQ_MAIN(x) \
		FIELD_PREP(TX_EQ_MAIN_MASK, x)
#define DW_VR_XS_PMA_MP_12G_16G_25G_TX_EQ_CTRL1		0x8037
#define TX_EQ_POST_MASK						GENMASK(5, 0)
#define TX_EQ_POST(x) \
		FIELD_PREP(TX_EQ_POST_MASK, x)
#define DW_VR_XS_PMA_MP_12G_16G_RX_GENCTRL2		0x8052
#define DW_VR_XS_PMA_MP_12G_16G_25G_RX_RATE_CTRL	0x8054
#define DW_VR_XS_PMA_MP_16G_25G_RX_EQ_CTRL0		0x8058
#define CTLE_BOOST_0_MASK					GENMASK(4, 0)
#define CTLE_BOOST_0(x) \
		FIELD_PREP(CTLE_BOOST_0_MASK, x)
#define CTLE_POLE_0_MASK					GENMASK(6, 5)
#define CTLE_POLE_0(x) \
		FIELD_PREP(CTLE_POLE_0_MASK, x)
#define DW_VR_XS_PMA_MP_12G_16G_25G_RX_EQ_CTRL4		0x805c
#define RX_AD_REQ						BIT(12)
#define DW_VR_XS_PMA_MP_16G_25G_RX_EQ_CTRL5		0x805d
#define DW_VR_XS_PMA_MP_16G_RX_CDR_CTRL1		0x8064
#define DW_VR_XS_PMA_MP_16G_25G_RX_GENCTRL4		0x8068
#define DW_VR_XS_PMA_MP_16G_25G_RX_MISC_CTRL0		0x8069
#define RX0_MISC_MASK						GENMASK(7, 0)
#define RX0_MISC(x) \
		FIELD_PREP(RX0_MISC_MASK, x)
#define DW_VR_XS_PMA_MP_16G_25G_RX_IQ_CTRL0		0x806b
#define RX0_DELTA_IQ_MASK					GENMASK(11, 8)
#define RX0_DELTA_IQ(x) \
		FIELD_PREP(RX0_DELTA_IQ_MASK, x)
#define DW_VR_XS_PMA_MP_12G_16G_MPLLA_CTRL0		0x8071
#define DW_VR_XS_PMA_MP_12G_16G_MPLLA_CTRL2		0x8073
#define DW_VR_XS_PMA_MP_16G_MPLLA_CTRL3			0x8077
#define MPLLA_BANDWIDTH(x)					(x)
#define DW_VR_XS_PMA_MP_12G_16G_25G_VCO_CAL_LD0		0x8092
#define DW_VR_XS_PMA_MP_16G_25G_VCO_CAL_REF0		0x8096
#define DW_VR_XS_PMA_MP_12G_16G_25G_MISC_STS		0x8098
#define RX_ADPT_ACK						BIT(12)
#define DW_VR_XS_PMA_MP_12G_16G_25G_SRAM		0x809b
#define INIT_DN							BIT(0)
#define EXT_LD_DN						BIT(1)

/* MDIO_MMD_PCS */
#define SR_XS_PCS_CTRL1					0x0000
#define RST							BIT(15)
#define DW_VR_XS_PCS_DIG_CTRL1				0x8000
#define BYP_PWRUP						BIT(1)
#define USXG_EN							BIT(9)
#define USRA_RST						BIT(10)
#define VR_RST							BIT(15)
#define DW_VR_XS_PCS_DEBUG_CTRL				0x8005
#define SUPRESS_LOS_DET						BIT(4)
#define RX_DT_EN_CTL						BIT(6)

/* MDIO_MMD_VEND2 */
#define DW_SR_MII_CTRL					0x0000
#define AN_ENABLE						BIT(12)
#define MII_SS5							BIT(5)
#define MII_SS6							BIT(6)
#define MII_SS13						BIT(13)
#define DW_VR_MII_AN_CTRL				0x8001
#define MII_AN_INTR_EN						BIT(0)
#define DW_VR_MII_AN_INTR_STS				0x8002
#define CL37_ANCMPLT_INTR					BIT(0)
#define USXG_AN_STS_MASK					GENMASK(14, 8)
#define USXG_AN_STS(x) \
		FIELD_GET(USXG_AN_STS_MASK, x)
#define USXG_AN_STS_SPEED_LINK_MASK				GENMASK(4, 2)
#define USXG_AN_STS_SPEED_LINK(x) \
		FIELD_GET(USXG_AN_STS_SPEED_LINK_MASK, x)
#define USXG_SPEED_LINK_10M					0x0
#define USXG_SPEED_LINK_100M					0x1
#define USXG_SPEED_LINK_1000M					0x2
#define USXG_SPEED_LINK_10G					0x3
#define USXG_SPEED_LINK_2_5G					0x4
#define USXG_SPEED_LINK_5G					0x5
#define USXG_LINK_UP						BIT(6)
#define USXG_DUPLEX_FULL					BIT(5)

static int xpcs_vr_reset(struct phy_device *phydev)
{
	int ret, val;

	ret = phy_modify_mmd(phydev, MDIO_MMD_PMAPMD,
			     DW_VR_XS_PMA_MP_12G_16G_25G_SRAM,
			     EXT_LD_DN, 0);
	if (ret < 0)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_PCS,
			     DW_VR_XS_PCS_DIG_CTRL1,
			     VR_RST, VR_RST);
	if (ret < 0)
		return ret;

	ret = phy_read_mmd_poll_timeout(phydev, MDIO_MMD_PMAPMD,
					DW_VR_XS_PMA_MP_12G_16G_25G_SRAM, val,
					(val & INIT_DN),
					1000, 100000, true);
	if (ret < 0)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_PMAPMD,
			     DW_VR_XS_PMA_MP_12G_16G_25G_SRAM,
			     EXT_LD_DN, EXT_LD_DN);
	if (ret < 0)
		return ret;

	ret = phy_read_mmd_poll_timeout(phydev, MDIO_MMD_PCS,
					DW_VR_XS_PCS_DIG_CTRL1, val,
					!(val & VR_RST),
					1000, 100000, true);
	if (ret < 0)
		return ret;

	return ret;
}

static int xpcs_config_10gbaser(struct phy_device *phydev)
{
	int ret, val;

	ret = phy_write_mmd(phydev, MDIO_MMD_PMAPMD,
			    DW_VR_XS_PMA_MP_16G_MPLLA_CTRL3,
			    MPLLA_BANDWIDTH(0xa03e));
	if (ret < 0)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_PMAPMD,
			     DW_VR_XS_PMA_MP_16G_25G_RX_EQ_CTRL0,
			     CTLE_BOOST_0_MASK | CTLE_POLE_0_MASK,
			     CTLE_BOOST_0(0xa) | CTLE_POLE_0(0x1));
	if (ret < 0)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_PMAPMD,
			     DW_VR_XS_PMA_MP_16G_25G_RX_MISC_CTRL0,
			     RX0_MISC_MASK, RX0_MISC(0x2));
	if (ret < 0)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_PMAPMD,
			     DW_VR_XS_PMA_MP_16G_25G_RX_IQ_CTRL0,
			     RX0_DELTA_IQ_MASK, RX0_DELTA_IQ(0x3));
	if (ret < 0)
		return ret;

	ret = xpcs_vr_reset(phydev);
	if (ret < 0) {
		pr_err("VR Reset failed\n");
		return ret;
	}

	ret = phy_modify_mmd(phydev, MDIO_MMD_PCS,
			     DW_VR_XS_PCS_DEBUG_CTRL,
			     SUPRESS_LOS_DET | RX_DT_EN_CTL,
			     SUPRESS_LOS_DET | RX_DT_EN_CTL);
	if (ret < 0)
		return ret;

	ret = phy_read_mmd_poll_timeout(phydev, MDIO_MMD_PCS,
					SR_XS_PCS_CTRL1, val,
					!(val & RST),
					1000, 100000, true);
	if (ret < 0) {
		pr_err("Reset done failed\n");
		return ret;
	}

	ret = phy_read_mmd_poll_timeout(phydev, MDIO_MMD_PMAPMD,
					DW_VR_XS_PMA_RX_LSTS, val,
					val & RX_VALID_0,
					1000, 100000, true);
	if (ret < 0) {
		pr_err("RX invlaid\n");
		return ret;
	}

	ret = phy_modify_mmd(phydev, MDIO_MMD_PMAPMD,
			     DW_VR_XS_PMA_MP_12G_16G_25G_RX_EQ_CTRL4,
			     RX_AD_REQ, RX_AD_REQ);
	if (ret < 0)
		return ret;

	ret = phy_read_mmd_poll_timeout(phydev, MDIO_MMD_PMAPMD,
					DW_VR_XS_PMA_MP_12G_16G_25G_MISC_STS, val,
					val & RX_ADPT_ACK,
					1000, 500000, true);
	if (ret < 0) {
		pr_err("EQ ACK failed\n");
		return ret;
	}

	ret = phy_modify_mmd(phydev, MDIO_MMD_PMAPMD,
			     DW_VR_XS_PMA_MP_12G_16G_25G_RX_EQ_CTRL4,
			     RX_AD_REQ, 0);
	if (ret < 0)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_PMAPMD,
			     DW_VR_XS_PMA_MP_12G_16G_25G_TX_EQ_CTRL0,
			     TX_EQ_MAIN_MASK, TX_EQ_MAIN(0x21));
	if (ret < 0)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_PMAPMD,
			     DW_VR_XS_PMA_MP_12G_16G_25G_TX_EQ_CTRL1,
			     TX_EQ_POST_MASK, TX_EQ_POST(0x1c));
	if (ret < 0)
		return ret;

	ret = phy_read_mmd_poll_timeout(phydev, MDIO_MMD_PCS,
					MDIO_STAT1, val,
					val & MDIO_STAT1_LSTATUS,
					1000, 100000, true);
	if (ret < 0) {
		pr_err("RX link up failed\n");
		return ret;
	}

	return ret;
}

static int xpcs_config_usxgmii(struct phy_device *phydev)
{
	int ret, val;

	ret = phy_write_mmd(phydev, MDIO_MMD_PMAPMD,
			    DW_VR_XS_PMA_MP_16G_MPLLA_CTRL3,
			    MPLLA_BANDWIDTH(0xa03e));
	if (ret < 0)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_PMAPMD,
			     DW_VR_XS_PMA_MP_16G_25G_RX_EQ_CTRL0,
			     CTLE_BOOST_0_MASK | CTLE_POLE_0_MASK,
			     CTLE_BOOST_0(0xa) | CTLE_POLE_0(0x1));
	if (ret < 0)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_PMAPMD,
			     DW_VR_XS_PMA_MP_16G_25G_RX_MISC_CTRL0,
			     RX0_MISC_MASK, RX0_MISC(0x2));
	if (ret < 0)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_PMAPMD,
			     DW_VR_XS_PMA_MP_16G_25G_RX_IQ_CTRL0,
			     RX0_DELTA_IQ_MASK, RX0_DELTA_IQ(0x3));
	if (ret < 0)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_PCS,
			     DW_VR_XS_PCS_DIG_CTRL1,
			     USXG_EN, USXG_EN);
	if (ret < 0)
		return ret;

	ret = xpcs_vr_reset(phydev);
	if (ret < 0) {
		pr_err("VR Reset failed\n");
		return ret;
	}

	ret = phy_modify_mmd(phydev, MDIO_MMD_PCS,
			     DW_VR_XS_PCS_DEBUG_CTRL,
			     SUPRESS_LOS_DET | RX_DT_EN_CTL,
			     SUPRESS_LOS_DET | RX_DT_EN_CTL);
	if (ret < 0)
		return ret;

	ret = phy_read_mmd_poll_timeout(phydev, MDIO_MMD_PCS,
					SR_XS_PCS_CTRL1, val,
					!(val & RST),
					1000, 100000, true);
	if (ret < 0) {
		pr_err("Reset done failed\n");
		return ret;
	}

	ret = phy_read_mmd_poll_timeout(phydev, MDIO_MMD_PMAPMD,
					DW_VR_XS_PMA_RX_LSTS, val,
					val & RX_VALID_0,
					1000, 100000, true);
	if (ret < 0) {
		pr_err("RX invlaid\n");
		return ret;
	}

	ret = phy_modify_mmd(phydev, MDIO_MMD_PMAPMD,
			     DW_VR_XS_PMA_MP_12G_16G_25G_RX_EQ_CTRL4,
			     RX_AD_REQ, RX_AD_REQ);
	if (ret < 0)
		return ret;

	ret = phy_read_mmd_poll_timeout(phydev, MDIO_MMD_PMAPMD,
					DW_VR_XS_PMA_MP_12G_16G_25G_MISC_STS, val,
					val & RX_ADPT_ACK,
					1000, 500000, true);
	if (ret < 0) {
		pr_err("EQ ACK failed\n");
		return ret;
	}

	ret = phy_modify_mmd(phydev, MDIO_MMD_PMAPMD,
			     DW_VR_XS_PMA_MP_12G_16G_25G_RX_EQ_CTRL4,
			     RX_AD_REQ, 0);
	if (ret < 0)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_PMAPMD,
			     DW_VR_XS_PMA_MP_12G_16G_25G_TX_EQ_CTRL0,
			     TX_EQ_MAIN_MASK, TX_EQ_MAIN(0x21));
	if (ret < 0)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_PMAPMD,
			     DW_VR_XS_PMA_MP_12G_16G_25G_TX_EQ_CTRL1,
			     TX_EQ_POST_MASK, TX_EQ_POST(0x1c));
	if (ret < 0)
		return ret;

	ret = phy_read_mmd_poll_timeout(phydev, MDIO_MMD_PCS,
					MDIO_STAT1, val,
					val & MDIO_STAT1_LSTATUS,
					1000, 100000, true);
	if (ret < 0) {
		pr_err("RX link up failed\n");
		return ret;
	}

	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND2, DW_VR_MII_AN_CTRL,
			     MII_AN_INTR_EN, MII_AN_INTR_EN);
	if (ret < 0)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND2, DW_SR_MII_CTRL,
			     AN_ENABLE, AN_ENABLE);
	if (ret < 0)
		return ret;

	return ret;
}

static int xpcs_config(struct phy_device *phydev)
{
	if (phydev->interface == PHY_INTERFACE_MODE_10GBASER)
		xpcs_config_10gbaser(phydev);
	else if (phydev->interface == PHY_INTERFACE_MODE_USXGMII)
		xpcs_config_usxgmii(phydev);

	return 0;
}

static int xpcs_usxgmii_nway_status(struct phy_device *phydev)
{
	int ret = 0, val, status;

	ret = phy_read_mmd_poll_timeout(phydev, MDIO_MMD_VEND2,
					DW_VR_MII_AN_INTR_STS, val,
					val & CL37_ANCMPLT_INTR,
					1000, 100000, true);
	if (ret < 0) {
		pr_err("USXGMII CL37 Nway failed\n");
		return ret;
	}

	/* Clear AN done status */
	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2,
			    DW_VR_MII_AN_INTR_STS,
			    CL37_ANCMPLT_INTR);
	if (ret < 0)
		return ret;

	val = phy_read_mmd(phydev, MDIO_MMD_VEND2, DW_VR_MII_AN_INTR_STS);
	if (val < 0)
		return val;

	status = USXG_AN_STS(val);
	val = 0;
	switch (USXG_AN_STS_SPEED_LINK(status)) {
	case USXG_SPEED_LINK_10M:
		phydev->speed = SPEED_10;
		break;
	case USXG_SPEED_LINK_100M:
		phydev->speed = SPEED_100;
		val |= MII_SS13;
		break;
	case USXG_SPEED_LINK_1000M:
		phydev->speed = SPEED_1000;
		val |= MII_SS6;
		break;
	case USXG_SPEED_LINK_10G:
		phydev->speed = SPEED_10000;
		val |= MII_SS13 | MII_SS6;
		break;
	case USXG_SPEED_LINK_2_5G:
		phydev->speed = SPEED_2500;
		val |= MII_SS5;
		break;
	case USXG_SPEED_LINK_5G:
		phydev->speed = SPEED_5000;
		val |= MII_SS13 | MII_SS5;
		break;
	default:
		pr_err("Error Speed\n");
		break;
	}
	phydev->link = !!(status & USXG_LINK_UP);
	phydev->duplex = !!(status & USXG_DUPLEX_FULL);

	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND2,
			     DW_SR_MII_CTRL,
			     MII_SS5 | MII_SS5 | MII_SS13, val);
	if (ret < 0)
		return ret;

	udelay(1000);

	ret = phy_modify_mmd(phydev, MDIO_MMD_PCS, DW_VR_XS_PCS_DIG_CTRL1,
			     USRA_RST, USRA_RST);
	if (ret < 0)
		return ret;

	ret = phy_read_mmd_poll_timeout(phydev, MDIO_MMD_PCS,
					DW_VR_XS_PCS_DIG_CTRL1, val,
					!(val & USRA_RST),
					1000, 100000, true);
	if (ret < 0) {
		pr_err("USXGMII RA reset done failed\n");
		return ret;
	}

	return ret;
}

static int xpcs_10gbaser_status(struct phy_device *phydev)
{
	int ret = 0, val;

	ret = phy_read_mmd_poll_timeout(phydev, MDIO_MMD_PCS,
					MDIO_STAT1, val,
					val & MDIO_STAT1_LSTATUS,
					1000, 100000, true);
	if (ret < 0) {
		pr_err("rx link up timeout\n");
		return ret;
	}

	phydev->speed = SPEED_10000;
	phydev->duplex = DUPLEX_FULL;
	phydev->link = true;

	return ret;
}

static int xpcs_startup(struct phy_device *phydev)
{
	int ret;

	if (phydev->interface == PHY_INTERFACE_MODE_10GBASER)
		ret = xpcs_10gbaser_status(phydev);
	else if (phydev->interface == PHY_INTERFACE_MODE_USXGMII)
		ret = xpcs_usxgmii_nway_status(phydev);
	else
		return -EINVAL;

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

	phydev->mmds = MDIO_DEVS_PMAPMD | MDIO_DEVS_PCS | MDIO_DEVS_VEND2;

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
	.shutdown	= NULL,
};
