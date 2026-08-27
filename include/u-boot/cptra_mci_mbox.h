/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2026 ASPEED Technology Inc.
 *
 * Caliptra-SS MCI (Manageability Controller Interface) mailbox: request/
 * response layout. Covers MC_FIRMWARE_VERSION, MC_SHA_*,
 * MC_ECDSA384_SIG_VERIFY, MC_LMS_SIG_VERIFY, MC_MLDSA87_SIG_VERIFY, and the
 * Cmk-managed MC_IMPORT / CM_ECDSA_CMK_* / CM_MLDSA_CMK_* subset; extend as
 * more commands are wired up.
 */

#ifndef _CPTRA_MCI_MBOX_H
#define _CPTRA_MCI_MBOX_H

#include <linux/types.h>

/* mcu_mbox0_csr register offsets, valid once the CSR page is selected */
#define CPTRA_MCI_MBOX_LOCK			0x00
#define CPTRA_MCI_MBOX_USER			0x04
#define CPTRA_MCI_MBOX_TARGET_USER		0x08
#define CPTRA_MCI_MBOX_TARGET_USER_VALID	0x0c
#define CPTRA_MCI_MBOX_CMD			0x10
#define CPTRA_MCI_MBOX_DLEN			0x14
#define CPTRA_MCI_MBOX_EXECUTE			0x18
#define CPTRA_MCI_MBOX_TARGET_STATUS		0x1c
#define CPTRA_MCI_MBOX_CMD_STATUS		0x20
#define   CPTRA_MCI_MBOX_CMD_STATUS_PS		GENMASK(3, 0)
#define CPTRA_MCI_MBOX_HW_STATUS		0x24

/* Actual backing size of the mailbox data SRAM */
#define CPTRA_MCI_MBOX_SRAM_SIZE		0x4000

/* MAX_CMB_DATA_SIZE: max variable-length payload per single mailbox call */
#define CPTRA_MCI_MBOX_MAX_INPUT_SIZE		4096

/* Max time to wait for the mailbox LOCK to become available */
#define CPTRA_MCI_MBOX_LOCK_TIMEOUT_MS		1000

/* Max time to wait for CMD_STATUS to leave CPTRA_MCI_MBSTS_CMD_BUSY */
#define CPTRA_MCI_MBOX_CMD_TIMEOUT_MS		1000

/* mbox_status_e (mcu_mbox0_csr.mbox_cmd_status) */
enum cptra_mci_mbox_sts {
	CPTRA_MCI_MBSTS_CMD_BUSY = 0,
	CPTRA_MCI_MBSTS_DATA_READY,
	CPTRA_MCI_MBSTS_CMD_COMPLETE,
	CPTRA_MCI_MBSTS_CMD_FAILURE,
};

/* CommandId ("common/mcu-mbox/src/messages.rs", caliptra-mcu-sw) */
enum cptra_mci_mbox_cmd {
	CPTRA_MCI_MBCMD_FIRMWARE_VERSION	= 0x4D465756, /* "MFWV" */
	CPTRA_MCI_MBCMD_SHA_INIT		= 0x4D435349, /* "MCSI" */
	CPTRA_MCI_MBCMD_SHA_UPDATE		= 0x4D435355, /* "MCSU" */
	CPTRA_MCI_MBCMD_SHA_FINAL		= 0x4D435346, /* "MCSF" */
	CPTRA_MCI_MBCMD_ECDSA384_SIG_VERIFY	= 0x4D454356, /* "MECV" */
	CPTRA_MCI_MBCMD_LMS_SIG_VERIFY		= 0x4D4C4D56, /* "MLMV" */
	CPTRA_MCI_MBCMD_MLDSA87_SIG_VERIFY	= 0x4D4D5356, /* "MMSV" */
	CPTRA_MCI_MBCMD_IMPORT			= 0x4D43494D, /* "MCIM" */
	CPTRA_MCI_MBCMD_ECDSA_CMK_PUBLIC_KEY	= 0x4D434550, /* "MCEP" */
	CPTRA_MCI_MBCMD_ECDSA_CMK_SIGN		= 0x4D434553, /* "MCES" */
	CPTRA_MCI_MBCMD_ECDSA_CMK_VERIFY	= 0x4D434556, /* "MCEV" */
	CPTRA_MCI_MBCMD_MLDSA_CMK_PUBLIC_KEY	= 0x4D4D4C50, /* "MMLP" */
	CPTRA_MCI_MBCMD_MLDSA_CMK_SIGN		= 0x4D4D4C53, /* "MMLS" */
	CPTRA_MCI_MBCMD_MLDSA_CMK_VERIFY	= 0x4D4D4C56, /* "MMLV" */
};

