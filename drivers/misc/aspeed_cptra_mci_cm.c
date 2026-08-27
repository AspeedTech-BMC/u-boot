// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2026 ASPEED Technology Inc.
 *
 * Caliptra-SS MCI mailbox Cmk-managed key commands. Only the ECDSA384/
 * ML-DSA-87 subset needed to build self-checking known-answer tests is
 * implemented here: MC_IMPORT plus CM_ECDSA_CMK_* / CM_MLDSA_CMK_*.
 * AES/HMAC/HKDF/ECDH/random/cm_status are not covered.
 */

#include <common.h>
#include <malloc.h>
#include <u-boot/cptra_mci_mbox.h>

/*
 * Caliptra-SS's MC_IMPORT validation (RUNTIME_CMB_INVALID_KEY_USAGE_AND_SIZE
 * in the RT firmware) accepts exactly one or two input_size values per
 * key_usage: Aes/Mldsa -> 32, Ecdsa -> 48, Hmac -> 48 or 64, Mlkem -> 64.
 * Enforced here so a bad combination fails locally instead of burning a
 * mailbox round trip.
 */
static bool cptra_mci_import_key_size_valid(enum cptra_mci_key_usage key_usage, size_t key_len)
{
	switch (key_usage) {
	case CPTRA_MCI_KEY_USAGE_AES:
	case CPTRA_MCI_KEY_USAGE_MLDSA:
		return key_len == 32;
	case CPTRA_MCI_KEY_USAGE_ECDSA:
		return key_len == 48;
	case CPTRA_MCI_KEY_USAGE_HMAC:
		return key_len == 48 || key_len == 64;
	case CPTRA_MCI_KEY_USAGE_MLKEM:
		return key_len == 64;
	default:
		return false;
	}
}

int cptra_mci_import_key(enum cptra_mci_key_usage key_usage, const u8 *key, size_t key_len,
			 u8 cmk[CPTRA_MCI_CMK_SIZE])
{
	struct cptra_mci_import_hdr req = {
		.key_usage = key_usage,
		.input_size = key_len,
	};
	struct cptra_mci_import_resp resp = { 0 };
	u32 resp_len, csum;
	int ret;

	if (!cptra_mci_import_key_size_valid(key_usage, key_len))
		return -EINVAL;

	csum = cptra_mci_mbox_checksum(CPTRA_MCI_MBCMD_IMPORT,
				       &req.key_usage, sizeof(req) - sizeof(req.hdr));
	req.hdr.chksum = cptra_mci_mbox_checksum_ext(csum, key, key_len);

	ret = cptra_mci_mbox_execute_sg(CPTRA_MCI_MBCMD_IMPORT, &req, sizeof(req),
					key, key_len, &resp, sizeof(resp), &resp_len);
	if (ret)
		return ret;

	if (resp_len < sizeof(resp))
		return -EIO;

	memcpy(cmk, resp.cmk.value, sizeof(resp.cmk.value));

	return 0;
}

int cptra_mci_ecdsa_cmk_public_key(const u8 cmk[CPTRA_MCI_CMK_SIZE],
				   u8 qx[CPTRA_MCI_ECC384_SCALAR_SIZE],
				   u8 qy[CPTRA_MCI_ECC384_SCALAR_SIZE])
{
	struct cptra_mci_ecdsa_pubkey_req req = { 0 };
	struct cptra_mci_ecdsa_pubkey_resp resp = { 0 };
	u32 resp_len;
	int ret;

	memcpy(req.cmk.value, cmk, sizeof(req.cmk.value));

	req.hdr.chksum = cptra_mci_mbox_checksum(CPTRA_MCI_MBCMD_ECDSA_CMK_PUBLIC_KEY,
						 &req.cmk, sizeof(req) - sizeof(req.hdr));

	ret = cptra_mci_mbox_execute(CPTRA_MCI_MBCMD_ECDSA_CMK_PUBLIC_KEY, &req, sizeof(req),
				     &resp, sizeof(resp), &resp_len);
	if (ret)
		return ret;

	if (resp_len < sizeof(resp))
		return -EIO;

	memcpy(qx, resp.qx, sizeof(resp.qx));
	memcpy(qy, resp.qy, sizeof(resp.qy));

	return 0;
}

