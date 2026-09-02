// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2023 Aspeed Technology Inc.
 */

#include <stdlib.h>
#include <common.h>
#include <console.h>
#include <bootretry.h>
#include <cli.h>
#include <command.h>
#include <console.h>
#include <malloc.h>
#include <inttypes.h>
#include <mapmem.h>
#include <asm/arch/platform.h>
#include <asm/io.h>
#include <linux/compiler.h>
#include <linux/iopoll.h>
#include <u-boot/sha256.h>
#include <u-boot/sha512.h>
#include <u-boot/rsa.h>
#include <u-boot/rsa-mod-exp.h>
#include <dm.h>
#include <misc.h>
#include <clk.h>
#include <asm/arch/otp_ast2700.h>

DECLARE_GLOBAL_DATA_PTR;

/***********************
 *                     *
 * OTP regs definition *
 *                     *
 ***********************/
#define OTP_REG_SIZE			0x200

#define OTP_PASSWD			0x349fe38a
#define OTP_CMD_READ			0x23b1e361
#define OTP_CMD_PROG			0x23b1e364
#define OTP_CMD_PROG_MULTI		0x23b1e365
#define OTP_CMD_CMP			0x23b1e363
#define OTP_CMD_BIST			0x23b1e368

#define OTP_CMD_OFFSET			0x20
#ifdef CONFIG_ARCH_ASPEED
/* CA35 */
#define OTP_MASTER			OTP_M1
#else
/* RISCV IBEX */
#define OTP_MASTER			OTP_M0
#endif

#define OTP_KEY				0x0
#define OTP_CMD				(OTP_MASTER * OTP_CMD_OFFSET + 0x4)
#define OTP_WDATA_0			(OTP_MASTER * OTP_CMD_OFFSET + 0x8)
#define OTP_WDATA_1			(OTP_MASTER * OTP_CMD_OFFSET + 0xc)
#define OTP_WDATA_2			(OTP_MASTER * OTP_CMD_OFFSET + 0x10)
#define OTP_WDATA_3			(OTP_MASTER * OTP_CMD_OFFSET + 0x14)
#define OTP_STATUS			(OTP_MASTER * OTP_CMD_OFFSET + 0x18)
#define OTP_ADDR			(OTP_MASTER * OTP_CMD_OFFSET + 0x1c)
#define OTP_RDATA			(OTP_MASTER * OTP_CMD_OFFSET + 0x20)

