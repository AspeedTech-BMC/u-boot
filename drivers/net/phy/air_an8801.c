// SPDX-License-Identifier: GPL-2.0
/* FILE NAME:  air_an8801.c
 * PURPOSE:
 *      Airoha phy driver for Uboot
 * NOTES:
 *
 */

/* INCLUDE FILE DECLARATIONS
 */

#include <common.h>
#include <malloc.h>
#include <phy.h>
#include <dm.h>

#include "air_an8801.h"

#define phydev_cfg(phy)            ((struct an8801r_priv *)(phy)->priv)

/* For reference only
 *	GPIO1    <-> LED0,
 *	GPIO2    <-> LED1,
 *	GPIO3    <-> LED2,
 */
/* User-defined.B */
static const struct AIR_LED_CFG_T led_cfg_dlt[MAX_LED_SIZE] = {
//   LED Enable,          GPIO,    LED Polarity,      LED ON,    LED Blink
	/* LED0 */
	{LED_ENABLE, AIR_LED_GPIO1, AIR_ACTIVE_LOW,  AIR_LED0_ON, AIR_LED0_BLK},
	/* LED1 */
	{LED_ENABLE, AIR_LED_GPIO2, AIR_ACTIVE_HIGH, AIR_LED1_ON, AIR_LED1_BLK},
	/* LED2 */
	{LED_ENABLE, AIR_LED_GPIO3, AIR_ACTIVE_HIGH, AIR_LED2_ON, AIR_LED2_BLK},
};

static const u16 led_blink_cfg_dlt = AIR_LED_BLK_DUR_64M;
/* RGMII delay */
static const u8 rxdelay_force = FALSE;
static const u8 txdelay_force = FALSE;
static const u16 rxdelay_step = AIR_RGMII_DELAY_NOSTEP;
static const u8 rxdelay_align = FALSE;
static const u16 txdelay_step = AIR_RGMII_DELAY_NOSTEP;
/* User-defined.E */

/************************************************************************
 *                  F U N C T I O N S
 ************************************************************************/
static int __air_buckpbus_reg_write(struct phy_device *phydev, u32 addr,
				    u32 data)
{
	int err = 0;

	err = phy_write(phydev, MDIO_DEVAD_NONE, 0x1F, 4);
	if (err)
		return err;

	err |= phy_write(phydev, MDIO_DEVAD_NONE, 0x10, 0);
	err |= phy_write(phydev, MDIO_DEVAD_NONE, 0x11, (u16)(addr >> 16));
	err |= phy_write(phydev, MDIO_DEVAD_NONE, 0x12, (u16)(addr & 0xffff));
	err |= phy_write(phydev, MDIO_DEVAD_NONE, 0x13, (u16)(data >> 16));
	err |= phy_write(phydev, MDIO_DEVAD_NONE, 0x14, (u16)(data & 0xffff));
	err |= phy_write(phydev, MDIO_DEVAD_NONE, 0x1F, 0);

	return err;
}

static u32 __air_buckpbus_reg_read(struct phy_device *phydev, u32 addr)
{
	int err = 0;
	u32 data_h, data_l, data;

	err = phy_write(phydev, MDIO_DEVAD_NONE, 0x1F, 4);
	if (err)
		return err;

	err |= phy_write(phydev, MDIO_DEVAD_NONE, 0x10, 0);
	err |= phy_write(phydev, MDIO_DEVAD_NONE, 0x15, (u16)(addr >> 16));
	err |= phy_write(phydev, MDIO_DEVAD_NONE, 0x16, (u16)(addr & 0xffff));
	data_h = phy_read(phydev, MDIO_DEVAD_NONE, 0x17);
	data_l = phy_read(phydev, MDIO_DEVAD_NONE, 0x18);
	err |= phy_write(phydev, MDIO_DEVAD_NONE, 0x1F, 0);
	if (err)
		return INVALID_DATA;

	data = ((data_h & 0xffff) << 16) | (data_l & 0xffff);
	return data;
}

static int air_buckpbus_reg_write(struct phy_device *phydev, u32 addr, u32 data)
{
	return __air_buckpbus_reg_write(phydev, addr, data);
}

static u32 air_buckpbus_reg_read(struct phy_device *phydev, u32 addr)
{
	return __air_buckpbus_reg_read(phydev, addr);
}

static int __an8801r_cl45_write(struct phy_device *phydev, int devad, u16 reg,
				u16 val)
{
	u32 addr = (AN8801R_EPHY_ADDR | AN8801R_CL22 | (devad << 18) |
				(reg << 2));

	return __air_buckpbus_reg_write(phydev, addr, val);
}

