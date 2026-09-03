// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2025 ASPEED Technology Inc.
 */
#include <common.h>
#include <crypto/ecdsa-uclass.h>
#include <u-boot/cptra_ipc.h>
#include <u-boot/ecdsa.h>

#define CPTRA_ECDSA_SIG_LEN			96	/* ECDSA384 */
#define CPTRA_ECDSA_SHA_LEN			48	/* SHA384 */

struct cptra_ecdsa_ctx {
	int qx_len;
	uint32_t qx;

	int qy_len;
	uint32_t qy;

	int r_len;
	uint32_t r;

	int s_len;
	uint32_t s;

	int  m_len;
	uint32_t m;
};

static int cptra_ipc_ecdsa_verify(struct udevice *dev,
				  const struct ecdsa_public_key *pubkey,
				  const void *hash, size_t hash_len,
				  const void *signature, size_t sig_len)
{
	enum cptra_ipc_cmd ipccmd = CPTRA_IPCCMD_ECDSA384_SIGNATURE_VERIFY;
	uint8_t *p8 = (uint8_t *)IPC_CHANNEL_1_NS_CA35_IN_ADDR;
	struct cptra_ecdsa_ctx ctx;
	uint8_t *p8_bmcu_in;
	int ret, rc;

	if (hash_len != CPTRA_ECDSA_SHA_LEN || sig_len != CPTRA_ECDSA_SIG_LEN) {
		pr_err("%s: bad hash_len %zu or sig_len %zu\n", __func__,
		       hash_len, sig_len);
		return -EINVAL;
	}

	if ((strcmp(pubkey->curve_name, "secp384r1") &&
	     strcmp(pubkey->curve_name, "prime384v1")) ||
	    pubkey->size_bits != ((CPTRA_ECDSA_SIG_LEN / 2) << 3)) {
		pr_err("%s: unsupported curve %s, %d bits\n", __func__,
		       pubkey->curve_name, pubkey->size_bits);
		return -EINVAL;
	}

	/* Prepare tx data to bootmcu */
	p8_bmcu_in = (uint8_t *)IPC_CHANNEL_1_BOOTMCU_IN_ADDR;
	p8_bmcu_in += sizeof(struct cptra_ecdsa_ctx);
	ctx.qx = (uint32_t)(uintptr_t)p8_bmcu_in;
	p8_bmcu_in += 48;
	ctx.qy = (uint32_t)(uintptr_t)p8_bmcu_in;
	p8_bmcu_in += 48;
	ctx.r = (uint32_t)(uintptr_t)p8_bmcu_in;
	p8_bmcu_in += 48;
	ctx.s = (uint32_t)(uintptr_t)p8_bmcu_in;
	p8_bmcu_in += 48;
	ctx.m = (uint32_t)(uintptr_t)p8_bmcu_in;
	p8_bmcu_in += CPTRA_ECDSA_SHA_LEN;
	ctx.qx_len = 48;
	ctx.qy_len = 48;
	ctx.r_len = 48;
	ctx.s_len = 48;
	ctx.m_len = CPTRA_ECDSA_SHA_LEN;

	memcpy(p8, (uint8_t *)&ctx, sizeof(struct cptra_ecdsa_ctx));
	p8 += sizeof(struct cptra_ecdsa_ctx);
	memcpy(p8, (uint8_t *)pubkey->x, 48);
	p8 += 48;
	memcpy(p8, (uint8_t *)pubkey->y, 48);
	p8 += 48;
	memcpy(p8, (uint8_t *)signature, 48);
	p8 += 48;
	memcpy(p8, (uint8_t *)signature + 48, 48);
	p8 += 48;
	/*
	 * Keep the BootMCU IPC ECDSA packet complete even though Caliptra 1.x
	 * verifies the digest retained by its SHA384 accelerator.
	 */
	memcpy(p8, (uint8_t *)hash, CPTRA_ECDSA_SHA_LEN);

	ret = cptra_ipc_trigger(ipccmd);
	if (ret) {
		pr_warn("cptra_ipc_trigger:0x%x is failure, ret:0x%x\n", ipccmd,
			ret);
		goto end;
	}

	rc = cptra_ipc_receive(CPTRA_IPC_RX_TYPE_INTERNAL, &ret, sizeof(ret));
	if (rc)
		return rc;
	if (ret) {
		pr_warn("cptra_ipc_receive:0x%x is failure, ret:0x%x\n", ipccmd,
			ret);
		goto end;
	}

end:
	return ret;
}

static int cptra_ipc_ecdsa_probe(struct udevice *dev)
{
	return 0;
}

static int cptra_ipc_ecdsa_remove(struct udevice *dev)
{
	return 0;
}

static const struct ecdsa_ops cptra_ecdsa_ops = {
	.verify = cptra_ipc_ecdsa_verify,
};

static const struct udevice_id cptra_ecdsa_ids[] = {
	{ .compatible = "aspeed,cptra-ipc-ecdsa" },
	{ }
};

U_BOOT_DRIVER(aspeed_cptra_ipc_ecdsa) = {
	.name = "aspeed_cptra_ipc_ecdsa",
	.id = UCLASS_ECDSA,
	.of_match = cptra_ecdsa_ids,
	.ops = &cptra_ecdsa_ops,
	.probe = cptra_ipc_ecdsa_probe,
	.remove = cptra_ipc_ecdsa_remove,
	.flags = DM_FLAG_PRE_RELOC,
};
