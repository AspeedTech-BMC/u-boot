/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) Aspeed Technology Inc.
 */
#ifndef _OTP_AST2700_H
#define _OTP_AST2700_H

enum otp_ioctl_cmds {
	GET_ECC_STATUS = 1,
	SET_ECC_ENABLE,
	SET_ECC_DISABLE,
	GET_ECC_POLICY,
	SET_ECC_POLICY,
	GET_OTP_ECC_CODE,	/* read word raw (non-ECC) and return data + OTP_DBG01[5:0] */
};

enum otp_ecc_codes {
	OTP_ECC_MISMATCH = -1,
	OTP_ECC_DISABLE = 0,
	OTP_ECC_ENABLE = 1,
};

/*
 * GET_ECC_STATUS result (replaces a plain enable/disable): distinguishes why
 * ECC is on, since a hardware-latched strap can never be turned back off for
 * the life of the session while a software-temporary enable still can.
 */
enum otp_ecc_global_status {
	OTP_ECC_GLOBAL_DISABLED = 0,
	OTP_ECC_GLOBAL_SW_ENABLED,
	OTP_ECC_GLOBAL_HW_ENABLED,
};

/*
 * Per-region ECC policy id, shared between the driver's ECC policy table
 * and the "otp ecc policy" command. Distinct from cmd/aspeed/otp_ast2700.c's
 * own enum otp_region (used for image programming) to avoid name clashes.
 */
enum otp_region_id {
	OTP_REGION_ID_ROM = 0,
	OTP_REGION_ID_RBP,
	OTP_REGION_ID_CFG,
	OTP_REGION_ID_STRAP,
	OTP_REGION_ID_STRAPEXT,
	OTP_REGION_ID_USR,
	OTP_REGION_ID_SEC,
	OTP_REGION_ID_CAL,
	OTP_REGION_ID_PUF,
	OTP_REGION_ID_MAX,
};

struct otp_ecc_policy {
	u32 region;		/* enum otp_region_id, set by caller */
	u32 ecc_en;		/* 0: disabled, 1: enabled */
	u32 ecc_supported;	/* GET: filled by driver, SET: ignored */
};

/* Used with GET_OTP_ECC_CODE */
struct otp_read_ecc {
	u32 offset;		/* input:  OTP word address */
	u32 data;		/* output: 16-bit read data */
	u32 ecc_code;		/* output: OTP_DBG01[5:0] after read */
};

#endif /* _OTP_AST2700_H */
