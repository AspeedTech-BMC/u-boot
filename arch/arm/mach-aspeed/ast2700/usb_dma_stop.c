// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) ASPEED Technology Inc.
 */

#include <stdio.h>
#include <asm/io.h>
#include <asm/arch-aspeed/platform.h>
#include <asm/arch-aspeed/usb_dma_stop_ast2700.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/errno.h>

/* Standard xHCI host controller register blocks. Only ports A/B have an
 * xHCI/USB3 PHY; ports C/D don't.
 */
#define ASPEED_XHCI0_BASE			0x12030000	/* Port A */
#define ASPEED_XHCI1_BASE			0x12050000	/* Port B */

/* xHCI operational registers start at CAPLENGTH bytes past the controller
 * base.
 */
#define ASPEED_USB_HC_CAPLENGTH_MASK		GENMASK(7, 0)
#define ASPEED_USB_HC_USBCMD			0x00
#define ASPEED_USB_HC_USBSTS			0x04
#define ASPEED_USB_XHCI_CMD_RUN		BIT(0)
#define ASPEED_USB_XHCI_STS_HALT		BIT(0)

/* Port B's xHCI is dual-role: GCTL.PRTCAPDIR says whether it's currently
 * acting as a host (USBCMD/USBSTS above) or a UDC gadget, in which case
 * DCTL/DSTS is the pair that actually needs to be quiesced instead.
 */
#define ASPEED_USB_XHCI_GCTL			0xc110
#define ASPEED_USB_XHCI_GCTL_PRTCAPDIR_MASK	GENMASK(13, 12)
#define ASPEED_USB_XHCI_GCTL_PRTCAP_DEVICE	2

#define ASPEED_USB_XHCI_DCTL			0xc704
#define ASPEED_USB_XHCI_DCTL_RUN_STOP		BIT(31)
#define ASPEED_USB_XHCI_DCTL_ULSTCHNGREQ_MASK	GENMASK(8, 5)
#define ASPEED_USB_XHCI_DSTS			0xc70c
#define ASPEED_USB_XHCI_DSTS_DEVCTRLHLT	BIT(22)

#define ASPEED_USB_XHCI_GUSB2PHYCFG		0xc200
#define ASPEED_USB_XHCI_GUSB2PHYCFG_SUSPHY	BIT(6)
#define ASPEED_USB_XHCI_GUSB2PHYCFG_ENBLSLPM	BIT(8)
#define ASPEED_USB_XHCI_GEVNTCOUNT		0xc40c
#define ASPEED_USB_XHCI_GEVNTCOUNT_MASK	0xfffc
#define ASPEED_USB_XHCI_DEVTEN			0xc708
#define ASPEED_USB_XHCI_DALEPENA		0xc720
#define ASPEED_USB_XHCI_NUM_EPS		32

#define ASPEED_USB_XHCI_DEPCMDPAR2(n)		(0xc800 + (n) * 0x10)
#define ASPEED_USB_XHCI_DEPCMDPAR1(n)		(0xc804 + (n) * 0x10)
#define ASPEED_USB_XHCI_DEPCMDPAR0(n)		(0xc808 + (n) * 0x10)
#define ASPEED_USB_XHCI_DEPCMD(n)		(0xc80c + (n) * 0x10)
#define ASPEED_USB_XHCI_DEPCMD_ENDTRANSFER	0x08
#define ASPEED_USB_XHCI_DEPCMD_CMDIOC		BIT(8)
#define ASPEED_USB_XHCI_DEPCMD_CMDACT		BIT(10)
#define ASPEED_USB_XHCI_DEPCMD_HIPRI_FORCERM	BIT(11)
#define ASPEED_USB_XHCI_DEPCMD_STATUS(x)	(((x) >> 12) & 0xf)
#define ASPEED_USB_XHCI_DEPCMD_PARAM(x)	((x) << 16)
#define ASPEED_USB_XHCI_DEPCMD_GET_RSC_IDX(x)	(((x) >> 16) & 0x7f)

/* 2 s, in line with Linux polling DEVCTRLHLT up to 2000 x 1-2 ms. */
#define ASPEED_USB_XHCI_DEV_HALT_TIMEOUT_US	2000000
#define ASPEED_USB_XHCI_DEPCMD_TIMEOUT_US	5000

/* usb_func_ctrl (SCU0 0x410) port A/B XHCI mode select: 0 = PCIe XHCI,
 * 1 = BMC XHCI. A PCIe-XHCI port's DMA is the PCIe host's problem, not
 * ours -- leave it alone.
 */