int cptra_mci_ecdsa_cmk_sign(const u8 cmk[CPTRA_MCI_CMK_SIZE],
			     const u8 *message, size_t message_len,
			     u8 r[CPTRA_MCI_ECC384_SCALAR_SIZE],
			     u8 s[CPTRA_MCI_ECC384_SCALAR_SIZE])
{
	struct cptra_mci_ecdsa_sign_hdr req = {
		.message_size = message_len,
	};
	struct cptra_mci_ecdsa_sign_resp resp = { 0 };
	u32 resp_len, csum;
	int ret;

	if (message_len > CPTRA_MCI_MBOX_MAX_INPUT_SIZE)
		return -EINVAL;

	memcpy(req.cmk.value, cmk, sizeof(req.cmk.value));

	csum = cptra_mci_mbox_checksum(CPTRA_MCI_MBCMD_ECDSA_CMK_SIGN,
				       &req.cmk, sizeof(req) - sizeof(req.hdr));
	req.hdr.chksum = cptra_mci_mbox_checksum_ext(csum, message, message_len);

	ret = cptra_mci_mbox_execute_sg(CPTRA_MCI_MBCMD_ECDSA_CMK_SIGN, &req, sizeof(req),
					message, message_len, &resp, sizeof(resp), &resp_len);
	if (ret)
		return ret;

	if (resp_len < sizeof(resp))
		return -EIO;

	memcpy(r, resp.r, sizeof(resp.r));
	memcpy(s, resp.s, sizeof(resp.s));

	return 0;
}

int cptra_mci_ecdsa_cmk_verify(const u8 cmk[CPTRA_MCI_CMK_SIZE],
			       const u8 r[CPTRA_MCI_ECC384_SCALAR_SIZE],
			       const u8 s[CPTRA_MCI_ECC384_SCALAR_SIZE],
			       const u8 *message, size_t message_len)
{
	struct cptra_mci_ecdsa_verify_hdr req = {
		.message_size = message_len,
	};
	struct cptra_mci_ecdsa_verify_resp resp = { 0 };
	u32 resp_len, csum;

	if (message_len > CPTRA_MCI_MBOX_MAX_INPUT_SIZE)
		return -EINVAL;

	memcpy(req.cmk.value, cmk, sizeof(req.cmk.value));
	memcpy(req.r, r, sizeof(req.r));
	memcpy(req.s, s, sizeof(req.s));

	csum = cptra_mci_mbox_checksum(CPTRA_MCI_MBCMD_ECDSA_CMK_VERIFY,
				       &req.cmk, sizeof(req) - sizeof(req.hdr));
	req.hdr.chksum = cptra_mci_mbox_checksum_ext(csum, message, message_len);

	return cptra_mci_mbox_execute_sg(CPTRA_MCI_MBCMD_ECDSA_CMK_VERIFY, &req, sizeof(req),
					 message, message_len, &resp, sizeof(resp), &resp_len);
}

int cptra_mci_mldsa_cmk_public_key(const u8 cmk[CPTRA_MCI_CMK_SIZE],
				   u8 public_key[CPTRA_MCI_MLDSA87_PUBKEY_SIZE])
{
	struct cptra_mci_mldsa_pubkey_req req = { 0 };
	/*
	 * public_key alone is 2592 bytes -- heap-allocated per call rather
	 * than a function-local static, so concurrent callers each get their
	 * own response buffer instead of aliasing one shared instance.
	 */
	struct cptra_mci_mldsa_pubkey_resp *resp;
	u32 resp_len;
	int ret;

	resp = malloc(sizeof(*resp));
	if (!resp)
		return -ENOMEM;

	memcpy(req.cmk.value, cmk, sizeof(req.cmk.value));

	req.hdr.chksum = cptra_mci_mbox_checksum(CPTRA_MCI_MBCMD_MLDSA_CMK_PUBLIC_KEY,
						 &req.cmk, sizeof(req) - sizeof(req.hdr));

	ret = cptra_mci_mbox_execute(CPTRA_MCI_MBCMD_MLDSA_CMK_PUBLIC_KEY, &req, sizeof(req),
				     resp, sizeof(*resp), &resp_len);
	if (ret)
		goto out;

	if (resp_len < sizeof(*resp)) {
		ret = -EIO;
		goto out;
	}

	memcpy(public_key, resp->public_key, sizeof(resp->public_key));

out:
	free(resp);

	return ret;
}

