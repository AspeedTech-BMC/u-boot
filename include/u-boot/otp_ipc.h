/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2025 ASPEED Technology Inc.
 */

#ifndef _OTP_IPC_H
#define _OTP_IPC_H

#include <linux/types.h>

/*
 * Standalone OTP-over-IPC channel: lets CA35 read and program bootmcu OTP,
 * independent of the Caliptra IPC channel/command set (see cptra_ipc.h).
 *
 * No shared memory is used for the payload - request and response both fit
 * in the mailbox's own 8x32-bit word message, with OTP's 16-bit words
 * packed two per 32-bit slot. All ops share one channel; word1 packs a
 * per-call sequence number into its top 8 bits, the op into the next 8
 * bits, and a small op-specific value into its lower 16 bits (see
 * OTP_IPC_PACK_OP_COUNT()). Must stay in lockstep with the Zephyr-side
 * protocol in zephyr/include/zephyr/drivers/misc/aspeed/otp_ipc.h.
 *
 * The response's word0 echoes that same sequence number back in its top
 * 8 bits alongside the status in its lower 16 bits (see
 * OTP_IPC_PACK_RESP()). otp_ipc_call() uses this to tell a late reply
 * from a call it already gave up on apart from the answer to whatever it
 * asks next, instead of risking silently accepting someone else's data.
 *
 * Read/program: word1's lower 16 bits are the OTP word count.
 * ECC_GET_STATUS:  word1's lower 16 bits unused. Response word1 = global ECC
 *                  status (enum otp_ecc_global_status): 0=disabled, 1=on
 *                  because software temporarily enabled it, 2=on because the
 *                  hardware strap forces it (can't be turned off this
 *                  session).
 * ECC_SET_ENABLE:  word1's lower 16 bits = enable (0/1).
 * ECC_GET_POLICY:  word1's lower 16 bits = region (enum otp_region_id).
 *                  Response word1 = ecc_en, word2 = ecc_supported.
 * ECC_SET_POLICY:  word1's lower 16 bits = region, word2 = ecc_en (0/1).
 * GET_ROM_PATCH_VERSION: word1's lower 16 bits unused. Response word1 = raw
 *                  SCU1 ROM patch version register value, undecoded (A1 vs
 *                  A2 decode differently - the caller decodes it).
 * READ_ECC_CODE:   word0 = addr, word1's lower 16 bits unused. Response
 *                  word1 = raw 16-bit data, word2 = OTP_DBG01[5:0] (ECC
 *                  code) captured by the same read. ECC decode is forced
 *                  off for this read regardless of addr's region's own ECC
 *                  policy.
 * GET_REGION_PROTECT: word1's lower 16 bits = group (enum
 *                  otp_region_protect_group). struct otp_region_protect
 *                  has 33 u32 fields - too many for one 8-word message -
 *                  so this fetches at most 4 raw OTPMISCREG values per
 *                  call, response word1..word4, in the field order given
 *                  in each group's comment below. The caller picks which
 *                  group(s) it needs.
 */

#define MBOX_OTP_DEVICE_NAME		"mbox-bootmcu-2"

enum otp_ipc_op {
	OTP_IPC_OP_READ = 0,
	OTP_IPC_OP_PROGRAM,
	OTP_IPC_OP_ECC_GET_STATUS,
	OTP_IPC_OP_ECC_SET_ENABLE,
	OTP_IPC_OP_ECC_GET_POLICY,
	OTP_IPC_OP_ECC_SET_POLICY,
	/*
	 * 6 and 7 are GET_REVID/SET_READ_MODE in the Zephyr-side enum
	 * (zephyr/include/zephyr/drivers/misc/aspeed/otp_ipc.h) - u-boot's
	 * client has no need for either, but this op's wire value must still
	 * match Zephyr's, hence the explicit 8 rather than falling through.
	 */
	OTP_IPC_OP_GET_ROM_PATCH_VERSION = 8,
	OTP_IPC_OP_READ_ECC_CODE,
	OTP_IPC_OP_GET_REGION_PROTECT,
};

/*
 * Groups for OTP_IPC_OP_GET_REGION_PROTECT, each at most 4 raw OTPMISCREG
 * values (one 8-word message can't carry all 33 of struct
 * otp_region_protect's fields at once - see GET_REGION_PROTECT above).
 * Field order within each group matches struct otp_region_protect's
 * declaration order.
 */
enum otp_region_protect_group {
	OTP_PROT_GROUP_FIXED = 0,	/* rom_patch, otpcfg, otpstrap, strap_ext */
	OTP_PROT_GROUP_RBP,		/* rbp_soc_svn, rbp_soc_keyretire, rbp_calip_svn, rbp_calip_keyretire */
	OTP_PROT_GROUP_SECURE_VAL,	/* secure0, secure1, secure2, secure3 */
	OTP_PROT_GROUP_SECURE_RANGE,	/* secure0_range, secure1_range, secure2_range, secure3_range */
	OTP_PROT_GROUP_USR_VAL,		/* usr0, usr1, usr2, usr3 */
	OTP_PROT_GROUP_USR_RANGE,	/* usr0_range, usr1_range, usr2_range, usr3_range */
	OTP_PROT_GROUP_CALIPTRA_VAL,	/* caliptra0, caliptra1, caliptra2, caliptra3 */
	OTP_PROT_GROUP_CALIPTRA_RANGE,	/* caliptra0_range, caliptra1_range, caliptra2_range, caliptra3_range */
	OTP_PROT_GROUP_PUF,		/* puf (word1 only) */
};

#define OTP_IPC_SEQ_SHIFT		24
#define OTP_IPC_SEQ_MASK		0xFF
#define OTP_IPC_OP_SHIFT		16
#define OTP_IPC_OP_MASK			0xFF
#define OTP_IPC_COUNT_MASK		0xFFFF

#define OTP_IPC_PACK_OP_COUNT(op, count) \
	((((u32)(op) & OTP_IPC_OP_MASK) << OTP_IPC_OP_SHIFT) | ((u32)(count) & OTP_IPC_COUNT_MASK))

/* Response word0: seq echoed back in the top 8 bits, status (sign-extended,
 * every status value here fits in a 16-bit signed range) in the lower
 * 16 bits.
 */
#define OTP_IPC_RESP_SEQ(word)		(((word) >> OTP_IPC_SEQ_SHIFT) & OTP_IPC_SEQ_MASK)
#define OTP_IPC_RESP_STATUS(word)	((s16)((word) & 0xFFFF))

#define OTP_IPC_READ_MAX_WORDS		14	/* 7 msg words x 2 */
#define OTP_IPC_PROGRAM_MAX_WORDS	12	/* 6 msg words x 2 */

int otp_ipc_read(u32 addr, u32 count, u16 *out);
int otp_ipc_program(u32 addr, u32 count, const u16 *data);
int otp_ipc_ecc_get_status(int *status);
int otp_ipc_ecc_set_enable(int enable);
int otp_ipc_ecc_get_policy(u32 region, u32 *ecc_en, u32 *ecc_supported);
int otp_ipc_ecc_set_policy(u32 region, u32 ecc_en);
int otp_ipc_get_rom_patch_version(u32 *version);
int otp_ipc_read_ecc_code(u32 addr, u32 *data, u32 *ecc_code);
int otp_ipc_get_region_protect(u32 group, u32 *out, u32 count);

#endif /* _OTP_IPC_H */
