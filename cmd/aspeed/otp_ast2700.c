// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2023 Aspeed Technology Inc.
 */

#include <stdlib.h>
#include <ansi.h>
#include <common.h>
#include <console.h>
#include <bootretry.h>
#include <cli.h>
#include <command.h>
#include <console.h>
#include <malloc.h>
#include <inttypes.h>
#include <mapmem.h>
#include <asm/io.h>
#include <linux/compiler.h>
#include <linux/iopoll.h>
#include <u-boot/sha256.h>
#include <u-boot/sha512.h>
#include <u-boot/rsa.h>
#include <u-boot/rsa-mod-exp.h>
#include <dm.h>
#include <misc.h>
#include <asm/arch/otp_ast2700.h>
#include <display_options.h>
#include <u-boot/otp_ipc.h>
#include "otp_info_ast2700.h"

DECLARE_GLOBAL_DATA_PTR;

enum otp_region {
	OTP_REGION_ROM,
	OTP_REGION_RBP,
	OTP_REGION_CONF,
	OTP_REGION_STRAP,
	OTP_REGION_STRAP_EXT,
	OTP_REGION_STRAP_EXT_VLD,
	OTP_REGION_USER,
	OTP_REGION_SECURE,
	OTP_REGION_CALIPTRA,
	OTP_REGION_PUF,
};

enum otp_status {
	OTP_FAILURE = -2,
	OTP_USAGE = -1,
	OTP_SUCCESS = 0,
	OTP_PROG_SKIP,
};

#define OTP_VER				"1.1.0"

#define ANSI_COLOR_RED			"\e[31m"
#define ANSI_COLOR_GREEN		"\e[32m"
#define ANSI_COLOR_YELLOW		"\e[33m"
#define ANSI_COLOR_CYAN			"\e[36m"
#define ANSI_COLOR_BOLD			"\e[1m"

#define OTP_AST2700_A0			0
#define OTP_AST2700_A1			1
#define OTP_AST2700_A2			2

#define ID0_AST2700A0			0x06000003
#define ID1_AST2700A0			0x06000003
#define ID0_AST2750A1			0x06010003
#define ID1_AST2750A1			0x06010003
#define ID0_AST2700A1			0x06010103
#define ID1_AST2700A1			0x06010103
#define ID0_AST2720A1			0x06010203
#define ID1_AST2720A1			0x06010203
#define ID0_AST2750A2			0x06020003
#define ID1_AST2750A2			0x06020003
#define ID0_AST2700A2			0x06020103
#define ID1_AST2700A2			0x06020103
#define ID0_AST2720A2			0x06020203
#define ID1_AST2720A2			0x06020203

#define SOC_AST2700A0			8
#define SOC_AST2700A1			9
#define SOC_AST2700A2			10

/* OTP memory address from 0x0~0x2000. (unit: Single Word 16-bits) */
/* ----  0x0  -----
 *       ROM
 * ---- 0x3e0 -----
 *       RBP
 * ---- 0x400 -----
 *      CONF
 * ---- 0x420 -----
 *      STRAP
 * ---- 0x430 -----
 *    STRAP EXT
 * ---- 0x440 -----
 *   User Region
 * ---- 0x1000 ----
 *  Secure Region
 * ---- 0x1c00 ----
 *     Caliptra
 * ---- 0x1f80 ----
 *      SW PUF
 * ---- 0x1fc0 ----
 *      HW PUF
 * ---- 0x2000 ----
 */
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
#define SW_PUF_REGION_END_ADDR		0x1fa0
#define HW_PUF_REGION_START_ADDR	SW_PUF_REGION_END_ADDR
#define HW_PUF_REGION_END_ADDR		0x2000

#define OTP_MEM_ADDR_MAX		HW_PUF_REGION_START_ADDR
#define OTP_ROM_REGION_SIZE		(ROM_REGION_END_ADDR - ROM_REGION_START_ADDR)
#define OTP_RBP_REGION_SIZE		(RBP_REGION_END_ADDR - RBP_REGION_START_ADDR)
#define OTP_CONF_REGION_SIZE		(CONF_REGION_END_ADDR - CONF_REGION_START_ADDR)
#define OTP_STRAP_REGION_SIZE		(STRAP_REGION_END_ADDR - STRAP_REGION_START_ADDR - 4)
#define OTP_STRAP_EXT_REGION_SIZE	(STRAPEXT_REGION_END_ADDR - STRAPEXT_REGION_START_ADDR)
#define OTP_USER_REGION_SIZE		(USER_REGION_END_ADDR - USER_REGION_START_ADDR)
#define OTP_SEC_REGION_SIZE		(SEC_REGION_END_ADDR - SEC_REGION_START_ADDR)
#define OTP_CAL_REGION_SIZE		(CAL_REGION_END_ADDR - CAL_REGION_START_ADDR)
#define OTP_PUF_REGION_SIZE		(HW_PUF_REGION_END_ADDR - SW_PUF_REGION_START_ADDR)
#define OTP_SWPUF_REGION_SIZE		(SW_PUF_REGION_END_ADDR - SW_PUF_REGION_START_ADDR)

/* OTPRBP */
#define OTPRBP0_ADDR			OTPRBP_START_ADDR
#define OTPRBP1_ADDR			0x1
#define OTPRBP2_ADDR			0x2
#define OTPRBP3_ADDR			0x3
#define OTPRBP4_ADDR			0x4
#define OTPRBP8_ADDR			0x8
#define OTPRBP10_ADDR			0xa
#define OTPRBP18_ADDR			0x12

#define SOC_ECC_KEY_RETIRE		OTPRBP0_ADDR
#define SOC_LMS_KEY_RETIRE		OTPRBP1_ADDR
#define CAL_OWN_KEY_RETURE		OTPRBP3_ADDR
#define SOC_HW_SVN_ADDR			OTPRBP4_ADDR
#define CAL_FMC_HW_SVN_ADDR		OTPRBP8_ADDR
#define CAL_RT_HW_SVN_ADDR		OTPRBP10_ADDR
#define CAL_MANU_ECC_KEY_MASK		OTPRBP18_ADDR

#define OTP_DEVICE_NAME_0		"otp@14c07000"
#define OTP_DEVICE_NAME_1		"otp@30c07000"

#define OTP_MAGIC			"SOCOTP"
#define CHECKSUM_LEN			48
#define OTP_INC_ROM			BIT(31)
#define OTP_INC_RBP			BIT(30)
#define OTP_INC_CONFIG			BIT(29)
#define OTP_INC_STRAP			BIT(28)
#define OTP_INC_STRAP_EXT		BIT(27)
#define OTP_INC_SECURE			BIT(26)
#define OTP_INC_CALIPTRA		BIT(25)
#define OTP_INC_USER			BIT(24)
#define OTP_REGION_SIZE(info)		(((info) >> 16) & 0xffff)
#define OTP_REGION_OFFSET(info)		((info) & 0xffff)
#define OTP_IMAGE_SIZE(info)		((info) & 0xffff)

/* OTP key header format */
#define OTP_KH_NUM			80
#define OTP_KH_KEY_ID(kh)		((kh) & 0xf)
#define OTP_KH_KEY_TYPE(kh)		(((kh) >> 4) & 0x3f)	/* bits 4~9: SOC types 1~6, OEM types 0x08~0x38 */
#define OTP_KH_LAST(kh)			(((kh) >> 15) & 0x1)
#define OTP_KH_OFFSET(kh)		(((kh) >> 16) & 0xfff)

struct otp_header {
	u8	otp_magic[8];
	u32	soc_ver;
	u32	otptool_ver;
	u32	image_info;
	u32	rom_info;
	u32	rbp_info;
	u32	config_info;
	u32	strap_info;
	u32	strap_ext_info;
	u32	secure_info;
	u32	cptra_info;
	u32	user_info;
	u32	checksum_offset;
} __packed;

struct otpstrap_status {
	int value;
	int option_value[6];
	int remain_times;
	int writeable_option;
	int protected;
};

union otp_pro_sts {
	u32 value;
	struct {
		char r_prot_strap_ext : 1;
		char r_prot_rom : 1;
		char r_prot_conf : 1;
		char r_prot_strap : 1;
		char w_prot_strap_ext : 1;
		char w_prot_rom : 1;
		char w_prot_conf : 1;
		char w_prot_strap : 1;
		char retire_option : 1;
		char en_sec_boot : 1;
		char w_prot_rbp : 1;
		char r_prot_cal : 1;
		char w_prot_cal : 1;
		char dis_otp_bist : 1;
		char w_prot_puf : 1;
		char mem_lock : 1;
	} fields;
};

struct otp_info_cb {
	int version;
	char ver_name[3];
	const struct otprbp_info *rbp_info;
	int rbp_info_len;
	const struct otpconf_info *conf_info;
	int conf_info_len;
	const struct otpstrap_info *strap_info;
	int strap_info_len;
	const struct otpstrap_ext_info *strap_ext_info;
	int strap_ext_info_len;
	const struct otpcal_info *cal_info;
	int cal_info_len;
	const struct otpkey_type *key_info;
	int key_info_len;
	union otp_pro_sts pro_sts;
};

static struct otp_info_cb info_cb;

struct otp_image_layout {
	int rom_length;
	int rbp_length;
	int conf_length;
	int strap_length;
	int strap_ext_length;
	int secure_length;
	int cptra_length;
	int user_length;
	u8 *rom;
	u8 *rbp;
	u8 *conf;
	u8 *strap;
	u8 *strap_ext;
	u8 *secure;
	u8 *cptra;
	u8 *user;
};

struct udevice *otp_dev;

static u32 chip_version(void)
{
	u32 revid0, revid1;

	revid0 = readl((u8 *)ASPEED_CPU_REVISION_ID);
	revid1 = readl((u8 *)ASPEED_IO_REVISION_ID);

	if (revid0 == ID0_AST2700A0 && revid1 == ID1_AST2700A0) {
		/* AST2700-A0 */
		return OTP_AST2700_A0;
	} else if ((revid0 == ID0_AST2700A1 && revid1 == ID1_AST2700A1) ||
		   (revid0 == ID0_AST2750A1 && revid1 == ID1_AST2750A1) ||
		   (revid0 == ID0_AST2720A1 && revid1 == ID1_AST2720A1)) {
		/* AST2700-A1 */
		return OTP_AST2700_A1;
	} else if ((revid0 == ID0_AST2700A2 || revid1 == ID1_AST2700A2) ||
		   (revid0 == ID0_AST2750A2 || revid1 == ID1_AST2750A2) ||
		   (revid0 == ID0_AST2720A2 || revid1 == ID1_AST2720A2)) {
		/* AST2700-A2 */
		return OTP_AST2700_A2;
	}

	return OTP_FAILURE;
}

static void buf_print(u8 *buf, int len)
{
	int i;

	printf("      00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n");
	for (i = 0; i < len; i++) {
		if (i % 16 == 0)
			printf("%04X: ", i);
		printf("%02X ", buf[i]);
		if ((i + 1) % 16 == 0)
			printf("\n");
	}
	printf("\n");
}

/*
 * Native OTP controller access takes priority; this only ever returns true
 * when CONFIG_ASPEED_OTP_IPC is enabled AND do_driver_init() already tried
 * and failed to bind the local OTP device (otp_dev left NULL), e.g. because
 * OTP ownership is locked to the BootMCU (master protection).
 */
static bool otp_use_ipc(void)
{
	return IS_ENABLED(CONFIG_ASPEED_OTP_IPC) && !otp_dev;
}

static int otp_read(u32 offset, u16 *data)
{
	if (otp_use_ipc())
		return otp_ipc_read(offset, 1, data);

	return misc_read(otp_dev, offset, data, 1);
}

static int otp_prog(u32 offset, u16 data)
{
	if (otp_use_ipc())
		return otp_ipc_program(offset, 1, &data);

	return misc_write(otp_dev, offset, &data, 1);
}

static int otp_prog_multi(u32 offset, u16 *data, int num)
{
	if (otp_use_ipc())
		return otp_ipc_program(offset, num, data);

	return misc_write(otp_dev, offset, data, num);
}

static int otp_read_rom(u32 offset, u16 *data)
{
	return otp_read(offset + ROM_REGION_START_ADDR, data);
}

static int otp_read_rbp(u32 offset, u16 *data)
{
	return otp_read(offset + RBP_REGION_START_ADDR, data);
}

static int otp_read_conf(u32 offset, u16 *data)
{
	return otp_read(offset + CONF_REGION_START_ADDR, data);
}

static int otp_read_strap(u32 offset, u16 *data)
{
	return otp_read(offset + STRAP_REGION_START_ADDR, data);
}

static int otp_read_strap_ext(u32 offset, u16 *data)
{
	return otp_read(offset + STRAPEXT_REGION_START_ADDR, data);
}

static int otp_read_strap_ext_vld(u32 offset, u16 *data)
{
	return otp_read(offset + STRAPEXT_REGION_START_ADDR + 0x8, data);
}

static int otp_read_data(u32 offset, u16 *data)
{
	return otp_read(offset + USER_REGION_START_ADDR, data);
}

static int otp_read_secure(u32 offset, u16 *data)
{
	return otp_read(offset + SEC_REGION_START_ADDR, data);
}

/*
 * Bulk read starting at absolute OTP address addr. Native access has no
 * per-call size limit; the IPC mailbox message does, so chunk at
 * OTP_IPC_READ_MAX_WORDS there to keep round-trips to a minimum.
 */
static int otp_read_multi(u32 addr, u16 *data, int num)
{
	int chunk;
	int ret;

	if (!otp_use_ipc())
		return misc_read(otp_dev, addr, data, num);

	while (num > 0) {
		chunk = num > OTP_IPC_READ_MAX_WORDS ? OTP_IPC_READ_MAX_WORDS : num;

		ret = otp_ipc_read(addr, chunk, data);
		if (ret)
			return ret;

		addr += chunk;
		data += chunk;
		num -= chunk;
	}

	return 0;
}

static int otp_read_secure_multi(u32 offset, u16 *data, int num)
{
	return otp_read_multi(offset + SEC_REGION_START_ADDR, data, num);
}

static int otp_read_cptra(u32 offset, u16 *data)
{
	return otp_read(offset + CAL_REGION_START_ADDR, data);
}

static int otp_print_rom(u32 offset, int w_count)
{
	int range = OTP_ROM_REGION_SIZE;
	u16 ret[1];
	int rc;

	if (offset + w_count > range)
		return OTP_USAGE;

	printf("ROM_REGION: 0x%x~0x%x\n", offset, offset + w_count);
	for (int i = offset; i < offset + w_count; i++) {
		rc = otp_read(ROM_REGION_START_ADDR + i, &ret[0]);
		if (rc)
			return rc;

		if (i % 8 == 0)
			printf("\n%03X: %04X ", i * 2, ret[0]);
		else
			printf("%04X ", ret[0]);
	}
	printf("\n");

	return OTP_SUCCESS;
}

static int otp_print_rbp(u32 offset, int w_count)
{
	int range = OTP_RBP_REGION_SIZE;
	u16 ret[1];
	int rc;

	if (offset + w_count > range)
		return OTP_USAGE;

	for (int i = offset; i < offset + w_count; i++) {
		rc = otp_read(RBP_REGION_START_ADDR + i, ret);
		if (rc)
			return rc;

		printf("OTPRBP0x%X: 0x%04X\n", i, ret[0]);
	}
	printf("\n");

	return OTP_SUCCESS;
}

static int otp_print_conf(u32 offset, int w_count)
{
	int range = OTP_CONF_REGION_SIZE;
	u16 ret[1];
	int rc;

	if (offset + w_count > range)
		return OTP_USAGE;

	for (int i = offset; i < offset + w_count; i++) {
		rc = otp_read(CONF_REGION_START_ADDR + i, ret);
		if (rc)
			return rc;

		printf("OTPCFG0x%X: 0x%04X\n", i, ret[0]);
	}
	printf("\n");

	return OTP_SUCCESS;
}

static int otp_print_strap(u32 offset, int w_count)
{
	int range = 12;	/* 32-bit * 6 / 16 (per word) */
	u16 ret[1];
	int rc;

	if (offset + w_count > range)
		return OTP_USAGE;

	for (int i = offset; i < offset + w_count; i++) {
		rc = otp_read(STRAP_REGION_START_ADDR + 2 + i, ret);
		if (rc)
			return rc;

		printf("OTPSTRAP0x%X: 0x%04X\n", i, ret[0]);
	}
	printf("\n");

	return OTP_SUCCESS;
}

