// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) ASPEED Technology Inc.
 */
#include <common.h>
#include <clk.h>
#include <dm.h>
#include <errno.h>
#include <ram.h>
#include <regmap.h>
#include <reset.h>
#include <asm/io.h>
#include <asm/global_data.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/sizes.h>
#include <dt-bindings/clock/ast2700-clock.h>
#include <asm/arch/sdram_ast2700.h>
#include <asm/arch/scu_ast2700.h>
#include <asm/arch/platform.h>
#include <aspeed_vga.h>

#define DRAMC_UNLOCK_KEY		0x1688a8a8
#define DRAMC_VIDEO_UNLOCK_KEY		0x00440003

#define SCU_CPU_REG                     0x12c02000
#define SCU_CPU_VGA0_SCRATCH            (SCU_CPU_REG + 0x900)
#define SCU_CPU_VGA1_SCRATCH            (SCU_CPU_REG + 0x910)

#define SCU_IO_HWSTRAP1			(ASPEED_IO_SCU_BASE + 0x010)
#define IO_HWSTRAP1_DRAM_TYPE		BIT(10)
#define SCU_IO_MCU0_CTRL		(ASPEED_IO_SCU_BASE + 0x110)
#define SCU_MCU0_MAP1_MASK		GENMASK(22, 16)
#define SCU_MCU0_MAP1_SHIFT		(16)

#define RFC 880

struct ddr_capacity {
	size_t size;
	int rfc[2];
};

struct sdramc_ac_timing ac_table[] = {
	/* DDR4 1600 */
	{
		DRAM_TYPE_4,
		"DDR4 1600",
		10, 9, 8,
	/*     rcd, rp, ras, rrd, rrd_l, faw, rtp */
		10, 10, 28,  5,   6,	 28,  6,
		2,	/* t_wtr */
		6,	/* t_wtr_l */
		0,	/* t_wtr_a */
		12,	/* t_wtp */
		0,	/* t_rtw */
	/*	ccd_l, dllk, cksre, pd, xp, rfc */
		5, 597,  8,	4,  5,	RFC,
		24,	/* t_mrd */
		0,	/* t_refsbrd */
		0,	/* t_rfcsb */
		0,	/* t_cshsr */
		80,	/* zq */
	},
	/* DDR4 2400 */
	{
		DRAM_TYPE_4,
		"DDR4 2400",
		15, 12, 8,
	/*     rcd, rp, ras, rrd, rrd_l, faw, rtp */
		16, 16, 39, 7, 8, 37, 10,
		4,	/* t_wtr */
		10,	/* t_wtr_l */
		0,	/* t_wtr_a */
		19,	/* t_wtp */
		0,
	/*	ccd_l, dllk, cksre, pd, xp, rfc */
		7, 768,  13,	7,  8,	RFC,
		24,	/* t_mrd */
		0,	/* t_refsbrd */
		0,	/* t_rfcsb */
		0,	/* t_cshsr */
		80,	/* zq */
	},
	/* DDR4 3200 */
	{
		DRAM_TYPE_4,
		"DDR4 3200",
		20, 16, 8,
	/*     rcd, rp, ras, rrd, rrd_l, faw, rtp */
		20, 20, 52, 9, 11, 48, 12,
		4,	/* t_wtr */
		12,	/* t_wtr_l */
		0,	/* t_wtr_a */
		24,	/* t_wtp */
		0,	/* t_rtw */
	/*	ccd_l, dllk, cksre, pd, xp, rfc */
		8, 1023, 16,	8,  10, RFC,
		24,	/* t_mrd */
		0,	/* t_refsbrd */
		0,	/* t_rfcsb */
		0,	/* t_cshsr */
		80,	/* zq */
	},
	/* DDR5 3200 */
	{
		DRAM_TYPE_5,
		"DDR5 3200",
		26, 24, 16,
	/*     rcd, rp, ras, rrd, rrd_l, faw, rtp */
		26, 26, 52,  8,   8,	 40,  12,
		4,	/* t_wtr */
		16,	/* t_wtr_l */
		36,	/* t_wtr_a */
		48,	/* t_wtp */
		0,
	/*	ccd_l, dllk, cksre, pd, xp, rfc */
		8, 1024, 9,	13, 13, RFC,
		23,	/* t_mrd */
		48,	/* t_refsbrd */
		208,	/* t_rfcsb */
		30,	/* t_cshsr */
		48,	/* zq */
	},
};

#define DRAMC_INIT_DONE		BIT(6)
static bool is_ddr_initialized(struct sdramc *sdramc)
{
	if (readl((void *)&sdramc->regs->mctl) & DRAMC_MCTL_PHY_POWER_ON) {
		debug("DDR has been initialized\n");
		return 1;
	}

	return 0;
}

bool is_ddr4(void)
{
	if (IS_ENABLED(CONFIG_ASPEED_FPGA))
		/* made fpga strap reverse */
		return ((readl((void *)SCU_IO_HWSTRAP1) & IO_HWSTRAP1_DRAM_TYPE) ? 0 : 1);

	/* asic strap default 0 is ddr5, 1 is ddr4 */
	return ((readl((void *)SCU_IO_HWSTRAP1) & IO_HWSTRAP1_DRAM_TYPE) ? 1 : 0);
}

#define ACTIME1(ccd, rrd_l, rrd, mrd)	\
	(((ccd) << 24) | (((rrd_l) >> 1) << 16) | (((rrd) >> 1) << 8) | ((mrd) >> 1))

#define ACTIME2(faw, rp, ras, rcd)	\
	((((faw) >> 1) << 24) | (((rp) >> 1) << 16) | (((ras) >> 1) << 8) | ((rcd) >> 1))

#define ACTIME3(wtr, rtw, wtp, rtp)	\
	((((wtr) >> 1) << 24) | \
	(((rtw) >> 1) << 16) | \
	(((wtp) >> 1) << 8) | \
	((rtp) >> 1))