#define ASPEED_USB_FUNC_CTRL			(ASPEED_CPU_SCU_BASE + 0x410)
#define ASPEED_USB_FUNC_XHCI_PORTA_BMC		BIT(9)
#define ASPEED_USB_FUNC_XHCI_PORTB_BMC		BIT(10)

#define ASPEED_SCU0_CLKGATE_CTRL		(ASPEED_CPU_SCU_BASE + 0x240)
#define ASPEED_SCU0_MODRST2_CTRL		(ASPEED_CPU_SCU_BASE + 0x220)
#define SCU0_CLKGATE1_USBA			BIT(14)
#define SCU0_CLKGATE1_USBB			BIT(7)
#define SCU0_RST2_USBA_PHY3			BIT(1)
#define SCU0_RST2_USBA_XHCI			BIT(2)
#define SCU0_RST2_USBB_PHY3			BIT(4)
#define SCU0_RST2_USBB_XHCI			BIT(5)

static int usb_reg_poll(ulong addr, u32 mask, u32 match, u32 timeout_us)
{
	while ((readl((void *)addr) & mask) != match) {
		if (!timeout_us)
			return -ETIMEDOUT;
		udelay(1);
		timeout_us--;
	}

	return 0;
}

static bool xhci_port_active(u32 clk_bit, u32 rst_bit)
{
	return !(readl((void *)ASPEED_SCU0_CLKGATE_CTRL) & clk_bit) &&
	       !(readl((void *)ASPEED_SCU0_MODRST2_CTRL) & rst_bit);
}

/*
 * No ISR runs here, so pending events must be acked by hand or the
 * controller never completes End Transfer and never halts (under Linux
 * the ISR does this in the background). The event contents are dropped
 * -- nothing here needs them.
 */
static void xhci_dev_ack_events(ulong base)
{
	u32 count = readl((void *)(base + ASPEED_USB_XHCI_GEVNTCOUNT)) &
		    ASPEED_USB_XHCI_GEVNTCOUNT_MASK;

	if (count)
		writel(count, (void *)(base + ASPEED_USB_XHCI_GEVNTCOUNT));
}

static int xhci_dev_poll(ulong base, ulong addr, u32 mask, u32 match, u32 timeout_us)
{
	while ((readl((void *)addr) & mask) != match) {
		if (!timeout_us)
			return -ETIMEDOUT;
		xhci_dev_ack_events(base);
		udelay(1);
		timeout_us--;
	}

	return 0;
}

static int xhci_dev_ep_cmd(ulong base, u32 ep, u32 cmd)
{
	ulong depcmd = base + ASPEED_USB_XHCI_DEPCMD(ep);
	int ret;

	writel(0, (void *)(base + ASPEED_USB_XHCI_DEPCMDPAR0(ep)));
	writel(0, (void *)(base + ASPEED_USB_XHCI_DEPCMDPAR1(ep)));
	writel(0, (void *)(base + ASPEED_USB_XHCI_DEPCMDPAR2(ep)));
	writel(cmd | ASPEED_USB_XHCI_DEPCMD_CMDACT, (void *)depcmd);

	ret = xhci_dev_poll(base, depcmd, ASPEED_USB_XHCI_DEPCMD_CMDACT, 0,
			    ASPEED_USB_XHCI_DEPCMD_TIMEOUT_US);
	if (ret)
		return ret;

	return ASPEED_USB_XHCI_DEPCMD_STATUS(readl((void *)depcmd)) ? -EIO : 0;
}

/*
 * The resource index End Transfer needs is read back from DEPCMD, where
 * the controller left it after Start Transfer -- the same place Linux
 * gets it from. 0 means nothing was started, except on physical ep0
 * (ep0 OUT), whose resource index is always 0. Non-control endpoints
 * are force-ended like Linux does; ep0/ep1 are not.
 */
static void xhci_dev_end_transfer(ulong base, u32 ep)
{
	u32 rsc = ASPEED_USB_XHCI_DEPCMD_GET_RSC_IDX(readl((void *)(base +
						     ASPEED_USB_XHCI_DEPCMD(ep))));
	u32 cmd = ASPEED_USB_XHCI_DEPCMD_ENDTRANSFER | ASPEED_USB_XHCI_DEPCMD_CMDIOC |
		  ASPEED_USB_XHCI_DEPCMD_PARAM(rsc);
	int ret;

	if (!rsc && ep)
		return;

	if (ep > 1)
		cmd |= ASPEED_USB_XHCI_DEPCMD_HIPRI_FORCERM;

	ret = xhci_dev_ep_cmd(base, ep, cmd);
	printf("XHCI@0x%08x: ep%u end transfer rsc=%u %s (%d)\n", (unsigned int)base,
	       ep, rsc, ret ? "failed" : "done", ret);
}