static int otp_print_strap_pro(u32 offset, int w_count)
{
	int range = 2;	/* 32-bit / 16 (per word) */
	u16 ret[1];
	int rc;

	if (offset + w_count > range)
		return OTP_USAGE;

	for (int i = offset; i < offset + w_count; i++) {
		rc = otp_read(STRAP_REGION_START_ADDR + i, ret);
		if (rc)
			return rc;

		printf("OTPSTRAP_PRO0x%X: 0x%04X\n", i, ret[0]);
	}
	printf("\n");

	return OTP_SUCCESS;
}

static int otp_print_strap_ext(u32 offset, int w_count)
{
	int range = (OTP_STRAP_EXT_REGION_SIZE) / 2;
	u16 ret[1];
	int rc;

	if (offset + w_count > range)
		return OTP_USAGE;

	for (int i = offset; i < offset + w_count; i++) {
		rc = otp_read(STRAPEXT_REGION_START_ADDR + i, ret);
		if (rc)
			return rc;

		printf("OTPSTRAPEXT0x%X: 0x%04X\n", i, ret[0]);
	}
	printf("\n");

	return OTP_SUCCESS;
}

static int otp_print_strap_ext_valid(u32 offset, int w_count)
{
	int range = (OTP_STRAP_EXT_REGION_SIZE) / 2;
	u16 ret[1];
	int rc;

	if (offset + w_count > range)
		return OTP_USAGE;

	for (int i = offset; i < offset + w_count; i++) {
		rc = otp_read(STRAPEXT_REGION_START_ADDR + 0x8 + i, ret);
		if (rc)
			return rc;

		printf("OTPSTRAPEXT_VLD0x%X: 0x%04X\n", i, ret[0]);
	}
	printf("\n");

	return OTP_SUCCESS;
}

static int otp_print_user_data(u32 offset, int w_count)
{
	int range = OTP_USER_REGION_SIZE;
	u16 ret[1];
	int rc;

	if (offset + w_count > range)
		return OTP_USAGE;

	printf("User Region: 0x%x~0x%x\n", offset, offset + w_count);
	for (int i = offset; i < offset + w_count; i++) {
		rc = otp_read(USER_REGION_START_ADDR + i, &ret[0]);
		if (rc)
			return rc;

		if (i % 8 == 0)
			printf("\n%03X: %04X ", i * 2, ret[0]);
		else
			printf("%04X ", ret[0]);
	}
	printf("\n");

	return OTP_SUCCESS;
}

static int otp_print_sec_data(u32 offset, int w_count)
{
	int range = OTP_SEC_REGION_SIZE;
	u16 ret;
	int rc;

	if (offset + w_count > range)
		return OTP_USAGE;

	printf("Secure Region: 0x%x~0x%x\n", offset, offset + w_count);
	for (int i = offset; i < offset + w_count; i++) {
		rc = otp_read(SEC_REGION_START_ADDR + i, &ret);
		if (rc)
			return rc;

		if (i % 8 == 0)
			printf("\n%03X: %04X ", i * 2, ret);
		else
			printf("%04X ", ret);
	}
	printf("\n");

	return OTP_SUCCESS;
}

static int otp_print_cptra(u32 offset, int w_count)
{
	int range = OTP_CAL_REGION_SIZE;
	u16 ret[1];
	int rc;

	if (offset + w_count > range)
		return OTP_USAGE;

	printf("Caliptra Region: 0x%x~0x%x\n", offset, offset + w_count);
	for (int i = offset; i < offset + w_count; i++) {
		rc = otp_read(CAL_REGION_START_ADDR + i, &ret[0]);
		if (rc)
			return rc;

		if (i % 8 == 0)
			printf("\n%03X: %04X ", i * 2, ret[0]);
		else
			printf("%04X ", ret[0]);
	}
	printf("\n");

	return OTP_SUCCESS;
}

static int otp_print_puf(u32 offset, int w_count)
{
	int range = OTP_PUF_REGION_SIZE;
	u16 ret[1];
	int rc;

	if (offset + w_count > range)
		return OTP_USAGE;

	printf("PUF: 0x%x~0x%x\n", offset, offset + w_count);
	for (int i = offset; i < offset + w_count; i++) {
		rc = otp_read(SW_PUF_REGION_START_ADDR + i, &ret[0]);
		if (rc)
			return rc;

		if (i % 8 == 0)
			printf("\n%03X: %04X ", i * 2, ret[0]);
		else
			printf("%04X ", ret[0]);
	}
	printf("\n");

	return OTP_SUCCESS;
}

/*
 * Region boundaries for otp_print_all()'s dump, in address order. All start
 * addresses happen to be 16-byte (8-word) aligned, matching its row size, so
 * a region change always lands exactly on a row boundary.
 */
static const struct {
	const char *name;
	u32 start;
} otp_all_region_tbl[] = {
	{ "ROM",      ROM_REGION_START_ADDR },
	{ "RBP",      RBP_REGION_START_ADDR },
	{ "CONF",     CONF_REGION_START_ADDR },
	{ "STRAP",    STRAP_REGION_START_ADDR },
	{ "STRAPEXT", STRAPEXT_REGION_START_ADDR },
	{ "USER",     USER_REGION_START_ADDR },
	{ "SECURE",   SEC_REGION_START_ADDR },
	{ "CALIPTRA", CAL_REGION_START_ADDR },
	{ "SW_PUF",   SW_PUF_REGION_START_ADDR },
};

/*
 * Raw sequential dump of every OTP word except HW PUF (SOCOTP's one-way
 * cell-unique secret, not meant to ever be printed). All other regions are
 * laid out back-to-back from ROM_REGION_START_ADDR up to
 * HW_PUF_REGION_START_ADDR (== SW_PUF_REGION_END_ADDR), so a single flat
 * read covers ROM/RBP/CONF/STRAP/STRAPEXT/USER/SECURE/CALIPTRA/SW_PUF
 * without needing per-region dispatch.
 */
static int otp_print_all(void)
{
	u16 buf[8];
	u32 addr;
	int remaining, chunk;
	int rc;

	addr = ROM_REGION_START_ADDR;
	remaining = HW_PUF_REGION_START_ADDR - ROM_REGION_START_ADDR;

	printf("WordAddr  ByteAddr  Data\n");
	while (remaining > 0) {
		chunk = remaining > ARRAY_SIZE(buf) ? ARRAY_SIZE(buf) : remaining;

		for (int i = 0; i < ARRAY_SIZE(otp_all_region_tbl); i++) {
			if (addr == otp_all_region_tbl[i].start) {
				printf("-- %s --\n", otp_all_region_tbl[i].name);
				break;
			}
		}

		rc = otp_read_multi(addr, buf, chunk);
		if (rc) {
			printf("OTP read failed at word addr 0x%x\n", addr);
			return OTP_FAILURE;
		}

		printf("0x%04x    0x%04x   ", addr, addr * 2);
		for (int i = 0; i < chunk; i++)
			printf(" %04x", buf[i]);
		printf("\n");

		addr += chunk;
		remaining -= chunk;
	}

	return OTP_SUCCESS;
}

static int otp_prog_data(int mode, int otp_w_offset, int bit_offset,
			 int value, int nconfirm, bool debug)
{
	bool prog_multi = false;
	u32 prog_address;
	u16 data[8];
	u16 read[1];
	int ret = 0;
	int w_count;

	memset(data, 0, sizeof(data));

	switch (mode) {
	case OTP_REGION_ROM:
		otp_read_rom(otp_w_offset, read);
		prog_address = ROM_REGION_START_ADDR + otp_w_offset;
		if (debug)
			printf("Program OTPROM%d[0x%X] = 0x%x...\n", otp_w_offset,
			       bit_offset, value);
		break;
	case OTP_REGION_RBP:
		otp_read_rbp(otp_w_offset, read);

		if (otp_w_offset >= SOC_HW_SVN_ADDR && otp_w_offset < CAL_FMC_HW_SVN_ADDR) {
			prog_multi = true;
			w_count = 4;
			prog_address = RBP_REGION_START_ADDR + SOC_HW_SVN_ADDR;
			data[otp_w_offset - SOC_HW_SVN_ADDR] = value << bit_offset;

		} else if (otp_w_offset >= CAL_FMC_HW_SVN_ADDR && otp_w_offset < CAL_RT_HW_SVN_ADDR) {
			prog_multi = true;
			w_count = 2;
			prog_address = RBP_REGION_START_ADDR + CAL_FMC_HW_SVN_ADDR;
			data[otp_w_offset - CAL_FMC_HW_SVN_ADDR] = value << bit_offset;

		} else if (otp_w_offset >= CAL_RT_HW_SVN_ADDR && otp_w_offset < CAL_MANU_ECC_KEY_MASK) {
			prog_multi = true;
			w_count = 8;
			prog_address = RBP_REGION_START_ADDR + CAL_RT_HW_SVN_ADDR;
			data[otp_w_offset - CAL_RT_HW_SVN_ADDR] = value << bit_offset;

		} else {
			prog_address = RBP_REGION_START_ADDR + otp_w_offset;
		}

		if (debug)
			printf("Program OTPRBP%d[0x%X] = 0x%x...\n", otp_w_offset,
			       bit_offset, value);

		if (prog_multi && debug) {
			printf("Program OTPRBP%d = ", prog_address - RBP_REGION_START_ADDR);
			for (int i = 0; i < w_count; i++)
				printf("0x%04x ", data[i]);
			printf("\n");
		}

		break;
	case OTP_REGION_CONF:
		otp_read_conf(otp_w_offset, read);
		prog_address = CONF_REGION_START_ADDR + otp_w_offset;
		if (debug)
			printf("Program OTPCFG%d[0x%X] = 0x%x...\n", otp_w_offset,
			       bit_offset, value);
		break;
	case OTP_REGION_STRAP:
		otp_read_strap(otp_w_offset, read);
		prog_address = STRAP_REGION_START_ADDR + otp_w_offset;
		if (debug)
			printf("Program OTPSTRAP%d[0x%X] = 0x%x...\n", otp_w_offset,
			       bit_offset, value);
		break;
	case OTP_REGION_STRAP_EXT:
		otp_read_strap_ext(otp_w_offset, read);
		prog_address = STRAPEXT_REGION_START_ADDR + otp_w_offset;
		if (debug)
			printf("Program OTPSTRAPEXT%d[0x%X] = 0x%x...\n", otp_w_offset,
			       bit_offset, value);
		break;
	case OTP_REGION_STRAP_EXT_VLD:
		otp_read_strap_ext_vld(otp_w_offset, read);
		prog_address = STRAPEXT_REGION_START_ADDR + 0x8 + otp_w_offset;
		if (debug)
			printf("Program OTPSTRAPEXT_VLD%d[0x%X] = 0x%x...\n", otp_w_offset,
			       bit_offset, value);
		break;
	case OTP_REGION_USER:
		otp_read_data(otp_w_offset, read);
		prog_address = USER_REGION_START_ADDR + otp_w_offset;
		if (debug)
			printf("Program OTPDATA%d[0x%X] = 0x%x...\n", otp_w_offset,
			       bit_offset, value);
		break;
	case OTP_REGION_SECURE:
		otp_read_secure(otp_w_offset, read);
		prog_address = SEC_REGION_START_ADDR + otp_w_offset;
		if (debug)
			printf("Program OTPSEC%d[0x%X] = 0x%x...\n", otp_w_offset,
			       bit_offset, value);
		break;
	case OTP_REGION_CALIPTRA:
		otp_read_cptra(otp_w_offset, read);
		prog_address = CAL_REGION_START_ADDR + otp_w_offset;
		if (debug)
			printf("Program OTPCAL%d[0x%X] = 0x%x...\n", otp_w_offset,
			       bit_offset, value);
		break;
	default:
		printf("mode 0x%x is not supported\n", mode);
		return OTP_FAILURE;
	}

	if (!nconfirm) {
		printf("type \"YES\" (no quotes) to continue:\n");
		if (!confirm_yesno()) {
			printf(" Aborting\n");
			return OTP_FAILURE;
		}
	}

	if (prog_multi) {
		ret = otp_prog_multi(prog_address, data, w_count);
	} else {
		value = value << bit_offset;
		ret = otp_prog(prog_address, value);
	}

	if (ret) {
		printf("OTP cannot be programmed\n");
		printf("FAILURE\n");
		return OTP_FAILURE;
	}

	if (debug)
		printf("SUCCESS\n");

	return OTP_SUCCESS;
}

static void otp_strap_status(struct otpstrap_status *otpstrap)
{
	int strap_start, strap_end;
	u16 data[2];
	int ret;

	/* Initial otpstrap */
	for (int i = 0; i < 32; i++) {
		otpstrap[i].value = 0;
		otpstrap[i].remain_times = 6;
		otpstrap[i].writeable_option = -1;
		otpstrap[i].protected = 0;
	}

	/* Check OTP strap value */
	strap_start = 2;
	strap_end = 2 + 12;

	for (int i = strap_start; i < strap_end; i += 2) {
		int option = (i - strap_start) / 2;

		otp_read_strap(i, &data[0]);
		otp_read_strap(i + 1, &data[1]);

		for (int j = 0; j < 16; j++) {
			char bit_value = ((data[0] >> j) & 0x1);

			if (bit_value == 0 && otpstrap[j].writeable_option == -1)
				otpstrap[j].writeable_option = option;
			if (bit_value == 1)
				otpstrap[j].remain_times--;
			otpstrap[j].value ^= bit_value;
			otpstrap[j].option_value[option] = bit_value;
		}

		for (int j = 16; j < 32; j++) {
			char bit_value = ((data[1] >> (j - 16)) & 0x1);

			if (bit_value == 0 && otpstrap[j].writeable_option == -1)
				otpstrap[j].writeable_option = option;
			if (bit_value == 1)
				otpstrap[j].remain_times--;
			otpstrap[j].value ^= bit_value;
			otpstrap[j].option_value[option] = bit_value;
		}
	}

	/* Check OTP strap write protect */
	ret = otp_read_strap(0, &data[0]);
	ret += otp_read_strap(1, &data[1]);
	if (ret)
		printf("OTP read strap failed, ret=0x%x\n", ret);

	for (int j = 0; j < 16; j++) {
		if (((data[0] >> j) & 0x1) == 1)
			otpstrap[j].protected = 1;
	}

	for (int j = 16; j < 32; j++) {
		if (((data[1] >> (j - 16)) & 0x1) == 1)
			otpstrap[j].protected = 1;
	}

#ifdef DEBUG
	for (int i = 0; i < 32; i++) {
		printf("otpstrap[%d]: value:%d, remain_times:%d, writeable_option:%d, protected:%d\n",
		       i, otpstrap[i].value, otpstrap[i].remain_times,
		       otpstrap[i].writeable_option, otpstrap[i].protected);

		printf("option_value: ");
		for (int j = 0; j < 6; j++)
			printf("%d ", otpstrap[i].option_value[j]);
		printf("\n");
	}
#endif
}

static int otp_print_rbp_info(void)
{
	const struct otprbp_info *rbp_info = info_cb.rbp_info;
	u16 OTPRBP[OTP_RBP_REGION_SIZE];
	u32 w_offset;
	u32 length;
	int ret;

	for (int i = 0; i < OTP_RBP_REGION_SIZE; i++) {
		ret = otp_read_rbp(i, &OTPRBP[i]);
		if (ret) {
			printf("OTP read rbp failed, ret=%d\n", ret);
			return ret;
		}
	}

	printf("W     bit-length Description                               Value\n");
	printf("__________________________________________________________________________\n");
	for (int i = 0; i < info_cb.rbp_info_len; i++) {
		w_offset = rbp_info[i].w_offset;
		length = rbp_info[i].length;

		printf("0x%-4X", w_offset);
		printf("0x%-9X", length);
		printf("%-40s: ", rbp_info[i].information);

		for (int j = 0; j < (length + 15) / 16; j++)
			printf("0x%04x ", OTPRBP[w_offset + j]);
		printf("\n");
	}

	return OTP_SUCCESS;
}