#define OTP_DBG00			0x0C4
#define OTP_DBG01			0x0C8
#define OTP_MASTER_PID			0x0D0
#define OTP_ECC_EN			0x0D4
#define OTP_CMD_LOCK			0x0D8
#define OTP_SW_RST			0x0DC
#define OTP_SLV_ID			0x0E0
#define OTP_PMC_CQ			0x0E4
#define OTP_FPGA			0x0EC
#define OTP_CLR_FPGA			0x0F0
#define OTP_REGION_ROM_PATCH		0x100
#define OTP_REGION_OTPCFG		0x104
#define OTP_REGION_OTPSTRAP		0x108
#define OTP_REGION_OTPSTRAP_EXT		0x10C
#define OTP_REGION_SECURE0		0x120
#define OTP_REGION_SECURE0_RANGE	0x124
#define OTP_REGION_SECURE1		0x128
#define OTP_REGION_SECURE1_RANGE	0x12C
#define OTP_REGION_SECURE2		0x130
#define OTP_REGION_SECURE2_RANGE	0x134
#define OTP_REGION_SECURE3		0x138
#define OTP_REGION_SECURE3_RANGE	0x13C
#define OTP_REGION_USR0			0x140
#define OTP_REGION_USR0_RANGE		0x144
#define OTP_REGION_USR1			0x148
#define OTP_REGION_USR1_RANGE		0x14C
#define OTP_REGION_USR2			0x150
#define OTP_REGION_USR2_RANGE		0x154
#define OTP_REGION_USR3			0x158
#define OTP_REGION_USR3_RANGE		0x15C
#define OTP_REGION_CALIPTRA_0		0x160
#define OTP_REGION_CALIPTRA_0_RANGE	0x164
#define OTP_REGION_CALIPTRA_1		0x168
#define OTP_REGION_CALIPTRA_1_RANGE	0x16C
#define OTP_REGION_CALIPTRA_2		0x170
#define OTP_REGION_CALIPTRA_2_RANGE	0x174
#define OTP_REGION_CALIPTRA_3		0x178
#define OTP_REGION_CALIPTRA_3_RANGE	0x17C
#define OTP_RBP_SOC_SVN			0x180
#define OTP_RBP_SOC_KEYRETIRE		0x184
#define OTP_RBP_CALIP_SVN		0x188
#define OTP_RBP_CALIP_KEYRETIRE		0x18C
#define OTP_PUF				0x1A0
#define OTP_MASTER_ID			0x1B0
#define OTP_MASTER_ID_EXT		0x1B4
#define OTP_R_MASTER_ID			0x1B8
#define OTP_R_MASTER_ID_EXT		0x1BC
#define OTP_SOC_ECCKEY			0x1C0
#define OTP_SEC_BOOT_EN			0x1C4
#define OTP_SOC_KEY			0x1C8
#define OTP_CALPITRA_MANU_KEY		0x1CC
#define OTP_CALPITRA_OWNER_KEY		0x1D0
#define OTP_FW_ID_LSB			0x1D4
#define OTP_FW_ID_MSB			0x1D8
#define OTP_CALIP_FMC_SVN		0x1DC
#define OTP_CALIP_RUNTIME_SVN0		0x1E0
#define OTP_CALIP_RUNTIME_SVN1		0x1E4
#define OTP_CALIP_RUNTIME_SVN2		0x1E8
#define OTP_CALIP_RUNTIME_SVN3		0x1EC
#define OTP_SVN_WLOCK			0x1F0
#define OTP_INTR_EN			0x200
#define OTP_INTR_STS			0x204
#define OTP_INTR_MID			0x208
#define OTP_INTR_FUNC_INFO		0x20C
#define OTP_INTR_M_INFO			0x210
#define OTP_INTR_R_INFO			0x214

#define OTP_PMC				0x400
#define OTP_DAP				0x500

#define OTP_DAP_CFG_RQ			0x538

/* OTP status: [0] */
#define OTP_STS_IDLE			0x0
#define OTP_STS_BUSY			0x1

/* OTP cmd status: [7:4] */
#define OTP_GET_CMD_STS(x)		(((x) & 0xF0) >> 4)
#define OTP_STS_PASS			0x0
#define OTP_STS_FAIL			0x1
#define OTP_STS_CMP_FAIL		0x2
#define OTP_STS_REGION_FAIL		0x3
#define OTP_STS_MASTER_FAIL		0x4

/*
 * OTP_DBG01 ECC status: [5] single-bit error (corrected), [4:0] ECC syndrome
 *   S[5]=0, S[4:0]=0    -> no error
 *   S[5]=0, S[4:0]!=0   -> dual-bit error in Data or ECC[4:0] (uncorrectable)
 *   S[5]=1, S[4:0]!=0   -> single-bit error in Data or ECC[4:0] (corrected)
 */
#define OTP_ECC_STS_SINGLE_ERR		BIT(5)
#define OTP_ECC_STS_SYNDROME(x)		((x) & GENMASK(4, 0))

/* OTP ECC EN */
#define ECC_ENABLE			0x1
#define ECC_DISABLE			0x0
#define ECCBRP_EN			BIT(0)

#define ROM_REGION_START_ADDR		0x0
#define ROM_REGION_END_ADDR		0x3e0
#define RBP_REGION_START_ADDR		ROM_REGION_END_ADDR
#define RBP_REGION_END_ADDR		0x400
#define CONF_REGION_START_ADDR		RBP_REGION_END_ADDR
#define CONF_REGION_END_ADDR		0x420
#define STRAP_REGION_START_ADDR		CONF_REGION_END_ADDR
#define STRAP_REGION_END_ADDR		0x430
#define STRAPEXT_REGION_START_ADDR	STRAP_REGION_END_ADDR
#define STRAPEXT_REGION_END_ADDR	0x440
#define USER_REGION_START_ADDR		STRAPEXT_REGION_END_ADDR
#define USER_REGION_END_ADDR		0x1000
#define SEC_REGION_START_ADDR		USER_REGION_END_ADDR
#define SEC_REGION_END_ADDR		0x1c00
#define CAL_REGION_START_ADDR		SEC_REGION_END_ADDR
#define CAL_REGION_END_ADDR		0x1f80
#define SW_PUF_REGION_START_ADDR	CAL_REGION_END_ADDR
#define SW_PUF_REGION_END_ADDR		0x1fc0
#define HW_PUF_REGION_START_ADDR	SW_PUF_REGION_END_ADDR
#define HW_PUF_REGION_END_ADDR		0x2000