static int __an8801r_cl45_read(struct phy_device *phydev, int devad, u16 reg)
{
	u32 addr = (AN8801R_EPHY_ADDR | AN8801R_CL22 | (devad << 18) |
				(reg << 2));

	return __air_buckpbus_reg_read(phydev, addr);
}

static int an8801r_cl45_write(struct phy_device *phydev, int devad, u16 reg,
			      u16 val)
{
	return __an8801r_cl45_write(phydev, devad, reg, val);
}

static int an8801r_cl45_read(struct phy_device *phydev, int devad, u16 reg,
			     u16 *read_data)
{
	int data = 0;

	data = __an8801r_cl45_read(phydev, devad, reg);

	if (data == INVALID_DATA)
		return -EINVAL;

	*read_data = data;

	return 0;
}

static int air_sw_reset(struct phy_device *phydev)
{
	u32 reg_value;
	u8 retry = MAX_RETRY;

	/* Software Reset PHY */
	reg_value = phy_read(phydev, MDIO_DEVAD_NONE, MII_BMCR);
	reg_value |= BMCR_RESET;
	phy_write(phydev, MDIO_DEVAD_NONE, MII_BMCR, reg_value);
	do {
		mdelay(10);
		reg_value = phy_read(phydev, MDIO_DEVAD_NONE, MII_BMCR);
		retry--;
		if (retry == 0) {
			printf("AN8801R: Reset fail !\n");
			return -1;
		}
	} while (reg_value & BMCR_RESET);

	return 0;
}

static int an8801r_led_set_usr_def(struct phy_device *phydev, u8 entity,
				   u16 polar, u16 on_evt, u16 blk_evt)
{
	int err;

	if (polar == AIR_ACTIVE_HIGH)
		on_evt |= LED_ON_POL;
	else
		on_evt &= ~LED_ON_POL;

	on_evt |= LED_ON_EN;

	err = an8801r_cl45_write(phydev, 0x1f, LED_ON_CTRL(entity), on_evt);
	if (err)
		return -1;

	return an8801r_cl45_write(phydev, 0x1f, LED_BLK_CTRL(entity), blk_evt);
}

static int an8801r_led_set_blink(struct phy_device *phydev, u16 blink)
{
	int err;

	err = an8801r_cl45_write(phydev, 0x1f, LED_BLK_DUR,
				 LED_BLINK_DURATION(blink));
	if (err)
		return err;

	return an8801r_cl45_write(phydev, 0x1f, LED_ON_DUR,
				 (LED_BLINK_DURATION(blink) >> 1));
}

static int an8801r_led_set_mode(struct phy_device *phydev, u8 mode)
{
	int err;
	u16 data;

	err = an8801r_cl45_read(phydev, 0x1f, LED_BCR, &data);
	if (err)
		return -1;

	switch (mode) {
	case AIR_LED_MODE_DISABLE:
		data &= ~LED_BCR_EXT_CTRL;
		data &= ~LED_BCR_MODE_MASK;
		data |= LED_BCR_MODE_DISABLE;
		break;
	case AIR_LED_MODE_USER_DEFINE:
		data |= (LED_BCR_EXT_CTRL | LED_BCR_CLK_EN);
		break;
	}
	return an8801r_cl45_write(phydev, 0x1f, LED_BCR, data);
}

static int an8801r_led_set_state(struct phy_device *phydev, u8 entity, u8 state)
{
	u16 data;
	int err;

	err = an8801r_cl45_read(phydev, 0x1f, LED_ON_CTRL(entity), &data);
	if (err)
		return err;

	if (state)
		data |= LED_ON_EN;
	else
		data &= ~LED_ON_EN;

	return an8801r_cl45_write(phydev, 0x1f, LED_ON_CTRL(entity), data);
}