/* Forward declarations: defined later, alongside the rest of the ECC-over-IPC plumbing */
static int otp_ecc_get_status(int *status);
static int otp_get_ecc_policy(u32 region, u32 *ecc_en, u32 *ecc_supported);

static int otp_print_conf_info(void)
{
	const struct otpconf_info *conf_info = info_cb.conf_info;
	u16 OTPCFG[32];
	u32 mask;
	u32 w_offset;
	u32 bit_offset;
	u32 otp_value;
	int ret;
	int global_status;
	u32 region_ecc_en = 0, region_ecc_supported = 0;
	bool conf_ecc_active;

	for (int i = 0; i < 32; i++) {
		ret = otp_read_conf(i, &OTPCFG[i]);
		if (ret) {
			printf("OTP read conf failed, ret=%d\n", ret);
			return ret;
		}
	}

	/*
	 * OTPCFG's ECC enable is a single per-region switch (not per-word);
	 * once it's active, re-programming a word that already has any bit
	 * set risks corrupting its ECC parity, so flag that case per word
	 * below instead of waiting for a program attempt to fail.
	 */
	ret = otp_ecc_get_status(&global_status);
	if (ret) {
		printf("Failed to get global ECC status, ret=%d\n", ret);
		return ret;
	}

	ret = otp_get_ecc_policy(OTP_REGION_ID_CFG, &region_ecc_en, &region_ecc_supported);
	if (ret) {
		printf("Failed to get OTPCFG ECC policy, ret=%d\n", ret);
		return ret;
	}

	conf_ecc_active = region_ecc_supported &&
			   (global_status != OTP_ECC_GLOBAL_DISABLED || region_ecc_en);

	printf("OTPCFG ECC: %s%s" ANSI_COLOR_RESET "\n",
	       conf_ecc_active ? ANSI_COLOR_CYAN : ANSI_COLOR_GREEN,
	       conf_ecc_active ? "active" : "inactive");
	printf("W     BIT        Value       Description                                                              Program Status\n");
	printf("______________________________________________________________________________________________________________________________\n");
	for (int i = 0; i < info_cb.conf_info_len; i++) {
		char desc[80];
		bool must_not_prog;

		w_offset = conf_info[i].w_offset;
		bit_offset = conf_info[i].bit_offset;
		mask = BIT(conf_info[i].length) - 1;
		otp_value = (OTPCFG[w_offset] >> bit_offset) & mask;
		must_not_prog = conf_ecc_active && OTPCFG[w_offset] != 0;

		if (otp_value != conf_info[i].value &&
		    conf_info[i].value != OTP_REG_RESERVED &&
		    conf_info[i].value != OTP_REG_VALUE)
			continue;
		printf("0x%-4X", w_offset);

		if (conf_info[i].length == 1) {
			printf("0x%-9X", conf_info[i].bit_offset);
		} else {
			printf("0x%-2X:0x%-4X",
			       conf_info[i].bit_offset + conf_info[i].length - 1,
			       conf_info[i].bit_offset);
		}
		if (otp_value)
			printf(ANSI_COLOR_BOLD "0x%-10x" ANSI_COLOR_RESET, otp_value);
		else
			printf("0x%-10x", otp_value);

		if (conf_info[i].value == OTP_REG_RESERVED)
			snprintf(desc, sizeof(desc), "Reserved");
		else if (conf_info[i].value == OTP_REG_VALUE)
			snprintf(desc, sizeof(desc), conf_info[i].information, otp_value);
		else
			snprintf(desc, sizeof(desc), "%s", conf_info[i].information);
		printf("%-73s", desc);

		if (must_not_prog)
			printf(ANSI_COLOR_RED "DO NOT PROGRAM" ANSI_COLOR_RESET "\n");
		else
			printf(ANSI_COLOR_GREEN "OK" ANSI_COLOR_RESET "\n");
	}

	return OTP_SUCCESS;
}

static void otp_print_strap_info(void)
{
	const struct otpstrap_info *strap_info = info_cb.strap_info;
	struct otpstrap_status strap_status[32];
	u32 bit_offset;
	u32 length;
	u32 otp_value;
	u32 otp_protect;

	otp_strap_status(strap_status);

	printf("BIT(hex) Value  Remains  Protect  Description\n");
	printf("___________________________________________________________________________________________________\n");

	for (int i = 0; i < info_cb.strap_info_len; i++) {
		otp_value = 0;
		otp_protect = 0;
		bit_offset = strap_info[i].bit_offset;
		length = strap_info[i].length;
		for (int j = 0; j < length; j++) {
			otp_value |= strap_status[bit_offset + j].value << j;
			otp_protect |= strap_status[bit_offset + j].protected << j;
		}

		if (otp_value != strap_info[i].value &&
		    strap_info[i].value != OTP_REG_RESERVED)
			continue;

		for (int j = 0; j < length; j++) {
			u32 bit_value = strap_status[bit_offset + j].value;
			u32 bit_protected = strap_status[bit_offset + j].protected;
			int bit_remain = strap_status[bit_offset + j].remain_times;

			printf("0x%-7X", strap_info[i].bit_offset + j);
			if (bit_value)
				printf(ANSI_COLOR_BOLD "0x%-5X" ANSI_COLOR_RESET, bit_value);
			else
				printf("0x%-5X", bit_value);
			if (bit_remain != 6)
				printf(ANSI_COLOR_BOLD "%-9d" ANSI_COLOR_RESET, bit_remain);
			else
				printf("%-9d", bit_remain);
			if (bit_protected)
				printf(ANSI_COLOR_BOLD "0x%-7X" ANSI_COLOR_RESET, bit_protected);
			else
				printf("0x%-7X", bit_protected);
			if (strap_info[i].value == OTP_REG_RESERVED) {
				printf(" Reserved\n");
				continue;
			}

			if (length == 1) {
				printf(" %s\n", strap_info[i].information);
				continue;
			}

			if (j == 0)
				printf("/%s\n", strap_info[i].information);
			else if (j == length - 1)
				printf("\\ \"\n");
			else
				printf("| \"\n");
		}
	}
}

static int otp_strap_bit_confirm(struct otpstrap_status *otpstrap, int offset, int value, int pbit)
{
	int prog_flag = 0;

	printf("OTPSTRAP[0x%X]:\n", offset);

	if (value == otpstrap->value) {
		if (!pbit) {
			printf("\tThe value is same as before, skip it.\n");
			return OTP_PROG_SKIP;
		}
		printf("\tThe value is same as before.\n");

	} else {
		prog_flag = 1;
	}

	if (otpstrap->protected == 1 && prog_flag) {
		printf("\tThis bit is protected and is not writable\n");
		return OTP_FAILURE;
	}

	if (otpstrap->remain_times == 0 && prog_flag) {
		printf("\tThis bit has no remaining chance to write.\n");
		return OTP_FAILURE;
	}

	if (pbit == 1)
		printf("\tThis bit will be protected and become non-writable.\n");

	if (prog_flag)
		printf("\tWrite 1 to OTPSTRAP[0x%X] OPTION[0x%X], that value becomes from 0x%X to 0x%X.\n",
		       offset, otpstrap->writeable_option, otpstrap->value, otpstrap->value ^ 1);

	return OTP_SUCCESS;
}

static void otp_print_strap_ext_info(void)
{
	const struct otpstrap_ext_info *strap_ext_info = info_cb.strap_ext_info;
	u32 bit_offset;
	u32 otp_value, otp_vld;
	u32 length;
	u16 data[8];
	u16 vld[8];
	int ret;
	int global_status;
	u32 region_ecc_en = 0, region_ecc_supported = 0;
	bool strapext_ecc_active;

	/* Read Flash strap */
	for (int i = 0; i < 8; i++)
		otp_read_strap_ext(i, &data[i]);

	/* Read Flash strap valid */
	for (int i = 0; i < 8; i++)
		otp_read_strap_ext_vld(i, &vld[i]);

	/*
	 * OTPSTRAP_EXT's ECC enable is a single per-region switch (not
	 * per-word); once it's active, re-programming a word that already
	 * has any bit set risks corrupting its ECC parity, so flag that
	 * case per word below instead of waiting for a program attempt to
	 * fail.
	 */
	ret = otp_ecc_get_status(&global_status);
	if (ret) {
		printf("Failed to get global ECC status, ret=%d\n", ret);
		return;
	}

	ret = otp_get_ecc_policy(OTP_REGION_ID_STRAPEXT, &region_ecc_en, &region_ecc_supported);
	if (ret) {
		printf("Failed to get OTPSTRAP_EXT ECC policy, ret=%d\n", ret);
		return;
	}

	strapext_ecc_active = region_ecc_supported &&
			       (global_status != OTP_ECC_GLOBAL_DISABLED || region_ecc_en);

	printf("OTPSTRAP_EXT ECC: %s%s" ANSI_COLOR_RESET "\n",
	       strapext_ecc_active ? ANSI_COLOR_CYAN : ANSI_COLOR_GREEN,
	       strapext_ecc_active ? "active" : "inactive");
	printf("BIT(hex) Value  Valid  Description                                                        Value Status    Valid Status\n");
	printf("____________________________________________________________________________________________________________________________\n");

	for (int i = 0; i < info_cb.strap_ext_info_len; i++) {
		bool must_not_prog_value;
		bool must_not_prog_vld;

		otp_value = 0;
		otp_vld = 0;
		bit_offset = strap_ext_info[i].bit_offset;
		length = strap_ext_info[i].length;

		int w_offset = bit_offset / 16;
		int b_offset = bit_offset % 16;

		otp_value = (data[w_offset] >> b_offset) &
			    GENMASK(length - 1, 0);
		otp_vld = (vld[w_offset] >> b_offset) &
			  GENMASK(length - 1, 0);

		if (otp_value != strap_ext_info[i].value)
			continue;

		/*
		 * Value and Valid are recorded in separate OTP words, so each
		 * carries its own independent ECC-parity programming risk.
		 */
		must_not_prog_value = strapext_ecc_active && data[w_offset] != 0;
		must_not_prog_vld = strapext_ecc_active && vld[w_offset] != 0;

		for (int j = 0; j < length; j++) {
			char desc[80];
			unsigned long bit_value = (otp_value & BIT(j)) >> j;
			unsigned long bit_vld = (otp_vld & BIT(j)) >> j;

			printf("0x%-7X", strap_ext_info[i].bit_offset + j);
			if (bit_value)
				printf(ANSI_COLOR_BOLD "0x%-5lX" ANSI_COLOR_RESET, bit_value);
			else
				printf("0x%-5lX", bit_value);
			if (bit_vld)
				printf(ANSI_COLOR_BOLD "0x%-5lX" ANSI_COLOR_RESET, bit_vld);
			else
				printf("0x%-5lX", bit_vld);

			if (length == 1)
				snprintf(desc, sizeof(desc), " %s", strap_ext_info[i].information);
			else if (j == 0)
				snprintf(desc, sizeof(desc), "/%s", strap_ext_info[i].information);
			else if (j == length - 1)
				snprintf(desc, sizeof(desc), "\\ \"");
			else
				snprintf(desc, sizeof(desc), "| \"");
			printf("%-67s", desc);

			if (must_not_prog_value)
				printf(ANSI_COLOR_RED "%-16s" ANSI_COLOR_RESET, "DO NOT PROGRAM");
			else
				printf(ANSI_COLOR_GREEN "%-16s" ANSI_COLOR_RESET, "OK");

			if (must_not_prog_vld)
				printf(ANSI_COLOR_RED "%s" ANSI_COLOR_RESET "\n", "DO NOT PROGRAM");
			else
				printf(ANSI_COLOR_GREEN "%s" ANSI_COLOR_RESET "\n", "OK");
		}
	}
}

static int otp_patch_prog(u64 addr, u32 offset, u32 size)
{
	int ret = 0;
	u32 val;

	printf("%s: addr:0x%llx, offset:0x%x, size:0x%x\n", __func__,
	       addr, offset, size);

	for (int i = 0; i < size / 2; i++) {
		val = readl((uintptr_t)addr + i * 4);
		printf("read 0x%llx = 0x%x..., prog into OTP addr 0x%x\n",
		       addr + i * 4, val, offset + i * 2);
		ret += otp_prog(offset + i * 2, val & GENMASK(15, 0));
		ret += otp_prog(offset + i * 2 + 1, (val >> 16) & GENMASK(15, 0));
	}

	return ret;
}

static int otp_patch_enable_pre(u16 offset, size_t size)
{
	int ret;

	/* Set location - OTPCFG4[10:1] */
	ret = otp_prog_data(OTP_REGION_CONF, 4, 1, offset, 1, true);
	if (ret) {
		printf("%s: Prog location Failed, ret:0x%x\n", __func__, ret);
		return ret;
	}

	/* Set Size - OTPCFG5[9:0] */
	ret = otp_prog_data(OTP_REGION_CONF, 5, 0, size, 1, true);
	if (ret) {
		printf("%s: Prog size Failed, ret:0x%x\n", __func__, ret);
		return ret;
	}

	/* enable pre_otp_patch_vld - OTPCFG4[0] */
	ret = otp_prog_data(OTP_REGION_CONF, 4, 0, 1, 1, true);
	if (ret) {
		printf("%s: Enable pre_otp_patch_vld Failed, ret:0x%x\n", __func__, ret);
		return ret;
	}

	return 0;
}

static int otp_patch_enable_post(u16 offset, size_t size)
{
	int ret;

	/* Set location - OTPCFG6[10:1] */
	ret = otp_prog_data(OTP_REGION_CONF, 6, 1, offset, 1, true);
	if (ret) {
		printf("%s: Prog location Failed, ret:0x%x\n", __func__, ret);
		return ret;
	}

	/* Set Size - OTPCFG7[9:0] */
	ret = otp_prog_data(OTP_REGION_CONF, 7, 0, size, 1, true);
	if (ret) {
		printf("%s: Prog size Failed, ret:0x%x\n", __func__, ret);
		return ret;
	}

	/* enable pre_otp_patch_vld - OTPCFG6[0] */
	ret = otp_prog_data(OTP_REGION_CONF, 6, 0, 1, 1, true);
	if (ret) {
		printf("%s: Enable post_otp_patch_vld Failed, ret:0x%x\n", __func__, ret);
		return ret;
	}

	return 0;
}

static void do_hash(const void *data, int data_len, const char *algo_name, uint8_t *value)
{
	struct hash_algo *algo;

	if (hash_lookup_algo(algo_name, &algo)) {
		printf("Unsupported hash algorithm\n");
		return;
	}

	algo->hash_func_ws(data, data_len, value, algo->chunk_size);
}

static int otp_verify_image(u8 *src_buf, u32 length, u8 *digest_buf)
{
	u8 digest_ret[48];
	int digest_len;

	do_hash(src_buf, length, "sha384", digest_ret);
	digest_len = 48;

	if (!memcmp(digest_buf, digest_ret, digest_len))
		return OTP_SUCCESS;

	printf("%s: digest should be:\n", __func__);
	buf_print(digest_ret, 48);
	return OTP_FAILURE;
}

static int otp_check_strap_image(struct otp_image_layout *image_layout,
				 struct otpstrap_status *otpstrap)
{
	int bit, pbit, ret;
	int fail = 0;
	u16 *strap;

	strap = (u16 *)image_layout->strap;

	for (int i = 0; i < 32; i++) {
		if (i < 16) {
			bit = (strap[0] >> i) & 0x1;
			pbit = (strap[2] >> i) & 0x1;
		} else {
			bit = (strap[1] >> (i - 16)) & 0x1;
			pbit = (strap[3] >> (i - 16)) & 0x1;
		}

		ret = otp_strap_bit_confirm(&otpstrap[i], i, bit, pbit);

		if (ret == OTP_FAILURE)
			fail = 1;
	}

	if (fail == 1) {
		printf("Input image can't program into OTP, please check.\n");
		return OTP_FAILURE;
	}

	return OTP_SUCCESS;
}

static int otp_print_rom_image(struct otp_image_layout *image_layout)
{
	u32 *buf;
	int size;

	buf = (u32 *)image_layout->rom;
	size = image_layout->rom_length;

	for (int i = 0; i < size / 4; i++) {
		if (i % 4 == 0)
			printf("\n%04x:", i * 4);
		printf(" %08x", buf[i]);
	}
	printf("\n");

	return 0;
}