#define ACTIME4(wtr_a, wtr_l)		\
	((((wtr_a) >> 1) << 8) | ((wtr_l) >> 1))

#define ACTIME5(refsbrd, rfcsb, rfc)	\
	((((refsbrd) >> 1) << 20) | (((rfcsb) >> 1) << 10) | ((rfc) >> 1))

#define ACTIME6(cshsr, pd, xp, cksre)	\
	((((cshsr) >> 1) << 24) | (((pd) >> 1) << 16) | (((xp) >> 1) << 8) | ((cksre) >> 1))

#define ACTIME7(zqcs, dllk)	\
	((((zqcs) >> 1) << 10) | ((dllk) >> 1))

static void sdramc_configure_ac_timing(struct sdramc *sdramc, struct sdramc_ac_timing *ac)
{
	struct sdramc_regs *regs = sdramc->regs;

	writel(ACTIME1(ac->t_ccd_l, ac->t_rrd_l, ac->t_rrd, ac->t_mrd),
	       &regs->actime1);
	writel(ACTIME2(ac->t_faw, ac->t_rp, ac->t_ras, ac->t_rcd),
	       &regs->actime2);
	writel(ACTIME3(ac->t_cwl + ac->t_bl / 2 + ac->t_wtr,
		       ac->t_cl - ac->t_cwl + (ac->t_bl / 2) + 2,
		       ac->t_cwl + ac->t_bl / 2 + ac->t_wtp,
		       ac->t_rtp),
	       &regs->actime3);
	writel(ACTIME4(ac->t_cwl + ac->t_bl / 2 + ac->t_wtr_a,
		       ac->t_cwl + ac->t_bl / 2 + ac->t_wtr_l),
	       &regs->actime4);
	writel(ACTIME5(ac->t_refsbrd, ac->t_rfcsb, ac->t_rfc),
	       &regs->actime5);
	writel(ACTIME6(ac->t_cshsr, ac->t_pd, ac->t_xp, ac->t_cksre), &regs->actime6);
	writel(ACTIME7(ac->t_zq, ac->t_dllk), &regs->actime7);
}

static void sdramc_configure_register(struct sdramc *sdramc, struct sdramc_ac_timing *ac)
{
	struct sdramc_regs *regs = sdramc->regs;

	u32 dram_size = 5;
	u32 t_phy_wrdata;
	u32 t_phy_wrlat;
	u32 t_phy_rddata_en;
	u32 t_phy_odtlat;
	u32 t_phy_odtext;

	if (IS_ENABLED(CONFIG_ASPEED_FPGA)) {
		t_phy_wrlat = ac->t_cwl - 6;
		t_phy_rddata_en = ac->t_cl - 5;
		t_phy_wrdata = 1;
		t_phy_odtlat = 1;
		t_phy_odtext = 0;
	} else {
		if (ac->type == DRAM_TYPE_4) {
			t_phy_wrlat = ac->t_cwl - 5 - 4;
			t_phy_rddata_en = ac->t_cl - 5 - 4;
			t_phy_wrdata = 2;
			t_phy_odtlat = ac->t_cwl - 5 - 4;
			t_phy_odtext = 0;
		} else {
			t_phy_wrlat = ac->t_cwl - 13 - 3;
			t_phy_rddata_en = ac->t_cl - 13 - 3;
			t_phy_wrdata = 6;
			t_phy_odtlat = 0;
			t_phy_odtext = 0;
		}
	}

	writel(0x20 + (dram_size << 2) + ac->type, &regs->mcfg);

	/*
	 * [5:0], t_phy_wrlat, for cycles from WR command to write data enable.
	 * [8:6], t_phy_wrdata, for cycles from write data enable to write data.
	 * [9], reserved
	 * [15:10] t_phy_rddata_en, for cycles from RD command to read data enable.
	 * [19:16], t_phy_odtlat, for cycles from WR command to ODT signal control.
	 * [21:20], ODT signal extension control
	 * [22], ODT signal enable
	 * [23], ODT signal auto mode
	 */
	writel((t_phy_odtext << 20) + (t_phy_odtlat << 16) + (t_phy_rddata_en << 10) + (t_phy_wrdata << 6) + t_phy_wrlat, &regs->dfi_timing);
	writel(0, &regs->dctl);

	/*
	 * [31:24]: refresh felxibility time period
	 * [23:16]: refresh time interfal
	 * [15]   : refresh function disable
	 * [14:10]: reserved
	 * [9:6]  : refresh threshold
	 * [5]	  : refresh option
	 * [4]	  : auto MR command sending for mode change
	 * [3]	  : same bank refresh operation
	 * [2]	  : refresh rate selection
	 * [1]	  : refresh mode selection
	 * [0]	  : refresh mode update trigger
	 */
	writel(0x40b48200, &regs->refctl);

	/*
	 * [31:16]: ZQ calibration period
	 * [15:8] : ZQ latch time period
	 * [7]	  : ZQ control status
	 * [6:3]  : reserved
	 * [2]	  : ZQCL command enable
	 * [1]	  : ZQ calibration auto mode
	 */
	writel(0x42aa1800, &regs->zqctl);

	/*
	 * [31:14]: reserved
	 * [13:12]: selection of limited request number for page-hit request
	 * [11]   : enable control of limitation for page-hit request counter
	 * [10]   : arbiter read threshold limitation disable control
	 * [9]	  : arbiter write threshold limitation disable control
	 * [8:5]  : read access limit threshold selection
	 * [4]	  : read request limit threshold enable
	 * [3:1]  : write request limit threshold selection
	 * [0]	  : write request limit enable
	 */
	writel(0, &regs->arbctl);

	if (ac->type)
		writel(0, &regs->refmng_ctl);

	writel(0xffffffff, &regs->intr_mask);
}