/* Common request/response headers (messages.rs MailboxReqHeader/MailboxRespHeaderVarSize) */
struct cptra_mci_mbox_req_hdr {
	u32 chksum;
};

struct cptra_mci_mbox_resp_hdr {
	u32 chksum;
	u32 fips_status;
};

struct cptra_mci_mbox_resp_hdr_var {
	struct cptra_mci_mbox_resp_hdr hdr;
	u32 data_len;
};

/* FwIndex (messages.rs) */
enum cptra_mci_fw_index {
	CPTRA_MCI_FW_INDEX_CALIPTRA_CORE = 0,
	CPTRA_MCI_FW_INDEX_MCU_RUNTIME,
	CPTRA_MCI_FW_INDEX_SOC,
};

#define CPTRA_MCI_MAX_FW_VERSION_STR_LEN	32

struct cptra_mci_fw_version_req {
	struct cptra_mci_mbox_req_hdr hdr;
	u32 index;
};

struct cptra_mci_fw_version_resp {
	struct cptra_mci_mbox_resp_hdr_var hdr;
	u8 version[CPTRA_MCI_MAX_FW_VERSION_STR_LEN];
};

#define CPTRA_MCI_ECC384_SCALAR_SIZE		48	/* ECC384_SCALAR_BYTE_SIZE */

#define CPTRA_MCI_SHA_CONTEXT_SIZE		200	/* CMB_SHA_CONTEXT_SIZE */
#define CPTRA_MCI_SHA384_DIGEST_SIZE		48
#define CPTRA_MCI_SHA512_DIGEST_SIZE		64

/* CmHashAlgorithm (messages.rs / mailbox.rs) */
enum cptra_mci_sha_algo {
	CPTRA_MCI_SHA_ALGO_RESERVED = 0,
	CPTRA_MCI_SHA_ALGO_SHA384 = 1,
	CPTRA_MCI_SHA_ALGO_SHA512 = 2,
};

/*
 * MC_SHA_INIT / MC_SHA_UPDATE / MC_SHA_FINAL (CmShaInitReq/CmShaUpdateReq/CmShaFinalReq).
 * Only the fixed-size header is modeled here; the (up to CPTRA_MCI_MBOX_MAX_INPUT_SIZE
 * byte) input payload is supplied separately to cptra_mci_mbox_execute_sg().
 */
struct cptra_mci_sha_init_hdr {
	struct cptra_mci_mbox_req_hdr hdr;
	u32 hash_algorithm;
	u32 input_size;
};

/* MC_SHA_UPDATE and MC_SHA_FINAL requests share this header shape */
struct cptra_mci_sha_data_hdr {
	struct cptra_mci_mbox_req_hdr hdr;
	u8 context[CPTRA_MCI_SHA_CONTEXT_SIZE];
	u32 input_size;
};

/* MC_SHA_INIT and MC_SHA_UPDATE responses share this shape */
struct cptra_mci_sha_ctx_resp {
	struct cptra_mci_mbox_resp_hdr hdr;
	u8 context[CPTRA_MCI_SHA_CONTEXT_SIZE];
};

struct cptra_mci_sha_final_resp {
	struct cptra_mci_mbox_resp_hdr_var hdr;
	u8 hash[CPTRA_MCI_SHA512_DIGEST_SIZE];
};