static int _otp_print_key(u32 header, u32 offset, u8 *data)
{
	const struct otpkey_type *key_info_array = info_cb.key_info;
	struct otpkey_type key_info = { .value = -1 };
	int key_id, key_w_offset, key_offset, key_type;
	int last;
	int i;

	if (!header)
		return -1;

	key_id = OTP_KH_KEY_ID(header);
	key_w_offset = OTP_KH_OFFSET(header);
	key_offset = key_w_offset * 2;
	key_type = OTP_KH_KEY_TYPE(header);
	last = OTP_KH_LAST(header);

	printf("\nKey[%d]:\n", offset);
	printf("Header: %x\n", header);

	for (i = 0; i < info_cb.key_info_len; i++) {
		if (key_type == key_info_array[i].value) {
			key_info = key_info_array[i];
			break;
		}
	}

	if (i == info_cb.key_info_len) {
		printf("Error: Cannot find the key type\n");
		return -1;
	}

	printf("Key Type: ");
	printf("%s\n", key_info.information);
	printf("Key Number ID: %d\n", key_id);
	printf("Key Word Offset: 0x%x\n", key_w_offset);
	if (last)
		printf("This is the last key\n");

	if (!data)
		return -1;

	printf("Key Value:\n");
	if (key_info.key_type == SOC_ECDSA_PUB) {
		printf("Q.x:\n");
		buf_print(&data[key_offset], 0x30);
		printf("Q.y:\n");
		buf_print(&data[key_offset + 0x30], 0x30);

	} else if (key_info.key_type == SOC_LMS_PUB) {
		printf("tree_type:\n");
		buf_print(&data[key_offset], 0x4);
		printf("otstype:\n");
		buf_print(&data[key_offset + 0x4], 0x4);
		printf("id:\n");
		buf_print(&data[key_offset + 0x8], 0x10);
		printf("digest:\n");
		buf_print(&data[key_offset + 0x18], 0x18);

	} else if (key_info.key_type == CAL_MANU_PUB_HASH) {
		buf_print(&data[key_offset], 0x30);
		printf("Manufacture ECC Key Mask: 0x%x\n", data[key_offset + 0x30]);
		printf("Manufacture LMS Key Mask: 0x%x\n", data[key_offset + 0x34]);

	} else if (key_info.key_type == CAL_OWN_PUB_HASH) {
		buf_print(&data[key_offset], 0x30);

	} else if (key_info.key_type == SOC_VAULT || key_info.key_type == SOC_VAULT_SEED) {
		buf_print(&data[key_offset], 0x20);
	} else if (key_info.key_type == OEM_DATA) {
		buf_print(&data[key_offset], 0x30);
	}

	return 0;
}

static void otp_print_key(u32 *data)
{
	u8 *byte_buf;
	int empty;
	int ret;

	byte_buf = (u8 *)data;
	empty = 1;

	for (int i = 0; i < OTP_KH_NUM; i++) {
		if (data[i] != 0)
			empty = 0;
	}

	if (empty) {
		printf("OTP data header is empty\n");
		return;
	}

	for (int i = 0; i < OTP_KH_NUM; i++)
		ret = _otp_print_key(data[i], i, byte_buf);
}

static void otp_print_key_info(void)
{
	u16 buf[OTP_SEC_REGION_SIZE];
	int ret;

	ret = otp_read_secure_multi(0, buf, OTP_SEC_REGION_SIZE);
	if (ret) {
		printf("OTP read secure failed, ret=%d\n", ret);
		return;
	}

	otp_print_key((u32 *)buf);
}

static int otp_print_secure_image(struct otp_image_layout *image_layout)
{
	u32 *buf;

	buf = (u32 *)image_layout->secure;
	otp_print_key(buf);

	return OTP_SUCCESS;
}

static int otp_print_rbp_image(struct otp_image_layout *image_layout)
{
	const struct otprbp_info *rbp_info = info_cb.rbp_info;
	u16 *OTPRBP = (u16 *)image_layout->rbp;
	u32 w_offset;
	u32 length;

	printf("W     bit-length Description                               Value\n");
	printf("__________________________________________________________________________\n");
	for (int i = 0; i < info_cb.rbp_info_len; i++) {
		w_offset = rbp_info[i].w_offset;
		length = rbp_info[i].length;

		printf("0x%-4X", w_offset);
		printf("0x%-9X", length);
		printf("%-40s: ", rbp_info[i].information);

		for (int j = 0; j < (length + 15) / 16; j++)
			printf("0x%04x ", OTPRBP[w_offset + j]);
		printf("\n");
	}

	return OTP_SUCCESS;
}

static int otp_print_conf_image(struct otp_image_layout *image_layout)
{
	const struct otpconf_info *conf_info = info_cb.conf_info;
	u16 *OTPCFG = (u16 *)image_layout->conf;
	u32 w_offset;
	u32 bit_offset;
	u32 otp_value;
	u32 mask;

	printf("Word    Bit        Value       Description\n");
	printf("__________________________________________________________________________\n");
	for (int i = 0; i < info_cb.conf_info_len; i++) {
		w_offset = conf_info[i].w_offset;
		bit_offset = conf_info[i].bit_offset;
		mask = BIT(conf_info[i].length) - 1;
		otp_value = (OTPCFG[w_offset] >> bit_offset) & mask;

		if (!otp_value)
			continue;

		if (conf_info[i].value != OTP_REG_VALUE && otp_value != conf_info[i].value)
			continue;

		printf("0x%-4X", w_offset);

		if (conf_info[i].length == 1) {
			printf("0x%-9X", conf_info[i].bit_offset);
		} else {
			printf("0x%-2X:0x%-4X",
			       conf_info[i].bit_offset + conf_info[i].length - 1,
			       conf_info[i].bit_offset);
		}
		printf("0x%-10x", otp_value);

		if (conf_info[i].value == OTP_REG_RESERVED) {
			printf("Reserved\n");

		} else if (conf_info[i].value == OTP_REG_VALUE) {
			printf(conf_info[i].information, otp_value);
			printf("\n");

		} else {
			printf("%s\n", conf_info[i].information);
		}
	}

	return OTP_SUCCESS;
}

static int otp_print_strap_image(struct otp_image_layout *image_layout)
{
	const struct otpstrap_info *strap_info = info_cb.strap_info;
	u16 *OTPSTRAP;
	u16 *OTPSTRAP_PRO;
	u32 w_offset;
	u32 bit_offset;
	u32 otp_value;
	u32 otp_protect;
	u32 mask;

	OTPSTRAP = (u16 *)image_layout->strap;
	OTPSTRAP_PRO = OTPSTRAP + 2;

	printf("Bit(hex)   Value       Protect     Description\n");
	printf("__________________________________________________________________________________________\n");

	for (int i = 0; i < info_cb.strap_info_len; i++) {
		if (strap_info[i].bit_offset > 15) {
			w_offset = 1;
			bit_offset = strap_info[i].bit_offset - 16;
		} else {
			w_offset = 0;
			bit_offset = strap_info[i].bit_offset;
		}

		mask = BIT(strap_info[i].length) - 1;
		otp_value = (OTPSTRAP[w_offset] >> bit_offset) & mask;
		otp_protect = (OTPSTRAP_PRO[w_offset] >> bit_offset) & mask;

		if (otp_value != strap_info[i].value)
			continue;

		if (strap_info[i].length == 1) {
			printf("0x%-9X", strap_info[i].bit_offset);
		} else {
			printf("0x%-2X:0x%-4X",
			       strap_info[i].bit_offset + strap_info[i].length - 1,
			       strap_info[i].bit_offset);
		}
		printf("0x%-10x", otp_value);
		printf("0x%-10x", otp_protect);
		printf("%s\n", strap_info[i].information);
	}

	return OTP_SUCCESS;
}

static int otp_print_strap_ext_image(struct otp_image_layout *image_layout)
{
	const struct otpstrap_ext_info *strap_ext_info = info_cb.strap_ext_info;
	u16 *OTPSTRAP_EXT;
	u16 *OTPSTRAP_EXT_VLD;
	u32 w_offset;
	u32 bit_offset;
	u32 otp_value;
	u32 otp_valid;
	u32 mask;

	OTPSTRAP_EXT = (u16 *)image_layout->strap_ext;
	OTPSTRAP_EXT_VLD = OTPSTRAP_EXT + 8;

	printf("Bit(hex)   Value       Valid     Description\n");
	printf("__________________________________________________________________________________________\n");

	for (int i = 0; i < info_cb.strap_ext_info_len; i++) {
		w_offset = strap_ext_info[i].bit_offset / 16;
		bit_offset = strap_ext_info[i].bit_offset % 16;

		mask = BIT(strap_ext_info[i].length) - 1;
		otp_value = (OTPSTRAP_EXT[w_offset] >> bit_offset) & mask;
		otp_valid = (OTPSTRAP_EXT_VLD[w_offset] >> bit_offset) & mask;

		if (!otp_value && !otp_valid)
			continue;

		if (otp_value != strap_ext_info[i].value)
			continue;

		if (strap_ext_info[i].length == 1) {
			printf("0x%-9X", strap_ext_info[i].bit_offset);
		} else {
			printf("0x%-2X:0x%-4X",
			       strap_ext_info[i].bit_offset + strap_ext_info[i].length - 1,
			       strap_ext_info[i].bit_offset);
		}
		printf("0x%-10x", otp_value);
		printf("0x%-10x", otp_valid);
		printf("%s\n", strap_ext_info[i].information);
	}

	return OTP_SUCCESS;
}

static int otp_print_cptra_image(struct otp_image_layout *image_layout)
{
	const struct otpcal_info *cal_info = info_cb.cal_info;
	u16 *OTPCAL = (u16 *)image_layout->cptra;
	u32 w_offset;
	u32 otp_value;
	u32 bit_len;

	printf("Word    Bit-length      Value       Description\n");
	printf("__________________________________________________________________________\n");
	for (int i = 0; i < info_cb.cal_info_len; i++) {
		w_offset = cal_info[i].w_offset;
		bit_len = cal_info[i].length;

		if (bit_len <= 32)
			otp_value = OTPCAL[w_offset] & (BIT(bit_len) - 1);
		else
			otp_value = OTPCAL[w_offset];

		if (cal_info[i].value != OTP_REG_VALUE && otp_value != cal_info[i].value)
			continue;

		printf("0x%-6X0x%-14X0x%-10x", w_offset, bit_len, otp_value);

		if (cal_info[i].value == OTP_REG_RESERVED) {
			printf("Reserved\n");

		} else if (cal_info[i].value == OTP_REG_VALUE) {
			printf(cal_info[i].information, otp_value);
			for (int i = 0; i < bit_len / 16; i++) {
				if (!otp_value)
					break;
				if (i % 8 == 0)
					printf("\n\t\t\t\t%02x: 0x%04x ", i, OTPCAL[w_offset + i]);
				else
					printf("0x%04x ", OTPCAL[w_offset + i]);
			}
			printf("\n");

		} else {
			printf("%s\n", cal_info[i].information);
		}
	}

	return OTP_SUCCESS;
}

static int otp_print_user_image(struct otp_image_layout *image_layout)
{
	u32 *buf;
	int size;

	buf = (u32 *)image_layout->user;
	size = image_layout->user_length;

	for (int i = 0; i < size / 4; i++) {
		if (i % 4 == 0)
			printf("\n%04x:", i * 4);
		printf(" %08x", buf[i]);
	}
	printf("\n");

	return 0;
}

/* Defined later, alongside the rest of the pb ECC-safety helpers */
static int otp_prog_region_ecc_id(int mode, u32 *id);

/* Whether this region's ECC is currently active (global force or its own policy). */
static int otp_region_ecc_active(int mode, bool *active)
{
	u32 region_id;
	int global_status;
	u32 ecc_en = 0, ecc_supported = 0;

	*active = false;

	if (otp_prog_region_ecc_id(mode, &region_id) ||
	    otp_ecc_get_status(&global_status) ||
	    otp_get_ecc_policy(region_id, &ecc_en, &ecc_supported))
		return -1;

	*active = ecc_supported && (global_status != OTP_ECC_GLOBAL_DISABLED || ecc_en);

	return 0;
}

static int otp_prog_image_region(struct otp_image_layout *image_layout, enum otp_region region_type,
				 bool force)
{
	int (*otp_read_func)(u32 offset, u16 *data);
	u16 otp_value;
	u16 *buf;
	int size, w_region_size;
	int ret;
	bool ecc_active;

	otp_region_ecc_active(region_type, &ecc_active);

	switch (region_type) {
	case OTP_REGION_ROM:
		buf = (u16 *)image_layout->rom;
		size = image_layout->rom_length;
		w_region_size = OTP_ROM_REGION_SIZE;
		otp_read_func = otp_read_rom;
		break;
	case OTP_REGION_RBP:
		buf = (u16 *)image_layout->rbp;
		size = image_layout->rbp_length;
		w_region_size = OTP_RBP_REGION_SIZE;
		otp_read_func = otp_read_rbp;
		break;
	case OTP_REGION_CONF:
		buf = (u16 *)image_layout->conf;
		size = image_layout->conf_length;
		w_region_size = OTP_CONF_REGION_SIZE;
		otp_read_func = otp_read_conf;
		break;
	case OTP_REGION_SECURE:
		buf = (u16 *)image_layout->secure;
		size = image_layout->secure_length;
		w_region_size = OTP_SEC_REGION_SIZE;
		otp_read_func = otp_read_secure;
		break;
	case OTP_REGION_CALIPTRA:
		buf = (u16 *)image_layout->cptra;
		size = image_layout->cptra_length;
		w_region_size = OTP_CAL_REGION_SIZE;
		otp_read_func = otp_read_cptra;
		break;
	case OTP_REGION_USER:
		buf = (u16 *)image_layout->user;
		size = image_layout->user_length;
		w_region_size = OTP_USER_REGION_SIZE;
		otp_read_func = otp_read_data;
		break;
	default:
		printf("%s: region type 0x%x is not supported\n", __func__, region_type);
		return OTP_FAILURE;
	}

	if (size != w_region_size * 2) {
		printf("image size is mismatch, size:0x%x, should be:0x%x\n",
		       size, w_region_size * 2);
		return OTP_FAILURE;
	}

	printf("Start Programing...\n");
	for (int i = 0; i < size / 2; i++) {
		if (buf[i] == 0)
			continue;

		otp_read_func(i, &otp_value);
		if (otp_value && otp_value != buf[i]) {
			if (ecc_active && !force) {
				printf("ERROR: OTP region w_offset [0x%x]=0x%x already programmed and this\n",
				       i, otp_value);
				printf("region's ECC is active - programming 0x%x on top of it would corrupt\n",
				       buf[i]);
				printf("its ECC parity. Pass \"f\" to force programming anyway.\n");
				return OTP_FAILURE;
			}
			printf("Warning: OTP region w_offset [0x%x]=0x%x prog to 0x%x\n",
			       i, otp_value, buf[i]);
			printf("continue to program it.\n");
		}

		ret = otp_prog_data(region_type, i, 0, buf[i], 1, false);
		if (ret) {
			printf("%s: Prog Failed, ret:0x%x\n", __func__, ret);
			return ret;
		}
	}
	printf("Done\n");

	return OTP_SUCCESS;
}

static int otp_prog_strap_image(struct otp_image_layout *image_layout,
				struct otpstrap_status *otpstrap)
{
	u32 *strap;
	u32 *strap_pro;
	u32 w_offset;
	int bit, pbit, offset;
	int fail = 0;
	int prog_flag = 0;
	int bit_offset;
	int ret;

	strap = (u32 *)image_layout->strap;
	strap_pro = strap + 1;

