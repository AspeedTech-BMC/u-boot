/*
 * (C) Copyright ASPEED Technology Inc.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <asm/io.h>
#include <linux/kernel.h>
#include <asm/arch/crypto.h>

static int ast_hace_wait_isr(u32 reg, u32 flag, u32 timeout)
{
	u32 ret;
	while (1) {
		ret = readl(reg);
		if ((ret & flag) == flag) {
			writel(flag, reg);
			break;
		}
		udelay(20);
		timeout -= 20;
	}
	if (timeout <= 0)
		return -ETIMEDOUT;

	return 0;
}


int aspeed_sg_digest(struct aspeed_sg_list *src_list, u32 list_length, u32 length, u8 *digest, u32 method)
{
	READ_ONCE(src_list[list_length - 1].phy_addr);
	writel((u32)src_list, ASPEED_HACE_HASH_SRC);
	writel((u32)digest, ASPEED_HACE_HASH_DIGEST_BUFF);
	writel(length, ASPEED_HACE_HASH_DATA_LEN);

	switch (method) {
	case ASPEED_SHA224:
		writel(0x40048, ASPEED_HACE_HASH_CMD);
		break;
	case ASPEED_SHA256:
		writel(0x40058, ASPEED_HACE_HASH_CMD);
		break;
	case ASPEED_SHA384:
		writel(0x40468, ASPEED_HACE_HASH_CMD);
		break;
	case ASPEED_SHA512:
		writel(0x40068, ASPEED_HACE_HASH_CMD);
		break;
	}

	return ast_hace_wait_isr(ASPEED_HACE_STS, HACE_HASH_ISR, 100000);
}

/**
 * digest a block of data, putting the result into digest.
 *
 * \param src		Source data
 * \param length	Size of source data
 * \param digest	digest
 * \param method	hash method
 */
int digest_object(u8 *src, u32 length, u8 *digest, u32 method)
{
	READ_ONCE(src[length - 1]);
	writel((u32)src, ASPEED_HACE_HASH_SRC);
	writel((u32)digest, ASPEED_HACE_HASH_DIGEST_BUFF);
	writel(length, ASPEED_HACE_HASH_DATA_LEN);

	switch (method) {
	case ASPEED_SHA224:
		writel(0x48, ASPEED_HACE_HASH_CMD);
		break;
	case ASPEED_SHA256:
		writel(0x58, ASPEED_HACE_HASH_CMD);
		break;
	case ASPEED_SHA384:
		writel(0x468, ASPEED_HACE_HASH_CMD);
		break;
	case ASPEED_SHA512:
		writel(0x68, ASPEED_HACE_HASH_CMD);
		break;
	}

	return ast_hace_wait_isr(ASPEED_HACE_STS, HACE_HASH_ISR, 100000);
}

int aes256ctr_decrypt_object(u8 *src, u8 *dst, u32 length, u8 *context)
{
	writel((u32)src, ASPEED_HACE_SRC);
	writel((u32)dst, ASPEED_HACE_DEST);
	writel((u32)context, ASPEED_HACE_CONTEXT);
	writel(length, ASPEED_HACE_DATA_LEN);
	writel(0x3048, ASPEED_HACE_CMD);

	return ast_hace_wait_isr(ASPEED_HACE_STS, HACE_CRYPTO_ISR, 100000);
}

int rsa_alg(u8 *data, int data_bytes, u8 *m, int m_bits, u8 *e, int e_bits, u8 *dst, void *contex_buf)
{
	u8 *contex;
	u8 *sram;
	int m_bytes;
	int e_bytes;
	int ret;
	int i, j;

	m_bytes = (m_bits + 7) / 8;
	e_bytes = (e_bits + 7) / 8;
	sram = (u8 *)0x1e710000;

	contex = (u8 *)ALIGN((u32) contex_buf, 16);

	j = 0;
	for (i = 0; i < e_bytes; i++) {
		contex[j] = e[i];
		j++;
		j = j % 16 ? j : j + 32;
	}

	j = 0;
	for (i = 0; i < m_bytes; i++) {
		contex[j + 16] = m[i];
		j++;
		j = j % 16 ? j : j + 32;
	}

	j = 0;
	for (i = 0; i < data_bytes; i++) {
		contex[j + 32] = data[i];
		j++;
		j = j % 16 ? j : j + 32;
	}

	writel((u32)contex, 0x1e6fa04c);
	writel((e_bits << 16) + m_bits, 0x1e6fa058);
	writel(0x600, 0x1e6fa050);
	writel(0x2, 0x1e6fa3f8);
	writel(0x30, 0x1e6fa048);
	writel(0x3, 0x1e6fa000);

	ret = ast_hace_wait_isr(0x1e6fa3fc, 6, 100000);

	if (ret != 0)
		return ret;

	writel(0, 0x1e6fa000);
	writel(0x100, 0x1e6fa048);

	udelay(20);
	j = 0;
	for (i = 0; i < m_bytes; i++) {
		dst[i] = sram[j + 32];
		j++;
		j = j % 16 ? j : j + 32;
	}
	return ret;
}

void enable_crypto()
{
	writel((0x1 << 4), 0x1e6e2040);
	udelay(300);
	writel((0x1 << 24), 0x1e6e2084);
	writel((0x1 << 13), 0x1e6e2084);
	mdelay(30);
	writel((0x1 << 4), 0x1e6e2044);
}