/*
 * MC_ECDSA384_SIG_VERIFY (EcdsaVerifyReq/Resp, pure passthrough to
 * Caliptra's ECDSA384_SIGNATURE_VERIFY). The public key is raw bytes
 * (qx/qy), not an opaque Cmk, and the caller must SHA-384-hash the message
 * itself -- this command takes the digest, not the raw message. No explicit
 * pass/fail field; a mismatch surfaces as CMD_FAILURE (-EIO).
 */
struct cptra_mci_ecdsa384_sig_verify_req {
	struct cptra_mci_mbox_req_hdr hdr;
	u8 pub_key_x[CPTRA_MCI_ECC384_SCALAR_SIZE];
	u8 pub_key_y[CPTRA_MCI_ECC384_SCALAR_SIZE];
	u8 signature_r[CPTRA_MCI_ECC384_SCALAR_SIZE];
	u8 signature_s[CPTRA_MCI_ECC384_SCALAR_SIZE];
	u8 hash[CPTRA_MCI_ECC384_SCALAR_SIZE];
};

struct cptra_mci_ecdsa384_sig_verify_resp {
	struct cptra_mci_mbox_resp_hdr hdr;
};

#define CPTRA_MCI_LMS_PUBKEY_ID_SIZE		16	/* LMS "I" identifier */
#define CPTRA_MCI_LMS_PUBKEY_DIGEST_SIZE	24	/* N=6 words, LmsSha256N24H15 */
#define CPTRA_MCI_LMS_OTS_SIGNATURE_SIZE	1252	/* fixed param set, see below */
#define CPTRA_MCI_LMS_TREE_PATH_SIZE		360	/* H=15 levels * 24-byte digest */
#define CPTRA_MCI_LMS_HASH_SIZE			48	/* SHA-384 digest of the signed message */

/*
 * MC_LMS_SIG_VERIFY (LmsVerifyReq/Resp, pure passthrough to Caliptra's
 * LMS_SIGNATURE_VERIFY). LMS keys are generated offline, never on-device.
 * Caller must SHA-384-hash the message itself; this command takes the
 * digest, not the raw message. Caliptra's runtime hard-codes and rejects
 * anything except tree_type=12 (LmsSha256N24H15) and ots_type=7 -- the
 * fixed-size arrays above are sized for exactly that one parameter set. No
 * explicit pass/fail field; a mismatch surfaces as CMD_FAILURE (-EIO).
 */
#define CPTRA_MCI_LMS_TREE_TYPE_FIXED		12
#define CPTRA_MCI_LMS_OTS_TYPE_FIXED		7

struct cptra_mci_lms_verify_req {
	struct cptra_mci_mbox_req_hdr hdr;
	u32 pub_key_tree_type;
	u32 pub_key_ots_type;
	u8 pub_key_id[CPTRA_MCI_LMS_PUBKEY_ID_SIZE];
	u8 pub_key_digest[CPTRA_MCI_LMS_PUBKEY_DIGEST_SIZE];
	u32 signature_q;
	u8 signature_ots[CPTRA_MCI_LMS_OTS_SIGNATURE_SIZE];
	u32 signature_tree_type;
	u8 signature_tree_path[CPTRA_MCI_LMS_TREE_PATH_SIZE];
	u8 hash[CPTRA_MCI_LMS_HASH_SIZE];
};

struct cptra_mci_lms_verify_resp {
	struct cptra_mci_mbox_resp_hdr hdr;
};

#define CPTRA_MCI_MLDSA87_PUBKEY_SIZE		2592	/* MLDSA87_PUB_KEY_BYTE_SIZE */
#define CPTRA_MCI_MLDSA87_SIGNATURE_SIZE	4628	/* MLDSA87_SIGNATURE_BYTE_SIZE */

/*
 * MC_MLDSA87_SIG_VERIFY (MldsaVerifyReq/Resp, pure passthrough to
 * Caliptra's MLDSA87_SIGNATURE_VERIFY). The public key is raw bytes rather
 * than an opaque Cmk. Caliptra hashes the message internally, so this
 * command takes the raw message (up to CPTRA_MCI_MBOX_MAX_INPUT_SIZE), not
 * a pre-hashed digest -- the message payload is supplied separately to
 * cptra_mci_mbox_execute_sg(). No explicit pass/fail field; a mismatch
 * surfaces as CMD_FAILURE (-EIO).
 */