	printf("Start Programing...\n");
	for (int i = 0; i < 32; i++) {
		offset = i;
		bit = (strap[0] >> offset) & 0x1;
		bit_offset = i % 16;
		pbit = (strap_pro[0] >> offset) & 0x1;
		if (i < 16)
			w_offset = otpstrap[i].writeable_option * 2 + 2;
		else
			w_offset = otpstrap[i].writeable_option * 2 + 3;

		if (bit == otpstrap[i].value)
			prog_flag = 0;
		else
			prog_flag = 1;

		if (otpstrap[i].protected == 1 && prog_flag) {
			fail = 1;
			printf("Warning: OTPSTRAP[0x%x] is protected, cannot be programmed\n", i);
			continue;
		}
		if (otpstrap[i].remain_times == 0 && prog_flag) {
			fail = 1;
			printf("Warning: OTPSTRAP[0x%x] no remain times\n", i);
			continue;
		}

		if (prog_flag) {
			ret = otp_prog_data(OTP_REGION_STRAP, w_offset, bit_offset, bit, 1, false);
			if (ret)
				return OTP_FAILURE;
		}

		if (pbit) {
			if (i < 16)
				w_offset = 0;
			else
				w_offset = 1;

			ret = otp_prog_data(OTP_REGION_STRAP, w_offset, bit_offset, pbit, 1, false);
			if (ret)
				return OTP_FAILURE;
		}
	}

	if (fail == 1)
		return OTP_FAILURE;

	printf("Done\n");
	return OTP_SUCCESS;
}

static int otp_prog_strap_ext_image(struct otp_image_layout *image_layout, bool force)
{
	u16 *strap_ext;
	u16 *strap_ext_vld;
	u16 current;
	bool ecc_active;
	int ret;

	strap_ext = (u16 *)image_layout->strap_ext;
	strap_ext_vld = strap_ext + 8;

	otp_region_ecc_active(OTP_REGION_STRAP_EXT, &ecc_active);

	/*
	 * One otp_prog_data() call per word, not per bit: STRAP_EXT supports
	 * ECC, and issuing several program calls against the same word would
	 * corrupt its parity the moment ECC is active for this region.
	 */
	printf("Start Programing...\n");
	for (int w_offset = 0; w_offset < 8; w_offset++) {
		if (strap_ext[w_offset]) {
			if (otp_read_strap_ext(w_offset, &current)) {
				if (!force) {
					printf("ERROR: could not read OTPSTRAPEXT word 0x%x to check for an ECC\n",
					       w_offset);
					printf("conflict - refusing to program it blind. Pass \"f\" to force anyway.\n");
					return OTP_FAILURE;
				}
			} else if (current && current != strap_ext[w_offset] &&
				   ecc_active && !force) {
				printf("ERROR: OTPSTRAPEXT word 0x%x=0x%x already programmed and ECC is active -\n",
				       w_offset, current);
				printf("programming 0x%x on top of it would corrupt its ECC parity.\n",
				       strap_ext[w_offset]);
				printf("Pass \"f\" to force programming anyway.\n");
				return OTP_FAILURE;
			}

			ret = otp_prog_data(OTP_REGION_STRAP_EXT, w_offset, 0,
					    strap_ext[w_offset], 1, false);
			if (ret)
				return OTP_FAILURE;
		}

		if (strap_ext_vld[w_offset]) {
			if (otp_read_strap_ext_vld(w_offset, &current)) {
				if (!force) {
					printf("ERROR: could not read OTPSTRAPEXT_VLD word 0x%x to check for an ECC\n",
					       w_offset);
					printf("conflict - refusing to program it blind. Pass \"f\" to force anyway.\n");
					return OTP_FAILURE;
				}
			} else if (current && current != strap_ext_vld[w_offset] &&
				   ecc_active && !force) {
				printf("ERROR: OTPSTRAPEXT_VLD word 0x%x=0x%x already programmed and ECC is\n",
				       w_offset, current);
				printf("active - programming 0x%x on top of it would corrupt its ECC parity.\n",
				       strap_ext_vld[w_offset]);
				printf("Pass \"f\" to force programming anyway.\n");
				return OTP_FAILURE;
			}

			ret = otp_prog_data(OTP_REGION_STRAP_EXT_VLD, w_offset, 0,
					    strap_ext_vld[w_offset], 1, false);
			if (ret)
				return OTP_FAILURE;
		}
	}

	printf("Done\n");
	return OTP_SUCCESS;
}

static int otp_prog_image(phys_addr_t addr, int nconfirm, bool force)
{
	struct otp_image_layout image_layout;
	struct otpstrap_status otpstrap[32];
	struct otp_header *otp_header;
	int image_soc_ver = 0;
	int image_size;
	int ret;
	u8 *checksum;
	u8 *buf;

	otp_header = map_physmem(addr, sizeof(struct otp_header), MAP_WRBACK);
	if (!otp_header) {
		printf("Failed to map physical memory\n");
		return OTP_FAILURE;
	}

	image_size = OTP_IMAGE_SIZE(otp_header->image_info);
	// printf("image_size: 0x%x\n", image_size);
	unmap_physmem(otp_header, MAP_WRBACK);

	buf = map_physmem(addr, image_size + CHECKSUM_LEN, MAP_WRBACK);

	if (!buf) {
		printf("Failed to map physical memory\n");
		return OTP_FAILURE;
	}
	otp_header = (struct otp_header *)buf;
	checksum = buf + otp_header->checksum_offset;

	/* Check Image Magic */
	if (strcmp(OTP_MAGIC, (char *)otp_header->otp_magic) != 0) {
		printf("Image is invalid\n");
		return OTP_FAILURE;
	}

	image_layout.rom_length = OTP_REGION_SIZE(otp_header->rom_info);
	image_layout.rom = buf + OTP_REGION_OFFSET(otp_header->rom_info);

	image_layout.rbp_length = OTP_REGION_SIZE(otp_header->rbp_info);
	image_layout.rbp = buf + OTP_REGION_OFFSET(otp_header->rbp_info);

	image_layout.conf_length = OTP_REGION_SIZE(otp_header->config_info);
	image_layout.conf = buf + OTP_REGION_OFFSET(otp_header->config_info);

	image_layout.strap_length = OTP_REGION_SIZE(otp_header->strap_info);
	image_layout.strap = buf + OTP_REGION_OFFSET(otp_header->strap_info);

	image_layout.strap_ext_length = OTP_REGION_SIZE(otp_header->strap_ext_info);
	image_layout.strap_ext = buf + OTP_REGION_OFFSET(otp_header->strap_ext_info);

	image_layout.secure_length = OTP_REGION_SIZE(otp_header->secure_info);
	image_layout.secure = buf + OTP_REGION_OFFSET(otp_header->secure_info);

	image_layout.cptra_length = OTP_REGION_SIZE(otp_header->cptra_info);
	image_layout.cptra = buf + OTP_REGION_OFFSET(otp_header->cptra_info);

	image_layout.user_length = OTP_REGION_SIZE(otp_header->user_info);
	image_layout.user = buf + OTP_REGION_OFFSET(otp_header->user_info);

	if (otp_header->soc_ver == SOC_AST2700A0) {
		image_soc_ver = OTP_AST2700_A0;
	} else if (otp_header->soc_ver == SOC_AST2700A1) {
		image_soc_ver = OTP_AST2700_A1;
	} else if (otp_header->soc_ver == SOC_AST2700A2) {
		image_soc_ver = OTP_AST2700_A2;
	} else {
		printf("Image SOC Version is not supported\n");
		return OTP_FAILURE;
	}

	if (image_soc_ver != info_cb.version) {
		printf("Image SOC version is not match to HW SOC version\n");
		return OTP_FAILURE;
	}

	// printf("buf addr:0x%llx, checksum addr: 0x%llx\n", buf, checksum);
	ret = otp_verify_image(buf, image_size, checksum);
	if (ret) {
		printf("checksum is invalid\n");
		return OTP_FAILURE;
	}

	if (info_cb.pro_sts.fields.mem_lock) {
		printf("OTP memory is locked\n");
		return OTP_FAILURE;
	}

	ret = 0;
	if (otp_header->image_info & OTP_INC_ROM) {
		if (info_cb.pro_sts.fields.w_prot_rom) {
			printf("OTP rom region is write protected\n");
			ret = -1;
		}
	}

	if (otp_header->image_info & OTP_INC_CONFIG) {
		if (info_cb.pro_sts.fields.w_prot_conf) {
			printf("OTP config region is write protected\n");
			ret = -1;
		}
	}

	if (otp_header->image_info & OTP_INC_STRAP) {
		if (info_cb.pro_sts.fields.w_prot_strap) {
			printf("OTP strap region is write protected\n");
			ret = -1;
		}
		printf("Read OTP Strap Region:\n");
		otp_strap_status(otpstrap);

		printf("Check writable...\n");
		if (otp_check_strap_image(&image_layout, otpstrap) == OTP_FAILURE)
			ret = -1;
	}

	if (otp_header->image_info & OTP_INC_STRAP_EXT) {
		if (info_cb.pro_sts.fields.w_prot_strap_ext) {
			printf("OTP strap extension region is write protected\n");
			ret = -1;
		}
	}

	if (otp_header->image_info & OTP_INC_CALIPTRA) {
		if (info_cb.pro_sts.fields.w_prot_cal) {
			printf("OTP caliptra region is write protected\n");
			ret = -1;
		}
	}

	if (ret == -1)
		return OTP_FAILURE;

	if (!nconfirm) {
		if (otp_header->image_info & OTP_INC_ROM) {
			printf("\nOTP ROM region :\n");
			if (otp_print_rom_image(&image_layout) < 0) {
				printf("OTP print rom error, please check.\n");
				return OTP_FAILURE;
			}
		}
		if (otp_header->image_info & OTP_INC_RBP) {
			printf("\nOTP RBP region :\n");
			if (otp_print_rbp_image(&image_layout) < 0) {
				printf("OTP print rbp error, please check.\n");
				return OTP_FAILURE;
			}
		}
		if (otp_header->image_info & OTP_INC_SECURE) {
			printf("\nOTP secure region :\n");
			if (otp_print_secure_image(&image_layout) < 0) {
				printf("OTP print secure error, please check.\n");
				return OTP_FAILURE;
			}
		}
		if (otp_header->image_info & OTP_INC_CONFIG) {
			printf("\nOTP configuration region :\n");
			if (otp_print_conf_image(&image_layout) < 0) {
				printf("OTP config error, please check.\n");
				return OTP_FAILURE;
			}
		}
		if (otp_header->image_info & OTP_INC_STRAP) {
			printf("\nOTP strap region :\n");
			if (otp_print_strap_image(&image_layout) < 0) {
				printf("OTP strap error, please check.\n");
				return OTP_FAILURE;
			}
		}
		if (otp_header->image_info & OTP_INC_STRAP_EXT) {
			printf("\nOTP strap extension region :\n");
			if (otp_print_strap_ext_image(&image_layout) < 0) {
				printf("OTP strap_ext error, please check.\n");
				return OTP_FAILURE;
			}
		}
		if (otp_header->image_info & OTP_INC_CALIPTRA) {
			printf("\nOTP caliptra region :\n");
			if (otp_print_cptra_image(&image_layout) < 0) {
				printf("OTP caliptra error, please check.\n");
				return OTP_FAILURE;
			}
		}
		if (otp_header->image_info & OTP_INC_USER) {
			printf("\nOTP user region :\n");
			if (otp_print_user_image(&image_layout) < 0) {
				printf("OTP user error, please check.\n");
				return OTP_FAILURE;
			}
		}

		printf("type \"YES\" (no quotes) to continue:\n");
		if (!confirm_yesno()) {
			printf(" Aborting\n");
			return OTP_FAILURE;
		}
	}

	if (otp_header->image_info & OTP_INC_ROM) {
		printf("programing rom region ...\n");
		ret = otp_prog_image_region(&image_layout, OTP_REGION_ROM, force);
		if (ret) {
			printf("Error\n");
			return ret;
		}
		// printf("Done\n");
	}
	if (otp_header->image_info & OTP_INC_RBP) {
		printf("programing rbp region ...\n");
		ret = otp_prog_image_region(&image_layout, OTP_REGION_RBP, force);
		if (ret) {
			printf("Error\n");
			return ret;
		}
		// printf("Done\n");
	}
	if (otp_header->image_info & OTP_INC_SECURE) {
		printf("programing secure region ...\n");
		ret = otp_prog_image_region(&image_layout, OTP_REGION_SECURE, force);
		if (ret) {
			printf("Error\n");
			return ret;
		}
		//printf("Done\n");
	}
	if (otp_header->image_info & OTP_INC_CONFIG) {
		printf("programing configuration region ...\n");
		ret = otp_prog_image_region(&image_layout, OTP_REGION_CONF, force);
		if (ret != 0) {
			printf("Error\n");
			return ret;
		}
		//printf("Done\n");
	}
	if (otp_header->image_info & OTP_INC_STRAP) {
		printf("programing strap region ...\n");
		ret = otp_prog_strap_image(&image_layout, otpstrap);
		if (ret != 0) {
			printf("Error\n");
			return ret;
		}
		//printf("Done\n");
	}
	if (otp_header->image_info & OTP_INC_STRAP_EXT) {
		printf("programing strap extension region ...\n");
		ret = otp_prog_strap_ext_image(&image_layout, force);
		if (ret != 0) {
			printf("Error\n");
			return ret;
		}
		//printf("Done\n");
	}
	if (otp_header->image_info & OTP_INC_CALIPTRA) {
		printf("programing caliptra region ...\n");
		ret = otp_prog_image_region(&image_layout, OTP_REGION_CALIPTRA, force);
		if (ret != 0) {
			printf("Error\n");
			return ret;
		}
		//printf("Done\n");
	}
	if (otp_header->image_info & OTP_INC_USER) {
		printf("programing user region ...\n");
		ret = otp_prog_image_region(&image_layout, OTP_REGION_USER, force);
		if (ret != 0) {
			printf("Error\n");
			return ret;
		}
	}

	return OTP_SUCCESS;
}

/*
 * SHA384 over OTPROM~OTPSEC (all regions except OTPCAL and OTPPUF).
 * Computed from otp-ast2700-cptra-prov-step1.bin (ROM patch v9), with
 * OTPSTRAP words 0xe/0xf (see OTPSTRAP_ECCBRP_W_OFFSET/
 * OTPSTRAP_MT_STAGE_W_OFFSET below) masked to 0 - those two carry the
 * ECC/BRP enable fuse and manufacturing test stage records, which vary
 * independently of the provisioned image, so they're excluded from the
 * hash rather than pinned to one golden value.
 */
static unsigned char otp_rompatch_v9_hash[] =
	"\x99\xa1\x35\x09\x86\x1b\x01\xaa\x2f\x41\x5e\xf5\xd2\xdd\xac\x98"
	"\xca\xcb\xb5\x19\x46\xfe\x91\x80\xe8\x31\x8a\xa4\xec\x2f\x92\x5b"
	"\x28\x90\xec\x15\x31\x43\xca\x5c\x20\x64\xe1\x27\xc0\x9f\x4a\xda";

#define OTP_ROM_PATCH_VERSION_ADDR	0x14c02180
#define OTP_ROM_PATCH_V9		0x3976

/*
 * OTPSTRAP word 0xe: bit0 eccbrp_en (enable ecc/brp, except otpstrap/
 * otprbp), bit1 eccbrp_en_BK (redundant bank copy of bit0) - see
 * otp_ecc_hw_enable(), which burns both bits together.
 */
#define OTPSTRAP_ECCBRP_W_OFFSET	0xe
#define OTPSTRAP_ECCBRP_EN_BIT		BIT(0)
#define OTPSTRAP_ECCBRP_EN_BK_BIT	BIT(1)
#define OTPSTRAP_ECCBRP_BURN_VALUE	(OTPSTRAP_ECCBRP_EN_BIT | OTPSTRAP_ECCBRP_EN_BK_BIT)

/*
 * OTPSTRAP word 0xf: six 2-bit manufacturing test stage records, stage 1
 * at bits[1:0] through stage 6 at bits[11:10]; bits[15:12] reserved.
 */
#define OTPSTRAP_MT_STAGE_W_OFFSET	0xf
#define OTPSTRAP_MT_STAGE_COUNT		6
#define OTPSTRAP_MT_STAGE_FIELD(val, stage)	(((val) >> ((stage) * 2)) & 0x3)

