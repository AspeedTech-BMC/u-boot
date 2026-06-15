// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) ASPEED Technology Inc.
 */
#include "platform.h"
#include "internal.h"
#include "phy.h"
#include "phy_tbl.h"

#define PHY_BMCR		0x00
#define   BMCR_RESET		BIT(15)
#define   BMCR_LOOPBACK		BIT(14)
#define   BMCR_SPEED_0		BIT(13)	/* 1=100M, 0=10M */
#define   BMCR_ANE		BIT(12)
#define   BMCR_PWD		BIT(11)
#define   BMCR_ISO		BIT(10)
#define   BMCR_RESTART_ANE	BIT(9)
#define   BMCR_DUPLEX		BIT(8)
#define   BMCR_COLLI_TEST	BIT(7)
#define   BMCR_SPEED_1		BIT(6)
#define     BMCR_SPEED_1000	BMCR_SPEED_1
#define     BMCR_SPEED_100	BMCR_SPEED_0
#define     BMCR_SPEED_10	0
#define   BMCR_BASIC_MASK	(BMCR_SPEED_1 | BMCR_DUPLEX | BMCR_ANE | \
				 BMCR_SPEED_0 | BMCR_LOOPBACK)

#define PHY_BMSR		0x01
#define   BMSR_AN_COMPLETE	BIT(5)
#define   BMSR_LINK		BIT(2)
#define PHY_ID_1		0x02
#define PHY_ID_2		0x03
#define PHY_ANER		0x06	/* Auto-negotiation Expansion Register */
#define PHY_GBCR		0x09	/* 1000Base-T Control Register */
#define PHY_INER		0x12	/* Interrupt Enable Register */
#define PHY_CR1			0x18
#define PHY_SR			0x1a	/* PHY Specific Status Register */
#define PHY_EPAGSR		0x1f	/* realtek extension page selection */

#define SR_SPEED		(0x3 << 4)
#define SR_SPEED_1000		(0x2 << 4)
#define SR_SPEED_100		(0x1 << 4)
#define SR_SPEED_10		(0x0 << 4)
#define SR_DUPLEX		BIT(3)
#define SR_LINK			BIT(2)

/* scan PHY ID on all possible PHY addresses (from 0 to 31) */
static int is_phy_id_valid(int id1, int id2)
{
	int mask = 0xffff;

	id1 &= mask;
	id2 &= mask;

	if ((id1 == 0 && id2 == 0) || (id1 == mask || id2 == mask))
		return 0;
	else
		return 1;
}

void phy_find_setting(struct phy_s *obj, u8 phy_addr)
{
	struct phy_desc *desc;
	int i;

	for (i = 0; i < ARRAY_SIZE(phy_lookup_tbl); i++) {
		desc = &phy_lookup_tbl[i];

		if (obj->phy_id[phy_addr][0] == desc->id1 &&
		    (obj->phy_id[phy_addr][1] & desc->id2_mask) == (desc->id2 & desc->id2_mask)) {
			obj->dev_phy[phy_addr] = desc;
			break;
		}
	}
}

void phy_print_info(struct phy_s *obj)
{
	printf("List all phy address:\n");
	print_hex_dump("", DUMP_PREFIX_OFFSET, 16, 2, obj->phy_id, sizeof(obj->phy_id), false);
	printf("List phy device:\n");
	printf("Select [addr][ID 0:ID 1] Device\n");
	printf("========================================\n");
	for (int addr = 0; addr < 32; addr++) {
		if (obj->dev_phy[addr]) {
			if (addr == obj->mdio->phy_addr)
				printf("     * ");
			else
				printf("       ");
			printf("[  %02d][%04X:%04X] %s\n", addr, obj->phy_id[addr][0],
			       obj->phy_id[addr][1], obj->dev_phy[addr]->name);
		}
	}
}

