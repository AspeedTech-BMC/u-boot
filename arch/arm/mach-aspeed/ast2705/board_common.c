// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) ASPEED Technology Inc.
 */

#include <dm.h>
#include <ram.h>
#include <init.h>
#include <timer.h>
#include <asm/io.h>
#include <asm/arch/timer.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <dm/uclass.h>
#include <power/regulator.h>
#include <asm/arch-aspeed/scu_ast2700.h>
#include <g_dnl.h>

#define AHBC_GROUP(x)				(0x40 * (x))
#define AHBC_HREADY_WAIT_CNT_REG		0x34
#define   AHBC_HREADY_WAIT_CNT_MAX		0x3f

DECLARE_GLOBAL_DATA_PTR;

int dram_init(void)
{
	int ret;
	struct udevice *dev;
	struct ram_info ram;

	ret = uclass_get_device(UCLASS_RAM, 0, &dev);
	if (ret) {
		debug("cannot get DRAM driver\n");
		return ret;
	}

	ret = ram_get_info(dev, &ram);
	if (ret) {
		debug("cannot get DRAM information\n");
		return ret;
	}

	gd->ram_size = ram.size;
	return 0;
}

#ifdef CONFIG_USB_GADGET_DOWNLOAD
int g_dnl_get_board_bcd_device_number(int gcnum)
{
	/* TODO: Use 0 as spl. */
	return 0;//FIELD_GET(SCU_CPU_REVISION_ID_HW, readl(ASPEED_CPU_REVISION_ID));
}

#define SCU1_CHIP_UNIQ_ID0	(ASPEED_IO_SCU_BASE + 0x810)
#define SCU1_CHIP_UNIQ_ID1	(ASPEED_IO_SCU_BASE + 0x814)
int g_dnl_bind_fixup(struct usb_device_descriptor *dev, const char *name)
{
	char bString[17];
	static const char hexmap[] = "0123456789ABCDEF";
	uint8_t byte;
	uint32_t id[2] = {
		readl(SCU1_CHIP_UNIQ_ID0),
		readl(SCU1_CHIP_UNIQ_ID1)
	};
	for (int i = 0; i < 8; i++) {
		byte = (id[1 - (i / 4)] >> ((3 - (i % 4)) * 8)) & 0xFF;
		bString[i * 2] = hexmap[(byte >> 4) & 0xF];
		bString[i * 2 + 1] = hexmap[byte & 0xF];
	}
	bString[16] = 0;
	g_dnl_set_serialnumber(bString);
	return 0;
}
#endif /* CONFIG_USB_GADGET_DOWNLOAD */

static void ahbc_init(void)
{
	uint32_t reg_val;
	int i;

	reg_val = readl(ASPEED_CPU_REVISION_ID);
	if (FIELD_GET(SCU_CPU_REVISION_ID_HW, reg_val))
		return;

	/* CPU-die AHBC timeout counter */
	for (i = 0; i < 4; i++)
		writel(AHBC_HREADY_WAIT_CNT_MAX,
		       (void *)ASPEED_CPU_AHBC_BASE + AHBC_GROUP(i) + AHBC_HREADY_WAIT_CNT_REG);

	/* IO-die AHBC timeout counter */
	for (i = 0; i < 8; i++)
		writel(AHBC_HREADY_WAIT_CNT_MAX,
		       (void *)ASPEED_IO_AHBC_BASE + AHBC_GROUP(i) + AHBC_HREADY_WAIT_CNT_REG);
}

int board_init(void)
{
	struct udevice *dev;
	int i = 0;
	int ret;

	ahbc_init();

	regulators_enable_boot_on(0);

	/*
	 * Loop over all MISC uclass drivers to call the comphy code
	 * and init all CP110 devices enabled in the DT
	 */
	while (1) {
		/* Call the comphy code via the MISC uclass driver */
		ret = uclass_get_device(UCLASS_MISC, i++, &dev);

		/* We're done, once no further CP110 device is found */
		if (ret)
			break;
	}

	return 0;
}