static void otp_print_strap_eccbrp_mt_stage(u16 eccbrp, u16 mt_stage)
{
	printf("OTPSTRAP eccbrp_en (word 0x%x): eccbrp_en=%s  eccbrp_en_BK=%s\n",
	       OTPSTRAP_ECCBRP_W_OFFSET,
	       (eccbrp & OTPSTRAP_ECCBRP_EN_BIT) ? "yes" : "no",
	       (eccbrp & OTPSTRAP_ECCBRP_EN_BK_BIT) ? "yes" : "no");

	printf("OTPSTRAP MT stage records (word 0x%x):\n", OTPSTRAP_MT_STAGE_W_OFFSET);
	for (int stage = 0; stage < OTPSTRAP_MT_STAGE_COUNT; stage++)
		printf("stage%-3d", stage + 1);
	printf("\n");
	for (int stage = 0; stage < OTPSTRAP_MT_STAGE_COUNT; stage++)
		printf("--------");
	printf("\n");
	for (int stage = 0; stage < OTPSTRAP_MT_STAGE_COUNT; stage++)
		printf("0x%-6x", OTPSTRAP_MT_STAGE_FIELD(mt_stage, stage));
	printf("\n");
}

static int otp_test_provision(void)
{
	sha512_context ctx;
	uint8_t digest[SHA384_SUM_LEN];
	u16 buf[OTP_IPC_READ_MAX_WORDS];
	u16 strap_eccbrp = 0, strap_mt_stage = 0;
	u32 eccbrp_addr, mt_stage_addr;
	int remaining, chunk;
	int i, j;

	/* Check ROM patch version */
	int rom_patch_ver = readl(OTP_ROM_PATCH_VERSION_ADDR);

	if (rom_patch_ver != OTP_ROM_PATCH_V9) {
		printf("ROM patch version mismatch: 0x%x, expected v9 (0x%x)\n",
		       rom_patch_ver, OTP_ROM_PATCH_V9);
		return OTP_FAILURE;
	}
	printf("ROM patch version v9 detected\n");

	eccbrp_addr = STRAP_REGION_START_ADDR + OTPSTRAP_ECCBRP_W_OFFSET;
	mt_stage_addr = STRAP_REGION_START_ADDR + OTPSTRAP_MT_STAGE_W_OFFSET;

	/* SHA384 over OTPROM~OTPSEC (all regions except OTPCAL and OTPPUF) */
	sha384_starts(&ctx);
	i = ROM_REGION_START_ADDR;
	remaining = SEC_REGION_END_ADDR - ROM_REGION_START_ADDR;
	while (remaining > 0) {
		chunk = remaining > ARRAY_SIZE(buf) ? ARRAY_SIZE(buf) : remaining;
		if (otp_read_multi(i, buf, chunk) != OTP_SUCCESS) {
			printf("OTP read failed at offset 0x%x\n", i);
			return OTP_FAILURE;
		}

		/* Capture, then mask out before hashing - see comment above
		 * otp_rompatch_v9_hash.
		 */
		if (eccbrp_addr >= (u32)i && eccbrp_addr < (u32)(i + chunk)) {
			strap_eccbrp = buf[eccbrp_addr - i];
			buf[eccbrp_addr - i] = 0;
		}
		if (mt_stage_addr >= (u32)i && mt_stage_addr < (u32)(i + chunk)) {
			strap_mt_stage = buf[mt_stage_addr - i];
			buf[mt_stage_addr - i] = 0;
		}

		sha384_update(&ctx, (uint8_t *)buf, chunk * sizeof(buf[0]));
		i += chunk;
		remaining -= chunk;
	}
	sha384_finish(&ctx, digest);

	if (memcmp(otp_rompatch_v9_hash, digest, SHA384_SUM_LEN) != 0) {
		printf("OTP hash mismatch (OTPROM~OTPSEC)\n");
		printf("expected digest:\n");
		buf_print((u8 *)otp_rompatch_v9_hash, SHA384_SUM_LEN);
		printf("computed digest:\n");
		buf_print(digest, SHA384_SUM_LEN);
		return OTP_FAILURE;
	}
	printf("OTPROM~OTPSEC hash check passed\n");
	otp_print_strap_eccbrp_mt_stage(strap_eccbrp, strap_mt_stage);

	/* OTPCAL per-field validation */
	i = 0;
	remaining = 0x292;
	while (remaining > 0) {
		chunk = remaining > ARRAY_SIZE(buf) ? ARRAY_SIZE(buf) : remaining;
		if (otp_read_multi(CAL_REGION_START_ADDR + i, buf, chunk) != OTP_SUCCESS) {
			printf("OTPCAL read failed at offset 0x%x\n", i);
			return OTP_FAILURE;
		}

		for (j = 0; j < chunk; j++, i++) {
			if (i < 0x12) {
				if (buf[j] != 0) {
					printf("OTPCAL[0x%03x] should be zero but is 0x%04x\n",
					       i, buf[j]);
					return OTP_FAILURE;
				}
			} else if ((i >= 0x12 && i <= 0x2a) ||
				   i == 0x36 ||
				   (i >= 0x38 && i <= 0x3f) ||
				   i == 0x62 ||
				   (i >= 0x262 && i <= 0x291)) {
				if (buf[j] == 0) {
					printf("OTPCAL[0x%03x] should be non-zero but is 0\n", i);
					return OTP_FAILURE;
				}
			}
		}

		remaining -= chunk;
	}
	printf("OTPCAL region is valid\n");

	/* OTPPUF: each 4-byte chunk must be non-zero */
	i = 0;
	remaining = OTP_SWPUF_REGION_SIZE;
	while (remaining > 0) {
		chunk = remaining > ARRAY_SIZE(buf) ? ARRAY_SIZE(buf) : remaining;
		if (otp_read_multi(SW_PUF_REGION_START_ADDR + i, buf, chunk) != OTP_SUCCESS) {
			printf("OTPPUF read failed at offset 0x%x\n", i);
			return OTP_FAILURE;
		}

		for (j = 0; j + 1 < chunk; j += 2) {
			if (buf[j] == 0 && buf[j + 1] == 0) {
				printf("OTPPUF has zero 4-byte chunk at offset 0x%x\n", i + j);
				return OTP_FAILURE;
			}
		}

		i += chunk;
		remaining -= chunk;
	}
	printf("OTPPUF region is valid\n");

	printf("This chip is provisioned successfully !!!\n");

	return OTP_SUCCESS;
}

static int do_otpinfo(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	if (argc != 2 && argc != 3)
		return CMD_RET_USAGE;

	if (!strcmp(argv[1], "rbp"))
		otp_print_rbp_info();
	else if (!strcmp(argv[1], "conf"))
		otp_print_conf_info();
	else if (!strcmp(argv[1], "strap"))
		otp_print_strap_info();
	else if (!strcmp(argv[1], "strap-ext"))
		otp_print_strap_ext_info();
	else if (!strcmp(argv[1], "key"))
		otp_print_key_info();
	else
		return CMD_RET_USAGE;

	return CMD_RET_SUCCESS;
}

static int do_otpread(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	u32 offset, count;
	int ret;

	if (argc == 2 && !strcmp(argv[1], "all")) {
		ret = otp_print_all();
		return ret == OTP_SUCCESS ? CMD_RET_SUCCESS : CMD_RET_FAILURE;
	}

	if (argc == 4) {
		offset = simple_strtoul(argv[2], NULL, 16);
		count = simple_strtoul(argv[3], NULL, 16);
	} else if (argc == 3) {
		offset = simple_strtoul(argv[2], NULL, 16);
		count = 1;
	} else {
		return CMD_RET_USAGE;
	}

	if (!strcmp(argv[1], "rom"))
		ret = otp_print_rom(offset, count);
	else if (!strcmp(argv[1], "rbp"))
		ret = otp_print_rbp(offset, count);
	else if (!strcmp(argv[1], "conf"))
		ret = otp_print_conf(offset, count);
	else if (!strcmp(argv[1], "strap"))
		ret = otp_print_strap(offset, count);
	else if (!strcmp(argv[1], "strap-pro"))
		ret = otp_print_strap_pro(offset, count);
	else if (!strcmp(argv[1], "strap-ext"))
		ret = otp_print_strap_ext(offset, count);
	else if (!strcmp(argv[1], "strap-ext-vld"))
		ret = otp_print_strap_ext_valid(offset, count);
	else if (!strcmp(argv[1], "u-data"))
		ret = otp_print_user_data(offset, count);
	else if (!strcmp(argv[1], "s-data"))
		ret = otp_print_sec_data(offset, count);
	else if (!strcmp(argv[1], "cptra"))
		ret = otp_print_cptra(offset, count);
	else if (!strcmp(argv[1], "puf"))
		ret = otp_print_puf(offset, count);
	else
		return CMD_RET_USAGE;

	if (ret == OTP_SUCCESS)
		return CMD_RET_SUCCESS;
	return CMD_RET_USAGE;
}

static int do_otppatch(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	phys_addr_t addr;
	u32 offset;
	size_t size;
	int ret;

	if (argc != 5)
		return CMD_RET_USAGE;

	/* Drop the cmd */
	argc--;
	argv++;

	if (!strcmp(argv[0], "prog")) {
		addr = simple_strtoul(argv[1], NULL, 16);
		offset = simple_strtoul(argv[2], NULL, 16);
		size = simple_strtoul(argv[3], NULL, 16);

		ret = otp_patch_prog((u64)addr, offset, (u32)size);

	} else if (!strcmp(argv[0], "enable")) {
		offset = simple_strtoul(argv[2], NULL, 16);
		size = simple_strtoul(argv[3], NULL, 16);

		if (!strcmp(argv[1], "pre"))
			ret = otp_patch_enable_pre(offset, size);

		else if (!strcmp(argv[1], "post"))
			ret = otp_patch_enable_post(offset, size);

		else
			return CMD_RET_USAGE;
	}

	if (ret == OTP_SUCCESS)
		return CMD_RET_SUCCESS;
	else if (ret == OTP_FAILURE)
		return CMD_RET_FAILURE;
	else
		return CMD_RET_USAGE;
}

static int do_otpprog(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	phys_addr_t addr;
	int ret;
	int nconfirm = 0;
	bool force = false;
	int next = 1;

	while (argc > next && (!strcmp(argv[next], "o") || !strcmp(argv[next], "f"))) {
		if (!strcmp(argv[next], "o"))
			nconfirm = 1;
		else
			force = true;
		next++;
	}

	if (argc != next + 1)
		return CMD_RET_USAGE;

	addr = simple_strtoul(argv[next], NULL, 16);
	ret = otp_prog_image(addr, nconfirm, force);

	if (ret == OTP_SUCCESS)
		return CMD_RET_SUCCESS;
	else if (ret == OTP_FAILURE)
		return CMD_RET_FAILURE;
	else
		return CMD_RET_USAGE;
}

/* Maps otp_prog_data()'s otp_region to its ECC-policy region ID. */
static int otp_prog_region_ecc_id(int mode, u32 *id)
{
	switch (mode) {
	case OTP_REGION_ROM:
		*id = OTP_REGION_ID_ROM;
		break;
	case OTP_REGION_RBP:
		*id = OTP_REGION_ID_RBP;
		break;
	case OTP_REGION_CONF:
		*id = OTP_REGION_ID_CFG;
		break;
	case OTP_REGION_STRAP:
		*id = OTP_REGION_ID_STRAP;
		break;
	case OTP_REGION_STRAP_EXT:
	case OTP_REGION_STRAP_EXT_VLD:
		*id = OTP_REGION_ID_STRAPEXT;
		break;
	case OTP_REGION_USER:
		*id = OTP_REGION_ID_USR;
		break;
	case OTP_REGION_SECURE:
		*id = OTP_REGION_ID_SEC;
		break;
	case OTP_REGION_CALIPTRA:
		*id = OTP_REGION_ID_CAL;
		break;
	default:
		return -1;
	}

	return 0;
}

/* Reads otp_w_offset's current value for whichever region otp_region selects. */
static int otp_prog_read_current(int mode, int otp_w_offset, u16 *value)
{
	switch (mode) {
	case OTP_REGION_ROM:
		return otp_read_rom(otp_w_offset, value);
	case OTP_REGION_RBP:
		return otp_read_rbp(otp_w_offset, value);
	case OTP_REGION_CONF:
		return otp_read_conf(otp_w_offset, value);
	case OTP_REGION_STRAP:
		return otp_read_strap(otp_w_offset, value);
	case OTP_REGION_STRAP_EXT:
		return otp_read_strap_ext(otp_w_offset, value);
	case OTP_REGION_STRAP_EXT_VLD:
		return otp_read_strap_ext_vld(otp_w_offset, value);
	case OTP_REGION_USER:
		return otp_read_data(otp_w_offset, value);
	case OTP_REGION_SECURE:
		return otp_read_secure(otp_w_offset, value);
	case OTP_REGION_CALIPTRA:
		return otp_read_cptra(otp_w_offset, value);
	default:
		return -1;
	}
}

/*
 * otp pb's safety net against incremental-program ECC corruption: if this
 * word already has data and this region's ECC is active, programming it
 * again would corrupt the parity locked in by whichever write set those
 * bits, with no indication beyond a later ECC read error. Refuse unless the
 * caller passed "f" to force it anyway.
 */
static int otp_pb_check_repeat(int mode, int otp_w_offset, bool force)
{
	u16 current = 0;
	u32 region_id;
	int global_status;
	u32 ecc_en = 0, ecc_supported = 0;

	if (force)
		return OTP_SUCCESS;

	if (otp_prog_read_current(mode, otp_w_offset, &current) || !current)
		return OTP_SUCCESS;

	if (otp_prog_region_ecc_id(mode, &region_id) ||
	    otp_ecc_get_status(&global_status) ||
	    otp_get_ecc_policy(region_id, &ecc_en, &ecc_supported))
		return OTP_SUCCESS;

	if (!ecc_supported || (global_status == OTP_ECC_GLOBAL_DISABLED && !ecc_en))
		return OTP_SUCCESS;

	printf("Warning: word 0x%x already has data 0x%x and this region's ECC is active -\n",
	       otp_w_offset, current);
	printf("programming it again risks corrupting its ECC parity.\n");
	printf("Pass \"f\" to force programming anyway.\n");

	return OTP_FAILURE;
}

/*
 * Both STRAP_EXT and STRAP_EXT_VLD support ECC, so programming the same
 * word more than once would corrupt its parity. For a caller who wants to
 * set several bits in the same word but doesn't already know its final
 * value, collect every bit first, then program each word exactly once -
 * see otp_pb_strap_ext_word() for the non-interactive counterpart.
 */
static int otp_pb_strap_ext_interactive(int mode, int nconfirm, bool force)
{
	const char *label = mode == OTP_REGION_STRAP_EXT_VLD ? "OTPSTRAPEXT_VLD" : "OTPSTRAPEXT";
	u16 word_value[8] = {0};
	bool word_dirty[8] = {false};
	bool any = false;
	int ret;

	printf("Enter %s bit offsets (0x0~0x7f) to program, one at a time.\n", label);
	printf("Type \"end\" when done.\n");

	while (1) {
		unsigned long bit_offset;
		char *endp;
		int len;

		len = cli_readline("bit offset: ");
		if (len < 0) {
			printf(" Aborting\n");
			return OTP_FAILURE;
		}

		if (!strcmp(console_buffer, "end"))
			break;

		bit_offset = simple_strtoul(console_buffer, &endp, 16);
		if (endp == console_buffer || *endp != '\0' || bit_offset >= 128) {
			printf("Invalid bit offset, must be a hex number < 0x80\n");
			continue;
		}

		word_value[bit_offset / 16] |= BIT(bit_offset % 16);
		word_dirty[bit_offset / 16] = true;
		any = true;
		printf("Queued bit 0x%lx (word 0x%lx)\n", bit_offset, bit_offset / 16);
	}

	if (!any) {
		printf("Nothing to program.\n");
		return OTP_SUCCESS;
	}

	printf("\nThe following %s words will be programmed:\n", label);
	for (int w = 0; w < 8; w++) {
		if (word_dirty[w])
			printf("  word 0x%x = 0x%04x\n", w, word_value[w]);
	}

	for (int w = 0; w < 8; w++) {
		if (!word_dirty[w])
			continue;

		ret = otp_pb_check_repeat(mode, w, force);
		if (ret)
			return OTP_FAILURE;
	}

	if (!nconfirm) {
		printf("type \"YES\" (no quotes) to continue:\n");
		if (!confirm_yesno()) {
			printf(" Aborting\n");
			return OTP_FAILURE;
		}
	}

	for (int w = 0; w < 8; w++) {
		if (!word_dirty[w])
			continue;

		ret = otp_prog_data(mode, w, 0, word_value[w], 1, false);
		if (ret)
			return OTP_FAILURE;
	}

	printf("\nProgram successful. Current %s values:\n", label);
	for (int w = 0; w < 8; w++) {
		u16 readback;

		if (!word_dirty[w])
			continue;

		if (otp_prog_read_current(mode, w, &readback))
			printf("  word 0x%x: failed to read back\n", w);
		else
			printf("  word 0x%x = 0x%04x\n", w, readback);
	}

	return OTP_SUCCESS;
}