#define OTP_TIMEOUT_US			10000

/* OTPSTRAP */
#define OTPSTRAP0_ADDR			STRAP_REGION_START_ADDR
#define OTPSTRAP14_ADDR			(OTPSTRAP0_ADDR + 0xe)

#define OTPTOOL_VERSION(a, b, c)	(((a) << 24) + ((b) << 12) + (c))
#define OTPTOOL_VERSION_MAJOR(x)	(((x) >> 24) & 0xff)
#define OTPTOOL_VERSION_PATCHLEVEL(x)	(((x) >> 12) & 0xfff)
#define OTPTOOL_VERSION_SUBLEVEL(x)	((x) & 0xfff)
#define OTPTOOL_COMPT_VERSION		2

#define SCU1_ROM_PATCH_ADDR		(ASPEED_IO_SCU_BASE + 0x180)

enum rom_patch_version {
	OTP_ROM_PATCH_NONE =	0x0,
	OTP_ROM_PATCH_V1 =	0x3,
	OTP_ROM_PATCH_V2 =	0x3276,
	OTP_ROM_PATCH_V3 =	0x3376,
};

enum otp_error_code {
	OTP_SUCCESS,

	/*
	 * Dedicated error codes for OTP_STATUS[7:4] command results, kept
	 * out of the POSIX errno range so callers can tell them apart from
	 * generic I/O errors.
	 */
	OTP_CMD_ERR_BASE = 200,
	OTP_CMD_ERR_FAIL,           /* prog fail or soak limit exceeded */
	OTP_CMD_ERR_CMP_FAIL,       /* compare mismatch */
	OTP_CMD_ERR_REGION_FAIL,    /* region write/read protected */
	OTP_CMD_ERR_MASTER_FAIL,    /* master protection error */
	OTP_ECC_ERR_DUAL,           /* uncorrectable dual-bit ECC error */
};

enum aspeed_otp_master_id {
	OTP_M0 = 0,
	OTP_M1,
	OTP_M2,
	OTP_M3,
	OTP_M4,
	OTP_M5,
	OTP_MID_MAX,
};

#ifdef CONFIG_ARCH_ASPEED
struct otp_region_ecc {
	u32	start;
	u32	end;
	bool	ecc_supported;
	bool	ecc_en;
};
#endif

struct aspeed_otp {
	u8 *base;
	struct clk clk;
	int gbl_ecc_en;
#ifdef CONFIG_ARCH_ASPEED
	struct otp_region_ecc region_ecc_tbl[OTP_REGION_ID_MAX];
#endif
};

#ifdef CONFIG_ARCH_ASPEED
/*
 * Default per-region ECC policy, applied when otp->gbl_ecc_en (force ECC) is
 * off. Copied into each aspeed_otp instance's region_ecc_tbl at probe time,
 * since otp0 (aspeed,ast2700-otp) and otp1 (aspeed,ast1700-otp) are two
 * independent chips and must not share runtime ECC policy state.
 * OTPRBP/OTPSTRAP don't support ECC in hardware, so ecc_supported is false
 * and ecc_en can never be set for them, even by force. OTPSTRAPEXT does
 * support ECC, unlike OTPSTRAP.
 *
 * Only CA35 exposes GET_ECC_POLICY/SET_ECC_POLICY (see aspeed_otp_ioctl()),
 * so RISC-V/IBEX has no way to change the per-region policy at runtime and
 * gets the plain gbl_ecc_en check below instead, to avoid paying for this
 * table in size-constrained SPL builds.
 */