struct cptra_mci_mldsa87_sig_verify_hdr {
	struct cptra_mci_mbox_req_hdr hdr;
	u8 pub_key[CPTRA_MCI_MLDSA87_PUBKEY_SIZE];
	u8 signature[CPTRA_MCI_MLDSA87_SIGNATURE_SIZE];
	u32 message_size;
};

struct cptra_mci_mldsa87_sig_verify_resp {
	struct cptra_mci_mbox_resp_hdr hdr;
};

#define CPTRA_MCI_CMK_SIZE			128	/* CMK_SIZE_BYTES */

/* Cmk: an opaque, encrypted key blob. The raw key material never leaves Caliptra-SS. */
struct cptra_mci_cmk {
	u8 value[CPTRA_MCI_CMK_SIZE];
};

/* CmKeyUsage (mailbox.rs) */
enum cptra_mci_key_usage {
	CPTRA_MCI_KEY_USAGE_RESERVED = 0,
	CPTRA_MCI_KEY_USAGE_HMAC = 1,
	CPTRA_MCI_KEY_USAGE_AES = 2,
	CPTRA_MCI_KEY_USAGE_ECDSA = 3,
	CPTRA_MCI_KEY_USAGE_MLDSA = 4,
	CPTRA_MCI_KEY_USAGE_MLKEM = 5,
};

/*
 * MC_IMPORT (CmImportReq/Resp). Imports raw key material under key_usage and
 * returns an opaque Cmk handle for it; the raw material never round-trips
 * back out. Only the fixed-size header is modeled; the input key payload is
 * supplied separately to cptra_mci_mbox_execute_sg().
 */
struct cptra_mci_import_hdr {
	struct cptra_mci_mbox_req_hdr hdr;
	u32 key_usage;
	u32 input_size;
};

struct cptra_mci_import_resp {
	struct cptra_mci_mbox_resp_hdr hdr;
	struct cptra_mci_cmk cmk;
};

/* MC_ECDSA_CMK_PUBLIC_KEY (CmEcdsaPublicKeyReq/Resp) */
struct cptra_mci_ecdsa_pubkey_req {
	struct cptra_mci_mbox_req_hdr hdr;
	struct cptra_mci_cmk cmk;
};

struct cptra_mci_ecdsa_pubkey_resp {
	struct cptra_mci_mbox_resp_hdr hdr;
	u8 qx[CPTRA_MCI_ECC384_SCALAR_SIZE];
	u8 qy[CPTRA_MCI_ECC384_SCALAR_SIZE];
};

/*
 * MC_ECDSA_CMK_SIGN (CmEcdsaSignReq/Resp). Only the fixed-size header is
 * modeled; the message payload is supplied separately to
 * cptra_mci_mbox_execute_sg(). Caliptra-SS SHA-384-hashes the message
 * internally before signing -- unlike MC_ECDSA384_SIG_VERIFY, this takes
 * the raw message, not a pre-hashed digest.
 */
struct cptra_mci_ecdsa_sign_hdr {
	struct cptra_mci_mbox_req_hdr hdr;
	struct cptra_mci_cmk cmk;
	u32 message_size;
};

struct cptra_mci_ecdsa_sign_resp {
	struct cptra_mci_mbox_resp_hdr hdr;
	u8 r[CPTRA_MCI_ECC384_SCALAR_SIZE];
	u8 s[CPTRA_MCI_ECC384_SCALAR_SIZE];
};

/*
 * MC_ECDSA_CMK_VERIFY (CmEcdsaVerifyReq/Resp). No explicit pass/fail field
 * -- a signature mismatch surfaces as CMD_FAILURE (-EIO).
 */