static int an8801r_led_init(struct phy_device *phydev)
{
	struct an8801r_priv *priv = phydev_cfg(phydev);
	struct AIR_LED_CFG_T *led_cfg = priv->led_cfg;
	int ret, led_id;
	u32 data;
	u16 led_blink_cfg = priv->led_blink_cfg;

	ret = an8801r_led_set_blink(phydev, led_blink_cfg);
	if (ret != 0)
		return ret;

	ret = an8801r_led_set_mode(phydev, AIR_LED_MODE_USER_DEFINE);
	if (ret != 0) {
		printf("AN8801R: Fail to set LED mode, ret %d!\n", ret);
		return ret;
	}

	for (led_id = AIR_LED0; led_id < MAX_LED_SIZE; led_id++) {
		ret = an8801r_led_set_state(phydev, led_id, led_cfg[led_id].en);
		if (ret != 0) {
			printf("AN8801R: Fail to set LED%d state, ret %d!\n",
			       led_id, ret);
			return ret;
		}
		if (led_cfg[led_id].en == LED_ENABLE) {
			data = air_buckpbus_reg_read(phydev, 0x10000054);
			data |= BIT(led_cfg[led_id].gpio);
			ret |= air_buckpbus_reg_write(phydev, 0x10000054, data);

			data = air_buckpbus_reg_read(phydev, 0x10000058);
			data |= LED_GPIO_SEL(led_id, led_cfg[led_id].gpio);
			ret |= air_buckpbus_reg_write(phydev, 0x10000058, data);

			data = air_buckpbus_reg_read(phydev, 0x10000070);
			data &= ~BIT(led_cfg[led_id].gpio);
			ret |= air_buckpbus_reg_write(phydev, 0x10000070, data);

			ret |= an8801r_led_set_usr_def(phydev, led_id,
				led_cfg[led_id].pol,
				led_cfg[led_id].on_cfg,
				led_cfg[led_id].blk_cfg);
			if (ret != 0) {
				printf("AN8801R: Fail to set LED%d, ret %d!\n",
				       led_id, ret);
				return ret;
			}
		}
	}
	printf("AN8801R: LED initialize OK !\n");
	return 0;
}

static int an8801r_of_init(struct phy_device *phydev)
{
	struct an8801r_priv *priv = phydev_cfg(phydev);
	ofnode node = phy_get_ofnode(phydev);
	u32 val = 0;

	if (!ofnode_valid(node))
		return -EINVAL;

	if (ofnode_get_property(node, "airoha,rxclk-delay", NULL)) {
		if (ofnode_read_u32(node, "airoha,rxclk-delay", &val) != 0) {
			printf("airoha,rxclk-delay value is invalid.");
			return -1;
		}
		if (val < AIR_RGMII_DELAY_NOSTEP ||
		    val > AIR_RGMII_DELAY_STEP_7) {
			printf("airoha,rxclk-delay value %u out of range.",
			       val);
			return -1;
		}
		priv->rxdelay_force = TRUE;
		priv->rxdelay_step = val;
		priv->rxdelay_align = ofnode_read_bool(node,
						       "airoha,rxclk-delay-align");
	}

	if (ofnode_get_property(node, "airoha,txclk-delay", NULL)) {
		if (ofnode_read_u32(node, "airoha,txclk-delay", &val) != 0) {
			printf("airoha,txclk-delay value is invalid.");
			return -1;
		}
		if (val < AIR_RGMII_DELAY_NOSTEP ||
		    val > AIR_RGMII_DELAY_STEP_7) {
			printf("airoha,txclk-delay value %u out of range.",
			       val);
			return -1;
		}
		priv->txdelay_force = TRUE;
		priv->txdelay_step = val;
	}

	return 0;
}

static int an8801r_rgmii_rxdelay(struct phy_device *phydev, u16 delay, u8 align)
{
	u32 reg_val = delay & RGMII_DELAY_STEP_MASK;

	/* align */
	if (align) {
		reg_val |= RGMII_RXDELAY_ALIGN;
		printf("AN8801R: Rxdelay align\n");
	}
	reg_val |= RGMII_RXDELAY_FORCE_MODE;
	air_buckpbus_reg_write(phydev, 0x1021C02C, reg_val);
	reg_val = air_buckpbus_reg_read(phydev, 0x1021C02C);
	printf("AN8801R: Force rxdelay = %d(0x%x)\n", delay, reg_val);
	return 0;
}

static int an8801r_rgmii_txdelay(struct phy_device *phydev, u16 delay)
{
	u32 reg_val = delay & RGMII_DELAY_STEP_MASK;

	reg_val |= RGMII_TXDELAY_FORCE_MODE;
	air_buckpbus_reg_write(phydev, 0x1021C024, reg_val);
	reg_val = air_buckpbus_reg_read(phydev, 0x1021C024);
	printf("AN8801R: Force txdelay = %d(0x%x)\n", delay, reg_val);
	return 0;
}

