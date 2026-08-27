// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2026 ASPEED Technology Inc.
 *
 * Caliptra-SS MCI (Manageability Controller Interface) mailbox transport.
 * Shared low-level engine (page-select, lock/execute) used by the
 * higher-level command wrappers in cmd/aspeed/cptra_mci.c and
 * drivers/crypto/aspeed/cptra_mci_*.c.
 *
 * Unlike the legacy always-mapped Caliptra mailbox used by cptra_sha.c /
 * cptra_ecdsa.c, the MCI mailbox's CSR block and its data SRAM are not
 * directly addressable: both are aliased onto the same 64KB local window,
 * and an SCU1 register has to be written with a 64KB-aligned page target to
 * select which one is currently visible in that window.
 */

#include <asm/io.h>
#include <config.h>
#include <common.h>
#include <dm.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <u-boot/cptra_mci_mbox.h>

/*
 * SCU1 register offset used to remap the MCI mailbox's 64KB local window,
 * and the page targets written to it. Both the offset and the page-target
 * encoding are specific to how this SoC's ARM-side bus master reaches the
 * MCI block, and differ from the encoding used by other bus masters (e.g.
 * the on-chip MCU's own view of the same hardware uses a different SCU1
 * offset and writes the full, unshifted target address) -- do not reuse
 * these values for a different bus master without re-deriving them.
 */
#define SCU1_CPTRA_SS_AXI_WIN		0x3c4

/*
 * Page targets for SCU1_CPTRA_SS_AXI_WIN, encoded as bits [31:16] of the
 * target address (i.e. the target's 64KB-aligned page number). CSR_PAGE was
 * confirmed on real hardware; SRAM_PAGE is inferred by the same encoding
 * and not yet independently confirmed.
 */
#define CPTRA_MCI_MBOX_SRAM_PAGE	0x2140	/* mailbox data (0x21400000 >> 16) */
#define CPTRA_MCI_MBOX_CSR_PAGE		0x2160	/* mcu_mbox0_csr (0x21600000 >> 16) */

struct cptra_mci_mbox_priv {
	void *regs;
	void *scu_regs;
};

static int cptra_mci_mbox_get_priv(struct cptra_mci_mbox_priv **privp)
{
	struct udevice *dev;
	int ret;

	ret = uclass_get_device_by_driver(UCLASS_MISC, DM_DRIVER_GET(aspeed_cptra_mci_mbox),
					  &dev);
	if (ret)
		return ret;

	*privp = dev_get_priv(dev);

	return 0;
}

static int cptra_mci_mbox_select_page(struct cptra_mci_mbox_priv *priv, u32 page)
{
	writel(page, priv->scu_regs + SCU1_CPTRA_SS_AXI_WIN);

	if (readl(priv->scu_regs + SCU1_CPTRA_SS_AXI_WIN) != page) {
		debug("cptra_mci_mbox: failed to select page 0x%x\n", page);
		return -EIO;
	}

	return 0;
}

static void cptra_mci_mbox_sram_write(struct cptra_mci_mbox_priv *priv,
				      const void *data, u32 len, u32 offset)
{
	const u8 *p8 = data;
	u32 word;
	u32 i;

	for (i = 0; (i + sizeof(u32)) <= len; i += sizeof(u32)) {
		memcpy(&word, p8 + i, sizeof(word));
		writel(word, priv->regs + offset + i);
	}

	if (i < len) {
		word = 0;
		memcpy(&word, p8 + i, len - i);
		writel(word, priv->regs + offset + i);
	}
}

static void cptra_mci_mbox_sram_read(struct cptra_mci_mbox_priv *priv, void *data, u32 len)
{
	u8 *p8 = data;
	u32 word;
	u32 i;

	for (i = 0; (i + sizeof(u32)) <= len; i += sizeof(u32)) {
		word = readl(priv->regs + i);
		memcpy(p8 + i, &word, sizeof(word));
	}

	if (i < len) {
		word = readl(priv->regs + i);
		memcpy(p8 + i, &word, len - i);
	}
}

/*
 * Extends an in-progress checksum accumulator with more bytes. Lets callers
 * whose request is split across multiple buffers (e.g. a small fixed header
 * plus a separate large payload passed to cptra_mci_mbox_execute_sg()) fold
 * each piece in without first copying everything into one contiguous buffer.
 */
u32 cptra_mci_mbox_checksum_ext(u32 checksum, const void *data, u32 len)
{
	const u8 *p8 = data;
	u32 i;

	for (i = 0; i < len; i++)
		checksum -= p8[i];

	return checksum;
}

u32 cptra_mci_mbox_checksum(u32 cmd, const void *data, u32 len)
{
	u32 checksum = cptra_mci_mbox_checksum_ext(0, &cmd, sizeof(cmd));

	return cptra_mci_mbox_checksum_ext(checksum, data, len);
}

/*
 * LOCK reads 0 (free) and atomically latches to locked as a side effect of
 * the read. A second read confirming 1 is required to know the lock
 * actually latched, rather than trusting the first read.
 */