struct cptra_mci_ecdsa_verify_hdr {
	struct cptra_mci_mbox_req_hdr hdr;
	struct cptra_mci_cmk cmk;
	u8 r[CPTRA_MCI_ECC384_SCALAR_SIZE];
	u8 s[CPTRA_MCI_ECC384_SCALAR_SIZE];
	u32 message_size;
};

struct cptra_mci_ecdsa_verify_resp {
	struct cptra_mci_mbox_resp_hdr hdr;
};

/* MC_MLDSA_CMK_PUBLIC_KEY (CmMldsaPublicKeyReq/Resp) */
struct cptra_mci_mldsa_pubkey_req {
	struct cptra_mci_mbox_req_hdr hdr;
	struct cptra_mci_cmk cmk;
};

struct cptra_mci_mldsa_pubkey_resp {
	struct cptra_mci_mbox_resp_hdr hdr;
	u8 public_key[CPTRA_MCI_MLDSA87_PUBKEY_SIZE];
};

/*
 * MC_MLDSA_CMK_SIGN (CmMldsaSignReq/Resp). Only the fixed-size header is
 * modeled; the message payload is supplied separately to
 * cptra_mci_mbox_execute_sg().
 */
struct cptra_mci_mldsa_sign_hdr {
	struct cptra_mci_mbox_req_hdr hdr;
	struct cptra_mci_cmk cmk;
	u32 message_size;
};

struct cptra_mci_mldsa_sign_resp {
	struct cptra_mci_mbox_resp_hdr hdr;
	u8 signature[CPTRA_MCI_MLDSA87_SIGNATURE_SIZE];
};

/*
 * MC_MLDSA_CMK_VERIFY (CmMldsaVerifyReq/Resp). The signature is large
 * enough that it is itself part of this fixed-size header rather than data
 * handed to execute_sg() -- only the message is supplied separately. No
 * explicit pass/fail field -- a mismatch surfaces as CMD_FAILURE (-EIO).
 */
struct cptra_mci_mldsa_verify_hdr {
	struct cptra_mci_mbox_req_hdr hdr;
	struct cptra_mci_cmk cmk;
	u8 signature[CPTRA_MCI_MLDSA87_SIGNATURE_SIZE];
	u32 message_size;
};

struct cptra_mci_mldsa_verify_resp {
	struct cptra_mci_mbox_resp_hdr hdr;
};

/*
 * Direct (non-mailbox-protocol) MCI subsystem status registers. These are
 * plain memory-mapped registers reachable through the same paged SCU1
 * window as the mailbox CSR/SRAM (see SCU1_CPTRA_SS_AXI_WIN in
 * aspeed_cptra_mci_mbox.c), each block on its own 64KB-aligned page --
 * confirmed on real hardware via the same address>>16 page encoding as
 * CPTRA_MCI_MBOX_CSR_PAGE/SRAM_PAGE. Unlike the mailbox, there is no
 * command/lock/execute protocol here: cptra_mci_reg_read() just selects the
 * page and does a plain register read.
 */

/* mci_reg block, absolute base 0x21000000 */
#define CPTRA_MCI_REG_PAGE			0x2100	/* 0x21000000 >> 16 */

#define CPTRA_MCI_REG_MCU_IFU_AXI_USER			0x0020
#define CPTRA_MCI_REG_MCU_LSU_AXI_USER			0x0024
#define CPTRA_MCI_REG_MCU_SRAM_CONFIG_AXI_USER		0x0028
#define CPTRA_MCI_REG_MCI_SOC_CONFIG_AXI_USER		0x002c
#define CPTRA_MCI_REG_RESET_REASON			0x0038
#define   CPTRA_MCI_REG_RESET_REASON_FW_HITLESS_UPD_RESET	BIT(0)
#define   CPTRA_MCI_REG_RESET_REASON_FW_BOOT_UPD_RESET		BIT(1)
#define   CPTRA_MCI_REG_RESET_REASON_WARM_RESET		BIT(2)
#define CPTRA_MCI_REG_SECURITY_STATE			0x0040
#define   CPTRA_MCI_REG_SECURITY_STATE_DEVICE_LIFECYCLE	GENMASK(1, 0)
#define   CPTRA_MCI_REG_SECURITY_STATE_DEBUG_LOCKED		BIT(2)
#define   CPTRA_MCI_REG_SECURITY_STATE_SCAN_MODE		BIT(3)