static void sdramc_mr_send(struct sdramc *sdramc, u32 ctrl, u32 op)
{
	struct sdramc_regs *regs = sdramc->regs;

	writel(op, &regs->mrwr);
	writel(ctrl | DRAMC_MRCTL_CMD_START, &regs->mrctl);

	while (!(readl(&regs->intr_status) & DRAMC_IRQSTA_MR_DONE))
		;

	writel(DRAMC_IRQSTA_MR_DONE, &regs->intr_clear);
}

static void sdramc_unlock(struct sdramc *sdramc)
{
	struct sdramc_regs *regs = sdramc->regs;

	writel(DRAMC_UNLOCK_KEY, &regs->prot_key);

	while (!readl(&regs->prot_key))
		;
}

static void sdramc_set_flag(u32 flag)
{
	u32 val;

	val = readl((void *)SCU_CPU_VGA0_SCRATCH);
	val |= flag;
	writel(val, (void *)SCU_CPU_VGA0_SCRATCH);

	val = readl((void *)SCU_CPU_VGA1_SCRATCH);
	val |= flag;
	writel(val, (void *)SCU_CPU_VGA1_SCRATCH);
}

static int sdramc_init(struct sdramc *sdramc, struct sdramc_ac_timing **ac)
{
	struct sdramc_ac_timing *tbl = ac_table;
	int speed;

	/* Detect dram type by a hw strap at IO SCU010 */
	if (is_ddr4()) {
		/* DDR4 type */
		if (IS_ENABLED(CONFIG_ASPEED_DDR_1600)) {
			speed = DDR4_1600;
		} else if (IS_ENABLED(CONFIG_ASPEED_DDR_2400)) {
			speed = DDR4_2400;
		} else if (IS_ENABLED(CONFIG_ASPEED_DDR_3200)) {
			speed = DDR4_3200;
		} else {
			debug("Speed %d is not supported!!!\n", speed);
			return 1;
		}
	} else {
		/* DDR5 type */
		speed = DDR5_3200;
	}

	debug("%s is selected\n", tbl[speed].desc);

	/* Configure ac timing */
	sdramc_configure_ac_timing(sdramc, &tbl[speed]);

	/* Configure register */
	sdramc_configure_register(sdramc, &tbl[speed]);

	*ac = &tbl[speed];

	return 0;
}

static void sdramc_phy_init(struct sdramc *sdramc, struct sdramc_ac_timing *ac)
{
	/* initialize phy */
	if (IS_ENABLED(CONFIG_ASPEED_FPGA))
		fpga_phy_init(sdramc);
#ifdef CONFIG_RISCV
	else
		dwc_phy_init(sdramc);
#endif
}

static int sdramc_exit_self_refresh(struct sdramc *sdramc)
{
	struct sdramc_regs *regs = sdramc->regs;

	/* exit self-refresh after phy init */
	setbits(le32, &regs->mctl, DRAMC_MCTL_SELF_REF_START);

	/* query if self-ref done */
	while (!(readl(&regs->intr_status) & DRAMC_IRQSTA_REF_DONE))
		;

	/* clear status */
	writel(DRAMC_IRQSTA_REF_DONE, &regs->intr_clear);

	udelay(1);

	return 0;
}

static void sdramc_enable_refresh(struct sdramc *sdramc)
{
	struct sdramc_regs *regs = sdramc->regs;

	/* refresh update */
	clrbits(le32, &regs->refctl, 0x8000);
}

