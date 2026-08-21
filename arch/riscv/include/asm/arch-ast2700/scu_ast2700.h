/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) Aspeed Technology Inc.
 */
#ifndef _ASM_ARCH_SCU_AST2700_H
#define _ASM_ARCH_SCU_AST2700_H

/* SoC0 SCU Register */
#define SCU_CPU_REVISION_ID_HW			GENMASK(23, 16)
#define SCU_CPU_REVISION_ID_EFUSE		GENMASK(15, 8)

#define SCU_CPU_HWSTRAP_DIS_RVAS		BIT(30)
#define SCU_CPU_HWSTRAP_DP_SRC			BIT(29)
#define SCU_CPU_HWSTRAP_DAC_SRC			BIT(28)
#define SCU_CPU_HWSTRAP_VRAM_SIZE		GENMASK(11, 10)
#define SCU_CPU_HWSTRAP_DIS_CPU			BIT(0)

#define SCU_CPU_MISC_DP_RESET_SRC		BIT(11)
#define SCU_CPU_MISC_XDMA_CLIENT_EN		BIT(4)
#define SCU_CPU_MISC_2D_CLIENT_EN		BIT(3)

#define SCU_CPU_RST_SSP				BIT(30)
#define SCU_CPU_RST_DPMCU			BIT(29)
#define SCU_CPU_RST_DP				BIT(28)
#define SCU_CPU_RST_XDMA1			BIT(26)
#define SCU_CPU_RST_XDMA0			BIT(25)
#define SCU_CPU_RST_EMMC			BIT(17)
#define SCU_CPU_RST_EN_DP_PCI			BIT(15)
#define SCU_CPU_RST_CRT				BIT(13)
#define SCU_CPU_RST_RVAS1			BIT(10)
#define SCU_CPU_RST_RVAS0			BIT(9)
#define SCU_CPU_RST_2D				BIT(7)
#define SCU_CPU_RST_VIDEO			BIT(6)
#define SCU_CPU_RST_SOC				BIT(5)
#define SCU_CPU_RST_DDRPHY			BIT(1)

#define SCU_CPU_RST2_VGA			BIT(12)
#define SCU_CPU_RST2_E2M1			BIT(11)
#define SCU_CPU_RST2_E2M0			BIT(10)
#define SCU_CPU_RST2_TSP			BIT(9)
#define SCU_CPU_RST2_USBB_VHUB			BIT(3)
#define SCU_CPU_RST2_USBA_VHUB			BIT(0)

#define SCU_CPU_VGA_FUNC_DAC_OUTPUT		GENMASK(11, 10)
#define SCU_CPU_VGA_FUNC_DP_OUTPUT		GENMASK(9, 8)
#define SCU_CPU_VGA_FUNC_DAC_DISABLE		BIT(7)

#define SCU_CPU_PCI_MISC0C_FB_SIZE		GENMASK(4, 0)

#define SCU_CPU_PCI_MISC70_EN_XHCI		BIT(3)
#define SCU_CPU_PCI_MISC70_EN_EHCI		BIT(2)
#define SCU_CPU_PCI_MISC70_EN_IPMI		BIT(1)
#define SCU_CPU_PCI_MISC70_EN_VGA		BIT(0)

#define SCU_CPU_HPLL_P				GENMASK(22, 19)
#define SCU_CPU_HPLL_N				GENMASK(18, 13)
#define SCU_CPU_HPLL_M				GENMASK(12, 0)

#define SCU_CPU_HPLL2_LOCK			BIT(31)
#define SCU_CPU_HPLL2_BWADJ			GENMASK(11, 0)

/* SSP control register 0 */
#define SCU_CPU_SSP_TSP_RESET_STS		BIT(8)
#define SCU_CPU_SSP_TSP_SRAM_SD			BIT(7)
#define SCU_CPU_SSP_TSP_SRAM_DSLP		BIT(6)
#define SCU_CPU_SSP_TSP_SRAM_SLP		BIT(5)
#define SCU_CPU_SSP_TSP_NIDEN			BIT(4)
#define SCU_CPU_SSP_TSP_DBGEN			BIT(3)
#define SCU_CPU_SSP_TSP_DBG_ENABLE		BIT(2)
#define SCU_CPU_SSP_TSP_RESET			BIT(1)
#define SCU_CPU_SSP_TSP_ENABLE			BIT(0)

/* SSP control register 6 */
#define SCU_CPU_SSP_TSP_CTRL_ICACHE_EN		BIT(1)
#define SCU_CPU_SSP_TSP_CTRL_DCACHE_EN		BIT(0)

/* SoC1 SCU Register */
#define SCU_IO_HWSTRAP_UFS			BIT(23)
#define SCU_IO_HWSTRAP_EMMC			BIT(11)
#define SCU_IO_HWSTRAP_LTPI0_EN			BIT(3)
#define SCU_IO_HWSTRAP_LTPI1_EN			BIT(1)

/* CLK information */
#define CLKIN_25M 25000000UL