static int cptra_mci_mbox_lock(struct cptra_mci_mbox_priv *priv)
{
	int ret;

	ret = cptra_mci_mbox_select_page(priv, CPTRA_MCI_MBOX_CSR_PAGE);
	if (ret)
		return ret;

	if (readl(priv->regs + CPTRA_MCI_MBOX_LOCK))
		return -EBUSY;

	if (!readl(priv->regs + CPTRA_MCI_MBOX_LOCK)) {
		/*
		 * The first read above already latched the lock as a side
		 * effect, regardless of what this confirmation read reports.
		 * Release it here so a glitched confirmation read doesn't
		 * wedge the mailbox for every later caller.
		 */
		cptra_mci_mbox_select_page(priv, CPTRA_MCI_MBOX_CSR_PAGE);
		writel(0x0, priv->regs + CPTRA_MCI_MBOX_EXECUTE);
		return -EIO;
	}

	return 0;
}

static void cptra_mci_mbox_unlock(struct cptra_mci_mbox_priv *priv)
{
	cptra_mci_mbox_select_page(priv, CPTRA_MCI_MBOX_CSR_PAGE);
	writel(0x0, priv->regs + CPTRA_MCI_MBOX_EXECUTE);
}

struct cptra_mci_mbox_iov {
	const void *base;
	u32 len;
};

/*
 * Shared implementation. req is scattered across up to two buffers (a small
 * fixed-size header struct plus a separate, possibly large, payload buffer)
 * so callers never need to memcpy a large payload into one contiguous
 * request struct just to hand it to this function.
 */
static int cptra_mci_mbox_execute_iov(u32 cmd, const struct cptra_mci_mbox_iov *iov,
				      int iovcnt, void *resp, u32 resp_buf_len,
				      u32 *resp_len)
{
	struct cptra_mci_mbox_priv *priv;
	u32 sts, dlen, req_len, off;
	ulong start;
	int ret, i;

	req_len = 0;
	for (i = 0; i < iovcnt; i++)
		req_len += iov[i].len;

	if (req_len > CPTRA_MCI_MBOX_SRAM_SIZE)
		return -EINVAL;

	ret = cptra_mci_mbox_get_priv(&priv);
	if (ret)
		return ret;

	start = get_timer(0);
	while ((ret = cptra_mci_mbox_lock(priv)) == -EBUSY) {
		if (get_timer(start) > CPTRA_MCI_MBOX_LOCK_TIMEOUT_MS) {
			debug("cptra_mci_mbox: timed out waiting for lock\n");
			return -ETIMEDOUT;
		}
	}
	if (ret)
		return ret;

	writel(cmd, priv->regs + CPTRA_MCI_MBOX_CMD);
	writel(req_len, priv->regs + CPTRA_MCI_MBOX_DLEN);

	ret = cptra_mci_mbox_select_page(priv, CPTRA_MCI_MBOX_SRAM_PAGE);
	if (ret)
		goto unlock;

	off = 0;
	for (i = 0; i < iovcnt; i++) {
		cptra_mci_mbox_sram_write(priv, iov[i].base, iov[i].len, off);
		off += iov[i].len;
	}

	ret = cptra_mci_mbox_select_page(priv, CPTRA_MCI_MBOX_CSR_PAGE);
	if (ret)
		goto unlock;

	writel(0x1, priv->regs + CPTRA_MCI_MBOX_EXECUTE);

	start = get_timer(0);
	do {
		sts = FIELD_GET(CPTRA_MCI_MBOX_CMD_STATUS_PS,
				readl(priv->regs + CPTRA_MCI_MBOX_CMD_STATUS));

		if (get_timer(start) > CPTRA_MCI_MBOX_CMD_TIMEOUT_MS) {
			debug("cptra_mci_mbox: timed out waiting for cmd 0x%x to complete\n", cmd);
			ret = -ETIMEDOUT;
			goto unlock;
		}
	} while (sts == CPTRA_MCI_MBSTS_CMD_BUSY);

	if (sts == CPTRA_MCI_MBSTS_CMD_FAILURE) {
		debug("cptra_mci_mbox: cmd 0x%x failed\n", cmd);
		ret = -EIO;
		goto unlock;
	}

	dlen = readl(priv->regs + CPTRA_MCI_MBOX_DLEN);
	if (dlen > CPTRA_MCI_MBOX_SRAM_SIZE) {
		debug("cptra_mci_mbox: invalid dlen 0x%x\n", dlen);
		ret = -EIO;
		goto unlock;
	}
	if (dlen > resp_buf_len) {
		debug("cptra_mci_mbox: response 0x%x exceeds buffer 0x%x\n", dlen, resp_buf_len);
		ret = -ENOSPC;
		goto unlock;
	}

	ret = cptra_mci_mbox_select_page(priv, CPTRA_MCI_MBOX_SRAM_PAGE);
	if (ret)
		goto unlock;

	cptra_mci_mbox_sram_read(priv, resp, dlen);

	if (resp_len)
		*resp_len = dlen;

	ret = 0;

unlock:
	cptra_mci_mbox_unlock(priv);

	return ret;
}