static const struct otp_region_ecc otp_region_ecc_defaults[OTP_REGION_ID_MAX] = {
	[OTP_REGION_ID_ROM]      = { ROM_REGION_START_ADDR,      ROM_REGION_END_ADDR,      true,  true  },
	[OTP_REGION_ID_RBP]      = { RBP_REGION_START_ADDR,      RBP_REGION_END_ADDR,      false, false },
	[OTP_REGION_ID_CFG]      = { CONF_REGION_START_ADDR,     CONF_REGION_END_ADDR,     true,  false },
	[OTP_REGION_ID_STRAP]    = { STRAP_REGION_START_ADDR,    STRAP_REGION_END_ADDR,    false, false },
	[OTP_REGION_ID_STRAPEXT] = { STRAPEXT_REGION_START_ADDR, STRAPEXT_REGION_END_ADDR, true,  false },
	[OTP_REGION_ID_USR]      = { USER_REGION_START_ADDR,     USER_REGION_END_ADDR,     true,  false },
	[OTP_REGION_ID_SEC]      = { SEC_REGION_START_ADDR,      SEC_REGION_END_ADDR,      true,  false },
	[OTP_REGION_ID_CAL]      = { CAL_REGION_START_ADDR,      CAL_REGION_END_ADDR,      true,  false },
	[OTP_REGION_ID_PUF]      = { SW_PUF_REGION_START_ADDR,   HW_PUF_REGION_END_ADDR,   true,  true  },
};

static bool otp_region_ecc_active(struct aspeed_otp *otp, u32 offset)
{
	struct otp_region_ecc *region;
	int i;

	for (i = 0; i < OTP_REGION_ID_MAX; i++) {
		region = &otp->region_ecc_tbl[i];
		if (offset < region->start || offset >= region->end)
			continue;

		if (!region->ecc_supported)
			return false;

		return otp->gbl_ecc_en || region->ecc_en;
	}

	return false;
}
#else
static bool otp_region_ecc_active(struct aspeed_otp *otp, u32 offset)
{
	return otp->gbl_ecc_en;
}
#endif

static void otp_unlock(struct udevice *dev)
{
	struct aspeed_otp *otp = dev_get_priv(dev);

	writel(OTP_PASSWD, otp->base + OTP_KEY);
}

static int wait_complete(struct udevice *dev)
{
	struct aspeed_otp *otp = dev_get_priv(dev);
	int ret;
	u32 val;
	u32 cmd_sts;
	u32 addr;

	ret = readl_poll_timeout(otp->base + OTP_STATUS, val,
				 !(val & OTP_STS_BUSY), OTP_TIMEOUT_US);
	if (ret) {
		printf("\n%s: timeout. sts:0x%x\n", __func__, val);
		return ret;
	}

	addr = readl(otp->base + OTP_ADDR);

	cmd_sts = OTP_GET_CMD_STS(val);
	switch (cmd_sts) {
	case OTP_STS_PASS:
		return 0;
	case OTP_STS_FAIL:
		printf("\n%s: prog fail or soak limit exceeded at addr 0x%x\n", __func__, addr);
		return -OTP_CMD_ERR_FAIL;
	case OTP_STS_CMP_FAIL:
		printf("\n%s: compare mismatch at addr 0x%x\n", __func__, addr);
		return -OTP_CMD_ERR_CMP_FAIL;
	case OTP_STS_REGION_FAIL:
		printf("\n%s: region write/read protected at addr 0x%x\n", __func__, addr);
		return -OTP_CMD_ERR_REGION_FAIL;
	case OTP_STS_MASTER_FAIL:
		printf("\n%s: master protection error at addr 0x%x\n", __func__, addr);
		return -OTP_CMD_ERR_MASTER_FAIL;
	default:
		printf("\n%s: unknown cmd sts:0x%x\n", __func__, cmd_sts);
		return -EIO;
	}
}