#define SCU_CPU_CLKGATE1_RVAS1			BIT(28)
#define SCU_CPU_CLKGATE1_RVAS0			BIT(25)
#define SCU_CPU_CLKGATE1_E2M1			BIT(19)
#define SCU_CPU_CLKGATE1_DP			BIT(18)
#define SCU_CPU_CLKGATE1_DAC			BIT(17)
#define SCU_CPU_CLKGATE1_USBA			BIT(14)
#define SCU_CPU_CLKGATE1_E2M0			BIT(12)
#define SCU_CPU_CLKGATE1_VGA1			BIT(10)
#define SCU_CPU_CLKGATE1_USBB			BIT(7)
#define SCU_CPU_CLKGATE1_VGA0			BIT(5)

/*
 * Clock divider/multiplier configuration struct.
 * For H-PLL and M-PLL the formula is
 * (Output Frequency) = CLKIN * ((M + 1) / (N + 1)) / (P + 1)
 * M - Numerator
 * N - Denumerator
 * P - Post Divider
 * They have the same layout in their control register.
 *
 */
union ast2700_pll_reg {
	uint32_t w;
	struct {
		uint16_t m : 13;		/* bit[12:0]	*/
		uint8_t n : 6;			/* bit[18:13]	*/
		uint8_t p : 4;			/* bit[22:19]	*/
		uint8_t off : 1;		/* bit[23]	*/
		uint8_t bypass : 1;		/* bit[24]	*/
		uint8_t reset : 1;		/* bit[25]	*/
		uint8_t reserved : 6;		/* bit[31:26]	*/
	} b;
};

struct ast2700_pll_cfg {
	union ast2700_pll_reg reg;
	unsigned int ext_reg;
};

struct ast2700_pll_desc {
	uint32_t in;
	uint32_t out;
	struct ast2700_pll_cfg cfg;
};

struct aspeed_clks {
	ulong id;
	const char *name;
};

