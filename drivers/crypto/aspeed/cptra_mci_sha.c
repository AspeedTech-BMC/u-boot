// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2026 ASPEED Technology Inc.
 *
 * Caliptra-SS MCI mailbox SHA-384/512 driver. Talks to the Caliptra-SS MCI
 * mailbox via the shared aspeed_cptra_mci_mbox transport
 * (drivers/misc/aspeed_cptra_mci_mbox.c).
 */

#include <common.h>
#include <dm.h>
#include <malloc.h>
#include <watchdog.h>
#include <u-boot/cptra_mci_mbox.h>
#include <u-boot/hash.h>

struct cptra_mci_sha_ctx {
	enum HASH_ALGO algo;
	u32 dgst_len;
	u8 context[CPTRA_MCI_SHA_CONTEXT_SIZE];
};

static int cptra_mci_sha_do_init(struct cptra_mci_sha_ctx *ctx, u32 algo)
{
	struct cptra_mci_sha_init_hdr req = {
		.hash_algorithm = algo,
		.input_size = 0,
	};
	struct cptra_mci_sha_ctx_resp resp = { 0 };
	u32 resp_len;
	int ret;

	req.hdr.chksum = cptra_mci_mbox_checksum(CPTRA_MCI_MBCMD_SHA_INIT,
						 (u8 *)&req + sizeof(req.hdr),
						 sizeof(req) - sizeof(req.hdr));

	ret = cptra_mci_mbox_execute(CPTRA_MCI_MBCMD_SHA_INIT, &req, sizeof(req),
				     &resp, sizeof(resp), &resp_len);
	if (ret)
		return ret;

	if (resp_len < sizeof(resp))
		return -EIO;

	memcpy(ctx->context, resp.context, sizeof(ctx->context));

	return 0;
}

static int cptra_mci_sha_do_update(struct cptra_mci_sha_ctx *ctx, const u8 *data, u32 len)
{
	struct cptra_mci_sha_data_hdr req = {
		.input_size = len,
	};
	struct cptra_mci_sha_ctx_resp resp = { 0 };
	u32 resp_len, csum;
	int ret;

	memcpy(req.context, ctx->context, sizeof(req.context));

	csum = cptra_mci_mbox_checksum(CPTRA_MCI_MBCMD_SHA_UPDATE,
				       (u8 *)&req + sizeof(req.hdr),
				       sizeof(req) - sizeof(req.hdr));
	req.hdr.chksum = cptra_mci_mbox_checksum_ext(csum, data, len);

	ret = cptra_mci_mbox_execute_sg(CPTRA_MCI_MBCMD_SHA_UPDATE, &req, sizeof(req),
					data, len, &resp, sizeof(resp), &resp_len);
	if (ret)
		return ret;

	if (resp_len < sizeof(resp))
		return -EIO;

	memcpy(ctx->context, resp.context, sizeof(ctx->context));

	return 0;
}

static int cptra_mci_sha_do_final(struct cptra_mci_sha_ctx *ctx, const u8 *data, u32 len,
				  u8 *digest)
{
	struct cptra_mci_sha_data_hdr req = {
		.input_size = len,
	};
	struct cptra_mci_sha_final_resp resp = { 0 };
	u32 resp_len, csum, dgst_len;
	int ret;

	memcpy(req.context, ctx->context, sizeof(req.context));

	csum = cptra_mci_mbox_checksum(CPTRA_MCI_MBCMD_SHA_FINAL,
				       (u8 *)&req + sizeof(req.hdr),
				       sizeof(req) - sizeof(req.hdr));
	req.hdr.chksum = cptra_mci_mbox_checksum_ext(csum, data, len);

	ret = cptra_mci_mbox_execute_sg(CPTRA_MCI_MBCMD_SHA_FINAL, &req, sizeof(req),
					data, len, &resp, sizeof(resp), &resp_len);
	if (ret)
		return ret;

	dgst_len = min_t(u32, ctx->dgst_len, sizeof(resp.hash));
	if (resp_len < sizeof(resp.hdr) + dgst_len)
		return -EIO;

	memcpy(digest, resp.hash, dgst_len);

	return 0;
}