static void sdramc_configure_mrs(struct sdramc *sdramc, struct sdramc_ac_timing *ac)
{
	struct sdramc_regs *regs = sdramc->regs;
	u32 mr0_cas, mr0_rtp, mr2_cwl, mr6_tccd_l;
	u32 mr0_val, mr1_val, mr2_val, mr3_val, mr4_val, mr5_val, mr6_val;

	if (ac->type == DRAM_TYPE_5)
		return;

	//-------------------------------------------------------------------
	// CAS Latency (Table-15)
	//-------------------------------------------------------------------
	switch (ac->t_cl) {
	case 9:
		mr0_cas = 0x00; //5'b00000;
		break;
	case 10:
		mr0_cas = 0x01; //5'b00001;
		break;
	case 11:
		mr0_cas = 0x02; //5'b00010;
		break;
	case 12:
		mr0_cas = 0x03; //5'b00011;
		break;
	case 13:
		mr0_cas = 0x04; //5'b00100;
		break;
	case 14:
		mr0_cas = 0x05; //5'b00101;
		break;
	case 15:
		mr0_cas = 0x06; //5'b00110;
		break;
	case 16:
		mr0_cas = 0x07; //5'b00111;
		break;
	case 18:
		mr0_cas = 0x08; //5'b01000;
		break;
	case 20:
		mr0_cas = 0x09; //5'b01001;
		break;
	case 22:
		mr0_cas = 0x0a; //5'b01010;
		break;
	case 24:
		mr0_cas = 0x0b; //5'b01011;
		break;
	case 23:
		mr0_cas = 0x0c; //5'b01100;
		break;
	case 17:
		mr0_cas = 0x0d; //5'b01101;
		break;
	case 19:
		mr0_cas = 0x0e; //5'b01110;
		break;
	case 21:
		mr0_cas = 0x0f; //5'b01111;
		break;
	case 25:
		mr0_cas = 0x10; //5'b10000;
		break;
	case 26:
		mr0_cas = 0x11; //5'b10001;
		break;
	case 27:
		mr0_cas = 0x12; //5'b10010;
		break;
	case 28:
		mr0_cas = 0x13; //5'b10011;
		break;
	case 30:
		mr0_cas = 0x15; //5'b10101;
		break;
	case 32:
		mr0_cas = 0x17; //5'b10111;
		break;
	}

	//-------------------------------------------------------------------
	// WR and RTP (Table-14)
	//-------------------------------------------------------------------
	switch (ac->t_rtp) {
	case 5:
		mr0_rtp = 0x0; //4'b0000;
		break;
	case 6:
		mr0_rtp = 0x1; //4'b0001;
		break;
	case 7:
		mr0_rtp = 0x2; //4'b0010;
		break;
	case 8:
		mr0_rtp = 0x3; //4'b0011;
		break;
	case 9:
		mr0_rtp = 0x4; //4'b0100;
		break;
	case 10:
		mr0_rtp = 0x5; //4'b0101;
		break;
	case 12:
		mr0_rtp = 0x6; //4'b0110;
		break;
	case 11:
		mr0_rtp = 0x7; //4'b0111;
		break;
	case 13:
		mr0_rtp = 0x8; //4'b1000;
		break;
	}

	//-------------------------------------------------------------------
	// CAS Write Latency (Table-21)
	//-------------------------------------------------------------------
	switch (ac->t_cwl)  {
	case 9:
		mr2_cwl = 0x0; // 3'b000; // 1600
		break;
	case 10:
		mr2_cwl = 0x1; // 3'b001; // 1866
		break;
	case 11:
		mr2_cwl = 0x2; // 3'b010; // 2133
		break;
	case 12:
		mr2_cwl = 0x3; // 3'b011; // 2400
		break;
	case 14:
		mr2_cwl = 0x4; // 3'b100; // 2666
		break;
	case 16:
		mr2_cwl = 0x5; // 3'b101; // 2933/3200
		break;
	case 18:
		mr2_cwl = 0x6; // 3'b110;
		break;
	case 20:
		mr2_cwl = 0x7; // 3'b111;
		break;
	}

	//-------------------------------------------------------------------
	// tCCD_L and tDLLK
	//-------------------------------------------------------------------
	switch (ac->t_ccd_l) {
	case 4:
		mr6_tccd_l = 0x0; //3'b000;  // rate <= 1333
		break;
	case 5:
		mr6_tccd_l = 0x1; //3'b001;  // 1333 < rate <= 1866
		break;
	case 6:
		mr6_tccd_l = 0x2; //3'b010;  // 1866 < rate <= 2400
		break;
	case 7:
		mr6_tccd_l = 0x3; //3'b011;  // 2400 < rate <= 2666
		break;
	case 8:
		mr6_tccd_l = 0x4; //3'b100;  // 2666 < rate <= 3200
		break;
	}

	/*
	 * mr0_val = {
	 * mr0_rtp[3],		// 13
	 * mr0_cas[4],		// 12
	 * mr0_rtp[2:0],	// 13,11-9: WR and RTP
	 * 1'b0,		// 8: DLL reset
	 * 1'b0,		// 7: TM
	 * mr0_cas[3:1],	// 6-4,2: CAS latency
	 * 1'b0,		// 3: sequential
	 * mr0_cas[0],
	 * 2'b00		// 1-0: burst length
	 */
	mr0_val = ((mr0_cas & 0x1) << 2) | (((mr0_cas >> 1) & 0x7) << 4) | (((mr0_cas >> 4) & 0x1) << 12) |
		  ((mr0_rtp & 0x7) << 9) | (((mr0_rtp >> 3) & 0x1) << 13);

	/*
	 * 3'b2 //[10:8]: rtt_nom, 000:disable,001:rzq/4,010:rzq/2,011:rzq/6,100:rzq/1,101:rzq/5,110:rzq/3,111:rzq/7
	 * 1'b0 //[7]: write leveling enable
	 * 2'b0 //[6:5]: reserved
	 * 2'b0 //[4:3]: additive latency
	 * 2'b0 //[2:1]: output driver impedance
	 * 1'b1 //[0]: enable dll
	 */
	mr1_val = 0x201;

	/*
	 * [10:9]: rtt_wr, 00:dynamic odt off, 01:rzq/2, 10:rzq/1, 11: hi-z
	 * [8]: 0
	 */
	mr2_val = ((mr2_cwl & 0x7) << 3) | 0x200;

	mr3_val = 0;

	mr4_val = 0;

	/*
	 * mr5_val = {
	 * 1'b0,		// 13: RFU
	 * 1'b0,		// 12: read DBI
	 * 1'b0,		// 11: write DBI
	 * 1'b1,		// 10: Data mask
	 * 1'b0,		// 9: C/A parity persistent error
	 * 3'b000,		// 8-6: RTT_PARK (disable)
	 * 1'b1,		// 5: ODT input buffer during power down mode
	 * 1'b0,		// 4: C/A parity status
	 * 1'b0,		// 3: CRC error clear
	 * 3'b0			// 2-0: C/A parity latency mode
	 * };
	 */
	mr5_val = 0x420;

	/*
	 * mr6_val = {
	 * 1'b0,		// 13, 9-8: RFU
	 * mr6_tccd_l[2:0],	// 12-10: tCCD_L
	 * 2'b0,		// 13, 9-8: RFU
	 * 1'b0,		// 7: VrefDQ training enable
	 * 1'b0,		// 6: VrefDQ training range
	 * 6'b0			// 5-0: VrefDQ training value
	 * };
	 */
	mr6_val = ((mr6_tccd_l & 0x7) << 10);

	writel((mr1_val << 16) + mr0_val, &regs->mr01);
	writel((mr3_val << 16) + mr2_val, &regs->mr23);
	writel((mr5_val << 16) + mr4_val, &regs->mr45);
	writel(mr6_val, &regs->mr67);

	/* Power-up initialization sequence */
	sdramc_mr_send(sdramc, MR_ADDR(3), 0);
	sdramc_mr_send(sdramc, MR_ADDR(6), 0);
	sdramc_mr_send(sdramc, MR_ADDR(5), 0);
	sdramc_mr_send(sdramc, MR_ADDR(4), 0);
	sdramc_mr_send(sdramc, MR_ADDR(2), 0);
	sdramc_mr_send(sdramc, MR_ADDR(1), 0);
	sdramc_mr_send(sdramc, MR_ADDR(0), 0);
}