#ifdef CONFIG_ARCH_ASPEED
static int otp_check_ecc_status(struct udevice *dev)
{
	struct aspeed_otp *otp = dev_get_priv(dev);
	u32 status, addr, syndrome;

	status = readl(otp->base + OTP_DBG01);
	syndrome = OTP_ECC_STS_SYNDROME(status);

	if (!syndrome)
		return 0;

	addr = readl(otp->base + OTP_ADDR);

	if (status & OTP_ECC_STS_SINGLE_ERR) {
		/*
		 * printf("\n%s: single-bit ECC error corrected, addr:0x%x, syndrome:0x%x\n",
		 *        __func__, addr, syndrome);
		 */
		return 0;
	}

	printf("\n%s: uncorrectable dual-bit ECC error, addr:0x%x, syndrome:0x%x\n",
	       __func__, addr, syndrome);
	return -OTP_ECC_ERR_DUAL;
}
#endif

static void otp_ecc_cfg(struct udevice *dev, bool ecc_en, bool auto_cfg)
{
	struct aspeed_otp *otp = dev_get_priv(dev);

	writel(ecc_en, otp->base + OTP_ECC_EN);
#ifdef CONFIG_ARCH_ASPEED
	bool self_cfg = ecc_en && !auto_cfg;

	/* Self config or auto config */
	writel(self_cfg ? 0x4 : 0x0, otp->base + OTP_PMC_CQ);
	/* Clearing OTP_PMC_CQ auto-reverts OTP_DAP_CFG_RQ, no explicit write needed to disable */
	if (self_cfg)
		writel(0x40008, otp->base + OTP_DAP_CFG_RQ);
#endif
}

static int otp_read_data(struct udevice *dev, u32 offset, u16 *data)
{
	struct aspeed_otp *otp = dev_get_priv(dev);
	int ret;
	bool ecc_en = otp_region_ecc_active(otp, offset);

	writel(offset, otp->base + OTP_ADDR);
	otp_ecc_cfg(dev, ecc_en, false);

	writel(OTP_CMD_READ, otp->base + OTP_CMD);
	ret = wait_complete(dev);
	if (!ret)
		data[0] = readl(otp->base + OTP_RDATA);

#ifdef CONFIG_ARCH_ASPEED
	if (!ret && ecc_en)
		ret = otp_check_ecc_status(dev);
#endif

	/* Restore ECC config to default */
	otp_ecc_cfg(dev, ecc_en, true);

	return ret;
}

int otp_prog_data(struct udevice *dev, u32 offset, u16 data)
{
	struct aspeed_otp *otp = dev_get_priv(dev);

	writel(otp_region_ecc_active(otp, offset), otp->base + OTP_ECC_EN);
	writel(0x0, otp->base + OTP_PMC_CQ);

	writel(offset, otp->base + OTP_ADDR);
	writel(data, otp->base + OTP_WDATA_0);
	writel(OTP_CMD_PROG, otp->base + OTP_CMD);

	return wait_complete(dev);
}

int otp_prog_multi_data(struct udevice *dev, u32 offset, u32 *data, int count)
{
	struct aspeed_otp *otp = dev_get_priv(dev);

	writel(otp_region_ecc_active(otp, offset), otp->base + OTP_ECC_EN);
	writel(0x0, otp->base + OTP_PMC_CQ);

	writel(offset, otp->base + OTP_ADDR);
	for (int i = 0; i < count; i++)
		writel(data[i], otp->base + OTP_WDATA_0 + 4 * i);

	writel(OTP_CMD_PROG_MULTI, otp->base + OTP_CMD);

	return wait_complete(dev);
}

static int aspeed_otp_read(struct udevice *dev, int offset,
			   void *buf, int size)
{
	int ret;
	u16 *data = buf;

	for (int i = 0; i < size; i++) {
		ret = otp_read_data(dev, offset + i, data + i);
		if (ret) {
			printf("%s: read failed\n", __func__);
			break;
		}
	}

	return ret;
}

static int aspeed_otp_write(struct udevice *dev, int offset,
			    const void *buf, int size)
{
	u32 *data32 = (u32 *)buf;
	u16 *data = (u16 *)buf;
	int ret;

	if (size == 1)
		ret = otp_prog_data(dev, offset, data[0]);
	else
		ret = otp_prog_multi_data(dev, offset, data32, size / 2);

	if (ret)
		printf("%s: prog failed\n", __func__);

	return ret;
}

#ifdef CONFIG_ARCH_ASPEED
static int aspeed_otp_ecc_en(struct udevice *dev)
{
	struct aspeed_otp *otp = dev_get_priv(dev);

	otp->gbl_ecc_en = 1;

	return 0;
}

