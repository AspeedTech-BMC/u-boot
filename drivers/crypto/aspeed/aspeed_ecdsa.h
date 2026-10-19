/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright 2026 ASPEED Technology Inc.
 */

#ifndef __ASPEED_ECDSA_H
#define __ASPEED_ECDSA_H

#include <clk.h>
#include <reset.h>
#include <asm/types.h>

/* ECDSA general definition */
#define ASPEED_ECDSA_TIMEOUT (100000)
#define TO_BYTES(_bits) ((_bits) >> 3)
#define ASPEED_ECDSA_MAX_DATA_SIZE (48)

/* ECDSA register definition */
#define ASPEED_ECC_STS_REG (0x0B0)
#define ASPEED_ECC_CTRL_REG (0x0B4)
#define ASPEED_ECC_CMD_REG (0x0Bc)
#define ASPEED_ECC_GX_REG (0x800)
#define ASPEED_ECC_GY_REG (0x840)
#define ASPEED_ECC_QX_REG (0x880)
#define ASPEED_ECC_QY_REG (0x8C0)
#define ASPEED_ECC_P_REG (0x900)
#define ASPEED_ECC_A_REG (0x940)
#define ASPEED_ECC_N_REG (0x980)
#define ASPEED_ECC_SIGN_R_REG (0x9C0)
#define ASPEED_ECC_SIGN_S_REG (0xA00)
#define ASPEED_ECC_MESSAGE_REG (0xA40)
#define ASPEED_ECC_ECDSA_VERIFY (0xBC0)

/* ECDSA status register */
#define ECC_IDLE BIT(0)
#define ECC_VERIFY_PASS BIT(1)

/* ECDSA control register */
#define ECC_EN BIT(0)
#define ECDSA_256_EN BIT(1)

/* ECDSA command register */
#define ECC_ENGINE_START BIT(0)

/* ECDSA verify register */
#define ECC_VERIFY_MODE BIT(0)

struct aspeed_ecdsa {
	void __iomem *base;
	struct clk clk;
	struct reset_ctl rst;
};

struct ecc_curve_param {
	const uint8_t nbytes;
	const uint8_t *Gx;
	const uint8_t *Gy;
	const uint8_t *P;
	const uint8_t *A;
	const uint8_t *N;
};

#endif //__ASPEED_ECDSA_H
