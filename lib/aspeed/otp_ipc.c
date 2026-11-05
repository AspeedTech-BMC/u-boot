// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2025 ASPEED Technology Inc.
 */
#include <config.h>
#include <common.h>
#include <console.h>
#include <dm.h>
#include <misc.h>
#include <linux/string.h>
#include <u-boot/otp_ipc.h>

#define OTP_IPC_TIMEOUT_MS	5000

enum mbox_ioctl {
	MBOX_IOCTL_CAPS,
	MBOX_IOCTL_SEND,
	MBOX_IOCTL_RECV,
};

static int do_mbox_ioctl(const char *dev_name, int cmd, void *buff)
{
	struct udevice *dev;
	int ret;

	ret = uclass_get_device_by_name(UCLASS_MISC, dev_name, &dev);
	if (ret) {
		pr_warn("Unable to find device %s\n", dev_name);
		return ret;
	}

	ret = misc_ioctl(dev, cmd, buff);
	if (ret < 0)
		pr_warn("%s: ioctl(%d) failed: %d\n", dev_name, cmd, ret);

	return ret;
}

static int otp_ipc_call(const char *dev_name, u32 *req, u32 *resp)
{
	static u8 next_seq;
	ulong start;
	u8 seq;
	int ret;

	seq = ++next_seq;
	req[1] |= ((u32)seq << OTP_IPC_SEQ_SHIFT);

	ret = do_mbox_ioctl(dev_name, MBOX_IOCTL_SEND, req);
	if (ret < 0)
		return ret;

	start = get_timer(0);
	while (get_timer(start) < OTP_IPC_TIMEOUT_MS) {
		ret = do_mbox_ioctl(dev_name, MBOX_IOCTL_RECV, resp);
		if (ret == 0) {
			/* A reply that doesn't echo our seq belongs to a
			 * call we already gave up on; discard it and keep
			 * polling for ours within what's left of the budget.
			 */
			if (OTP_IPC_RESP_SEQ(resp[0]) != seq) {
				pr_warn("%s: discarding stale reply (seq %u, expected %u)\n",
					dev_name, OTP_IPC_RESP_SEQ(resp[0]), seq);
				continue;
			}

			return OTP_IPC_RESP_STATUS(resp[0]);
		}

		if (ctrlc())
			return -EINTR;
	}

	pr_warn("%s: timed out waiting for bootmcu response\n", dev_name);
	return -ETIMEDOUT;
}

int otp_ipc_read(u32 addr, u32 count, u16 *out)
{
	u32 req[8] = {addr, 0, 0, 0, 0, 0, 0, 0};
	u32 resp[8] = {0};
	int status;

	if (count > OTP_IPC_READ_MAX_WORDS)
		return -EINVAL;

	req[1] = OTP_IPC_PACK_OP_COUNT(OTP_IPC_OP_READ, count);

	status = otp_ipc_call(MBOX_OTP_DEVICE_NAME, req, resp);

	debug("%s: addr=0x%x count=%u status=%d\n", __func__, addr, count, status);

	if (status == 0)
		memcpy(out, &resp[1], count * sizeof(u16));

	return status;
}

int otp_ipc_program(u32 addr, u32 count, const u16 *data)
{
	u32 req[8] = {addr, 0, 0, 0, 0, 0, 0, 0};
	u32 resp[8] = {0};
	int status;

	if (count > OTP_IPC_PROGRAM_MAX_WORDS)
		return -EINVAL;

	req[1] = OTP_IPC_PACK_OP_COUNT(OTP_IPC_OP_PROGRAM, count);
	memcpy(&req[2], data, count * sizeof(u16));

	status = otp_ipc_call(MBOX_OTP_DEVICE_NAME, req, resp);

	debug("%s: addr=0x%x count=%u status=%d\n", __func__, addr, count, status);

	return status;
}

int otp_ipc_ecc_get_status(int *status)
{
	u32 req[8] = {0, OTP_IPC_PACK_OP_COUNT(OTP_IPC_OP_ECC_GET_STATUS, 0),
		      0, 0, 0, 0, 0, 0};
	u32 resp[8] = {0};
	int ret;

	ret = otp_ipc_call(MBOX_OTP_DEVICE_NAME, req, resp);
	if (ret == 0)
		*status = (int)resp[1];

	return ret;
}

int otp_ipc_ecc_set_enable(int enable)
{
	u32 req[8] = {0, OTP_IPC_PACK_OP_COUNT(OTP_IPC_OP_ECC_SET_ENABLE, enable ? 1 : 0),
		      0, 0, 0, 0, 0, 0};
	u32 resp[8] = {0};

	return otp_ipc_call(MBOX_OTP_DEVICE_NAME, req, resp);
}

int otp_ipc_ecc_get_policy(u32 region, u32 *ecc_en, u32 *ecc_supported)
{
	u32 req[8] = {0, OTP_IPC_PACK_OP_COUNT(OTP_IPC_OP_ECC_GET_POLICY, region),
		      0, 0, 0, 0, 0, 0};
	u32 resp[8] = {0};
	int status;

	status = otp_ipc_call(MBOX_OTP_DEVICE_NAME, req, resp);
	if (status == 0) {
		*ecc_en = resp[1];
		*ecc_supported = resp[2];
	}

	return status;
}

int otp_ipc_ecc_set_policy(u32 region, u32 ecc_en)
{
	u32 req[8] = {0, OTP_IPC_PACK_OP_COUNT(OTP_IPC_OP_ECC_SET_POLICY, region),
		      ecc_en, 0, 0, 0, 0, 0};
	u32 resp[8] = {0};

	return otp_ipc_call(MBOX_OTP_DEVICE_NAME, req, resp);
}

int otp_ipc_get_rom_patch_version(u32 *version)
{
	u32 req[8] = {0, OTP_IPC_PACK_OP_COUNT(OTP_IPC_OP_GET_ROM_PATCH_VERSION, 0),
		      0, 0, 0, 0, 0, 0};
	u32 resp[8] = {0};
	int status;

	status = otp_ipc_call(MBOX_OTP_DEVICE_NAME, req, resp);
	if (status == 0)
		*version = resp[1];

	return status;
}

int otp_ipc_read_ecc_code(u32 addr, u32 *data, u32 *ecc_code)
{
	u32 req[8] = {addr, OTP_IPC_PACK_OP_COUNT(OTP_IPC_OP_READ_ECC_CODE, 0),
		      0, 0, 0, 0, 0, 0};
	u32 resp[8] = {0};
	int status;

	status = otp_ipc_call(MBOX_OTP_DEVICE_NAME, req, resp);
	if (status == 0) {
		*data = resp[1];
		*ecc_code = resp[2];
	}

	return status;
}