#ifndef __ASSEMBLY__
struct ast2700_scu0 {
	uint32_t chip_id1;		/* 0x000 */
	uint32_t rsv_0x04[3];		/* 0x004 ~ 0x00C */
	uint32_t hwstrap1;		/* 0x010 */
	uint32_t hwstrap1_clr;		/* 0x014 */
	uint32_t rsv_0x18[2];		/* 0x018 ~ 0x01C */
	uint32_t hwstrap1_lock;		/* 0x020 */
	uint32_t hwstrap1_sec1;	/* 0x024 */
	uint32_t hwstrap1_sec2;	/* 0x028 */
	uint32_t hwstrap1_sec3;	/* 0x02C */
	uint32_t rsv_0x30[8];		/* 0x030 ~ 0x4C */
	uint32_t sysrest_log1;		/* 0x050 */
	uint32_t sysrest_log1_sec1;	/* 0x054 */
	uint32_t sysrest_log1_sec2;	/* 0x058 */
	uint32_t sysrest_log1_sec3;	/* 0x05C */
	uint32_t sysrest_log2;		/* 0x060 */
	uint32_t sysrest_log2_sec1;	/* 0x064 */
	uint32_t sysrest_log2_sec2;	/* 0x068 */
	uint32_t sysrest_log2_sec3;	/* 0x06C */
	uint32_t sysrest_log3;		/* 0x070 */
	uint32_t sysrest_log3_sec1; /* 0x074 */
	uint32_t sysrest_log3_sec2; /* 0x078 */
	uint32_t sysrest_log3_sec3; /* 0x07C */
	uint32_t rsv_0x80[8];		/* 0x080 ~ 0x9C */
	uint32_t probe_sig_select;	/* 0x0A0 */
	uint32_t probe_sig_enable1;	/* 0x0A4 */
	uint32_t probe_sig_enable2; /* 0x0A8 */
	uint32_t uart_dbg_rate;		/* 0x0AC */
	uint32_t rsv_0xB0[4];		/* 0x0B0 ~ 0xBC*/
	uint32_t misc;			/* 0x0C0 */
	uint32_t rsv_0xC4;		/* 0x0C4 */
	uint32_t debug_ctrl;		/* 0x0C8 */
	uint32_t rsv_0xCC[5];		/* 0x0CC ~ 0x0DC */
	uint32_t free_counter_read_low;		/* 0x0E0 */
	uint32_t free_counter_read_high;	/* 0x0E4 */
	uint32_t rsv_0xE8[2];		/* 0x0E8 ~ 0x0EC */
	uint32_t random_num_ctrl;	/* 0x0F0 */
	uint32_t random_num_data;	/* 0x0F4 */
	uint32_t rsv_0xF8[10];		/* 0x0F8 ~ 0x11C */
	uint32_t ssp_ctrl_0;		/* 0x120 */
	uint32_t ssp_ctrl_1;		/* 0x124 */
	uint32_t ssp_ctrl_2;		/* 0x128 */
	uint32_t ssp_ctrl_3;		/* 0x12C */
	uint32_t ssp_ctrl_4;		/* 0x130 */
	uint32_t ssp_ctrl_5;		/* 0x134 */
	uint32_t ssp_ctrl_6;		/* 0x138 */
	uint32_t rsv_0x13c[1];		/* 0x13C */
	uint32_t ssp_tcm_base;		/* 0x140 */
	uint32_t ssp_tcm_size;		/* 0x144 */
	uint32_t ssp_ahb_base;		/* 0x148 */
	uint32_t ssp_ahb_size;		/* 0x14c */
	uint32_t ssp_memory_base;	/* 0x150 */
	uint32_t ssp_memory_size;	/* 0x154 */
	uint32_t rsv_0x158[2];		/* 0x158 ~ 0x15C */
	uint32_t tsp_ctrl_0;		/* 0x160 */
	uint32_t rsv_0x164[1];		/* 0x164 */
	uint32_t tsp_ctrl_1;		/* 0x168 */
	uint32_t tsp_ctrl_2;		/* 0x16C */
	uint32_t tsp_ctrl_3;		/* 0x170 */
	uint32_t tsp_ctrl_4;		/* 0x174 */
	uint32_t tsp_ctrl_5;		/* 0x178 */
	uint32_t rsv_0x17c[6];		/* 0x17C ~ 0x190 */
	uint32_t tsp_remap_size;	/* 0x194 */
	uint32_t rsv_0x198[26];		/* 0x198 ~ 0x1FC */
	uint32_t modrst1_ctrl;		/* 0x200 */
	uint32_t modrst1_clr;		/* 0x204 */
	uint32_t rsv_0x208[2];		/* 0x208 ~ 0x20C */
	uint32_t modrst1_lock;		/* 0x210 */
	uint32_t modrst1_prot1;		/* 0x214 */
	uint32_t modrst1_prot2;		/* 0x218 */
	uint32_t modrst1_prot3;		/* 0x21C */
	uint32_t modrst2_ctrl;		/* 0x220 */
	uint32_t modrst2_clr;		/* 0x224 */
	uint32_t rsv_0x228[2];		/* 0x228 ~ 0x22C */
	uint32_t modrst2_lock;		/* 0x230 */
	uint32_t modrst2_prot1;		/* 0x234 */
	uint32_t modrst2_prot2;		/* 0x238 */
	uint32_t modrst2_prot3;		/* 0x23C */
	uint32_t clkgate_ctrl;		/* 0x240 */
	uint32_t clkgate_clr;		/* 0x244 */
	uint32_t rsv_0x248[2];		/* 0x248 */
	uint32_t clkgate_lock;		/* 0x250 */
	uint32_t clkgate_secure1;	/* 0x254 */
	uint32_t clkgate_secure2;	/* 0x258 */
	uint32_t clkgate_secure3;	/* 0x25c */
	uint32_t rsv_0x260[8];		/* 0x260 */
	uint32_t clk_sel1;		/* 0x280 */
	uint32_t clk_sel2;		/* 0x284 */
	uint32_t clk_sel3;		/* 0x288 */
	uint32_t rsv_0x28c;		/* 0x28c */
	uint32_t clk_sel1_lock;		/* 0x290 */
	uint32_t clk_sel2_lock;		/* 0x294 */
	uint32_t clk_sel3_lock;		/* 0x298 */
	uint32_t rsv_0x29c;		/* 0x29c */
	uint32_t clk_sel1_secure1;	/* 0x2a0 */
	uint32_t clk_sel1_secure2;	/* 0x2a4 */
	uint32_t clk_sel1_secure3;	/* 0x2a8 */
	uint32_t rsv_0x2ac;		/* 0x2ac */
	uint32_t clk_sel2_secure1;	/* 0x2b0 */
	uint32_t clk_sel2_secure2;	/* 0x2b4 */
	uint32_t clk_sel2_secure3;	/* 0x2b8 */
	uint32_t rsv_0x2bc;		/* 0x2bc */
	uint32_t clk_sel3_secure1;	/* 0x2c0 */
	uint32_t clk_sel3_secure2;	/* 0x2c4 */
	uint32_t clk_sel3_secure3;	/* 0x2c8 */
	uint32_t rsv_0x2cc[9];		/* 0x2cc */
	uint32_t extrst_sel;		/* 0x2f0 */
	uint32_t rsv_0x2f4[3];		/* 0x2f4 */
	uint32_t hpll;			/* 0x300 */
	uint32_t hpll_ext;		/* 0x304 */
	uint32_t dpll;			/* 0x308 */
	uint32_t dpll_ext;		/* 0x30C */
	uint32_t mpll;			/* 0x310 */
	uint32_t mpll_ext;		/* 0x314 */
	uint32_t rsv_0x318[2];		/* 0x318 ~ 0x31C */
	uint32_t d1clk_para;		/* 0x320 */
	uint32_t rsv_0x324[3];		/* 0x324 ~ 0x32C */
	uint32_t d2clk_para;		/* 0x330 */
	uint32_t rsv_0x334[3];		/* 0x334 ~ 0x33C */
	uint32_t crt1clk_para;		/* 0x340 */
	uint32_t rsv_0x344[3];		/* 0x344 ~ 0x34C */
	uint32_t crt2clk_para;		/* 0x350 */
	uint32_t rsv_0x354[3];		/* 0x354 ~ 0x35C */
	uint32_t mphyclk_para;		/* 0x360 */
	uint32_t rsv_0x364[7];		/* 0x364 ~ 0x37C */
	uint32_t clkduty_meas_ctrl;	/* 0x380 */
	uint32_t clkduty1;		/* 0x384 */
	uint32_t clkduty2;		/* 0x368 */
	uint32_t clkduty_meas_res;	/* 0x38c */
	uint32_t rsv_0x390[4];		/* 0x390 ~ 0x39C */
	uint32_t freq_counter_ctrl;	/* 0x3a0 */
	uint32_t freq_counter_cmp;	/* 0x3a4 */
	uint32_t prog_delay_ring_ctrl0;	/* 0x3a8 */
	uint32_t prog_delay_ring_ctrl1;	/* 0x3ac */
	uint32_t freq_counter_readback;	/* 0x3b0 */
	uint32_t rsv_0x3b4[19];		/* 0x3b4 */
	uint32_t pinmux1;		/* 0x400 */
	uint32_t pinmux2;		/* 0x404 */
	uint32_t pinmux3;		/* 0x408 */
	uint32_t rsv_0x40c;		/* 0x40C */
	uint32_t pinmux4;		/* 0x410 */
	uint32_t vga_func_ctrl;		/* 0x414 */
	uint32_t rsv_0x418[2];	/* 0x418 */
	uint32_t pinmux_lock0;	/* 0x420 */
	uint32_t pinmux_lock1;	/* 0x424 */
	uint32_t pinmux_lock2;	/* 0x428 */
	uint32_t rsv_0x42c;
	uint32_t pinmux_lock3;	/* 0x430 */
	uint32_t pinmux_lock4;	/* 0x434 */
	uint32_t rsv_0x438[18];
	uint32_t gpio18d0_ioctrl;	/* 0x480 */
	uint32_t gpio18d1_ioctrl;	/* 0x484 */
	uint32_t gpio18d2_ioctrl;	/* 0x488 */
	uint32_t gpio18d3_ioctrl;	/* 0x48c */
	uint32_t gpio18d4_ioctrl;	/* 0x490 */
	uint32_t gpio18d5_ioctrl;	/* 0x494 */
	uint32_t gpio18d6_ioctrl;	/* 0x498 */
	uint32_t gpio18d7_ioctrl;	/* 0x49c */
	uint32_t gpio18e0_ioctrl;	/* 0x4a0 */
	uint32_t gpio18e1_ioctrl;	/* 0x4a4 */
	uint32_t gpio18e2_ioctrl;	/* 0x4a8 */
	uint32_t gpio18e3_ioctrl;	/* 0x4ac */
	uint32_t jtag_ioctrl;	/* 0x4b0 */
	uint32_t uart_ioctrl;	/* 0x4b4 */
	uint32_t misc_ioctrl;	/* 0x4b8 */
	uint32_t rsv_0x4bc[17];		/* 0x4bc ~ 0x4fc */
	uint32_t pinmux_seucre0_0;	/* 0x500 */
	uint32_t pinmux_seucre0_1;	/* 0x504 */
	uint32_t pinmux_seucre0_2;	/* 0x508 */
	uint32_t rsv_0x50c;
	uint32_t pinmux_seucre0_3;	/* 0x510 */
	uint32_t pinmux_seucre0_4;	/* 0x514 */
	uint32_t rsv_0x518[58];
	uint32_t pinmux_seucre1_0;	/* 0x600 */
	uint32_t pinmux_seucre1_1;	/* 0x604 */
	uint32_t pinmux_seucre1_2;	/* 0x608 */
	uint32_t rsv_0x60c;
	uint32_t pinmux_seucre1_3;	/* 0x610 */
	uint32_t pinmux_seucre1_4;	/* 0x614 */
	uint32_t rsv_0x618[58];
	uint32_t pinmux_seucre2_0;	/* 0x700 */
	uint32_t pinmux_seucre2_1;	/* 0x704 */
	uint32_t pinmux_seucre2_2;	/* 0x708 */
	uint32_t rsv_0x70c;
	uint32_t pinmux_seucre2_3;	/* 0x710 */
	uint32_t pinmux_seucre2s_4;	/* 0x714 */
	uint32_t rsv_0x718[26];
	uint32_t cpu_scratch[96];	/* 0x780 ~ 0x8FC */
	uint32_t vga0_scratch1[4];	/* 0x900 ~ 0x90C */
	uint32_t vga1_scratch1[4];	/* 0x910 ~ 0x91C */
	uint32_t vga0_scratch2[8];	/* 0x920 ~ 0x93C */
	uint32_t vga1_scratch2[8];	/* 0x940 ~ 0x95C */
	uint32_t pci_cfg1[3];		/* 0x960 ~ 0x968 */
	uint32_t rsv_0x96c;		/* 0x96C */
	uint32_t pcie_cfg1;		/* 0x970 */
	uint32_t mmio_decode1;		/* 0x974 */
	uint32_t reloc_ctrl_decode1[2];	/* 0x978 ~ 0x97C */
	uint32_t rsv_0x980[4];		/* 0x980 ~ 0x98C */
	uint32_t mbox_decode1;		/* 0x990 */
	uint32_t shared_sram_decode1[2];/* 0x994 ~ 0x998 */
	uint32_t rsv_0x99c;		/* 0x99C */
	uint32_t pci_cfg2[3];		/* 0x9A0 ~ 0x9A8 */
	uint32_t rsv_0x9ac;		/* 0x9AC */
	uint32_t pcie_cfg2;		/* 0x9B0 */
	uint32_t mmio_decode2;		/* 0x9B4 */
	uint32_t reloc_ctrl_decode2[2];	/* 0x9B8 ~ 0x9BC */
	uint32_t rsv_0x9c0[4];		/* 0x9C0 ~ 0x9CC */
	uint32_t mbox_decode2;		/* 0x9D0 */
	uint32_t shared_sram_decode2[2];/* 0x9D4 ~ 0x9D8 */
	uint32_t rsv_0x9dc[9];		/* 0x9DC ~ 0x9FC */
	uint32_t pci0_misc[32];		/* 0xA00 ~ 0xA7C */
	uint32_t pci1_misc[32];		/* 0xA80 ~ 0xAFC */
};