/* device_lifecycle_e */
enum cptra_mci_device_lifecycle {
	CPTRA_MCI_DEVICE_UNPROVISIONED = 0,
	CPTRA_MCI_DEVICE_MANUFACTURING = 1,
	CPTRA_MCI_DEVICE_PRODUCTION = 3,
};

/* Each MBOXn_*_AXI_USER block is CPTRA_MCI_REG_MBOX_AXI_USER_COUNT 32-bit regs */
#define CPTRA_MCI_REG_MBOX_AXI_USER_COUNT		5
#define CPTRA_MCI_REG_MBOX0_VALID_AXI_USER(n)		(0x0180 + 4 * (n))
#define CPTRA_MCI_REG_MBOX0_AXI_USER_LOCK(n)		(0x01a0 + 4 * (n))
#define CPTRA_MCI_REG_MBOX1_VALID_AXI_USER(n)		(0x01c0 + 4 * (n))
#define CPTRA_MCI_REG_MBOX1_AXI_USER_LOCK(n)		(0x01e0 + 4 * (n))

#define CPTRA_MCI_REG_SS_DEBUG_INTENT			0x0418
#define CPTRA_MCI_REG_SS_CONFIG_DONE_STICKY		0x0440
#define CPTRA_MCI_REG_SS_CONFIG_DONE			0x0444

/*
 * soc_ifc_reg block, absolute base 0xa0030000 (page base and block base are
 * the same address here). Bit fields for CPTRA_RESET_REASON/
 * CPTRA_SECURITY_STATE cross-checked against
 * caliptra-mcu-sw/registers/generated-firmware/src/soc.rs (CptraResetReason/
 * CptraSecurityState) -- note CPTRA_RESET_REASON only has 2 bits (no
 * hitless/boot split), unlike mci_reg's own 3-bit RESET_REASON.
 */
#define CPTRA_MCI_SOC_IFC_PAGE				0xa003	/* 0xa0030000 >> 16 */

#define CPTRA_MCI_SOC_IFC_CPTRA_RESET_REASON		0x0040
#define   CPTRA_MCI_SOC_IFC_CPTRA_RESET_REASON_FW_UPD_RESET	BIT(0)
#define   CPTRA_MCI_SOC_IFC_CPTRA_RESET_REASON_WARM_RESET	BIT(1)
#define CPTRA_MCI_SOC_IFC_CPTRA_SECURITY_STATE		0x0044
#define   CPTRA_MCI_SOC_IFC_CPTRA_SECURITY_STATE_DEVICE_LIFECYCLE	GENMASK(1, 0)
#define   CPTRA_MCI_SOC_IFC_CPTRA_SECURITY_STATE_DEBUG_LOCKED		BIT(2)
#define   CPTRA_MCI_SOC_IFC_CPTRA_SECURITY_STATE_SCAN_MODE		BIT(3)
#define CPTRA_MCI_SOC_IFC_MBOX_AXI_USER_COUNT		5
#define CPTRA_MCI_SOC_IFC_CPTRA_MBOX_VALID_AXI_USER(n)	(0x0048 + 4 * (n))
#define CPTRA_MCI_SOC_IFC_CPTRA_MBOX_AXI_USER_LOCK(n)	(0x005c + 4 * (n))
#define CPTRA_MCI_SOC_IFC_CPTRA_TRNG_VALID_AXI_USER	0x0070
#define CPTRA_MCI_SOC_IFC_CPTRA_TRNG_AXI_USER_LOCK	0x0074
#define CPTRA_MCI_SOC_IFC_CPTRA_FUSE_VALID_AXI_USER	0x0108