int phy_init(struct phy_s *obj)
{
	int i, addr = -1;

	memset(obj->dev_phy, 0, sizeof(obj->dev_phy));
	memset(obj->phy_id, 0xFF, sizeof(obj->phy_id));

	for (i = 0; i < 32; i++) {
		obj->phy_id[i][0] = aspeed_mdio_read(obj->mdio, i, PHY_ID_1);
		obj->phy_id[i][1] = aspeed_mdio_read(obj->mdio, i, PHY_ID_2);
		if (is_phy_id_valid(obj->phy_id[i][0], obj->phy_id[i][1])) {
			addr = i;
			phy_find_setting(obj, i);
		}
	}

	obj->mdio->phy_addr = (addr == -1) ? 0 : addr;
	obj->id[0] = aspeed_mdio_read(obj->mdio, addr, PHY_ID_1);
	obj->id[1] = aspeed_mdio_read(obj->mdio, addr, PHY_ID_2);

	if (obj->dev_phy[obj->mdio->phy_addr])
		obj->dev_phy[obj->mdio->phy_addr]->config(obj);

	phy_print_info(obj);

	return addr;
}

void phy_free(struct phy_s *obj)
{
	if (obj->dev_phy[obj->mdio->phy_addr]->clear)
		obj->dev_phy[obj->mdio->phy_addr]->clear(obj);
}

void phy_restart_aneg(struct phy_s *obj)
{
	struct mdio_s *mdio = obj->mdio;
	int ctl;

	ctl = aspeed_mdio_read(mdio, mdio->phy_addr, PHY_BMCR);
	if (ctl < 0)
		return;

	ctl |= (BMCR_ANE | BMCR_RESTART_ANE);

	/* Don't isolate the PHY if we're negotiating */
	ctl &= ~(BMCR_ISO);

	aspeed_mdio_write(mdio, mdio->phy_addr, PHY_BMCR, ctl);
}

static void phy_default_config(struct phy_s *obj)
{
	struct mdio_s *mdio = obj->mdio;
	u32 bmcr = BMCR_DUPLEX;

	switch (obj->speed) {
	case 1000:
		bmcr |= BMCR_SPEED_1000;
		break;
	case 100:
		bmcr |= BMCR_SPEED_100;
		break;
	case 10:
	default:
		break;
	}

	if (obj->loopback == PHY_LOOPBACK_INT)
		bmcr |= BMCR_LOOPBACK;

	aspeed_mdio_write(mdio, mdio->phy_addr, PHY_BMCR, bmcr);

	if (obj->autoneg)
		phy_restart_aneg(obj);
}

static void phy_reset(struct phy_s *obj)
{
	struct mdio_s *mdio = obj->mdio;
	int reg;
	int timeout = 500;

	aspeed_mdio_write(mdio, mdio->phy_addr, PHY_BMCR, BMCR_RESET);

	reg = aspeed_mdio_read(mdio, mdio->phy_addr, PHY_BMCR);
	while ((reg & BMCR_RESET) && timeout--) {
		reg = aspeed_mdio_read(mdio, mdio->phy_addr, PHY_BMCR);

		if (reg < 0) {
			printf("PHY status read failed\n");
			return;
		}
		mdelay(1);
	}

	if (reg & BMCR_RESET)
		printf("PHY reset timed out\n");
}