static int sdramc_bist(struct sdramc *sdramc, u32 addr, u32 size, u32 cfg, u32 timeout)
{
	struct sdramc_regs *regs = sdramc->regs;
	u32 val;
	u32 err = 0;

	writel(0, &regs->bistcfg);
	writel(cfg, &regs->bistcfg);
	writel(addr >> 4, &regs->bist_addr);
	writel(size, &regs->bist_size);
	writel(0x89abcdef, &regs->bist_patt);
	writel(cfg | DRAMC_BISTCFG_START, &regs->bistcfg);

	while (!(readl(&regs->intr_status) & DRAMC_IRQSTA_BIST_DONE))
		;

	writel(DRAMC_IRQSTA_BIST_DONE, &regs->intr_clear);

	val = readl(&regs->bist_res);

	/* bist done */
	if (val & DRAMC_BISTRES_DONE) {
		/* bist pass [9]=0 */
		if (val & DRAMC_BISTRES_FAIL)
			err++;
	} else {
		err++;
	}

	return err;
}

static void sdramc_aes_enable(struct sdramc *sdramc)
{
	struct sdramc_regs *regs = sdramc->regs;
	u32 addr_min = 0;
	u32 addr_max = sdramc->aes_size;

	if (!sdramc->aes_enable) {
		debug("AES is not enabled\n");
		return;
	}

	writel(addr_min >> 4, &regs->enc_min_addr);
	writel(addr_max >> 4, &regs->enc_max_addr);
	writel(1, &regs->enccfg);
}

#define DRAM_SIZE_DEF	3
static size_t ast2700_sdrammc_get_vga_mem_size(struct sdramc *sdramc)
{
	struct sdramc_regs *regs = sdramc->regs;
	u32 vga_ram_size[] = {
		0x2000000, // 32MB
		0x4000000, // 64MB
		};
	int vga_sz_sel;
	int vga_cnt;

	vga_sz_sel = readl(&regs->gfmcfg) & 0x1;

	vga_cnt = ast_vga_get_nodes(NULL, NULL);

	if (!IS_ENABLED(CONFIG_SPL_BUILD)) {
		if (vga_cnt > 1)
			printf("VGA1:%dMiB, ", vga_ram_size[vga_sz_sel] / SZ_1M);
		if (vga_cnt > 0)
			printf("VGA0:%dMiB, ", vga_ram_size[vga_sz_sel] / SZ_1M);
	}

	return vga_ram_size[vga_sz_sel] * vga_cnt;
}

static int sdramc_ecc_enable(struct sdramc *sdramc)
{
	u32 ram_size_ary[] = {
		0x1000000, // 256MB
		0x2000000, // 512MB
		0x4000000, // 1GB
		0x8000000, // 2GB
		0x10000000, // 4GB
		0x20000000, // 8GB
		};
	u32 ecc_sz, ram_size;
	struct sdramc_regs *regs = sdramc->regs;
	u32 bistcfg;
	u32 val;
	int err;

	if (!sdramc->ecc_enable) {
		debug("ECC is not enabled\n");
		return 0;
	}

	/* declare a size arrary that already shifted by 4 */
	ram_size = ram_size_ary[sdramc->sz];

	/* Clean up all the dram for ECC redundant */
	bistcfg = 0x82;
	err = sdramc_bist(sdramc, 0, ram_size, bistcfg, 0x200000);
	if (err) {
		printf("ecc bist failed\n");
		return err;
	}

	/*
	 * When ecc enabled without given a size, it will use the full dram size.
	 * The rule of ecc range is (dram size - vga memory size) * 8 / 9.
	 */
	if (sdramc->ecc_size == 0)
		ecc_sz = (ram_size - (ast2700_sdrammc_get_vga_mem_size(sdramc) >> 4)) * 8 / 9;
	else
		ecc_sz = sdramc->ecc_size >> 4;

	writel(ecc_sz, &regs->ecc_addr_range);

	/* enable ecc, page matching should be disabled */
	val = readl(&regs->mcfg);
	val &= ~(DRAMC_MCFG_PGM_EN);
	val |= (DRAMC_MCFG_ECC_EN);
	writel(val, &regs->mcfg);

	return err;
}

static void sdramc_qos_init(struct sdramc *sdramc)
{
	/* raise SLI write/read priority */
	writel(QOS_SLI_LEVEL(10),
	       (void *)&sdramc->regs->port[4].write_qos);
	writel(QOS_SLI_LEVEL(9),
	       (void *)&sdramc->regs->port[4].read_qos);
	writel(DRAMC_PORT_CFG_RDQOS_EN | DRAMC_PORT_CFG_WRQOS_EN | DEFAULT_RDQOS_LEVEL,
	       (void *)&sdramc->regs->port[4].cfg);

	/* raise usb 2.0 B1/B2, vga1 priority */
	writel(QOS_USB2_B1_LEVEL(9) | QOS_USB2_B2_LEVEL(9) | QOS_VGA1_CR_LEVEL(9),
	       (void *)&sdramc->regs->port[2].read_qos);
	writel(DRAMC_PORT_CFG_RDQOS_EN | DRAMC_PORT_CFG_WRQOS_EN | DEFAULT_RDQOS_LEVEL,
	       (void *)&sdramc->regs->port[2].cfg);

	/* raise vga2 priority */
	writel(QOS_VGA2_CR_LEVEL(9),
	       (void *)&sdramc->regs->port[3].read_qos);
	writel(DRAMC_PORT_CFG_RDQOS_EN | DRAMC_PORT_CFG_WRQOS_EN | DEFAULT_RDQOS_LEVEL,
	       (void *)&sdramc->regs->port[3].cfg);

	/* raise u2 A1/A2 priority */
	writel(QOS_USB2_A1_LEVEL(9) | QOS_USB2_A2_LEVEL(9),
	       (void *)&sdramc->regs->port[1].read_qos);
	writel(DRAMC_PORT_CFG_RDQOS_EN | DRAMC_PORT_CFG_WRQOS_EN | DEFAULT_RDQOS_LEVEL,
	       (void *)&sdramc->regs->port[1].cfg);
}