static int an8801r_rgmii_delay_config(struct phy_device *phydev)
{
	struct an8801r_priv *priv = phydev_cfg(phydev);

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII_TXID:
		an8801r_rgmii_txdelay(phydev, AIR_RGMII_DELAY_STEP_4);
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		an8801r_rgmii_rxdelay(phydev, AIR_RGMII_DELAY_NOSTEP, TRUE);
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		an8801r_rgmii_txdelay(phydev, AIR_RGMII_DELAY_STEP_4);
		an8801r_rgmii_rxdelay(phydev, AIR_RGMII_DELAY_NOSTEP, TRUE);
		break;
	case PHY_INTERFACE_MODE_RGMII:
	default:
		if (priv->rxdelay_force)
			an8801r_rgmii_rxdelay(phydev, priv->rxdelay_step,
					      priv->rxdelay_align);
		if (priv->txdelay_force)
			an8801r_rgmii_txdelay(phydev, priv->txdelay_step);
		break;
	}
	return 0;
}

static int an8801r_config_init(struct phy_device *phydev)
{
	int ret;

	ret = an8801r_of_init(phydev);
	if (ret < 0)
		return ret;

	ret = air_sw_reset(phydev);
	if (ret < 0)
		return ret;

	air_buckpbus_reg_write(phydev, 0x11F808D0, 0x180);

	air_buckpbus_reg_write(phydev, 0x1021c004, 0x1);
	air_buckpbus_reg_write(phydev, 0x10270004, 0x3f);
	air_buckpbus_reg_write(phydev, 0x10270104, 0xff);
	air_buckpbus_reg_write(phydev, 0x10270204, 0xff);

	an8801r_rgmii_delay_config(phydev);

	ret = an8801r_led_init(phydev);
	if (ret != 0) {
		printf("AN8801R: LED initialize fail, ret %d !\n", ret);
		return ret;
	}
	printf("AN8801R: Initialize OK ! (%s)\n", AN8801R_DRIVER_VERSION);
	return 0;
}

static int an8801r_phy_probe(struct phy_device *phydev)
{
	u32 reg_value, phy_id, led_id;
	struct an8801r_priv *priv = NULL;

	reg_value = phy_read(phydev, MDIO_DEVAD_NONE, 2);
	phy_id = reg_value << 16;
	reg_value = phy_read(phydev, MDIO_DEVAD_NONE, 3);
	phy_id |= reg_value;
	printf("AN8801R: PHY-ID = %x\n", phy_id);

	if (phy_id != AN8801R_PHY_ID) {
		printf("AN8801R can't be detected.\n");
		return -1;
	}

	priv = malloc(sizeof(struct an8801r_priv));
	if (!priv)
		return -ENOMEM;

	for (led_id = AIR_LED0; led_id < MAX_LED_SIZE; led_id++)
		priv->led_cfg[led_id] = led_cfg_dlt[led_id];

	priv->led_blink_cfg  = led_blink_cfg_dlt;
	priv->rxdelay_force  = rxdelay_force;
	priv->txdelay_force  = txdelay_force;
	priv->rxdelay_step   = rxdelay_step;
	priv->rxdelay_align  = rxdelay_align;
	priv->txdelay_step   = txdelay_step;

	phydev->priv = priv;
	return 0;
}

static int an8801r_config(struct phy_device *phydev)
{
	int ret;

	ret = an8801r_phy_probe(phydev);
	if (ret)
		return ret;

	return an8801r_config_init(phydev);
}

static int an8801r_read_status(struct phy_device *phydev)
{
	u32 data;

	if (phydev->link == LINK_UP) {
		debug("AN8801R: SPEED %d\n", phydev->speed);
		if (phydev->speed == SPEED_1000) {
			data = air_buckpbus_reg_read(phydev, 0x10005054);
			data |= BIT(0);
			air_buckpbus_reg_write(phydev, 0x10005054, data);
		} else {
			data = air_buckpbus_reg_read(phydev, 0x10005054);
			data &= ~BIT(0);
			air_buckpbus_reg_write(phydev, 0x10005054, data);
		}
	}
	return 0;
}

static int an8801r_startup(struct phy_device *phydev)
{
	int ret;

	ret = genphy_startup(phydev);
	if (ret)
		return ret;

	return an8801r_read_status(phydev);
}

static struct phy_driver AIR_AN8801R_driver = {
	.name = "Airoha AN8801R",
	.uid = AN8801R_PHY_ID,
	.mask = 0x0ffffff0,
	.features = PHY_GBIT_FEATURES,
	.config = &an8801r_config,
	.startup = &an8801r_startup,
	.shutdown = &genphy_shutdown,
};

int phy_air_an8801_init(void)
{
	phy_register(&AIR_AN8801R_driver);
	return 0;
}