/*
 * Non-interactive counterpart to otp_pb_strap_ext_interactive(): lets a
 * caller who already knows the word's full target value burn it in one
 * shot. Only a word-aligned bit_offset (a multiple of 16) is accepted so
 * the whole word is always programmed in a single otp_prog_data() call.
 */
static int otp_pb_strap_ext_word(int mode, const char *bit_offset_str, const char *value_str,
				 int nconfirm, bool force)
{
	char *endp;
	u32 bit_offset;
	u32 value;
	int ret;

	bit_offset = simple_strtoul(bit_offset_str, &endp, 16);
	if (endp == bit_offset_str || *endp != '\0' || bit_offset >= 128 ||
	    bit_offset % 16 != 0) {
		printf("bit_offset must be a word-aligned (multiple of 16) hex number < 0x80\n");
		return OTP_USAGE;
	}

	ret = otp_pb_check_repeat(mode, bit_offset / 16, force);
	if (ret)
		return ret;

	value = simple_strtoul(value_str, &endp, 16);
	if (endp == value_str || *endp != '\0')
		return OTP_USAGE;

	return otp_prog_data(mode, bit_offset / 16, 0, value, nconfirm, true);
}

static int do_otppb(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	struct otpstrap_status otpstrap[32];
	bool otpstrap_prot = false;
	int mode = 0;
	int nconfirm = 0;
	/*
	 * u32, not int: these come straight from simple_strtoul() (unsigned
	 * long), and every bounds check below (e.g. "bit_offset >= 32") needs
	 * to run as an unsigned comparison so a huge hex input can't wrap
	 * into a negative value and slip past it.
	 */
	u32 otp_addr = 0;
	u32 bit_offset;
	u32 value;
	int ret;
	/* "f": force past otp_pb_check_repeat()'s incremental-program ECC warning */
	bool force = false;

	if (argc >= 2 && !strcmp(argv[1], "strap-ext")) {
		int next = 2;

		while (argc > next && (!strcmp(argv[next], "o") || !strcmp(argv[next], "f"))) {
			if (!strcmp(argv[next], "o"))
				nconfirm = 1;
			else
				force = true;
			next++;
		}

		if (argc == next)
			ret = otp_pb_strap_ext_interactive(OTP_REGION_STRAP_EXT, nconfirm, force);
		else if (argc == next + 2)
			ret = otp_pb_strap_ext_word(OTP_REGION_STRAP_EXT, argv[next],
						    argv[next + 1], nconfirm, force);
		else
			return CMD_RET_USAGE;

		goto end;
	}

	if (argc >= 2 && !strcmp(argv[1], "strap-ext-vld") &&
	    !(argc >= 3 && !strcmp(argv[2], "all"))) {
		int next = 2;

		while (argc > next && (!strcmp(argv[next], "o") || !strcmp(argv[next], "f"))) {
			if (!strcmp(argv[next], "o"))
				nconfirm = 1;
			else
				force = true;
			next++;
		}

		if (argc == next)
			ret = otp_pb_strap_ext_interactive(OTP_REGION_STRAP_EXT_VLD, nconfirm,
							   force);
		else if (argc == next + 2)
			ret = otp_pb_strap_ext_word(OTP_REGION_STRAP_EXT_VLD, argv[next],
						    argv[next + 1], nconfirm, force);
		else
			return CMD_RET_USAGE;

		goto end;
	}

	if (argc != 3 && argc != 4 && argc != 5 && argc != 6 && argc != 7) {
		printf("%s: argc:%d\n", __func__, argc);
		return CMD_RET_USAGE;
	}

	/* Drop the pb cmd */
	argc--;
	argv++;

	if (!strcmp(argv[0], "conf"))
		mode = OTP_REGION_CONF;
	else if (!strcmp(argv[0], "rbp"))
		mode = OTP_REGION_RBP;
	else if (!strcmp(argv[0], "strap"))
		mode = OTP_REGION_STRAP;
	else if (!strcmp(argv[0], "strap-pro")) {
		mode = OTP_REGION_STRAP;
		otpstrap_prot = true;
	} else if (!strcmp(argv[0], "strap-ext-vld"))
		mode = OTP_REGION_STRAP_EXT_VLD;
	else if (!strcmp(argv[0], "u-data"))
		mode = OTP_REGION_USER;
	else if (!strcmp(argv[0], "s-data"))
		mode = OTP_REGION_SECURE;
	else if (!strcmp(argv[0], "cptra"))
		mode = OTP_REGION_CALIPTRA;
	else
		return CMD_RET_USAGE;

	/* Drop the region cmd */
	argc--;
	argv++;

	while (argc > 0 && (!strcmp(argv[0], "o") || !strcmp(argv[0], "f"))) {
		if (!strcmp(argv[0], "o"))
			nconfirm = 1;
		else
			force = true;
		argc--;
		argv++;
	}

	if (mode == OTP_REGION_STRAP) {
		bit_offset = simple_strtoul(argv[0], NULL, 16);
		value = simple_strtoul(argv[1], NULL, 16);
		if (bit_offset >= 32)
			return CMD_RET_USAGE;
		if (value != 0 && value != 1)
			return CMD_RET_USAGE;

	} else if (mode == OTP_REGION_STRAP_EXT_VLD) {
		/* do_otppb()'s top-level interception only lets "all" [o] [f] reach here */
		int next = 1;

		while (argc > next && (!strcmp(argv[next], "o") || !strcmp(argv[next], "f"))) {
			if (!strcmp(argv[next], "o"))
				nconfirm = 1;
			else
				force = true;
			next++;
		}

		if (argc != next)
			return CMD_RET_USAGE;

		for (int i = 0; i < 8; i++) {
			ret = otp_pb_check_repeat(mode, i, force);
			if (ret)
				goto end;
		}

		if (!nconfirm) {
			printf("This will mark every OTPSTRAPEXT_VLD bit as valid.\n");
			printf("type \"YES\" (no quotes) to continue:\n");
			if (!confirm_yesno()) {
				printf(" Aborting\n");
				ret = OTP_FAILURE;
				goto end;
			}
		}

		for (int i = 0; i < 8; i++) {
			ret = otp_prog_data(mode, i, 0, GENMASK(15, 0), 1, true);
			if (ret)
				goto end;
		}

		goto end;

	} else {
		otp_addr = simple_strtoul(argv[0], NULL, 16);
		bit_offset = simple_strtoul(argv[1], NULL, 16);
		value = simple_strtoul(argv[2], NULL, 16);
		if (bit_offset >= 16)
			return CMD_RET_USAGE;
	}

	/* Check param */
	if (mode == OTP_REGION_RBP) {
		if (otp_addr >= OTP_RBP_REGION_SIZE)
			return CMD_RET_USAGE;

	} else if (mode == OTP_REGION_CONF) {
		if (otp_addr >= OTP_CONF_REGION_SIZE)
			return CMD_RET_USAGE;

	} else if (mode == OTP_REGION_STRAP) {
		if (otpstrap_prot) {
			if (bit_offset < 16) {
				otp_addr = 0;
			} else {
				otp_addr = 1;
				bit_offset -= 16;
			}
		} else {
			// get otpstrap status
			otp_strap_status(otpstrap);

			ret = otp_strap_bit_confirm(&otpstrap[bit_offset], bit_offset, value, 0);
			if (ret != OTP_SUCCESS)
				return ret;

			// assign writable otp address
			if (bit_offset < 16) {
				otp_addr = 2 + otpstrap[bit_offset].writeable_option * 2;
			} else {
				otp_addr = 3 + otpstrap[bit_offset].writeable_option * 2;
				bit_offset -= 16;
			}

			value = 1;
		}

	} else if (mode == OTP_REGION_USER) {
		if (otp_addr >= OTP_USER_REGION_SIZE)
			return CMD_RET_USAGE;

	} else if (mode == OTP_REGION_SECURE) {
		if (otp_addr >= OTP_SEC_REGION_SIZE)
			return CMD_RET_USAGE;

	} else if (mode == OTP_REGION_CALIPTRA) {
		if (otp_addr >= OTP_CAL_REGION_SIZE)
			return CMD_RET_USAGE;
	}

	ret = otp_pb_check_repeat(mode, otp_addr, force);
	if (ret)
		goto end;

	ret = otp_prog_data(mode, otp_addr, bit_offset, value, nconfirm, true);

end:
	if (ret == OTP_SUCCESS)
		return CMD_RET_SUCCESS;
	else if (ret == OTP_FAILURE)
		return CMD_RET_FAILURE;
	else
		return CMD_RET_USAGE;
}

static const struct {
	const char *name;
	u32 id;
} otp_ecc_policy_region_tbl[] = {
	{ "rom",       OTP_REGION_ID_ROM      },
	{ "rbp",       OTP_REGION_ID_RBP      },
	{ "conf",      OTP_REGION_ID_CFG      },
	{ "strap",     OTP_REGION_ID_STRAP    },
	{ "strap-ext", OTP_REGION_ID_STRAPEXT },
	{ "u-data",    OTP_REGION_ID_USR      },
	{ "s-data",    OTP_REGION_ID_SEC      },
	{ "cptra",     OTP_REGION_ID_CAL      },
	{ "puf",       OTP_REGION_ID_PUF      },
};

/*
 * Returns enum otp_ecc_global_status, distinguishing ECC being off from the
 * two ways it can be on: temporarily by software, or latched by a hardware
 * strap that can't be turned back off for the life of the session.
 */
static int otp_ecc_get_status(int *status)
{
	if (otp_use_ipc())
		return otp_ipc_ecc_get_status(status);

	return misc_ioctl(otp_dev, GET_ECC_STATUS, status);
}

/*
 * Native path (SET_ECC_ENABLE/SET_ECC_DISABLE) only flips aspeed_otp's
 * in-RAM sw_gbl_ecc_en (see aspeed_otp_ecc_init()/aspeed_otp_probe() in
 * drivers/soc/aspeed/aspeed-otp.c) for the current u-boot session - it's
 * never written back to OTP hardware, so it doesn't survive reset and
 * has no effect on whatever OS boots next.
 */
static int otp_ecc_set_enable(int enable)
{
	if (otp_use_ipc())
		return otp_ipc_ecc_set_enable(enable);

	return misc_ioctl(otp_dev, enable ? SET_ECC_ENABLE : SET_ECC_DISABLE, NULL);
}

/* Set once otp_ecc_hw_enable() burns the fuse this session, so "status" can
 * flag that the current in-RAM state is stale until the next power cycle.
 */
static bool ecc_hw_burn_pending;

/*
 * Permanently burns the OTPSTRAP ECC-enable fuse, unlike otp_ecc_set_enable()
 * which only flips a RAM flag for the current session. Once burned, every
 * future boot reads otp_hw_gbl_ecc_effect == 1 and global ECC can never be
 * turned off again. Reads the real hardware strap directly rather than any
 * cached gbl_ecc_en-type state, since this must reflect what's actually
 * fused regardless of what has or hasn't been probed this session.
 */
static int otp_ecc_hw_enable(int nconfirm)
{
	u16 val;
	int ret;

	ret = otp_read_strap(OTPSTRAP_ECCBRP_W_OFFSET, &val);
	if (ret) {
		printf("Failed to read OTP strap ECC enable bit\n");
		return CMD_RET_FAILURE;
	}

	if (val & OTPSTRAP_ECCBRP_EN_BIT) {
		printf("OTP strap ECC enable bit is already programmed\n");
		return CMD_RET_SUCCESS;
	}

	printf("WARNING: this permanently burns the OTP strap ECC-enable fuse.\n");
	printf("Global ECC will be forced on by hardware on every future boot and can never be turned off again.\n");

	if (!nconfirm) {
		printf("type \"YES\" (no quotes) to continue:\n");
		if (!confirm_yesno()) {
			printf("Aborting\n");
			return CMD_RET_FAILURE;
		}
	}

	ret = otp_prog(STRAP_REGION_START_ADDR + OTPSTRAP_ECCBRP_W_OFFSET,
		       OTPSTRAP_ECCBRP_BURN_VALUE);
	if (ret) {
		printf("OTP cannot be programmed\n");
		return CMD_RET_FAILURE;
	}

	ret = otp_read_strap(OTPSTRAP_ECCBRP_W_OFFSET, &val);
	if (ret || (val & OTPSTRAP_ECCBRP_BURN_VALUE) != OTPSTRAP_ECCBRP_BURN_VALUE) {
		printf("OTP strap ECC enable bit readback mismatch, got 0x%x\n", val);
		return CMD_RET_FAILURE;
	}

	ecc_hw_burn_pending = true;
	printf("SUCCESS: OTP strap ECC enable bit burned\n");
	printf("note: hardware-forced global ECC only takes effect after the next power cycle; this session is unaffected\n");

	return CMD_RET_SUCCESS;
}

static int otp_get_ecc_policy(u32 region, u32 *ecc_en, u32 *ecc_supported)
{
	struct otp_ecc_policy policy;
	int ret;

	if (otp_use_ipc())
		return otp_ipc_ecc_get_policy(region, ecc_en, ecc_supported);

	policy.region = region;

	ret = misc_ioctl(otp_dev, GET_ECC_POLICY, &policy);
	if (ret)
		return ret;

	*ecc_en = policy.ecc_en;
	*ecc_supported = policy.ecc_supported;

	return 0;
}

/*
 * Native path (SET_ECC_POLICY) only mutates aspeed_otp's in-RAM
 * region_ecc_tbl[] for the current u-boot session - not written back to
 * OTP hardware, so it doesn't survive reset and has no effect on
 * whatever OS boots next.
 */
static int otp_set_ecc_policy(u32 region, u32 ecc_en)
{
	struct otp_ecc_policy policy;

	if (otp_use_ipc())
		return otp_ipc_ecc_set_policy(region, ecc_en);

	policy.region = region;
	policy.ecc_en = ecc_en;
	policy.ecc_supported = 0;

	return misc_ioctl(otp_dev, SET_ECC_POLICY, &policy);
}

static int otp_ecc_policy_region_id(const char *name, u32 *id)
{
	for (int i = 0; i < ARRAY_SIZE(otp_ecc_policy_region_tbl); i++) {
		if (!strcmp(name, otp_ecc_policy_region_tbl[i].name)) {
			*id = otp_ecc_policy_region_tbl[i].id;
			return 0;
		}
	}

	return -1;
}

/*
 * global_status (enum otp_ecc_global_status) reflects the GET_ECC_STATUS
 * force switch: whenever it's not OTP_ECC_GLOBAL_DISABLED, the driver forces
 * ECC on for every region with ecc_supported, overriding that region's own
 * ecc_en, while ecc_supported=false regions (rbp/strap) always stay off
 * regardless of the global switch.
 */
