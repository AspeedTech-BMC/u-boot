// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2026 ASPEED Technology Inc.
 */

#include <asm/io.h>
#include <crypto/ecdsa-uclass.h>
#include <dm/fdtaddr.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <u-boot/ecdsa.h>
#include <u-boot/hash-checksum.h>

#include "aspeed_ecdsa.h"

static const struct ecc_curve_param curve_secp256r1 = {
	.nbytes = 32,
	.Gx = "\x6b\x17\xd1\xf2\xe1\x2c\x42\x47\xf8\xbc\xe6\xe5\x63\xa4\x40\xf2"
	      "\x77\x03\x7d\x81\x2d\xeb\x33\xa0\xf4\xa1\x39\x45\xd8\x98\xc2\x96",
	.Gy = "\x4f\xe3\x42\xe2\xfe\x1a\x7f\x9b\x8e\xe7\xeb\x4a\x7c\x0f\x9e\x16"
	      "\x2b\xce\x33\x57\x6b\x31\x5e\xce\xcb\xb6\x40\x68\x37\xbf\x51\xf5",
	.P = "\xff\xff\xff\xff\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00"
	     "\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff",
	.A = "\xff\xff\xff\xff\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00"
	     "\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfc",
	.N = "\xff\xff\xff\xff\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff"
	     "\xbc\xe6\xfa\xad\xa7\x17\x9e\x84\xf3\xb9\xca\xc2\xfc\x63\x25\x51",
};

static const struct ecc_curve_param curve_secp384r1 = {
	.nbytes = 48,
	.Gx = "\xaa\x87\xca\x22\xbe\x8b\x05\x37\x8e\xb1\xc7\x1e\xf3\x20\xad\x74"
	      "\x6e\x1d\x3b\x62\x8b\xa7\x9b\x98\x59\xf7\x41\xe0\x82\x54\x2a\x38"
	      "\x55\x02\xf2\x5d\xbf\x55\x29\x6c\x3a\x54\x5e\x38\x72\x76\x0a\xb7",
	.Gy = "\x36\x17\xde\x4a\x96\x26\x2c\x6f\x5d\x9e\x98\xbf\x92\x92\xdc\x29"
	      "\xf8\xf4\x1d\xbd\x28\x9a\x14\x7c\xe9\xda\x31\x13\xb5\xf0\xb8\xc0"
	      "\x0a\x60\xb1\xce\x1d\x7e\x81\x9d\x7a\x43\x1d\x7c\x90\xea\x0e\x5f",
	.P = "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
	     "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe"
	     "\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00\xff\xff\xff\xff",
	.A = "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
	     "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe"
	     "\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00\xff\xff\xff\xfc",
	.N = "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
	     "\xff\xff\xff\xff\xff\xff\xff\xff\xc7\x63\x4d\x81\xf4\x37\x2d\xdf"
	     "\x58\x1a\x0d\xb2\x48\xb0\xa7\x7a\xec\xec\x19\x6a\xcc\xc5\x29\x73",
};

static int aspeed_ecdsa_init_curve(struct udevice *dev,
				   const struct ecdsa_public_key *pubk)
{
	uint32_t ctrl = 0;
	struct aspeed_ecdsa *ecdsa = dev_get_priv(dev);
	const struct ecc_curve_param *curve = NULL;

	if (!pubk || !(pubk->curve_name) || !ecdsa)
		return -EINVAL;

	if (!strncmp("prime256v1", pubk->curve_name, strlen("prime256v1"))) {
		curve = &curve_secp256r1;
		ctrl = ECDSA_256_EN;
	} else if (!strcmp("prime384v1", pubk->curve_name) ||
			   !strcmp("secp384r1", pubk->curve_name)) {
		curve = &curve_secp384r1;
	} else {
		return -ENOTSUPP;
	}

	if (curve->nbytes > ASPEED_ECDSA_MAX_DATA_SIZE)
		return -EINVAL;

	writel(ECC_EN | ctrl, ecdsa->base + ASPEED_ECC_CTRL_REG);

	memcpy_toio(ecdsa->base + ASPEED_ECC_GX_REG, curve->Gx, curve->nbytes);
	memcpy_toio(ecdsa->base + ASPEED_ECC_GY_REG, curve->Gy, curve->nbytes);
	memcpy_toio(ecdsa->base + ASPEED_ECC_P_REG, curve->P, curve->nbytes);
	memcpy_toio(ecdsa->base + ASPEED_ECC_A_REG, curve->A, curve->nbytes);
	memcpy_toio(ecdsa->base + ASPEED_ECC_N_REG, curve->N, curve->nbytes);

	return 0;
}

static int aspeed_ecdsa_set_pubk(struct udevice *dev,
				 const struct ecdsa_public_key *pubk)
{
	int nbytes = 0;
	struct aspeed_ecdsa *ecdsa = dev_get_priv(dev);

	if (!pubk || !ecdsa)
		return -EINVAL;

	nbytes = TO_BYTES(pubk->size_bits);
	if (nbytes != ECDSA256_BYTES && nbytes != ECDSA384_BYTES)
		return -EINVAL;

	memcpy_toio(ecdsa->base + ASPEED_ECC_QX_REG, pubk->x, nbytes);
	memcpy_toio(ecdsa->base + ASPEED_ECC_QY_REG, pubk->y, nbytes);

	return 0;
}