void phy_rtl8211f_config(struct phy_s *obj)
{
	struct mdio_s *mdio = obj->mdio;
	u32 reg, mask;
	u32 physr = SR_DUPLEX | SR_LINK;

	if (obj->phy_mode == PHY_INTERFACE_MODE_SGMII) {
		if (obj->loopback == PHY_LOOPBACK_OFF) {
			return;
		} else if (obj->loopback == PHY_LOOPBACK_EXT) {
			switch (obj->speed) {
			case 100:
				aspeed_mdio_write(mdio, mdio->phy_addr, PHY_EPAGSR, 0);
				aspeed_mdio_write(mdio, mdio->phy_addr, PHY_BMCR, 0x2100);
				break;
			case 10:
				aspeed_mdio_write(mdio, mdio->phy_addr, PHY_EPAGSR, 0);
				aspeed_mdio_write(mdio, mdio->phy_addr, PHY_BMCR, 0x0100);
				break;
			case 1000:
			default:
				aspeed_mdio_write(mdio, mdio->phy_addr, PHY_EPAGSR, 0xa43);
				aspeed_mdio_write(mdio, mdio->phy_addr, PHY_BMCR, 0x8000);
				mdelay(200);
				aspeed_mdio_write(mdio, mdio->phy_addr, PHY_BMCR, 0x0140);
				aspeed_mdio_write(mdio, mdio->phy_addr, PHY_CR1, 0x2d18);
				aspeed_mdio_write(mdio, mdio->phy_addr, PHY_EPAGSR, 0);
				mdelay(200);
				break;
			}
		} else {
			aspeed_mdio_write(mdio, mdio->phy_addr, PHY_EPAGSR, 0);
			aspeed_mdio_write(mdio, mdio->phy_addr, PHY_BMCR, 0x8000);
			mdelay(200);
			switch (obj->speed) {
			case 100:
				aspeed_mdio_write(mdio, mdio->phy_addr, PHY_BMCR, 0x6000);
				break;
			case 10:
				aspeed_mdio_write(mdio, mdio->phy_addr, PHY_BMCR, 0x4000);
				break;
			case 1000:
			default:
				aspeed_mdio_write(mdio, mdio->phy_addr, PHY_BMCR, 0x4040);
				break;
			}
			mdelay(200);
		}
		aspeed_mdio_write(mdio, mdio->phy_addr, PHY_EPAGSR, 0xdcf);
		(void)aspeed_mdio_read(mdio, mdio->phy_addr, 21);
		(void)aspeed_mdio_read(mdio, mdio->phy_addr, 21);
		(void)aspeed_mdio_read(mdio, mdio->phy_addr, 22);
		(void)aspeed_mdio_read(mdio, mdio->phy_addr, 22);
		aspeed_mdio_write(mdio, mdio->phy_addr, PHY_EPAGSR, 0);
		(void)aspeed_mdio_read(mdio, mdio->phy_addr, PHY_BMSR);
		(void)aspeed_mdio_read(mdio, mdio->phy_addr, PHY_BMSR);
		return;
	}

	phy_reset(obj);
	phy_default_config(obj);

	/* TX interface delay: page 0xd08 reg 0x11[8] */
	aspeed_mdio_write(mdio, mdio->phy_addr, PHY_EPAGSR, 0xd08);
	reg = aspeed_mdio_read(mdio, mdio->phy_addr, 0x11);
	reg &= ~BIT(8);
	if (obj->phy_mode == PHY_INTERFACE_MODE_RGMII_ID ||
	    obj->phy_mode == PHY_INTERFACE_MODE_RGMII_TXID) {
		reg |= BIT(8);
	}
	aspeed_mdio_write(mdio, mdio->phy_addr, 0x11, reg);

	/* RX interface delay: page 0xd08 reg 0x15[3] */
	reg = aspeed_mdio_read(mdio, mdio->phy_addr, 0x15);
	reg &= ~BIT(3);
	if (obj->phy_mode == PHY_INTERFACE_MODE_RGMII_ID ||
	    obj->phy_mode == PHY_INTERFACE_MODE_RGMII_RXID) {
		reg |= BIT(3);
	}
	aspeed_mdio_write(mdio, mdio->phy_addr, 0x15, reg);

	if (obj->loopback == PHY_LOOPBACK_OFF)
		return;

	/* page 0xa43 reg 0x18[10], enable MDI loopback
	 *            reg 0x18[11], MDI loopback for short cable
	 */
	aspeed_mdio_write(mdio, mdio->phy_addr, PHY_EPAGSR, 0xa43);
	if (obj->loopback == PHY_LOOPBACK_EXT)
		aspeed_mdio_write(mdio, mdio->phy_addr, PHY_CR1, 0x2d18);
	else
		aspeed_mdio_write(mdio, mdio->phy_addr, PHY_CR1, 0x2118);

	/* page 0 reg 0x09[9], advertise 1000Base-T full-duplex capacity */
	aspeed_mdio_write(mdio, mdio->phy_addr, PHY_EPAGSR, 0);
	switch (obj->speed) {
	case 1000:
		aspeed_mdio_write(mdio, mdio->phy_addr, PHY_GBCR, BIT(9));
		physr |= 0x2 << 4;
		break;
	case 100:
		physr |= 0x1 << 4;
	case 10:
		aspeed_mdio_write(mdio, mdio->phy_addr, PHY_GBCR, 0);
		break;
	}

	/* page 0xa43 reg 0x1a */
	aspeed_mdio_write(mdio, mdio->phy_addr, PHY_EPAGSR, 0xa43);
	mask = SR_SPEED | SR_DUPLEX | SR_LINK;
	while ((aspeed_mdio_read(mdio, mdio->phy_addr, PHY_SR) & mask) != physr)
		;
	aspeed_mdio_write(mdio, mdio->phy_addr, PHY_EPAGSR, 0);
}

