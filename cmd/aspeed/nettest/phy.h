/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) ASPEED Technology Inc.
 */

#ifndef PHY_H
#define PHY_H

#define PHY_IS_VALID(dat) (((dat & 0xffff) != 0xffff))

#define PHY_REG_BMCR 0x00 // Basic Mode Control Register
#define PHY_REG_BMSR 0x01 // Basic Mode Status Register
#define PHY_REG_ID_1 0x02
#define PHY_REG_ID_2 0x03
#define PHY_ANER 0x06 // Auto-negotiation Expansion Register
#define PHY_GBCR 0x09 // 1000Base-T Control Register
#define PHY_SR 0x11 // PHY Specific Status Register
#define PHY_INER 0x12 // Interrupt Enable Register

#define PHYID3_Mask 0xfc00 //0xffc0

/* Note for SettingPHY chip
 *
 * void phy_xxxx (int loop_phy) {
 *
 *	if ( BurstEnable ) {
 *		// IEEE test
 *	}
 *	else if (loop_phy) {
 *		// Internal loop back
 *	}
 *	else {
 *		// external loop back
 *	}
 * }
 */

#endif // PHY_H