static int aspeed_ecdsa_set_sig(struct udevice *dev, const void *hash,
				int hash_len, const void *sig, size_t sig_len)
{
	uint8_t len_nbytes = (sig_len / 2);
	uint8_t msg[SHA384_SUM_LEN] = { 0 };
	uint8_t offset = 0;
	const uint8_t *sig_r = sig;
	const uint8_t *sig_s = sig + len_nbytes;
	struct aspeed_ecdsa *ecdsa = dev_get_priv(dev);

	if (!hash || !sig || !ecdsa)
		return -EINVAL;

	if (len_nbytes != ECDSA256_BYTES && len_nbytes != ECDSA384_BYTES)
		return -EINVAL;

	if (hash_len != 28 && hash_len != SHA256_SUM_LEN &&
	    hash_len != SHA384_SUM_LEN)
		return -EINVAL;

	/* Copy message and padding 0 */
	offset = len_nbytes > hash_len ? len_nbytes - hash_len : 0;
	memcpy(msg + offset, hash, hash_len);
	memcpy_toio(ecdsa->base + ASPEED_ECC_MESSAGE_REG, msg, sizeof(msg));
	memcpy_toio(ecdsa->base + ASPEED_ECC_SIGN_R_REG, sig_r, len_nbytes);
	memcpy_toio(ecdsa->base + ASPEED_ECC_SIGN_S_REG, sig_s, len_nbytes);

	return 0;
}

static int aspeed_ecdsa_trigger_engine(struct udevice *dev)
{
	int ret = 0;
	uint32_t sts = 0;
	struct aspeed_ecdsa *ecdsa = dev_get_priv(dev);

	if (!ecdsa)
		return -EINVAL;

	setbits_le32(ecdsa->base + ASPEED_ECC_ECDSA_VERIFY, ECC_VERIFY_MODE);
	setbits_le32(ecdsa->base + ASPEED_ECC_CMD_REG, ECC_ENGINE_START);
	clrbits_le32(ecdsa->base + ASPEED_ECC_CMD_REG, ECC_ENGINE_START);

	ret = readl_poll_timeout(ecdsa->base + ASPEED_ECC_STS_REG, sts,
				 ((sts & ECC_IDLE) == ECC_IDLE),
				 ASPEED_ECDSA_TIMEOUT);
	if (ret)
		return -ETIME;

	sts = readl(ecdsa->base + ASPEED_ECC_STS_REG) & ECC_VERIFY_PASS;

	return sts == ECC_VERIFY_PASS ? 0 : -EBADMSG;
}

static int aspeed_ecdsa_verify(struct udevice *dev,
			       const struct ecdsa_public_key *pubkey,
			       const void *hash, size_t hash_len,
			       const void *signature, size_t sig_len)
{
	int ret = 0;

	if (!dev)
		return -ENODEV;

	ret = aspeed_ecdsa_init_curve(dev, pubkey);
	if (ret)
		goto fail;

	ret = aspeed_ecdsa_set_pubk(dev, pubkey);
	if (ret)
		goto fail;

	ret = aspeed_ecdsa_set_sig(dev, hash, hash_len, signature, sig_len);
	if (ret)
		goto fail;

	ret = aspeed_ecdsa_trigger_engine(dev);
fail:
	return ret;
}

static int aspeed_ecdsa_probe(struct udevice *dev)
{
	struct aspeed_ecdsa *ecdsa = dev_get_priv(dev);
	int ret = 0;

	/* Get ecdsa base address */
	ecdsa->base = devfdt_get_addr_index_ptr(dev, 0);
	if (!ecdsa->base)
		return -EINVAL;

	/* Get ecdsa clock information */
	ret = clk_get_by_index(dev, 0, &ecdsa->clk);
	if (ret < 0)
		return ret;

	/* Get ecdsa reset information */
	ret = reset_get_by_index(dev, 0, &ecdsa->rst);
	if (ret)
		return ret;

	/* Enable ecdsa clock */
	ret = clk_enable(&ecdsa->clk);
	if (ret)
		return ret;

	/* Release ecdsa reset */
	ret = reset_deassert(&ecdsa->rst);
	if (ret)
		return ret;

	return 0;
}

static int aspeed_ecdsa_remove(struct udevice *dev)
{
	return 0;
}

static const struct ecdsa_ops aspeed_ecdsa_ops = {
	.verify = aspeed_ecdsa_verify,
};

static const struct udevice_id aspeed_ecdsa_ids[] = {
	{ .compatible = "aspeed,ast2700-ecdsa" },
	{}
};

U_BOOT_DRIVER(aspeed_ecdsa) = {
	.name = "aspeed_ecdsa",
	.id = UCLASS_ECDSA,
	.of_match = aspeed_ecdsa_ids,
	.ops = &aspeed_ecdsa_ops,
	.probe = aspeed_ecdsa_probe,
	.remove = aspeed_ecdsa_remove,
	.priv_auto = sizeof(struct aspeed_ecdsa),
	.flags = DM_FLAG_PRE_RELOC,
};