static int aspeed_otp_ecc_dis(struct udevice *dev)
{
	struct aspeed_otp *otp = dev_get_priv(dev);

	otp->gbl_ecc_en = 0;

	return 0;
}

static int aspeed_otp_ioctl(struct udevice *dev, unsigned long request,
			    void *buf)
{
	struct aspeed_otp *otp = dev_get_priv(dev);
	struct otp_ecc_policy *policy;
	int *data = (int *)buf;
	int ret = 0;

	switch (request) {
	case GET_ECC_STATUS:
		if (otp->gbl_ecc_en == 1)
			*data = OTP_ECC_ENABLE;
		else
			*data = OTP_ECC_DISABLE;
		break;
	case SET_ECC_ENABLE:
		ret = aspeed_otp_ecc_en(dev);
		break;
	case SET_ECC_DISABLE:
		ret = aspeed_otp_ecc_dis(dev);
		break;
	case GET_ECC_POLICY:
		policy = (struct otp_ecc_policy *)buf;
		if (policy->region >= OTP_REGION_ID_MAX)
			return -EINVAL;

		policy->ecc_en = otp->region_ecc_tbl[policy->region].ecc_en;
		policy->ecc_supported = otp->region_ecc_tbl[policy->region].ecc_supported;
		break;
	case SET_ECC_POLICY:
		policy = (struct otp_ecc_policy *)buf;
		if (policy->region >= OTP_REGION_ID_MAX)
			return -EINVAL;
		if (policy->ecc_en && !otp->region_ecc_tbl[policy->region].ecc_supported)
			return -EINVAL;

		otp->region_ecc_tbl[policy->region].ecc_en = !!policy->ecc_en;
		break;
	default:
		break;
	}

	return ret;
}
#endif

static int aspeed_otp_ecc_init(struct udevice *dev)
{
	struct aspeed_otp *otp = dev_get_priv(dev);
	int ret;
	u32 val;

	/* Check cfg_ecc_en */
	writel(0, otp->base + OTP_ECC_EN);
	writel(OTPSTRAP14_ADDR, otp->base + OTP_ADDR);
	writel(OTP_CMD_READ, otp->base + OTP_CMD);
	ret = wait_complete(dev);
	if (ret)
		return ret;

	val = readl(otp->base + OTP_RDATA);
	if (val & 0x1)
		otp->gbl_ecc_en = 0x1;
	else
		otp->gbl_ecc_en = 0x0;

	return 0;
}

static int aspeed_otp_probe(struct udevice *dev)
{
	struct aspeed_otp *otp = dev_get_priv(dev);
	int rc;

	otp->base = (u8 *)devfdt_get_addr(dev);
#ifdef CONFIG_ARCH_ASPEED
	memcpy(otp->region_ecc_tbl, otp_region_ecc_defaults, sizeof(otp->region_ecc_tbl));
#endif

	otp_unlock(dev);

	/* OTP ECC init */
	rc = aspeed_otp_ecc_init(dev);
	if (rc) {
		debug("OTP ECC init failed, rc:%d\n", rc);
		return rc;
	}

	return rc;
}

static int aspeed_otp_remove(struct udevice *dev)
{
	struct aspeed_otp *otp = dev_get_priv(dev);

	clk_disable(&otp->clk);

	return 0;
}

static const struct misc_ops aspeed_otp_ops = {
	.read = aspeed_otp_read,
	.write = aspeed_otp_write,
#ifdef CONFIG_ARCH_ASPEED
	.ioctl = aspeed_otp_ioctl,
#endif
};

static const struct udevice_id aspeed_otp_ids[] = {
	{ .compatible = "aspeed,ast2700-otp" },
	{ .compatible = "aspeed,ast1700-otp" },
	{ }
};

U_BOOT_DRIVER(aspeed_otp) = {
	.name = "aspeed_otp",
	.id = UCLASS_MISC,
	.of_match = aspeed_otp_ids,
	.ops = &aspeed_otp_ops,
	.probe = aspeed_otp_probe,
	.remove	= aspeed_otp_remove,
	.priv_auto = sizeof(struct aspeed_otp),
	.flags = DM_FLAG_PRE_RELOC,
};