static int xhci_dev_wait_halt(ulong base)
{
	int ret = xhci_dev_poll(base, base + ASPEED_USB_XHCI_DSTS,
				ASPEED_USB_XHCI_DSTS_DEVCTRLHLT, ASPEED_USB_XHCI_DSTS_DEVCTRLHLT,
				ASPEED_USB_XHCI_DEV_HALT_TIMEOUT_US);

	if (ret)
		printf("XHCI@0x%08x: DEVCTRLHLT timeout, DSTS=0x%08x GEVNTCOUNT=0x%08x\n",
		       (unsigned int)base, readl((void *)(base + ASPEED_USB_XHCI_DSTS)),
		       readl((void *)(base + ASPEED_USB_XHCI_GEVNTCOUNT)));

	return ret;
}

/*
 * Fallback when a plain run/stop didn't halt: End Transfer on every
 * active endpoint in [first, last], then wait again. GUSB2PHYCFG.SUSPHY
 * and ENBLSLPM are held off meanwhile, as endpoint commands can time out
 * with them set.
 */
static int xhci_dev_end_transfers_and_halt(ulong base, u32 first, u32 last)
{
	ulong phycfg = base + ASPEED_USB_XHCI_GUSB2PHYCFG;
	u32 saved = readl((void *)phycfg) &
		    (ASPEED_USB_XHCI_GUSB2PHYCFG_SUSPHY | ASPEED_USB_XHCI_GUSB2PHYCFG_ENBLSLPM);
	u32 epena = readl((void *)(base + ASPEED_USB_XHCI_DALEPENA));
	u32 ep;
	int ret;

	printf("XHCI@0x%08x: ending active transfers on ep%u-%u, DALEPENA=0x%08x\n",
	       (unsigned int)base, first, last, epena);

	if (saved)
		clrbits_le32(phycfg, saved);

	for (ep = first; ep <= last; ep++)
		if (epena & BIT(ep))
			xhci_dev_end_transfer(base, ep);

	ret = xhci_dev_wait_halt(base);

	if (saved)
		setbits_le32(phycfg, saved);

	return ret;
}

/*
 * Device-mode stop: only DMA needs to end here; Linux's probe core-soft-
 * resets the controller anyway, so endpoint state is left as is. Once
 * DSTS.DEVCTRLHLT is set no more TRB/data DMA happens, and DEVTEN=0 stops
 * further event-buffer writes into DRAM.
 */
static int xhci_device_stop_port(ulong base)
{
	u32 dctl;
	int ret;

	printf("XHCI@0x%08x: stop device port, DCTL=0x%08x DSTS=0x%08x GEVNTCOUNT=0x%08x\n",
	       (unsigned int)base, readl((void *)(base + ASPEED_USB_XHCI_DCTL)),
	       readl((void *)(base + ASPEED_USB_XHCI_DSTS)),
	       readl((void *)(base + ASPEED_USB_XHCI_GEVNTCOUNT)));

	/* Masking ULSTCHNGREQ keeps a stale link state request from re-firing. */
	dctl = readl((void *)(base + ASPEED_USB_XHCI_DCTL));
	dctl &= ~(ASPEED_USB_XHCI_DCTL_RUN_STOP | ASPEED_USB_XHCI_DCTL_ULSTCHNGREQ_MASK);
	writel(dctl, (void *)(base + ASPEED_USB_XHCI_DCTL));

	/*
	 * If run/stop alone doesn't halt, end the non-control endpoints first,
	 * as the databook asks for before a device-initiated disconnect. Only
	 * then touch ep0/ep1: Linux ends ep0 only in its data phase, since End
	 * Transfer while a SETUP is pending can stall every endpoint command,
	 * and the phase isn't visible from here. No STALL/SETUP re-arm after
	 * that -- Linux does it to keep ep0 running, which would start new DMA.
	 */
	ret = xhci_dev_wait_halt(base);
	if (ret)
		ret = xhci_dev_end_transfers_and_halt(base, 2, ASPEED_USB_XHCI_NUM_EPS - 1);
	if (ret)
		ret = xhci_dev_end_transfers_and_halt(base, 0, 1);

	writel(0, (void *)(base + ASPEED_USB_XHCI_DEVTEN));
	xhci_dev_ack_events(base);

	return ret;
}