static int sdramc_size_detect(struct sdramc *sdramc)
{
	struct sdramc_regs *regs = sdramc->regs;
	struct ddr_capacity ram_size[] = {
		{0x40,	{208, 256}}, // 256MB
		{0x40,	{208, 416}}, // 512MB
		{0x40,	{208, 560}}, // 1GB
		{0x44,	{472, 880}}, // 2GB
		{0x48,	{656, 880}}, // 4GB
		{0x50,	{880, 880}}, // 8GB
		};
	u32 val;
	int sz, ddr4;
	u32 pattern = 0xdeadbeef;
	void *test_addr = (void *)0xc0000000;
	void *start_addr = (void *)0x80000000;

	/* Assume the mimimum dram size is 1GB, hence test starts from 2GB */
	for (sz = SDRAM_SIZE_2GB; sz < SDRAM_SIZE_MAX; sz++) {
		/* change mapping address for mcu0 upper 1G space */
		writel((readl((void *)SCU_IO_MCU0_CTRL) & ~SCU_MCU0_MAP1_MASK)
		       | (ram_size[sz].size << SCU_MCU0_MAP1_SHIFT),
		       (void *)SCU_IO_MCU0_CTRL);

		/* test if pattern wrapped around */
		writel(pattern, test_addr);

		/* prevent RAW hazzard */
		mdelay(1);

		/* if it is wrapped around, the size should be smaller one */
		if (readl(start_addr) == pattern)
			break;

		pattern = pattern >> 4;
	}

	sz--;
	sdramc->sz = sz;

	/* re-configure ram size to dramc. */
	val = readl(&regs->mcfg);
	val &= ~(0x7 << 2);
	writel(val | (sz << 2), &regs->mcfg);

	ddr4 = is_ddr4();

	/* update rfc in ac_timing5 register. */
	val = readl(&regs->actime5);
	val &= ~(0x3ff);
	val |= (ram_size[sz].rfc[ddr4] >> 1);
	writel(val, &regs->actime5);

	return 0;
}

static void sdramc_get_property(struct udevice *dev)
{
	struct sdramc *sdramc = (struct sdramc *)dev_get_priv(dev);
	uint32_t phandle;
	ofnode subnode;
	const char *name;
	char prop_name[8];
	u32 protect_start, protect_end;
	int i, j;
	int err = 0;
	int len;

	if (dev_read_bool(dev, "ecc-enable")) {
		sdramc->ecc_enable = 1;
		sdramc->ecc_size = dev_read_u32_default(dev, "ecc-size", 0);
	}

	if (dev_read_bool(dev, "aes-enable")) {
		sdramc->aes_enable = 1;
		sdramc->aes_size = dev_read_u32_default(dev, "aes-size", 0);
	}

	for (i = 0; i < MAX_MPU_COUNT; i++) {
		snprintf(prop_name, sizeof(prop_name), "%s%d", MPU_PROP_NAME, i);

		j = sdramc->mpu_cnt;

		if (dev_read_bool(dev, prop_name)) {
			err = ofnode_read_u32(dev_ofnode(dev), prop_name, &phandle);
			if (err)
				continue;

			subnode = ofnode_get_by_phandle(phandle);
			if (!ofnode_valid(subnode))
				continue;

			name = ofnode_read_string(subnode, "protect-name");

			if (ofnode_read_u32(subnode, "protect-start", &protect_start) ||
			    ofnode_read_u32(subnode, "protect-end", &protect_end) ||
			    !ofnode_read_prop(subnode, "allow", &len) || !len)
				continue;

			sdramc->mpu[j].start = protect_start;
			sdramc->mpu[j].end = protect_end;
			sdramc->mpu[j].allow = (struct mpu_allow *)malloc(len);
			if (!sdramc->mpu[j].allow)
				continue;

			ofnode_read_u32_array(subnode, "allow", (u32 *)sdramc->mpu[j].allow, len / sizeof(u32));

			sdramc->mpu[j].allow_cnt = len / sizeof(struct mpu_allow);

			memcpy(sdramc->mpu[j].name, name, strlen(name));

			sdramc->mpu_cnt++;
		}
	}
}

struct mpu_attr {
	u32 attr;
	const char *str;
};

struct mpu_id {
	int id;
	const char *str;
	u8 ofst;
	u32 mask;
};