struct ast2700_scu1 {
	uint32_t chip_id1;		/* 0x000 */
	uint32_t rsv_0x04[3];		/* 0x004 ~ 0x00C */
	uint32_t hwstrap1;		/* 0x010 */
	uint32_t hwstrap1_clr;		/* 0x014 */
	uint32_t rsv_0x18[2];		/* 0x018 ~ 0x01C */
	uint32_t hwstrap1_lock;		/* 0x020 */
	uint32_t hwstrap1_sec1;	/* 0x024 */
	uint32_t hwstrap1_sec2;	/* 0x028 */
	uint32_t hwstrap1_sec3;	/* 0x02C */
	uint32_t hwstrap2;		/* 0x030 */
	uint32_t hwstrap2_clr;		/* 0x034 */
	uint32_t rsv_0x38[2];		/* 0x038 ~ 0x03C */
	uint32_t hwstrap2_lock;		/* 0x040 */
	uint32_t hwstrap2_sec1;	/* 0x044 */
	uint32_t hwstrap2_sec2;	/* 0x048 */
	uint32_t hwstrap2_sec3;	/* 0x04C */
	uint32_t sysrest_log1;		/* 0x050 */
	uint32_t sysrest_log1_sec1;	/* 0x054 */
	uint32_t sysrest_log1_sec2;	/* 0x058 */
	uint32_t sysrest_log1_sec3;	/* 0x05C */
	uint32_t sysrest_log2;		/* 0x060 */
	uint32_t sysrest_log2_sec1;	/* 0x064 */
	uint32_t sysrest_log2_sec2;	/* 0x068 */
	uint32_t sysrest_log2_sec3;	/* 0x06C */
	uint32_t sysrest_log3;		/* 0x070 */
	uint32_t sysrest_log3_sec1; /* 0x074 */
	uint32_t sysrest_log3_sec2; /* 0x078 */
	uint32_t sysrest_log3_sec3; /* 0x07C */
	uint32_t sysrest_log4;		/* 0x080 */
	uint32_t sysrest_log4_sec1; /* 0x084 */
	uint32_t sysrest_log4_sec2; /* 0x088 */
	uint32_t sysrest_log4_sec3; /* 0x08C */
	uint32_t rsv_0x90[7];		/* 0x090 ~ 0xA8 */
	uint32_t uart_dbg_rate;		/* 0x0AC */
	uint32_t rsv_0xB0[4];		/* 0x0B0 ~ 0xBC*/
	uint32_t misc;			/* 0x0C0 */
	uint32_t rsv_0xC4;		/* 0x0C4 */
	uint32_t debug_ctrl;		/* 0x0C8 */
	uint32_t rsv_0xCC;		/* 0x0CC */
	uint32_t dac_ctrl;		/* 0x0D0 */
	uint32_t dac_crc_ctrl;		/* 0x0D4 */
	uint32_t rsv_0xD8[2];		/* 0x0D8 ~ 0x0DC */
	uint32_t video_input_ctrl;		/* 0x0E0 */
	uint32_t rsv_0xE4[3];		/* 0x0E4 ~ 0x0EC */
	uint32_t random_num_ctrl;	/* 0x0F0 */
	uint32_t random_num_data;	/* 0x0F4 */
	uint32_t rsv_0xF0[2];		/* 0x0F8 ~ 0x0FC */
	uint32_t rsv_0x100[32];		/* 0x100 ~ 0x17C */
	uint32_t scratch[32];		/* 0x180 ~ 0x1FC */
	uint32_t modrst1_ctrl;		/* 0x200 */
	uint32_t modrst1_clr;		/* 0x204 */
	uint32_t rsv_0x208[2];		/* 0x208 ~ 0x20C */
	uint32_t modrst_lock1;		/* 0x210 */
	uint32_t modrst1_sec1;		/* 0x214 */
	uint32_t modrst1_sec2;		/* 0x218 */
	uint32_t modrst1_sec3;		/* 0x21C */
	uint32_t modrst2_ctrl;		/* 0x220 */
	uint32_t modrst2_clr;		/* 0x224 */
	uint32_t rsv_0x228[2];		/* 0x228 ~ 0x22C */
	uint32_t modrst2_lock;		/* 0x230 */
	uint32_t modrst2_prot1;		/* 0x234 */
	uint32_t modrst2_prot2;		/* 0x238 */
	uint32_t modrst2_prot3;		/* 0x23C */
	uint32_t clkgate_ctrl1;		/* 0x240 */
	uint32_t clkgate_clr1;		/* 0x244 */
	uint32_t rsv_0x248[2];		/* 0x248 */
	uint32_t clkgate_lock1;		/* 0x250 */
	uint32_t clkgate_secure11;		/* 0x254 */
	uint32_t clkgate_secure12;		/* 0x258 */
	uint32_t clkgate_secure13;		/* 0x25c */
	uint32_t clkgate_ctrl2;		/* 0x260 */
	uint32_t clkgate_clr2;		/* 0x264 */
	uint32_t rsv_0x268[2];		/* 0x268 */
	uint32_t clkgate_lock2;		/* 0x270 */
	uint32_t clkgate_secure21;		/* 0x274 */
	uint32_t clkgate_secure22;		/* 0x278 */
	uint32_t clkgate_secure23;		/* 0x27c */
	uint32_t clk_sel1;		/* 0x280 */
	uint32_t clk_sel2;		/* 0x284 */
	uint32_t rsv_0x288[2];		/* 0x288 */
	uint32_t clk_sel1_lock;		/* 0x290 */
	uint32_t clk_sel2_lock;		/* 0x294 */
	uint32_t rsv_0x298[2];		/* 0x298 */
	uint32_t clk_sel1_secure1;		/* 0x2a0 */
	uint32_t clk_sel1_secure2;		/* 0x2a4 */
	uint32_t rsv_0x2a8[2];		/* 0x2a8 */
	uint32_t clk_sel2_secure1;		/* 0x2b0 */
	uint32_t clk_sel2_secure2;		/* 0x2b4 */
	uint32_t rsv_0x2b8[2];		/* 0x2b8 */
	uint32_t clk_sel3_secure1;		/* 0x2c0 */
	uint32_t clk_sel3_secure2;		/* 0x2c4 */
	uint32_t rsv_0x2c8[10];		/* 0x2c8 */
	uint32_t extrst_sel1;		/* 0x2f0 */
	uint32_t extrst_sel2;		/* 0x2f4 */
	uint32_t rsv_0x2f8[2];		/* 0x2f8 */
	uint32_t hpll;			/* 0x300 */
	uint32_t hpll_ext;		/* 0x304 */
	uint32_t rsv_0x308[2];		/* 0x308 ~ 0x30C */
	uint32_t apll;			/* 0x310 */
	uint32_t apll_ext;		/* 0x314 */
	uint32_t rsv_0x318[2];		/* 0x318 ~ 0x31C */
	uint32_t dpll;			/* 0x320 */
	uint32_t dpll_ext;		/* 0x324 */
	uint32_t rsv_0x328[2];		/* 0x328 ~ 0x32C */
	uint32_t uxclk_ctrl;		/* 0x330 */
	uint32_t huxclk_ctrl;		/* 0x334 */
	uint32_t rsv_0x338[18];		/* 0x338 ~ 0x37C */
	uint32_t clkduty_meas_ctrl;	/* 0x380 */
	uint32_t clkduty1;		/* 0x384 */
	uint32_t clkduty2;		/* 0x388 */
	uint32_t rsv_0x38c;		/* 0x38c */
	uint32_t mac_delay;		/* 0x390 */
	uint32_t mac_100m_delay;		/* 0x394 */
	uint32_t mac_10m_delay;		/* 0x398 */
	uint32_t rsv_0x39c;		/* 0x39c */
	uint32_t freq_counter_ctrl;	/* 0x3a0 */
	uint32_t freq_counter_cmp;	/* 0x3a4 */
	uint32_t rsv_0x3a8[2];		/* 0x3a8 ~ 0x3aC */
	uint32_t usb_ctrl;		/* 0x3b0 */
	uint32_t usb_lock;		/* 0x3b4 */
	uint32_t usb_secure1;	/* 0x3b8 */
	uint32_t usb_secure2;	/* 0x3bc */
	uint32_t usb_secure3;	/* 0x3c0 */
	uint32_t rsv_0x3c4[15];	/* 0x3c4 ~ 0x3fc */
	uint32_t pinumx1;		/* 0x400 */
	uint32_t pinumx2;		/* 0x404 */
	uint32_t pinumx3;		/* 0x408 */
	uint32_t pinumx4;		/* 0x40c */
	uint32_t pinumx5;		/* 0x410 */
	uint32_t pinumx6;		/* 0x414 */
	uint32_t pinumx7;		/* 0x418 */
	uint32_t pinumx8;		/* 0x41c */
	uint32_t pinumx9;		/* 0x420 */
	uint32_t pinumx10;		/* 0x424 */
	uint32_t pinumx11;		/* 0x428 */
	uint32_t pinumx12;		/* 0x42c */
	uint32_t pinumx13;		/* 0x430 */
	uint32_t pinumx14;		/* 0x434 */
	uint32_t pinumx15;		/* 0x438 */
	uint32_t pinumx16;		/* 0x43c */
	uint32_t pinumx17;		/* 0x440 */
	uint32_t pinumx18;		/* 0x444 */
	uint32_t pinumx19;		/* 0x448 */
	uint32_t pinumx20;		/* 0x44c */
	uint32_t pinumx21;		/* 0x450 */
	uint32_t pinumx22;		/* 0x454 */
	uint32_t pinumx23;		/* 0x458 */
	uint32_t pinumx24;		/* 0x45c */
	uint32_t pinumx25;		/* 0x460 */
	uint32_t pinumx26;		/* 0x464 */
	uint32_t pinumx27;		/* 0x468 */
	uint32_t rsv_0x46c[4];	/* 0x46c ~ 0x478 */
	uint32_t pinumx31;		/* 0x47c */
	uint32_t pull_down_dis[8];	/* 0x480 ~ 0x49c */
	uint32_t pin_conf;		/* 0x4a0 */
	uint32_t rsv_0x4a4[7];	/* 0x4a4 ~ 0x4bc */
	uint32_t io_driving0;	/* 0x4c0 */
	uint32_t io_driving1;	/* 0x4c4 */
	uint32_t io_driving2;	/* 0x4c8 */
	uint32_t io_driving3;	/* 0x4cc */
	uint32_t io_driving4;	/* 0x4d0 */
	uint32_t io_driving5;	/* 0x4d4 */
	uint32_t io_driving6;	/* 0x4d8 */
	uint32_t io_driving7;	/* 0x4dc */
	uint32_t io_driving8;	/* 0x4e0 */
};