/*
 * xhci0/1 (ports A/B) talk to DRAM over AXI directly, so resetting one
 * mid-DMA-transfer can hang DRAMC -- quiesce it here first. xHCI's
 * USBCMD.Run/Stop can be cleared directly (mirrors Linux's
 * xhci_quiesce() + xhci_halt()).
 */
static int xhci_stop_port(ulong base)
{
	ulong op;
	u32 caplen, cmd, prtcap;

	prtcap = (readl((void *)(base + ASPEED_USB_XHCI_GCTL)) &
		  ASPEED_USB_XHCI_GCTL_PRTCAPDIR_MASK) >> 12;
	if (prtcap == ASPEED_USB_XHCI_GCTL_PRTCAP_DEVICE)
		return xhci_device_stop_port(base);

	caplen = readl((void *)base) & ASPEED_USB_HC_CAPLENGTH_MASK;
	if (!caplen) {
		printf("XHCI@0x%08x: caplen=0, skip\n", (unsigned int)base);
		return -EIO;
	}
	op = base + caplen;

	cmd = readl((void *)(op + ASPEED_USB_HC_USBCMD)) & ~ASPEED_USB_XHCI_CMD_RUN;
	writel(cmd, (void *)(op + ASPEED_USB_HC_USBCMD));

	return usb_reg_poll(op + ASPEED_USB_HC_USBSTS, ASPEED_USB_XHCI_STS_HALT,
			     ASPEED_USB_XHCI_STS_HALT, 32000);
}

/*
 * Defensively stop any USB3 DMA left running across a reset, before this
 * boot stage touches anything else. Only ports whose xHCI is BMC-owned
 * (not PCIe's) and whose clock/reset are actually up are touched.
 */
void ast2700_xhci_dma_stop(void)
{
	u32 func_ctrl = readl((void *)ASPEED_USB_FUNC_CTRL);
	bool porta_bmc = !!(func_ctrl & ASPEED_USB_FUNC_XHCI_PORTA_BMC);
	bool portb_bmc = !!(func_ctrl & ASPEED_USB_FUNC_XHCI_PORTB_BMC);
	bool porta_active = xhci_port_active(SCU0_CLKGATE1_USBA,
					      SCU0_RST2_USBA_PHY3 | SCU0_RST2_USBA_XHCI);
	bool portb_active = xhci_port_active(SCU0_CLKGATE1_USBB,
					      SCU0_RST2_USBB_PHY3 | SCU0_RST2_USBB_XHCI);
	int ret;

	printf("%s: FUNC_CTRL=0x%08x CLKGATE=0x%08x MODRST2=0x%08x\n", __func__, func_ctrl,
	       readl((void *)ASPEED_SCU0_CLKGATE_CTRL), readl((void *)ASPEED_SCU0_MODRST2_CTRL));

	if (porta_bmc && porta_active) {
		printf("XHCI@0x%08x: stopping\n", ASPEED_XHCI0_BASE);
		ret = xhci_stop_port(ASPEED_XHCI0_BASE);
		if (ret)
			printf("XHCI@0x%08x: stop failed (%d)\n", ASPEED_XHCI0_BASE, ret);
		else
			printf("XHCI@0x%08x: stopped\n", ASPEED_XHCI0_BASE);
	} else {
		printf("XHCI@0x%08x: skipped (%s)\n", ASPEED_XHCI0_BASE,
		       !porta_bmc ? "not BMC-owned" : "clock/reset gated");
	}

	if (portb_bmc && portb_active) {
		printf("XHCI@0x%08x: stopping\n", ASPEED_XHCI1_BASE);
		ret = xhci_stop_port(ASPEED_XHCI1_BASE);
		if (ret)
			printf("XHCI@0x%08x: stop failed (%d)\n", ASPEED_XHCI1_BASE, ret);
		else
			printf("XHCI@0x%08x: stopped\n", ASPEED_XHCI1_BASE);
	} else {
		printf("XHCI@0x%08x: skipped (%s)\n", ASPEED_XHCI1_BASE,
		       !portb_bmc ? "not BMC-owned" : "clock/reset gated");
	}
}