void phy_bcm54616_config(struct phy_s *obj)
{
	struct mdio_s *mdio = obj->mdio;
	u32 reg18, reg1c;

	obj->phy_reg[PHY_BMCR] = aspeed_mdio_read(mdio, mdio->phy_addr, PHY_BMCR);
	obj->phy_reg[PHY_GBCR] = aspeed_mdio_read(mdio, mdio->phy_addr, PHY_GBCR);

	phy_reset(obj);
	phy_default_config(obj);

	/*
	 * RX interface delay: reg 0x18, shadow value b'0111: misc control
	 * bit[8] RGMII RXD to RXC skew
	 */
	aspeed_mdio_write(mdio, mdio->phy_addr, 0x18, (0x7 << 12) | 0x7);
	reg18 = aspeed_mdio_read(mdio, mdio->phy_addr, 0x18);
	debug("brcm-phy reg18 = %04x\n", reg18);
	reg18 &= ~(GENMASK(14, 12) | BIT(8) | GENMASK(2, 0));
	reg18 |= BIT(15) | (0x7 << 12) | 0x7;
	if (obj->phy_mode == PHY_INTERFACE_MODE_RGMII_ID ||
	    obj->phy_mode == PHY_INTERFACE_MODE_RGMII_RXID)
		reg18 |= BIT(8);
	aspeed_mdio_write(mdio, mdio->phy_addr, 0x18, reg18);

	/*
	 * TX interface delay: reg 0x1c, shadow value b'0011: clock alignment control
	 * bit[9] GTXCLK clock delay enable
	 */
	aspeed_mdio_write(mdio, mdio->phy_addr, 0x1c, 0x3 << 10);
	reg1c = aspeed_mdio_read(mdio, mdio->phy_addr, 0x1c);
	debug("brcm-phy reg1c = %04x\n", reg1c);
	reg1c &= ~(GENMASK(14, 10) | BIT(9));
	reg1c |= BIT(15) | (0x3 << 10);
	if (obj->phy_mode == PHY_INTERFACE_MODE_RGMII_ID ||
	    obj->phy_mode == PHY_INTERFACE_MODE_RGMII_TXID)
		reg1c |= BIT(9);
	aspeed_mdio_write(mdio, mdio->phy_addr, 0x1c, reg1c);

	/*
	 * Special setting for external loopback
	 */
	if (obj->loopback == PHY_LOOPBACK_EXT) {
		switch (obj->speed) {
		case 1000:
			aspeed_mdio_write(mdio, mdio->phy_addr, 0x9, 0x1800);
			aspeed_mdio_write(mdio, mdio->phy_addr, 0x0, 0x0140);
			aspeed_mdio_write(mdio, mdio->phy_addr, 0x18, 0x8400);
			break;
		case 100:
			aspeed_mdio_write(mdio, mdio->phy_addr, 0x0, 0x2100);
			aspeed_mdio_write(mdio, mdio->phy_addr, 0x18, 0x8400);
			break;
		case 10:
			aspeed_mdio_write(mdio, mdio->phy_addr, 0x0, 0x0100);
			aspeed_mdio_write(mdio, mdio->phy_addr, 0x18, 0x8400);
			break;
		}
	} else if (obj->loopback == PHY_LOOPBACK_INT) {
		/* BCM54213 needs to set force-link: reg1E[12] */
		if (obj->id[0] == 0x600d && obj->id[1] == 0x84a2)
			aspeed_mdio_write(mdio, mdio->phy_addr, 0x1e, BIT(12));
	}

	mdelay(100);
}

void phy_bcm54616_clear(struct phy_s *obj)
{
	struct mdio_s *mdio = obj->mdio;

	phy_reset(obj);

	aspeed_mdio_write(mdio, mdio->phy_addr, PHY_BMCR, obj->phy_reg[PHY_BMCR]);
	aspeed_mdio_write(mdio, mdio->phy_addr, PHY_GBCR, obj->phy_reg[PHY_GBCR]);

	phy_restart_aneg(obj);
}