static int sdramc_mpu_init(struct sdramc *sdramc)
{
	struct sdramc_regs *regs = sdramc->regs;
	u32 val;
	int id, attr;
	int i;
	char *name;
	struct mpu_attr attr_info[] = {
		{S_READWRITE, "_rw"},
		{S_READONLY, "_ro"},
		{S_WRITEONLY, "_wo"},
		{NS_READWRITE, "_nrw"},
		{NS_READONLY, "_nro"},
		{NS_WRITEONLY, "_nwo"},
	};

	struct mpu_id id_info[] = {
		{MPU_ID_CA35,	"ca35", 0x00, BIT(4)},
		{MPU_ID_VE_HI,	"ve_hi", 0x10, BIT(0)},
		{MPU_ID_VE_LO,	"ve_lo", 0x10, BIT(1)},
		{MPU_ID_USB_A1, "usb_a1", 0x10, BIT(2)},
		{MPU_ID_USB_A2, "usb_a2", 0x10, BIT(3)},
		{MPU_ID_E2M,	"e2m", 0x10, BIT(4)},
		{MPU_ID_MCTP,	"mctp", 0x10, BIT(5)},
		{MPU_ID_H2M,	"h2m", 0x10, BIT(6)},
		{MPU_ID_HMAC,	"hmac", 0x10, BIT(7)},
		{MPU_ID_USB_B1, "usb_b1", 0x10, BIT(16)},
		{MPU_ID_USB_B2, "usb_b2", 0x10, BIT(17)},
		{MPU_ID_VGA1_CR, "vga1_cr", 0x10, BIT(18)},
		{MPU_ID_VGA1_LE, "vga1_le", 0x10, BIT(19)},
		{MPU_ID_TSP_INST, "tsp_i", 0x10, BIT(20)},
		{MPU_ID_VE,	"ve", 0x10, BIT(21)},
		{MPU_ID_MCTP8,	"mctp8", 0x10, BIT(22)},
		{MPU_ID_UHCI,	"uhci", 0x10, BIT(23)},
		{MPU_ID_USB3_A1, "usb3_a1", 0x14, BIT(0)},
		{MPU_ID_USB3_A2, "usb3_a2", 0x14, BIT(1)},
		{MPU_ID_SHA3,	"sha3", 0x14, BIT(2)},
		{MPU_ID_VGA2_CR, "vga2_cr", 0x14, BIT(3)},
		{MPU_ID_VGA2_LE, "vga2_le", 0x14, BIT(4)},
		{MPU_ID_TSP_DATA, "tsp_d", 0x14, BIT(5)},
		{MPU_ID_E2M1,	"e2m1", 0x14, BIT(6)},
		{MPU_ID_GFX,	"gfx", 0x14, BIT(7)},
		{MPU_ID_RVAS1,	"rvas1", 0x14, BIT(8)},
		{MPU_ID_RVAS2,	"rvas2", 0x14, BIT(9)},
		{MPU_ID_MHMAC,	"mhmac", 0x14, BIT(10)},
		{MPU_ID_M2D,	"m2d", 0x14, BIT(11)},
		{MPU_ID_M2D2,	"m2d2", 0x14, BIT(12)},
		{MPU_ID_SSP_INST, "ssp_i", 0x14, BIT(13)},
		{MPU_ID_SSP_DATA, "ssp_d", 0x14, BIT(14)},
		{MPU_ID_XDMA8,	"xdma8", 0x14, BIT(15)},
		{MPU_ID_XDMA,	"xdma", 0x14, BIT(16)},
		{MPU_ID_SDIO,	"sdio", 0x14, BIT(17)},
		{MPU_ID_SLIM,	"slim", 0x14, BIT(19)},
		{MPU_ID_USBH_A, "usbh_a", 0x14, BIT(24)},
		{MPU_ID_USBH_B, "usbh_b", 0x14, BIT(25)},
		{MPU_ID_UFS,	"ufs", 0x14, BIT(26)},
	};

	for (i = 0; i < sdramc->mpu_cnt; i++) {
		/* define mpu range */
		writel(sdramc->mpu[i].start >> 4, &regs->region[i].start);
		writel((sdramc->mpu[i].end - 1) >> 4, &regs->region[i].end);

		/* protect from all master by default */
		writel(0x00000030, &regs->region[i].ctrl);
		writel(0xffffffff, &regs->region[i].wr_master_0);
		writel(0xffffffff, &regs->region[i].wr_master_1);
		writel(0xffffffff, &regs->region[i].rd_master_0);
		writel(0xffffffff, &regs->region[i].rd_master_1);
		writel(0x00000000, &regs->region[i].wr_secure_0);
		writel(0x00000000, &regs->region[i].wr_secure_1);
		writel(0x00000000, &regs->region[i].rd_secure_0);
		writel(0x00000000, &regs->region[i].rd_secure_1);

		name = malloc(128);
		if (!name)
			return -ENOMEM;

		memset(name, 0, 128);

		/* define mpu policy */
		for (int j = 0; j < sdramc->mpu[i].allow_cnt; j++) {
			id = sdramc->mpu[i].allow[j].id;
			attr = sdramc->mpu[i].allow[j].attr;

			switch (attr) {
			case S_READWRITE:
				if (id == MPU_ID_CA35) {
					writel(0xf0, (void *)&regs->region[i].ctrl);
					break;
				}
				val = readl((u8 *)&regs->region[i].ctrl + id_info[id].ofst);
				writel(val & ~id_info[id].mask, (u8 *)&regs->region[i].ctrl + id_info[id].ofst);
				val = readl((u8 *)&regs->region[i].ctrl + id_info[id].ofst + 0x8);
				writel(val & ~id_info[id].mask, (u8 *)&regs->region[i].ctrl + id_info[id].ofst + 0x8);
				val = readl((u8 *)&regs->region[i].ctrl + id_info[id].ofst + 0x10);
				writel(val & ~id_info[id].mask, (u8 *)&regs->region[i].ctrl + id_info[id].ofst + 0x10);
				val = readl((u8 *)&regs->region[i].ctrl + id_info[id].ofst + 0x18);
				writel(val & ~id_info[id].mask, (u8 *)&regs->region[i].ctrl + id_info[id].ofst + 0x18);
				break;
			case S_READONLY:
				if (id == MPU_ID_CA35) {
					writel(0xb0, (void *)&regs->region[i].ctrl);
					break;
				}
				val = readl((u8 *)&regs->region[i].ctrl + id_info[id].ofst + 0x8);
				writel(val & ~id_info[id].mask, (u8 *)&regs->region[i].ctrl + id_info[id].ofst + 0x8);
				val = readl((u8 *)&regs->region[i].ctrl + id_info[id].ofst + 0x18);
				writel(val & ~id_info[id].mask, (u8 *)&regs->region[i].ctrl + id_info[id].ofst + 0x18);
				break;
			case S_WRITEONLY:
				if (id == MPU_ID_CA35) {
					writel(0x70, (void *)&regs->region[i].ctrl);
					break;
				}
				val = readl((u8 *)&regs->region[i].ctrl + id_info[id].ofst);
				writel(val & ~id_info[id].mask, (u8 *)&regs->region[i].ctrl + id_info[id].ofst);
				val = readl((u8 *)&regs->region[i].ctrl + id_info[id].ofst + 0x10);
				writel(val & ~id_info[id].mask, (u8 *)&regs->region[i].ctrl + id_info[id].ofst + 0x10);
				break;
			case NS_READWRITE:
				if (id == MPU_ID_CA35) {
					writel(0x00, (void *)&regs->region[i].ctrl);
					break;
				}
				break;
			case NS_READONLY:
				break;
			case NS_WRITEONLY:
				break;
			default:
				break;
			};

			strcat(name, id_info[id].str);
			strcat(name, attr_info[attr].str);
			strcat(name, ",");
		}

		printf("[mpu-%d:%s:	0x%08x~0x%08x] %s\n", i, sdramc->mpu[i].name, sdramc->mpu[i].start, sdramc->mpu[i].end, name);
		free(name);
	}

	return 0;
}

