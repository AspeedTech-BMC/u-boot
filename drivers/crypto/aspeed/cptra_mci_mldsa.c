// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2026 ASPEED Technology Inc.
 *
 * Caliptra-SS MCI mailbox ML-DSA-87 signature verifier. Pure passthrough
 * to Caliptra's MLDSA87_SIGNATURE_VERIFY -- takes a raw public key rather
 * than an opaque Cmk, and Caliptra hashes the message internally so this
 * takes the raw message, not a pre-hashed digest. No on-device signing
 * command for a raw public key, and u-boot has no ML-DSA verify uclass to
 * register a DM driver under, so this is exposed as a plain function
 * instead.
 */

#include <common.h>
#include <malloc.h>
#include <u-boot/cptra_mci_mbox.h>

int cptra_mci_mldsa87_verify(const u8 pub_key[CPTRA_MCI_MLDSA87_PUBKEY_SIZE],
			     const u8 signature[CPTRA_MCI_MLDSA87_SIGNATURE_SIZE],
			     const u8 *message, size_t message_len)
{
	/*
	 * pub_key (2592 bytes) + signature (4628 bytes) make this request
	 * 7228 bytes -- heap-allocated per call rather than a stack local
	 * (too big to risk on every caller's stack) or a function-local
	 * static (would alias between concurrent callers).
	 */
	struct cptra_mci_mldsa87_sig_verify_hdr *req;
	struct cptra_mci_mldsa87_sig_verify_resp resp = { 0 };
	u32 resp_len, csum;
	int ret;

	if (message_len > CPTRA_MCI_MBOX_MAX_INPUT_SIZE)
		return -EINVAL;

	req = malloc(sizeof(*req));
	if (!req)
		return -ENOMEM;

	memcpy(req->pub_key, pub_key, sizeof(req->pub_key));
	memcpy(req->signature, signature, sizeof(req->signature));
	req->message_size = message_len;

	csum = cptra_mci_mbox_checksum(CPTRA_MCI_MBCMD_MLDSA87_SIG_VERIFY,
				       &req->pub_key, sizeof(*req) - sizeof(req->hdr));
	req->hdr.chksum = cptra_mci_mbox_checksum_ext(csum, message, message_len);

	ret = cptra_mci_mbox_execute_sg(CPTRA_MCI_MBCMD_MLDSA87_SIG_VERIFY, req, sizeof(*req),
					message, message_len, &resp, sizeof(resp), &resp_len);

	free(req);

	return ret;
}
