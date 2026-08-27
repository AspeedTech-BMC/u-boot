// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2026 ASPEED Technology Inc.
 *
 * Caliptra-SS MCI mailbox ECDSA384 signature verifier. Pure passthrough to
 * Caliptra's ECDSA384_SIGNATURE_VERIFY -- takes a raw (qx, qy) public key
 * and an already-hashed digest, not a Cmk handle, so there is no signing
 * counterpart here (unlike the Cmk-managed CM_ECDSA_CMK_SIGN command).
 */

#include <common.h>
#include <crypto/ecdsa-uclass.h>
#include <string.h>
#include <u-boot/cptra_mci_mbox.h>
#include <u-boot/ecdsa.h>

static int cptra_mci_ecdsa_verify(struct udevice *dev,
				  const struct ecdsa_public_key *pubkey,
				  const void *hash, size_t hash_len,
				  const void *signature, size_t sig_len)
{
	struct cptra_mci_ecdsa384_sig_verify_req req = { 0 };
	struct cptra_mci_ecdsa384_sig_verify_resp resp = { 0 };
	u32 resp_len;
	int ret;

	if ((strcmp(pubkey->curve_name, "secp384r1") &&
	     strcmp(pubkey->curve_name, "prime384v1")) ||
	    pubkey->size_bits != (CPTRA_MCI_ECC384_SCALAR_SIZE << 3))
		return -EINVAL;

	if (hash_len != CPTRA_MCI_ECC384_SCALAR_SIZE ||
	    sig_len != 2 * CPTRA_MCI_ECC384_SCALAR_SIZE)
		return -EINVAL;

	memcpy(req.pub_key_x, pubkey->x, sizeof(req.pub_key_x));
	memcpy(req.pub_key_y, pubkey->y, sizeof(req.pub_key_y));
	memcpy(req.signature_r, signature, sizeof(req.signature_r));
	memcpy(req.signature_s, (const u8 *)signature + sizeof(req.signature_r),
	       sizeof(req.signature_s));
	memcpy(req.hash, hash, sizeof(req.hash));

	req.hdr.chksum = cptra_mci_mbox_checksum(CPTRA_MCI_MBCMD_ECDSA384_SIG_VERIFY,
						 &req.pub_key_x, sizeof(req) - sizeof(req.hdr));

	ret = cptra_mci_mbox_execute(CPTRA_MCI_MBCMD_ECDSA384_SIG_VERIFY, &req, sizeof(req),
				     &resp, sizeof(resp), &resp_len);
	if (ret)
		return ret;

	return 0;
}

static const struct ecdsa_ops cptra_mci_ecdsa_ops = {
	.verify = cptra_mci_ecdsa_verify,
};

static const struct udevice_id cptra_mci_ecdsa_ids[] = {
	{ .compatible = "aspeed,ast2705-cptra-mci-ecdsa" },
	{ }
};

U_BOOT_DRIVER(aspeed_cptra_mci_ecdsa) = {
	.name = "aspeed_cptra_mci_ecdsa",
	.id = UCLASS_ECDSA,
	.of_match = cptra_mci_ecdsa_ids,
	.ops = &cptra_mci_ecdsa_ops,
	.flags = DM_FLAG_PRE_RELOC,
};