void phy_bcm5221_config(struct phy_s *obj)
{
	struct mdio_s *mdio = obj->mdio;

	phy_reset(obj);
	phy_default_config(obj);

	if (obj->loopback == PHY_LOOPBACK_INT) {
		u32 mask = (obj->speed == 100) ? 0x7 : 0x1;
		u32 reg;

		do {
			reg = aspeed_mdio_read(mdio, mdio->phy_addr, 0x18) & mask;
		} while (reg != mask);
	}
}

static void air_buckpbus_reg_write(struct mdio_s *mdio, uint32_t addr, uint32_t data)
{
	aspeed_mdio_write(mdio, mdio->phy_addr, 0x1F, 4);
	aspeed_mdio_write(mdio, mdio->phy_addr, 0x10, 0);
	aspeed_mdio_write(mdio, mdio->phy_addr, 0x11, (uint16_t)(addr >> 16));
	aspeed_mdio_write(mdio, mdio->phy_addr, 0x12, (uint16_t)(addr & 0xffff));
	aspeed_mdio_write(mdio, mdio->phy_addr, 0x13, (uint16_t)(data >> 16));
	aspeed_mdio_write(mdio, mdio->phy_addr, 0x14, (uint16_t)(data & 0xffff));
	aspeed_mdio_write(mdio, mdio->phy_addr, 0x1F, 0);
}

#define RGMII_RXDELAY_ALIGN         BIT(4)
#define RGMII_RXDELAY_FORCE_MODE    BIT(24)
#define RGMII_TXDELAY_FORCE_MODE    BIT(24)

void phy_an8801_config(struct phy_s *obj)
{
	struct mdio_s *mdio = obj->mdio;
	uint32_t reg;

	phy_reset(obj);
	phy_default_config(obj);

	reg = RGMII_TXDELAY_FORCE_MODE;
	if (obj->phy_mode == PHY_INTERFACE_MODE_RGMII_ID ||
	    obj->phy_mode == PHY_INTERFACE_MODE_RGMII_TXID)
		reg |= 0x4;
	air_buckpbus_reg_write(mdio, 0x1021C024, reg);

	reg = RGMII_RXDELAY_FORCE_MODE;
	if (obj->phy_mode == PHY_INTERFACE_MODE_RGMII_ID ||
	    obj->phy_mode == PHY_INTERFACE_MODE_RGMII_RXID)
		reg |= RGMII_RXDELAY_ALIGN;
	air_buckpbus_reg_write(mdio, 0x1021C02C, reg);

	if (obj->loopback == PHY_LOOPBACK_INT) {
		switch (obj->speed) {
		case 1000:
			aspeed_mdio_write(mdio, mdio->phy_addr, 0, 0x4140);
			break;
		case 100:
			aspeed_mdio_write(mdio, mdio->phy_addr, 0, 0x6100);
			break;
		case 10:
			aspeed_mdio_write(mdio, mdio->phy_addr, 0, 0x4100);
			break;
		}
	} else if (obj->loopback == PHY_LOOPBACK_EXT) {
		switch (obj->speed) {
		case 1000:
			aspeed_mdio_write(mdio, mdio->phy_addr, 0x9, 0x1a00);
			aspeed_mdio_write(mdio, mdio->phy_addr, 0x18, 0x1);
			aspeed_mdio_write(mdio, mdio->phy_addr, 0, 0x1200);
			break;
		case 100:
			aspeed_mdio_write(mdio, mdio->phy_addr, 0, 0x2100);
			break;
		case 10:
			aspeed_mdio_write(mdio, mdio->phy_addr, 0, 0x0100);
			break;
		}

		/* poll link status up to 5 seconds */
		aspeed_mdio_read(mdio, mdio->phy_addr, PHY_BMSR);
		{
			int timeout = 500;

			do {
				mdelay(10);
				reg = aspeed_mdio_read(mdio, mdio->phy_addr, PHY_BMSR);
			} while (!(reg & BMSR_LINK) && --timeout);
		}
	}
}

uint32_t read_ti(struct mdio_s *mdio, uint32_t offset)
{
	aspeed_mdio_write(mdio, mdio->phy_addr, 0xd, 0x1f);
	aspeed_mdio_write(mdio, mdio->phy_addr, 0xe, offset);
	aspeed_mdio_write(mdio, mdio->phy_addr, 0xd, 0x401f);
	return aspeed_mdio_read(mdio, mdio->phy_addr, 0xe);
}