void sdramc_mpu_enable(struct udevice *dev)
{
	struct sdramc *sdramc = (struct sdramc *)dev_get_priv(dev);
	int i;

	for (i = 0; i < sdramc->mpu_cnt; i++)
		setbits_le32(&sdramc->regs->region[i].ctrl, DRAMC_MPU_EN);
}

static int sdram_init(struct udevice *dev)
{
	struct sdramc *sdramc = (struct sdramc *)dev_get_priv(dev);
	struct sdramc_ac_timing *ac;
	u32 bistcfg;
	int err = 0;

	if (is_ddr_initialized(sdramc))
		return 0;

	sdramc_unlock(sdramc);

	err = sdramc_init(sdramc, &ac);
	if (err)
		return err;

	sdramc_phy_init(sdramc, ac);

	sdramc_exit_self_refresh(sdramc);

	sdramc_configure_mrs(sdramc, ac);

	sdramc_enable_refresh(sdramc);

	if (IS_ENABLED(CONFIG_SPL_BUILD) && IS_ENABLED(CONFIG_ASPEED_MPU))
		sdramc_get_property(dev);

	bistcfg = FIELD_PREP(DRAMC_BISTCFG_PMODE, BIST_PMODE_CRC)
		| FIELD_PREP(DRAMC_BISTCFG_BMODE, BIST_BMODE_RW_SWITCH)
		| DRAMC_BISTCFG_ENABLE;

	err = sdramc_bist(sdramc, 0, 0x10000, bistcfg, 0x200000);
	if (err) {
		printf("%s bist is failed\n", ac->desc);
		return err;
	}

	sdramc_size_detect(sdramc);

	sdramc_ecc_enable(sdramc);
	sdramc_aes_enable(sdramc);

	if (IS_ENABLED(CONFIG_SPL_BUILD) && IS_ENABLED(CONFIG_ASPEED_MPU))
		sdramc_mpu_init(sdramc);

	sdramc_qos_init(sdramc);

	debug("%s is successfully initialized\n", ac->desc);
	sdramc_set_flag(DRAMC_INIT_DONE);

	return 0;
}

static int ast2700_sdrammc_probe(struct udevice *dev)
{
	int err;

	err = sdram_init(dev);

	return err;
}

static int ast2700_sdrammc_of_to_plat(struct udevice *dev)
{
	struct sdramc *sdramc = dev_get_priv(dev);

	sdramc->regs = (void *)(uintptr_t)devfdt_get_addr_index(dev, 0);
	sdramc->phy_setting = (void *)(uintptr_t)devfdt_get_addr_index(dev, 1);

	return 0;
}

#ifndef CONFIG_RISCV
static int ast2700_sdrammc_calc_size(struct sdramc *sdramc)
{
	struct sdramc_regs *regs = sdramc->regs;
	struct ddr_capacity ram_size[] = {
		{0x10000000,	{208, 256}}, // 256MB
		{0x20000000,	{208, 416}}, // 512MB
		{0x40000000,	{208, 560}}, // 1GB
		{0x80000000,	{472, 880}}, // 2GB
		{0x100000000,	{656, 880}}, // 4GB
		{0x200000000,	{880, 880}}, // 8GB
		};
	int sz;

	/* size already get in the spl */
	sz = (readl(&regs->mcfg) >> 2) & 0x7;
	sdramc->info.base = CFG_SYS_SDRAM_BASE;
	sdramc->info.size = ram_size[sz].size - ast2700_sdrammc_get_vga_mem_size(sdramc);
	return 0;
}

static int ast2700_sdrammc_get_info(struct udevice *dev, struct ram_info *info)
{
	struct sdramc *sdramc = dev_get_priv(dev);
	struct sdramc_regs *regs = sdramc->regs;

	if (regs->mcfg & DRAMC_MCFG_ECC_EN) {
		ast2700_sdrammc_get_vga_mem_size(sdramc);

		info->base = CFG_SYS_SDRAM_BASE;
		info->size = regs->ecc_addr_range << 4;
		printf("ECC on, ");
	} else {
		ast2700_sdrammc_calc_size(sdramc);
		info->base = CFG_SYS_SDRAM_BASE;
		info->size = sdramc->info.size;
	}

	if (regs->enccfg & 0x1)
		printf("AES on, ");

	return 0;
}

static struct ram_ops ast2700_sdrammc_ops = {
	.get_info = ast2700_sdrammc_get_info,
};
#endif

static const struct udevice_id ast2700_sdrammc_ids[] = {
	{ .compatible = "aspeed,ast2700-sdrammc" },
	{ }
};

U_BOOT_DRIVER(sdrammc_ast2700) = {
	.name = "aspeed_ast2700_sdrammc",
	.id = UCLASS_RAM,
	.of_match = ast2700_sdrammc_ids,
#ifndef CONFIG_RISCV
	.ops = &ast2700_sdrammc_ops,
#endif
	.of_to_plat = ast2700_sdrammc_of_to_plat,
	.probe = ast2700_sdrammc_probe,
	.priv_auto = sizeof(struct sdramc),
};