static int otp_ecc_policy_dump_all(int global_status)
{
	u32 ecc_en, ecc_supported;
	int effective_en;
	int ret;
	int fail = 0;
	bool global_forced = global_status != OTP_ECC_GLOBAL_DISABLED;

	if (global_status == OTP_ECC_GLOBAL_HW_ENABLED)
		printf("note: global ECC switch is enabled (hardware strap), forcing ECC on for every capable region below\n");
	else if (global_status == OTP_ECC_GLOBAL_SW_ENABLED)
		printf("note: global ECC switch is enabled (temporarily, by software), forcing ECC on for every capable region below\n");

	printf("%-14s  %-14s  %s\n", "region", "ecc_supported", "ecc_enabled");
	printf("----------------------------------------\n");

	for (int i = 0; i < ARRAY_SIZE(otp_ecc_policy_region_tbl); i++) {
		ret = otp_get_ecc_policy(otp_ecc_policy_region_tbl[i].id, &ecc_en, &ecc_supported);
		if (ret) {
			printf("%-14s  failed to get ECC policy, ret=%d\n",
			       otp_ecc_policy_region_tbl[i].name, ret);
			fail = 1;
			continue;
		}

		/* global switch forces ECC on for capable regions, no effect otherwise */
		effective_en = ecc_supported && (global_forced || ecc_en);

		printf("%-14s  %-14s  %s%s\n",
		       otp_ecc_policy_region_tbl[i].name,
		       ecc_supported ? "yes" : "no",
		       effective_en ? "on" : "off",
		       (global_forced && ecc_supported) ? " (forced)" : "");
	}

	if (!otp_use_ipc())
		printf("note: native backend - this table reflects in-RAM state for the current u-boot session only\n");

	return fail ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}

static int do_otpecc_policy(int argc, char *const argv[], int global_status)
{
	u32 region_id;
	u32 ecc_en;
	int ret;

	if (argc < 1)
		return CMD_RET_USAGE;

	if (!strcmp(argv[0], "dump")) {
		if (argc != 1)
			return CMD_RET_USAGE;

		return otp_ecc_policy_dump_all(global_status);

	} else if (!strcmp(argv[0], "set")) {
		if (argc != 3)
			return CMD_RET_USAGE;

		if (global_status == OTP_ECC_GLOBAL_HW_ENABLED) {
			printf("OTP ECC policy cannot be set: hardware strap forces global ECC on for this session\n");
			return CMD_RET_FAILURE;
		}

		if (otp_ecc_policy_region_id(argv[1], &region_id)) {
			printf("Unknown OTP region '%s'\n", argv[1]);
			return CMD_RET_USAGE;
		}

		if (region_id == OTP_REGION_ID_RBP || region_id == OTP_REGION_ID_STRAP) {
			printf("region '%s' does not support ECC, nothing to set\n", argv[1]);
			return CMD_RET_USAGE;
		}

		if (!strcmp(argv[2], "enable")) {
			ecc_en = OTP_ECC_ENABLE;
		} else if (!strcmp(argv[2], "disable")) {
			ecc_en = OTP_ECC_DISABLE;
		} else {
			printf("ECC enable value must be 'enable' or 'disable'\n");
			return CMD_RET_USAGE;
		}

		ret = otp_set_ecc_policy(region_id, ecc_en);
		if (ret) {
			printf("Failed to set ECC policy for region '%s', ret=%d\n",
			       argv[1], ret);
			return CMD_RET_FAILURE;
		}

		printf("region %s ECC policy set to %s\n", argv[1], ecc_en ? "enabled" : "disabled");

		return otp_ecc_policy_dump_all(global_status);
	}

	return CMD_RET_USAGE;
}

static int do_otpecc(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	int ret;
	int global_status = OTP_ECC_GLOBAL_DISABLED;

	/* Get ECC status */
	ret = otp_ecc_get_status(&global_status);
	if (ret)
		return CMD_RET_FAILURE;

	/* Drop the ecc cmd */
	argc--;
	argv++;

	if (argc < 1)
		return CMD_RET_USAGE;

	if (!strcmp(argv[0], "status")) {
		switch (global_status) {
		case OTP_ECC_GLOBAL_HW_ENABLED:
			printf("OTP ECC is " ANSI_COLOR_CYAN
			       "ENABLED (hardware strap, cannot be disabled this session)"
			       ANSI_COLOR_RESET "\n");
			printf("note: this forces ECC on for every region that supports it, overriding that region's own 'ecc policy' setting. Regions that don't support ECC (rbp/strap) are unaffected and stay disabled regardless.\n");
			break;
		case OTP_ECC_GLOBAL_SW_ENABLED:
			printf("OTP ECC is " ANSI_COLOR_YELLOW
			       "ENABLED (temporarily, by software)"
			       ANSI_COLOR_RESET "\n");
			printf("note: this forces ECC on for every region that supports it, overriding that region's own 'ecc policy' setting. Regions that don't support ECC (rbp/strap) are unaffected and stay disabled regardless.\n");
			break;
		default:
			printf("OTP ECC is " ANSI_COLOR_GREEN "DISABLED" ANSI_COLOR_RESET "\n");
			printf("note: each region's ECC now follows its own 'ecc policy' setting instead\n");
			break;
		}
		if (!otp_use_ipc())
			printf("note: native backend - this reflects in-RAM state for the current u-boot session only\n");
		if (ecc_hw_burn_pending)
			printf(ANSI_COLOR_YELLOW
			       "note: the OTP strap ECC-enable fuse was burned this session - it will take effect after the next power cycle"
			       ANSI_COLOR_RESET "\n");

		return CMD_RET_SUCCESS;

	} else if (!strcmp(argv[0], "tmp-enable")) {
		switch (global_status) {
		case OTP_ECC_GLOBAL_HW_ENABLED:
			printf("OTP ECC is already enabled (hardware strap)\n");
			break;
		case OTP_ECC_GLOBAL_SW_ENABLED:
			printf("OTP ECC is already enabled (temporarily, by software)\n");
			break;
		default:
			ret = otp_ecc_set_enable(1);
			if (ret)
				return CMD_RET_FAILURE;

			global_status = OTP_ECC_GLOBAL_SW_ENABLED;
			printf("OTP ECC is enabled (temporarily)\n");
			break;
		}

		return otp_ecc_policy_dump_all(global_status);

	} else if (!strcmp(argv[0], "tmp-disable")) {
		switch (global_status) {
		case OTP_ECC_GLOBAL_HW_ENABLED:
			printf("OTP ECC cannot be disabled: hardware strap forces it on for this session\n");
			return CMD_RET_FAILURE;
		case OTP_ECC_GLOBAL_DISABLED:
			printf("OTP ECC is already disabled\n");
			break;
		default:
			ret = otp_ecc_set_enable(0);
			if (ret)
				return CMD_RET_FAILURE;

			global_status = OTP_ECC_GLOBAL_DISABLED;
			printf("OTP ECC is disabled (temporarily)\n");
			break;
		}

		return otp_ecc_policy_dump_all(global_status);

	} else if (!strcmp(argv[0], "hw-enable")) {
		int nconfirm = (argc == 2 && !strcmp(argv[1], "o"));

		if (argc != 1 && !nconfirm)
			return CMD_RET_USAGE;

		return otp_ecc_hw_enable(nconfirm);

	} else if (!strcmp(argv[0], "policy")) {
		/* Drop the policy cmd */
		argc--;
		argv++;

		return do_otpecc_policy(argc, argv, global_status);

	} else {
		return CMD_RET_USAGE;
	}
}

static int do_otpver(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	printf("OTP tool version: %s\n", OTP_VER);
	printf("OTP info version: %s\n", OTP_INFO_VER);
	printf("OTP backend: %s\n", otp_use_ipc() ? "IPC (BootMCU)" : "native");

	return CMD_RET_SUCCESS;
}

static int do_otptest(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	int ret;

	/* Drop the test cmd */
	argc--;
	argv++;

	if (strcmp(argv[0], "prov"))
		return CMD_RET_USAGE;

	printf("OTP test prov...\n");

	ret = otp_test_provision();
	if (ret == OTP_SUCCESS) {
		printf("\n"
			"______________________________________________\n"
			"___  __ \\__    |_  ___/_  ___/__  ____/__  __ \\\n"
			"__  /_/ /_  /| |____ \\_____ \\__  __/  __  / / /\n"
			"_  ____/_  ___ |___/ /____/ /_  /___  _  /_/ /\n"
			"/_/     /_/  |_/____/ /____/ /_____/  /_____/\n"
			);
		printf("\n"
			" .----------------.  .----------------.\n"
			"| .--------------. || .--------------. |\n"
			"| |     ____     | || |  ___  ____   | |\n"
			"| |   .'    `.   | || | |_  ||_  _|  | |\n"
			"| |  /  .--.  \\  | || |   | |_/ /    | |\n"
			"| |  | |    | |  | || |   |  __'.    | |\n"
			);
		printf("| |  \\  `--'  /  | || |  _| |  \\ \\_  | |\n"
			"| |   `.____.'   | || | |____||____| | |\n"
			"| |              | || |              | |\n"
			"| '--------------' || '--------------' |\n"
			" '----------------'  '----------------'\n"
			"\n");
	} else {
		printf("\n"
			"______________________________________________\n"
			"___  ____/__    |___  _/__  /___  ____/__  __ \\\n"
			"__  /_   __  /| |__  / __  / __  __/  __  / / /\n"
			"_  __/   _  ___ |_/ /  _  /___  /___  _  /_/ /\n"
			"/_/      /_/  |_/___/  /_____/_____/  /_____/\n"
			);
	}

	return CMD_RET_SUCCESS;
}

static struct cmd_tbl cmd_otp[] = {
	U_BOOT_CMD_MKENT(version, 1, 0, do_otpver, "", ""),
	U_BOOT_CMD_MKENT(read, 4, 0, do_otpread, "", ""),
	U_BOOT_CMD_MKENT(prog, 4, 0, do_otpprog, "", ""),
	U_BOOT_CMD_MKENT(pb, 7, 0, do_otppb, "", ""),
	U_BOOT_CMD_MKENT(patch, 5, 0, do_otppatch, "", ""),
	U_BOOT_CMD_MKENT(ecc, 5, 0, do_otpecc, "", ""),
	U_BOOT_CMD_MKENT(info, 3, 0, do_otpinfo, "", ""),
	U_BOOT_CMD_MKENT(test, 2, 0, do_otptest, "", ""),
};

static int do_driver_init(int argc, char *const argv[])
{
	int index;
	int ret;
	char *name;

	index = simple_strtoul(argv[1], NULL, 10);

	switch (index) {
	case 0:
		name = OTP_DEVICE_NAME_0;
		break;
	case 1:
		name = OTP_DEVICE_NAME_1;
		break;
	default:
		printf("Unknown OTP device index %d\n", index);
		return CMD_RET_USAGE;
	}

	/*
	 * Native OTP controller access takes priority; only fall back to the
	 * BootMCU IPC channel if this core has no direct access to it (e.g.
	 * OTP ownership is locked to BootMCU - see master protection).
	 */
	ret = uclass_get_device_by_name(UCLASS_MISC, name, &otp_dev);
	if (ret) {
		if (!IS_ENABLED(CONFIG_ASPEED_OTP_IPC)) {
			printf("%s: get Aspeed otp driver failed\n", __func__);
			return ret;
		}

		debug("%s: native otp driver unavailable (%d), falling back to IPC\n",
		      __func__, ret);
		otp_dev = NULL;
	}

	return 0;
}

static int do_ast_otp(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	union otp_pro_sts *pro_sts;
	struct cmd_tbl *cp;
	u32 ver;
	int ret;
	u16 otp_conf0;

	if (argc < 2)
		return CMD_RET_USAGE;

	ret = do_driver_init(argc, argv);
	if (ret)
		return ret;

	printf("OTP backend: %s\n", otp_use_ipc() ? "IPC (BootMCU)" : "native");

	/* Drop the otp command */
	argc--;
	argv++;

	cp = find_cmd_tbl(argv[1], cmd_otp, ARRAY_SIZE(cmd_otp));

	/* Drop the otp command */
	argc--;
	argv++;

	if (!cp || argc > cp->maxargs)
		return CMD_RET_USAGE;
	if (flag == CMD_FLAG_REPEAT && !cmd_is_repeatable(cp))
		return CMD_RET_SUCCESS;

	ver = chip_version();
	switch (ver) {
	case OTP_AST2700_A0:
		printf("Chip: AST2700-A0 is deprecated\n");
		break;
	case OTP_AST2700_A1:
		printf("Chip: AST2700-A1\n");
		info_cb.version = OTP_AST2700_A1;
		info_cb.rbp_info = a1_rbp_info;
		info_cb.rbp_info_len = ARRAY_SIZE(a1_rbp_info);
		info_cb.conf_info = a1_conf_info;
		info_cb.conf_info_len = ARRAY_SIZE(a1_conf_info);
		info_cb.strap_info = a1_strap_info;
		info_cb.strap_info_len = ARRAY_SIZE(a1_strap_info);
		info_cb.strap_ext_info = a1_strap_ext_info;
		info_cb.strap_ext_info_len = ARRAY_SIZE(a1_strap_ext_info);
		info_cb.cal_info = a1_cal_info;
		info_cb.cal_info_len = ARRAY_SIZE(a1_cal_info);
		info_cb.key_info = a1_key_type;
		info_cb.key_info_len = ARRAY_SIZE(a1_key_type);
		break;
	case OTP_AST2700_A2:
		printf("Chip: AST2700-A2\n");
		info_cb.version = OTP_AST2700_A2;
		info_cb.rbp_info = a2_rbp_info;
		info_cb.rbp_info_len = ARRAY_SIZE(a2_rbp_info);
		info_cb.conf_info = a2_conf_info;
		info_cb.conf_info_len = ARRAY_SIZE(a2_conf_info);
		info_cb.strap_info = a2_strap_info;
		info_cb.strap_info_len = ARRAY_SIZE(a2_strap_info);
		info_cb.strap_ext_info = a2_strap_ext_info;
		info_cb.strap_ext_info_len = ARRAY_SIZE(a2_strap_ext_info);
		info_cb.cal_info = a2_cal_info;
		info_cb.cal_info_len = ARRAY_SIZE(a2_cal_info);
		info_cb.key_info = a2_key_type;
		info_cb.key_info_len = ARRAY_SIZE(a2_key_type);
		break;
	default:
		printf("SOC is not supported\n");
		return CMD_RET_FAILURE;
	}

	otp_read_conf(0, &otp_conf0);
	pro_sts = &info_cb.pro_sts;
	pro_sts->value = otp_conf0;

	ret = cp->cmd(cmdtp, flag, argc, argv);

	return ret;
}

U_BOOT_CMD(otp, 9, 0,  do_ast_otp,
	   "ASPEED One-Time-Programmable sub-system",
	   "<dev> version\n"
	   "otp <dev> read rom|rbp|conf|strap|strap-pro|strap-ext|strap-ext-vld|u-data|s-data|cptra|puf <otp_w_offset> <w_count>\n"
	   "otp <dev> read all (dumps every OTP word except HW PUF)\n"
	   "otp <dev> pb rbp|conf|u-data|s-data|cptra [o] [f] <otp_w_offset> <bit_offset> <value>\n"
	   "otp <dev> pb strap|strap-pro [o] <bit_offset> <value>\n"
	   "otp <dev> pb strap-ext-vld all [o] [f]\n"
	   "otp <dev> pb strap-ext|strap-ext-vld [o] [f] (interactive: enter bit offsets one at a time, \"end\" to finish)\n"
	   "otp <dev> pb strap-ext|strap-ext-vld [o] [f] <word-aligned bit_offset> <full word value>\n"
	   "otp <dev> pb: \"o\" skips the \"type YES\" confirm; \"f\" forces programming anyway when\n"
	   "    the target word already has data and this region's ECC is active (otherwise refused,\n"
	   "    since a second write would corrupt the ECC parity locked in by the first)\n"
	   "otp <dev> prog [o] [f] <addr>\n"
	   "otp <dev> prog: \"o\" skips the \"type YES\" confirm; \"f\" forces programming anyway\n"
	   "    when a region's existing data conflicts with the image while that region's ECC is\n"
	   "    active (otherwise refused, since the second write would corrupt its ECC parity)\n"
	   "otp <dev> info key|rbp|conf|strap|strap-ext\n"
	   "otp <dev> patch prog <dram_addr> <otp_w_offset> <w_count>\n"
	   "otp <dev> patch enable pre|post <otp_start_w_offset> <w_count>\n"
	   "otp <dev> ecc status|tmp-enable|tmp-disable\n"
	   "otp <dev> ecc hw-enable [o] (permanently burns the OTP strap ECC-enable fuse)\n"
	   "otp <dev> ecc policy dump\n"
	   "otp <dev> ecc policy set rom|conf|strap-ext|u-data|s-data|cptra|puf enable|disable\n"
	   "otp <dev> test prov\n"
	  );