int cptra_mci_mldsa_cmk_sign(const u8 cmk[CPTRA_MCI_CMK_SIZE],
			     const u8 *message, size_t message_len,
			     u8 signature[CPTRA_MCI_MLDSA87_SIGNATURE_SIZE])
{
	struct cptra_mci_mldsa_sign_hdr req = {
		.message_size = message_len,
	};
	/*
	 * signature alone is 4628 bytes -- heap-allocated per call rather
	 * than a function-local static, so concurrent callers each get their
	 * own response buffer instead of aliasing one shared instance.
	 */
	struct cptra_mci_mldsa_sign_resp *resp;
	u32 resp_len, csum;
	int ret;

	if (message_len > CPTRA_MCI_MBOX_MAX_INPUT_SIZE)
		return -EINVAL;

	resp = malloc(sizeof(*resp));
	if (!resp)
		return -ENOMEM;

	memcpy(req.cmk.value, cmk, sizeof(req.cmk.value));

	csum = cptra_mci_mbox_checksum(CPTRA_MCI_MBCMD_MLDSA_CMK_SIGN,
				       &req.cmk, sizeof(req) - sizeof(req.hdr));
	req.hdr.chksum = cptra_mci_mbox_checksum_ext(csum, message, message_len);

	ret = cptra_mci_mbox_execute_sg(CPTRA_MCI_MBCMD_MLDSA_CMK_SIGN, &req, sizeof(req),
					message, message_len, resp, sizeof(*resp), &resp_len);
	if (ret)
		goto out;

	if (resp_len < sizeof(*resp)) {
		ret = -EIO;
		goto out;
	}

	memcpy(signature, resp->signature, sizeof(resp->signature));

out:
	free(resp);

	return ret;
}

int cptra_mci_mldsa_cmk_verify(const u8 cmk[CPTRA_MCI_CMK_SIZE],
			       const u8 signature[CPTRA_MCI_MLDSA87_SIGNATURE_SIZE],
			       const u8 *message, size_t message_len)
{
	/*
	 * The signature field alone makes this request header 4764 bytes --
	 * heap-allocated per call rather than a function-local static, so
	 * concurrent callers each get their own request buffer instead of
	 * aliasing one shared instance. Every field is written below before
	 * use, so no explicit zero-init is needed.
	 */
	struct cptra_mci_mldsa_verify_hdr *req;
	struct cptra_mci_mldsa_verify_resp resp = { 0 };
	u32 resp_len, csum;
	int ret;

	if (message_len > CPTRA_MCI_MBOX_MAX_INPUT_SIZE)
		return -EINVAL;

	req = malloc(sizeof(*req));
	if (!req)
		return -ENOMEM;

	req->message_size = message_len;
	memcpy(req->cmk.value, cmk, sizeof(req->cmk.value));
	memcpy(req->signature, signature, sizeof(req->signature));

	csum = cptra_mci_mbox_checksum(CPTRA_MCI_MBCMD_MLDSA_CMK_VERIFY,
				       &req->cmk, sizeof(*req) - sizeof(req->hdr));
	req->hdr.chksum = cptra_mci_mbox_checksum_ext(csum, message, message_len);

	ret = cptra_mci_mbox_execute_sg(CPTRA_MCI_MBCMD_MLDSA_CMK_VERIFY, req, sizeof(*req),
					message, message_len, &resp, sizeof(resp), &resp_len);

	free(req);

	return ret;
}