int cptra_mci_mbox_execute(u32 cmd, const void *req, u32 req_len,
			   void *resp, u32 resp_buf_len, u32 *resp_len)
{
	struct cptra_mci_mbox_iov iov = {
		.base = req,
		.len = req_len,
	};

	return cptra_mci_mbox_execute_iov(cmd, &iov, 1, resp, resp_buf_len, resp_len);
}

int cptra_mci_mbox_execute_sg(u32 cmd, const void *hdr, u32 hdr_len,
			      const void *data, u32 data_len,
			      void *resp, u32 resp_buf_len, u32 *resp_len)
{
	struct cptra_mci_mbox_iov iov[2] = {
		{ .base = hdr, .len = hdr_len },
		{ .base = data, .len = data_len },
	};

	return cptra_mci_mbox_execute_iov(cmd, iov, 2, resp, resp_buf_len, resp_len);
}

int cptra_mci_get_firmware_version(enum cptra_mci_fw_index index, char *version, size_t len)
{
	struct cptra_mci_fw_version_req req = { .index = index };
	struct cptra_mci_fw_version_resp resp = { 0 };
	u32 resp_len, vlen;
	int ret;

	if (len == 0)
		return -EINVAL;

	req.hdr.chksum = cptra_mci_mbox_checksum(CPTRA_MCI_MBCMD_FIRMWARE_VERSION,
						 &req.index, sizeof(req.index));

	ret = cptra_mci_mbox_execute(CPTRA_MCI_MBCMD_FIRMWARE_VERSION, &req, sizeof(req),
				     &resp, sizeof(resp), &resp_len);
	if (ret)
		return ret;

	if (resp_len < sizeof(resp.hdr))
		return -EIO;

	vlen = min_t(u32, resp.hdr.data_len, sizeof(resp.version));
	vlen = min_t(u32, vlen, len - 1);

	if (resp_len < sizeof(resp.hdr) + vlen)
		return -EIO;

	memset(version, 0, len);
	memcpy(version, resp.version, vlen);

	return 0;
}

/*
 * Direct register read outside the mailbox command/lock/execute protocol --
 * see the comment on the CPTRA_MCI_REG_* / CPTRA_MCI_SOC_IFC_* definitions in
 * cptra_mci_mbox.h. Not covered by the mailbox HW LOCK (that semaphore only
 * arbitrates the mcu_mbox0_csr block, a different page); the only shared
 * resource here is the page-select window itself, which every mailbox
 * transaction already re-selects before use, so leaving it on a read page
 * afterward doesn't disturb a later mailbox call.
 */
int cptra_mci_reg_read(u32 page, u32 offset, u32 *value)
{
	struct cptra_mci_mbox_priv *priv;
	int ret;

	ret = cptra_mci_mbox_get_priv(&priv);
	if (ret)
		return ret;

	ret = cptra_mci_mbox_select_page(priv, page);
	if (ret)
		return ret;

	*value = readl(priv->regs + offset);

	cptra_mci_mbox_select_page(priv, CPTRA_MCI_MBOX_CSR_PAGE);

	return 0;
}

static int cptra_mci_mbox_probe(struct udevice *dev)
{
	struct cptra_mci_mbox_priv *priv = dev_get_priv(dev);
	u32 phandle;
	ofnode node;
	int ret;

	priv->regs = (void *)devfdt_get_addr(dev);
	if (priv->regs == (void *)FDT_ADDR_T_NONE) {
		debug("cannot map Caliptra MCI mailbox registers\n");
		return -ENODEV;
	}

	ret = ofnode_read_u32(dev_ofnode(dev), "aspeed,scu1", &phandle);
	if (ret) {
		debug("cannot get SCU FDT handle\n");
		return -ENODEV;
	}

	node = ofnode_get_by_phandle(phandle);
	if (!ofnode_valid(node)) {
		debug("cannot get SCU FDT node\n");
		return -ENODEV;
	}

	priv->scu_regs = (void *)ofnode_get_addr(node);
	if (priv->scu_regs == (void *)FDT_ADDR_T_NONE) {
		debug("cannot map SCU registers\n");
		return -ENODEV;
	}

	return 0;
}

static const struct udevice_id cptra_mci_mbox_ids[] = {
	{ .compatible = "aspeed,ast2705-cptra-mci-mbox" },
	{ }
};

U_BOOT_DRIVER(aspeed_cptra_mci_mbox) = {
	.name = "aspeed_cptra_mci_mbox",
	.id = UCLASS_MISC,
	.of_match = cptra_mci_mbox_ids,
	.probe = cptra_mci_mbox_probe,
	.priv_auto = sizeof(struct cptra_mci_mbox_priv),
	.flags = DM_FLAG_PRE_RELOC,
};
