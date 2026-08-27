// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2026 ASPEED Technology Inc.
 *
 * Caliptra-SS MCI mailbox LMS signature verifier. Pure passthrough to
 * Caliptra's LMS_SIGNATURE_VERIFY -- takes a raw public key and an
 * already-hashed digest. LMS keys are generated offline (e.g. for firmware
 * signing) and Caliptra-SS exposes no on-device LMS signing command at all,
 * and u-boot has no LMS verify uclass to register a DM driver under, so
 * this is exposed as a plain function instead.
 */

#include <common.h>
#include <malloc.h>
#include <u-boot/cptra_mci_mbox.h>

int cptra_mci_lms_verify(u32 pub_key_tree_type, u32 pub_key_ots_type,
			 const u8 pub_key_id[CPTRA_MCI_LMS_PUBKEY_ID_SIZE],
			 const u8 pub_key_digest[CPTRA_MCI_LMS_PUBKEY_DIGEST_SIZE],
			 u32 signature_q,
			 const u8 signature_ots[CPTRA_MCI_LMS_OTS_SIGNATURE_SIZE],
			 u32 signature_tree_type,
			 const u8 signature_tree_path[CPTRA_MCI_LMS_TREE_PATH_SIZE],
			 const u8 hash[CPTRA_MCI_LMS_HASH_SIZE])
{
	/*
	 * signature_ots (1252 bytes) + signature_tree_path (360 bytes) make
	 * this request 1720 bytes -- heap-allocated per call rather than a
	 * stack local (too big to risk on every caller's stack) or a
	 * function-local static (would alias between concurrent callers).
	 */
	struct cptra_mci_lms_verify_req *req;
	struct cptra_mci_lms_verify_resp resp = { 0 };
	u32 resp_len;
	int ret;

	/*
	 * Caliptra-SS's runtime hard-codes and rejects every LMS/LM-OTS
	 * parameter set except this one -- see CPTRA_MCI_LMS_TREE_TYPE_FIXED/
	 * CPTRA_MCI_LMS_OTS_TYPE_FIXED in cptra_mci_mbox.h.
	 */
	if (pub_key_tree_type != CPTRA_MCI_LMS_TREE_TYPE_FIXED ||
	    pub_key_ots_type != CPTRA_MCI_LMS_OTS_TYPE_FIXED)
		return -EINVAL;

	req = malloc(sizeof(*req));
	if (!req)
		return -ENOMEM;

	req->pub_key_tree_type = pub_key_tree_type;
	req->pub_key_ots_type = pub_key_ots_type;
	memcpy(req->pub_key_id, pub_key_id, sizeof(req->pub_key_id));
	memcpy(req->pub_key_digest, pub_key_digest, sizeof(req->pub_key_digest));
	req->signature_q = signature_q;
	memcpy(req->signature_ots, signature_ots, sizeof(req->signature_ots));
	req->signature_tree_type = signature_tree_type;
	memcpy(req->signature_tree_path, signature_tree_path, sizeof(req->signature_tree_path));
	memcpy(req->hash, hash, sizeof(req->hash));

	req->hdr.chksum = cptra_mci_mbox_checksum(CPTRA_MCI_MBCMD_LMS_SIG_VERIFY,
						  &req->pub_key_tree_type,
						  sizeof(*req) - sizeof(req->hdr));

	ret = cptra_mci_mbox_execute(CPTRA_MCI_MBCMD_LMS_SIG_VERIFY, req, sizeof(*req),
				     &resp, sizeof(resp), &resp_len);

	free(req);

	return ret;
}