static int cptra_mci_sha_algo_params(enum HASH_ALGO algo, u32 *mci_algo, u32 *dgst_len)
{
	switch (algo) {
	case HASH_ALGO_SHA384:
		*mci_algo = CPTRA_MCI_SHA_ALGO_SHA384;
		*dgst_len = CPTRA_MCI_SHA384_DIGEST_SIZE;
		return 0;
	case HASH_ALGO_SHA512:
		*mci_algo = CPTRA_MCI_SHA_ALGO_SHA512;
		*dgst_len = CPTRA_MCI_SHA512_DIGEST_SIZE;
		return 0;
	default:
		return -EINVAL;
	}
}

static int cptra_mci_sha_init(struct udevice *dev, enum HASH_ALGO algo, void **ctxp)
{
	struct cptra_mci_sha_ctx *ctx;
	u32 mci_algo, dgst_len;
	int ret;

	ret = cptra_mci_sha_algo_params(algo, &mci_algo, &dgst_len);
	if (ret)
		return ret;

	ctx = malloc(sizeof(*ctx));
	if (!ctx)
		return -ENOMEM;

	ctx->algo = algo;
	ctx->dgst_len = dgst_len;

	ret = cptra_mci_sha_do_init(ctx, mci_algo);
	if (ret) {
		free(ctx);
		return ret;
	}

	*ctxp = ctx;

	return 0;
}

static int cptra_mci_sha_update(struct udevice *dev, void *ctx, const void *ibuf, uint32_t ilen)
{
	struct cptra_mci_sha_ctx *sctx = ctx;
	const u8 *p8 = ibuf;
	u32 remaining = ilen;
	u32 chunk;
	int ret;

	while (remaining > 0) {
		chunk = min_t(u32, remaining, CPTRA_MCI_MBOX_MAX_INPUT_SIZE);

		ret = cptra_mci_sha_do_update(sctx, p8, chunk);
		if (ret)
			return ret;

		p8 += chunk;
		remaining -= chunk;
	}

	return 0;
}

static int cptra_mci_sha_finish(struct udevice *dev, void *ctx, void *obuf)
{
	struct cptra_mci_sha_ctx *sctx = ctx;
	int ret;

	ret = cptra_mci_sha_do_final(sctx, NULL, 0, obuf);

	free(sctx);

	return ret;
}

static int cptra_mci_sha_digest_wd(struct udevice *dev, enum HASH_ALGO algo,
				   const void *ibuf, const uint32_t ilen,
				   void *obuf, uint32_t chunk_sz)
{
	const void *cur, *end;
	u32 chunk;
	void *ctx;
	int ret;

	ret = cptra_mci_sha_init(dev, algo, &ctx);
	if (ret)
		return ret;

	cur = ibuf;
	end = ibuf + ilen;

	while (cur < end) {
		chunk = end - cur;
		if (chunk > chunk_sz)
			chunk = chunk_sz;

		ret = cptra_mci_sha_update(dev, ctx, cur, chunk);
		if (ret) {
			free(ctx);
			return ret;
		}

		cur += chunk;
		schedule();
	}

	return cptra_mci_sha_finish(dev, ctx, obuf);
}

static int cptra_mci_sha_digest(struct udevice *dev, enum HASH_ALGO algo,
				const void *ibuf, const uint32_t ilen, void *obuf)
{
	/* re-use the watchdog version with input length as the chunk_sz */
	return cptra_mci_sha_digest_wd(dev, algo, ibuf, ilen, obuf, ilen ? ilen : 1);
}

static const struct hash_ops cptra_mci_sha_ops = {
	.hash_init = cptra_mci_sha_init,
	.hash_update = cptra_mci_sha_update,
	.hash_finish = cptra_mci_sha_finish,
	.hash_digest_wd = cptra_mci_sha_digest_wd,
	.hash_digest = cptra_mci_sha_digest,
};

static const struct udevice_id cptra_mci_sha_ids[] = {
	{ .compatible = "aspeed,ast2705-cptra-mci-sha" },
	{ }
};

U_BOOT_DRIVER(aspeed_cptra_mci_sha) = {
	.name = "aspeed_cptra_mci_sha",
	.id = UCLASS_HASH,
	.of_match = cptra_mci_sha_ids,
	.ops = &cptra_mci_sha_ops,
	.flags = DM_FLAG_PRE_RELOC,
};