void write_ti(struct mdio_s *mdio, uint32_t offset, uint16_t data)
{
	aspeed_mdio_write(mdio, mdio->phy_addr, 0xd, 0x1f);
	aspeed_mdio_write(mdio, mdio->phy_addr, 0xe, offset);
	aspeed_mdio_write(mdio, mdio->phy_addr, 0xd, 0x401f);
	aspeed_mdio_write(mdio, mdio->phy_addr, 0xe, data);
}

void phy_ti(struct phy_s *obj)
{
	struct mdio_s *mdio = obj->mdio;
	uint32_t reg;

	if (obj->speed == 1000)
		aspeed_mdio_write(mdio, mdio->phy_addr, 0, 0x0140);
	else if (obj->speed == 100)
		aspeed_mdio_write(mdio, mdio->phy_addr, 0, 0x2100);
	else
		aspeed_mdio_write(mdio, mdio->phy_addr, 0, 0x0100);

	reg = aspeed_mdio_read(mdio, mdio->phy_addr, 0x16) & ~GENMASK(5, 0);
	if (obj->loopback == PHY_LOOPBACK_INT) {
		/* Digital loopback */
		reg |= BIT(2);
	}
	aspeed_mdio_write(mdio, mdio->phy_addr, 0x16, reg);

	mdelay(500);
}

void phy_ti_dp83867(struct phy_s *obj)
{
	struct mdio_s *mdio = obj->mdio;
	uint32_t reg, reg_delay;

	// TX/RX internal delay
	reg = read_ti(mdio, 0x32);
	reg_delay = read_ti(mdio, 0x86) & ~GENMASK(7, 0);
	if (obj->phy_mode == PHY_INTERFACE_MODE_RGMII_ID ||
	    obj->phy_mode == PHY_INTERFACE_MODE_RGMII_RXID) {
		reg |= BIT(0);
		// 2ns
		reg_delay |= 0x7;
	} else {
		reg &= ~BIT(0);
		// 0ns
		reg_delay |= 0xF;
	}
	if (obj->phy_mode == PHY_INTERFACE_MODE_RGMII_ID ||
	    obj->phy_mode == PHY_INTERFACE_MODE_RGMII_TXID) {
		reg |= BIT(1);
		reg_delay |= (0x7 << 4);
	} else {
		reg &= ~BIT(1);
		reg_delay |= (0xF << 4);
	}
	write_ti(mdio, 0x32, reg);
	write_ti(mdio, 0x86, reg_delay);

	phy_ti(obj);
}

void phy_ti_dp83869(struct phy_s *obj)
{
	struct mdio_s *mdio = obj->mdio;
	uint32_t reg, reg_delay;

	// TX/RX internal delay
	reg = read_ti(mdio, 0x32);
	reg_delay = read_ti(mdio, 0x86) & ~GENMASK(7, 0);
	if (obj->phy_mode == PHY_INTERFACE_MODE_RGMII_ID ||
	    obj->phy_mode == PHY_INTERFACE_MODE_RGMII_RXID) {
		reg &= ~BIT(0);
		reg_delay |= 0x7;
	} else {
		reg |= BIT(0);
		reg_delay |= 0xF;
	}
	if (obj->phy_mode == PHY_INTERFACE_MODE_RGMII_ID ||
	    obj->phy_mode == PHY_INTERFACE_MODE_RGMII_TXID) {
		reg &= ~BIT(1);
		reg_delay |= (0x7 << 4);
	} else {
		reg |= BIT(1);
		reg_delay |= (0xF << 4);
	}
	write_ti(mdio, 0x32, reg);
	write_ti(mdio, 0x86, reg_delay);

	phy_ti(obj);
}

void recov_phy_ti(struct phy_s *obj)
{
	struct mdio_s *mdio = obj->mdio;
	uint32_t reg;

	reg = aspeed_mdio_read(mdio, mdio->phy_addr, 0x16) & ~GENMASK(5, 0);
	aspeed_mdio_write(mdio, mdio->phy_addr, 0x16, reg);
}