struct ast2705_scu1 {
	uint32_t chip_id1;		/* 0x000 */
	uint32_t rsv_0x04[3];		/* 0x004 ~ 0x00C */
	uint32_t hwstrap1;		/* 0x010 */
	uint32_t hwstrap1_clr;		/* 0x014 */
	uint32_t rsv_0x18[2];		/* 0x018 ~ 0x01C */
	uint32_t hwstrap1_lock;		/* 0x020 */
	uint32_t hwstrap1_sec1;		/* 0x024 */
	uint32_t hwstrap1_sec2;		/* 0x028 */
	uint32_t hwstrap1_sec3;		/* 0x02C */
	uint32_t hwstrap2;		/* 0x030 */
	uint32_t hwstrap2_clr;		/* 0x034 */
	uint32_t rsv_0x38[2];		/* 0x038 ~ 0x03C */
	uint32_t hwstrap2_lock;		/* 0x040 */
	uint32_t hwstrap2_sec1;		/* 0x044 */
	uint32_t hwstrap2_sec2;		/* 0x048 */
	uint32_t hwstrap2_sec3;		/* 0x04C */
	uint32_t sysrest_log1;		/* 0x050 */
	uint32_t sysrest_log1_sec1;	/* 0x054 */
	uint32_t sysrest_log1_sec2;	/* 0x058 */
	uint32_t sysrest_log1_sec3;	/* 0x05C */
	uint32_t sysrest_log2;		/* 0x060 */
	uint32_t sysrest_log2_sec1;	/* 0x064 */
	uint32_t sysrest_log2_sec2;	/* 0x068 */
	uint32_t sysrest_log2_sec3;	/* 0x06C */
	uint32_t sysrest_log3;		/* 0x070 */
	uint32_t sysrest_log3_sec1;	/* 0x074 */
	uint32_t sysrest_log3_sec2;	/* 0x078 */
	uint32_t sysrest_log3_sec3;	/* 0x07C */
	uint32_t sysrest_log4;		/* 0x080 */
	uint32_t sysrest_log4_sec1;	/* 0x084 */
	uint32_t sysrest_log4_sec2;	/* 0x088 */
	uint32_t sysrest_log4_sec3;	/* 0x08C */
	uint32_t rsv_0x90[7];		/* 0x090 ~ 0xA8 */
	uint32_t uart_dbg_rate;		/* 0x0AC */
	uint32_t rsv_0xB0[4];		/* 0x0B0 ~ 0xBC*/
	uint32_t misc;			/* 0x0C0 */
	uint32_t rsv_0xC4;		/* 0x0C4 */
	uint32_t debug_ctrl;		/* 0x0C8 */
	uint32_t rsv_0xCC;		/* 0x0CC */
	uint32_t dac_ctrl;		/* 0x0D0 */
	uint32_t dac_crc_ctrl;		/* 0x0D4 */
	uint32_t rsv_0xD8[2];		/* 0x0D8 ~ 0x0DC */
	uint32_t video_input_ctrl;	/* 0x0E0 */
	uint32_t rsv_0xE4[3];		/* 0x0E4 ~ 0x0EC */
	uint32_t random_num_ctrl;	/* 0x0F0 */
	uint32_t random_num_data;	/* 0x0F4 */
	uint32_t rsv_0xF0[2];		/* 0x0F8 ~ 0x0FC */
	uint32_t rsv_0x100[32];		/* 0x100 ~ 0x17C */
	uint32_t scratch[32];		/* 0x180 ~ 0x1FC */
	uint32_t modrst1_ctrl;		/* 0x200 */
	uint32_t modrst1_clr;		/* 0x204 */
	uint32_t modrst_lock1;		/* 0x208 */
	uint32_t modrst1_sec1;		/* 0x20C */
	uint32_t modrst1_sec2;		/* 0x210 */
	uint32_t modrst1_sec3;		/* 0x214 */
	uint32_t modrst2_ctrl;		/* 0x218 */
	uint32_t modrst2_clr;		/* 0x21C */
	uint32_t modrst2_lock;		/* 0x220 */
	uint32_t modrst2_prot1;		/* 0x224 */
	uint32_t modrst2_prot2;		/* 0x228 */
	uint32_t modrst2_prot3;		/* 0x22C */
	uint32_t modrst3_ctrl;		/* 0x230 */
	uint32_t modrst3_clr;		/* 0x234 */
	uint32_t modrst3_lock;		/* 0x238 */
	uint32_t modrst3_prot1;		/* 0x23C */
	uint32_t modrst3_prot2;		/* 0x240 */
	uint32_t modrst3_prot3;		/* 0x244 */
	uint32_t clkgate_ctrl1;		/* 0x248 */
	uint32_t clkgate_clr1;		/* 0x24C */
	uint32_t clkgate_lock1;		/* 0x250 */
	uint32_t clkgate_secure11;	/* 0x254 */
	uint32_t clkgate_secure12;	/* 0x258 */
	uint32_t clkgate_secure13;	/* 0x25C */
	uint32_t clkgate_ctrl2;		/* 0x260 */
	uint32_t clkgate_clr2;		/* 0x264 */
	uint32_t rsv_0x268[2];		/* 0x268 ~ 0x26C */
	uint32_t clkgate_lock2;		/* 0x270 */
	uint32_t clkgate_secure21;	/* 0x274 */
	uint32_t clkgate_secure22;	/* 0x278 */
	uint32_t clkgate_secure23;	/* 0x27C */
	uint32_t clk_sel1;		/* 0x280 */
	uint32_t clk_sel2;		/* 0x284 */
	uint32_t rsv_0x288[6];		/* 0x288 ~ 0x29C */
	uint32_t clk_sel1_lock;		/* 0x2A0 */
	uint32_t clk_sel1_secure1;	/* 0x2A4 */
	uint32_t clk_sel1_secure2;	/* 0x2A8 */
	uint32_t clk_sel1_secure3;	/* 0x2AC */
	uint32_t clk_sel2_lock;		/* 0x2B0 */
	uint32_t clk_sel2_secure1;	/* 0x2B4 */
	uint32_t clk_sel2_secure2;	/* 0x2B8 */
	uint32_t clk_sel2_secure3;	/* 0x2BC */
	uint32_t rsv_0x2C0[12];		/* 0x2C0 ~ 0x2EC */
	uint32_t extrst_sel1;		/* 0x2F0 */
	uint32_t extrst_sel2;		/* 0x2F4 */
	uint32_t extrst_sel3;		/* 0x2F8 */
	uint32_t rsv_0x2FC;		/* 0x2FC */
	uint32_t hpll;			/* 0x300 */
	uint32_t hpll_ext;		/* 0x304 */
	uint32_t rsv_0x308[2];		/* 0x308 ~ 0x30C */
	uint32_t apll;			/* 0x310 */
	uint32_t apll_ext;		/* 0x314 */
	uint32_t rsv_0x318[2];		/* 0x318 ~ 0x31C */
	uint32_t dpll;			/* 0x320 */
	uint32_t dpll_ext;		/* 0x324 */
	uint32_t rsv_0x328[2];		/* 0x328 ~ 0x32C */
	uint32_t uxclk_ctrl;		/* 0x330 */
	uint32_t huxclk_ctrl;		/* 0x334 */
	uint32_t rsv_0x338[18];		/* 0x338 ~ 0x37C */
	uint32_t clkduty_meas_ctrl;	/* 0x380 */
	uint32_t clkduty1;		/* 0x384 */
	uint32_t clkduty2;		/* 0x388 */
	uint32_t rsv_0x38c[5];		/* 0x38C ~ 0x39C */
	uint32_t freq_counter_ctrl;	/* 0x3A0 */
	uint32_t freq_counter_cmp;	/* 0x3A4 */
	uint32_t rsv_0x3a8[2];		/* 0x3A8 ~ 0x3AC */
	uint32_t usb_ctrl;		/* 0x3B0 */
	uint32_t usb_lock;		/* 0x3B4 */
	uint32_t usb_secure1;		/* 0x3b8 */
	uint32_t usb_secure2;		/* 0x3BC */
	uint32_t usb_secure3;		/* 0x3C0 */
	uint32_t rsv_0x3c4[15];		/* 0x3C4 ~ 0x3FC */
	uint32_t pinumx1;		/* 0x400 */
	uint32_t pinumx2;		/* 0x404 */
	uint32_t pinumx3;		/* 0x408 */
	uint32_t pinumx4;		/* 0x40C */
	uint32_t pinumx5;		/* 0x410 */
	uint32_t pinumx6;		/* 0x414 */
	uint32_t pinumx7;		/* 0x418 */
	uint32_t pinumx8;		/* 0x41C */
	uint32_t pinumx9;		/* 0x420 */
	uint32_t pinumx10;		/* 0x424 */
	uint32_t pinumx11;		/* 0x428 */
	uint32_t pinumx12;		/* 0x42C */
	uint32_t pinumx13;		/* 0x430 */
	uint32_t pinumx14;		/* 0x434 */
	uint32_t pinumx15;		/* 0x438 */
	uint32_t pinumx16;		/* 0x43C */
	uint32_t pinumx17;		/* 0x440 */
	uint32_t pinumx18;		/* 0x444 */
	uint32_t pinumx19;		/* 0x448 */
	uint32_t pinumx20;		/* 0x44C */
	uint32_t pinumx21;		/* 0x450 */
	uint32_t pinumx22;		/* 0x454 */
	uint32_t pinumx23;		/* 0x458 */
	uint32_t pinumx24;		/* 0x45C */
	uint32_t pinumx25;		/* 0x460 */
	uint32_t pinumx26;		/* 0x464 */
	uint32_t pinumx27;		/* 0x468 */
	uint32_t pinumx31;		/* 0x46C */
	uint32_t pull_down_dis7;	/* 0x470 */
	uint32_t sli_pin_conf;		/* 0x474 */
	uint32_t pin_conf;		/* 0x478 */
};

#endif
#endif