/* cptra_mci_mbox.c */
u32 cptra_mci_mbox_checksum(u32 cmd, const void *data, u32 len);
u32 cptra_mci_mbox_checksum_ext(u32 checksum, const void *data, u32 len);
int cptra_mci_mbox_execute(u32 cmd, const void *req, u32 req_len,
			   void *resp, u32 resp_buf_len, u32 *resp_len);
int cptra_mci_mbox_execute_sg(u32 cmd, const void *hdr, u32 hdr_len,
			      const void *data, u32 data_len,
			      void *resp, u32 resp_buf_len, u32 *resp_len);
int cptra_mci_get_firmware_version(enum cptra_mci_fw_index index, char *version, size_t len);
int cptra_mci_reg_read(u32 page, u32 offset, u32 *value);

/* cptra_mci_lms.c */
int cptra_mci_lms_verify(u32 pub_key_tree_type, u32 pub_key_ots_type,
			 const u8 pub_key_id[CPTRA_MCI_LMS_PUBKEY_ID_SIZE],
			 const u8 pub_key_digest[CPTRA_MCI_LMS_PUBKEY_DIGEST_SIZE],
			 u32 signature_q,
			 const u8 signature_ots[CPTRA_MCI_LMS_OTS_SIGNATURE_SIZE],
			 u32 signature_tree_type,
			 const u8 signature_tree_path[CPTRA_MCI_LMS_TREE_PATH_SIZE],
			 const u8 hash[CPTRA_MCI_LMS_HASH_SIZE]);

/* cptra_mci_mldsa.c */
int cptra_mci_mldsa87_verify(const u8 pub_key[CPTRA_MCI_MLDSA87_PUBKEY_SIZE],
			     const u8 signature[CPTRA_MCI_MLDSA87_SIGNATURE_SIZE],
			     const u8 *message, size_t message_len);

/*
 * cptra_mci_cryptographic_mbox.c -- Cmk-managed ECDSA/ML-DSA subset only
 * (MC_IMPORT plus CM_ECDSA_CMK_* / CM_MLDSA_CMK_*). AES/HMAC/HKDF/ECDH/
 * random/cm_status are not ported.
 */
int cptra_mci_import_key(enum cptra_mci_key_usage key_usage, const u8 *key, size_t key_len,
			 u8 cmk[CPTRA_MCI_CMK_SIZE]);
int cptra_mci_ecdsa_cmk_public_key(const u8 cmk[CPTRA_MCI_CMK_SIZE],
				   u8 qx[CPTRA_MCI_ECC384_SCALAR_SIZE],
				   u8 qy[CPTRA_MCI_ECC384_SCALAR_SIZE]);
int cptra_mci_ecdsa_cmk_sign(const u8 cmk[CPTRA_MCI_CMK_SIZE],
			     const u8 *message, size_t message_len,
			     u8 r[CPTRA_MCI_ECC384_SCALAR_SIZE],
			     u8 s[CPTRA_MCI_ECC384_SCALAR_SIZE]);
int cptra_mci_ecdsa_cmk_verify(const u8 cmk[CPTRA_MCI_CMK_SIZE],
			       const u8 r[CPTRA_MCI_ECC384_SCALAR_SIZE],
			       const u8 s[CPTRA_MCI_ECC384_SCALAR_SIZE],
			       const u8 *message, size_t message_len);
int cptra_mci_mldsa_cmk_public_key(const u8 cmk[CPTRA_MCI_CMK_SIZE],
				   u8 public_key[CPTRA_MCI_MLDSA87_PUBKEY_SIZE]);
int cptra_mci_mldsa_cmk_sign(const u8 cmk[CPTRA_MCI_CMK_SIZE],
			     const u8 *message, size_t message_len,
			     u8 signature[CPTRA_MCI_MLDSA87_SIGNATURE_SIZE]);
int cptra_mci_mldsa_cmk_verify(const u8 cmk[CPTRA_MCI_CMK_SIZE],
			       const u8 signature[CPTRA_MCI_MLDSA87_SIGNATURE_SIZE],
			       const u8 *message, size_t message_len);

#endif /* _CPTRA_MCI_MBOX_H */
