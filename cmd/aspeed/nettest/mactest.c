// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) ASPEED Technology Inc.
 */

#define MACTEST_C

#include "swfunc.h"
#include "comminf.h"
#include <command.h>
#include <common.h>
#include <malloc.h>
#include <net.h>
#include <post.h>
#include "mem_io.h"

#include "phy_api.h"
#include "mac_api.h"

#define ARGV_MAC_IDX		1
#define ARGV_MDIO_IDX		2
#define ARGV_SPEED		3
#define ARGV_CTRL		4
#define ARGV_LOOP		5
#define ARGV_TEST_MODE		6
#define ARGV_PHY_ADDR		7
#define ARGV_TIMING_MARGIN	8


uint8_t __attribute__ ((aligned (1024*1024))) tdes_buf[TDES_SIZE];
uint8_t __attribute__ ((aligned (1024*1024))) rdes_buf[RDES_SIZE];
uint8_t __attribute__ ((aligned (1024*1024))) dma_buf[DMA_BUF_SIZE];

struct mac_ctrl_desc {
	uint32_t base_reset_assert;
	uint32_t bit_reset_assert;
	uint32_t base_reset_deassert;
	uint32_t bit_reset_deassert;

	uint32_t base_clk_stop;
	uint32_t bit_clk_stop;
	uint32_t base_clk_start;
	uint32_t bit_clk_start;
};

static const uint32_t timeout_th_tbl[3] = {
    TIME_OUT_Des_1G, TIME_OUT_Des_100M, TIME_OUT_Des_10M};
#if defined(CONFIG_ASPEED_AST2600)
const uint32_t mac_base_lookup_tbl[4] = {MAC1_BASE, MAC2_BASE, MAC3_BASE,
					 MAC4_BASE};
const uint32_t mdio_base_lookup_tbl[4] = {MDIO0_BASE, MDIO1_BASE, MDIO2_BASE,
					 MDIO3_BASE};
const struct mac_ctrl_desc mac_ctrl_lookup_tbl[4] = {
	{
		.base_reset_assert = 0x40, .bit_reset_assert = BIT(11),
		.base_reset_deassert = 0x44,.bit_reset_deassert = BIT(11),
		.base_clk_stop = 0x80, .bit_clk_stop = BIT(20),
		.base_clk_start = 0x84, .bit_clk_start = BIT(20),
	},
	{
		.base_reset_assert = 0x40, .bit_reset_assert = BIT(12),
		.base_reset_deassert = 0x44,.bit_reset_deassert = BIT(12),
		.base_clk_stop = 0x80, .bit_clk_stop = BIT(21),
		.base_clk_start = 0x84,.bit_clk_start = BIT(21),
	},
	{
		.base_reset_assert = 0x50, .bit_reset_assert = BIT(20),
		.base_reset_deassert = 0x54,.bit_reset_deassert = BIT(20),
		.base_clk_stop = 0x90, .bit_clk_stop = BIT(20),
		.base_clk_start = 0x94, .bit_clk_start = BIT(20),
	},
	{
		.base_reset_assert = 0x50, .bit_reset_assert = BIT(21),
		.base_reset_deassert = 0x54,.bit_reset_deassert = BIT(21),
		.base_clk_stop = 0x90, .bit_clk_stop = BIT(21),
		.base_clk_start = 0x94,.bit_clk_start = BIT(21),
	}
};
#else
const uint32_t mac_base_lookup_tbl[2] = {MAC1_BASE, MAC2_BASE};
const uint32_t mdio_base_lookup_tbl[2] = {MDIO0_BASE, MDIO1_BASE};
const struct mac_ctrl_desc mac_ctrl_lookup_tbl[2] = {
	{
		.base_reset_assert = 0x04, .bit_reset_assert = BIT(11),
		.base_reset_deassert = 0x04, .bit_reset_deassert = BIT(11),
		.base_clk_stop = 0x0c, .bit_clk_stop = BIT(20),
		.base_clk_start = 0x0c, .bit_clk_start = BIT(20),
	},
	{
		.base_reset_assert = 0x04, .bit_reset_assert = BIT(12),
		.base_reset_deassert = 0x04, .bit_reset_deassert = BIT(12),
		.base_clk_stop = 0x0c, .bit_clk_stop = BIT(21),
		.base_clk_start = 0x0c, .bit_clk_start = BIT(21),
	}
};
#endif

void Print_Header(MAC_ENGINE *p_eng, uint8_t option)
{
	if (p_eng->run.speed_sel[0]) {
		PRINTF(option, " 1G   ");
	} else if (p_eng->run.speed_sel[1]) {
		PRINTF(option, " 100M ");
	} else {
		PRINTF(option, " 10M  ");
	}

	switch (p_eng->arg.test_mode) {
	case 0: 
		PRINTF(option, "TX/RX delay margin check\n");
		break;
	case 1:
		PRINTF(option, "TX/RX delay scan\n");
		break;
	case 2:
		PRINTF(option, "TX/RX delay and IO driving scan\n");
		break;
	case 3:
		PRINTF(option, "TX frame - ARP\n");
		break;
	case 4:
		PRINTF(option, "TX frame - random\n");
		break;
	case 5:
		PRINTF(option, "TX frame - 0x%08x\n", p_eng->arg.user_def_val);
		break;
	}
}

static void print_arg_test_mode(MAC_ENGINE *p_eng) 
{
	uint8_t item[32] = "test_mode[dec]";

	if (p_eng->arg.run_mode == MODE_NCSI) {
		printf("%20s| 0: NCSI configuration with    "
		       "Disable_Channel request\n", item);
		printf("%20s| (default:%3d)\n", "", DEF_GTESTMODE);
		printf("%20s| 1: TX/RX delay scan\n", "");
		printf("%20s| 2: TX/RX delay and IO driving scan\n", "");
		printf("%20s| 3: NCSI configuration without "
		       "Disable_Channel request\n", "");
	} else {
		printf("%20s| (default:%3d)\n", item, DEF_GTESTMODE);
		printf("%20s| 0: TX/RX delay margin check\n", "");
		printf("%20s| 1: TX/RX delay scan\n", "");
		printf("%20s| 2: TX/RX delay and IO driving scan\n", "");
		printf("%20s| 3: TX frame - ARP\n", "");
		printf("%20s| 4: TX frame - random\n", "");
		printf("%20s| 5: TX frame - user defined (default:0x%8x)\n", "",
		       DEF_GUSER_DEF_PACKET_VAL);
	}	
}

static void print_arg_phy_addr(MAC_ENGINE *p_eng)
{
	uint8_t item[32] = "phy_addr[dec]";

	printf("%20s| 0~31: PHY Address (default:%d)\n", item, DEF_GPHY_ADR);
}

static void print_arg_ieee_select(MAC_ENGINE *p_eng) 
{
	uint8_t item[32] = "IEEE packet select";

	printf("%20s| 0/1/2 (default:0)  only for test_mode 3,4,5)\n", item);
}

static void print_arg_delay_scan_range(MAC_ENGINE *p_eng) 
{
	uint8_t item[32] = "TX/RX delay margin";

	printf("%20s| 1/2/3/... (default:%d) only for test_mode 0\n", item,
	       DEF_GIOTIMINGBUND);
	printf("%20s| check range = (orig - margin) ~ (orig + margin)\n", "");
}

static void print_arg_channel_num(MAC_ENGINE *p_eng) 
{
	uint8_t item[32] = "channel_num[dec]";

	printf("%20s| 1~32: Total Number of NCSI Channel (default:%d)\n", item,
	       DEF_GCHANNEL2NUM);
}

static void print_arg_package_num(MAC_ENGINE *p_eng) 
{
	uint8_t item[32] = "package_num[dec]";

	printf("%20s| 1~ 8: Total Number of NCSI Package (default:%d)\n", item,
	       DEF_GPACKAGE2NUM);
}

static void print_arg_loop(MAC_ENGINE *p_eng)
{
	uint8_t item[32] = "loop_max[dec]";

	printf("%20s| 1G  :  (default:%3d)\n", item, DEF_GLOOP_MAX * 20);
	printf("%20s| 100M:  (default:%3d)\n", "", DEF_GLOOP_MAX * 2);
	printf("%20s| 10M :  (default:%3d)\n", "", DEF_GLOOP_MAX);
}

static void print_arg_ctrl(MAC_ENGINE *p_eng)
{
	uint8_t item[32] = "ctrl[hex]";

	printf("%20s| default  : 0x%03x\n", item, DEF_GCTRL);
	printf("%20s| bit0     : skip PHY init/deinit\n", "");
	printf("%20s| bit1     : skip PHY deinit\n", "");
	printf("%20s| bit2     : skip PHY ID check\n", "");
	printf("%20s| bit3     : reserved\n", "");
	printf("%20s| bit4     : PHY internal loopback\n", "");
	printf("%20s| bit5     : MAC internal loopback\n", "");	
	printf("%20s| bit6     : Enable PHY RX delay\n", "");
	printf("%20s| bit7     : Enable PHY TX delay\n", "");
	printf("%20s| bit8     : RMII 50MHz Output enable\n", "");
	printf("%20s| bit9     : RMII REFCLK pin input enable\n", "");
	printf("%20s| bit10    : inverse RGMII RXCLK\n", "");
	printf("%20s| bit11    : reserved\n", "");
	printf("%20s| bit12    : TX single packet for each test point\n", "");
	printf("%20s| bit13    : full range scan\n", "");
	printf("%20s| bit15~14 : reserved\n", "");
	printf("%20s| bit16    : NCSI verbose log\n", "");
	printf("%20s| bit17    : NCSI skip RX error\n", "");
	printf("%20s| bit31~18 : reserved\n", "");
}

static void print_arg_speed(MAC_ENGINE *p_eng) 
{
	uint8_t item[32] = "speed[hex]";

	printf("%20s| bit[0]->1G  bit[1]->100M  bit[2]->10M "
	       "(default:0x%02lx)\n",
	       item, DEF_GSPEED);
}

static void print_arg_mdio_idx(MAC_ENGINE *p_eng) 
{
	uint8_t item[32] = "mdio_idx[dec]";

	printf("%20s| 0->MDIO1 1->MDIO2", item);
	
	if (p_eng->env.mac_num > 2) {
		printf(" 2->MDIO3 3->MDIO4");
	}
	printf("\n");
}

static void print_arg_mac_idx(MAC_ENGINE *p_eng) 
{
	uint8_t item[32] = "mac_idx[dec]";

	printf("%20s| 0->MAC1 1->MAC2", item);
	
	if (p_eng->env.mac_num > 2) {
		printf(" 2->MAC3 3->MAC4");
	}
	printf("\n");
}
static void print_legend(void)
{
	printf("Legend:\n");
	printf("    o : OK\n");	
	printf("    x : CRC error\n");
	printf("    . : packet not found\n");
	printf("    System default setting\n");
	printf("    O : OK\n");	
	printf("    X : CRC error\n");
	printf("    * : packet not found\n");
}
static void print_usage(MAC_ENGINE *p_eng)
{
	if (MODE_DEDICATED == p_eng->arg.run_mode) {
		printf("mactest <mac_idx> <mdio_idx> <speed> <ctrl> <loop_max> <test "
		       "mode> <phy addr> <margin / IEEE select> <user data>\n");
		print_arg_mac_idx(p_eng);
		print_arg_mdio_idx(p_eng);
		print_arg_speed(p_eng);
		print_arg_ctrl(p_eng);
		print_arg_loop(p_eng);
		print_arg_test_mode(p_eng);
		print_arg_phy_addr(p_eng);
		print_arg_delay_scan_range(p_eng);
		print_arg_ieee_select(p_eng);
	} else if (MODE_NCSI == p_eng->arg.run_mode) {
		printf("ncsitest <idx> <packet num> <channel num> <test mode> "
		       "<margin> <ctrl>\n");
		print_arg_mac_idx(p_eng);
		print_arg_package_num(p_eng);
		print_arg_channel_num(p_eng);
		print_arg_test_mode(p_eng);
		print_arg_delay_scan_range(p_eng);
		print_arg_ctrl(p_eng);
	} else {
		printf("unknown run mode\n");
	}
}

static void push_reg(MAC_ENGINE *p_eng)
{
	/* SCU delay settings */
	p_eng->io.mac12_1g_delay.value.w = readl(p_eng->io.mac12_1g_delay.addr);
	p_eng->io.mac12_100m_delay.value.w = readl(p_eng->io.mac12_100m_delay.addr);
	p_eng->io.mac12_10m_delay.value.w = readl(p_eng->io.mac12_10m_delay.addr);
		
#ifdef CONFIG_ASPEED_AST2600
	p_eng->io.mac34_1g_delay.value.w = readl(p_eng->io.mac34_1g_delay.addr);
	p_eng->io.mac34_100m_delay.value.w = readl(p_eng->io.mac34_100m_delay.addr);
	p_eng->io.mac34_10m_delay.value.w = readl(p_eng->io.mac34_10m_delay.addr);
	
	p_eng->io.mac34_drv_reg.value.w = readl(p_eng->io.mac34_drv_reg.addr);
#else
	p_eng->io.mac12_drv_reg.value.w = readl(p_eng->io.mac12_drv_reg.addr);
#endif

	/* MAC registers */
	p_eng->reg.maccr.w = mac_reg_read(p_eng, 0x50);

	p_eng->reg.mac_madr = mac_reg_read(p_eng, 0x08);
	p_eng->reg.mac_ladr = mac_reg_read(p_eng, 0x0c);
	p_eng->reg.mac_fear = mac_reg_read(p_eng, 0x40);
}

static void pop_reg(MAC_ENGINE *p_eng)
{
	/* SCU delay settings */
	writel(p_eng->io.mac12_1g_delay.value.w, p_eng->io.mac12_1g_delay.addr);
	writel(p_eng->io.mac12_100m_delay.value.w, p_eng->io.mac12_100m_delay.addr);
	writel(p_eng->io.mac12_10m_delay.value.w, p_eng->io.mac12_10m_delay.addr);	

#ifdef CONFIG_ASPEED_AST2600	
	writel(p_eng->io.mac34_1g_delay.value.w, p_eng->io.mac34_1g_delay.addr);
	writel(p_eng->io.mac34_100m_delay.value.w, p_eng->io.mac34_100m_delay.addr);
	writel(p_eng->io.mac34_10m_delay.value.w, p_eng->io.mac34_10m_delay.addr);
	
	writel(p_eng->io.mac34_drv_reg.value.w, p_eng->io.mac34_drv_reg.addr);
#else
	writel(p_eng->io.mac12_drv_reg.value.w, p_eng->io.mac12_drv_reg.addr);
#endif

	/* MAC registers */
	mac_reg_write(p_eng, 0x50, p_eng->reg.maccr.w);
	mac_reg_write(p_eng, 0x08, p_eng->reg.mac_madr);
	mac_reg_write(p_eng, 0x0c, p_eng->reg.mac_ladr);
	mac_reg_write(p_eng, 0x40, p_eng->reg.mac_fear);
}

static void finish_close(MAC_ENGINE *p_eng) 
{
	nt_log_func_name();		
	pop_reg(p_eng);
}

char finish_check(MAC_ENGINE *p_eng, int value) 
{
	nt_log_func_name();

	if (p_eng->arg.run_mode == MODE_DEDICATED) {
		if (p_eng->dat.FRAME_LEN)
			free(p_eng->dat.FRAME_LEN);

		if (p_eng->dat.wp_lst)
			free(p_eng->dat.wp_lst);
	}

	p_eng->flg.error = p_eng->flg.error | value;

	if (DBG_PRINT_ERR_FLAG)
		printf("flags: error = %08x\n", p_eng->flg.error);

	if (!p_eng->run.tm_tx_only)
		FPri_ErrFlag(p_eng, FP_LOG);

	if (p_eng->run.TM_IOTiming)
		FPri_ErrFlag(p_eng, FP_IO);

	FPri_ErrFlag(p_eng, STD_OUT);

	if (!p_eng->run.tm_tx_only)
		FPri_End(p_eng, FP_LOG);

	if (p_eng->run.TM_IOTiming)
		FPri_End(p_eng, FP_IO);

	FPri_End(p_eng, STD_OUT);

	if (!p_eng->run.tm_tx_only)
		FPri_RegValue(p_eng, FP_LOG);
	if (p_eng->run.TM_IOTiming)
		FPri_RegValue(p_eng, FP_IO);

	finish_close(p_eng);

	if (p_eng->flg.error) {
		return (1);
	} else {
		return (0);
	}
}

static uint32_t check_test_mode(MAC_ENGINE *p_eng)
{
	if (p_eng->arg.run_mode == MODE_NCSI ) {
		switch (p_eng->arg.test_mode) {
		case 0:
			break;
		case 1:
			p_eng->run.TM_IOTiming = 1;
			break;
		case 2:
			p_eng->run.TM_IOTiming = 1;
			p_eng->run.TM_IOStrength = 1;
			break;
		case 3:
			p_eng->run.TM_NCSI_DiSChannel = 0;
			break;
		default:
			printf("Error test_mode!!!\n");
			print_arg_test_mode(p_eng);
			return (1);
		}
	} else {
		switch (p_eng->arg.test_mode) {
		case 0:
			break;
		case 1:
			p_eng->run.TM_IOTiming = 1;
			break;
		case 2:
			p_eng->run.TM_IOTiming = 1;
			p_eng->run.TM_IOStrength = 1;
			break;
		case 3:
			/* TX ARP frame */
			p_eng->run.TM_RxDataEn = 0;
			p_eng->run.tm_tx_only = 1;
			p_eng->run.TM_IEEE = 0;
			break;
		case 4:
		case 5:			
			p_eng->run.TM_RxDataEn = 0;
			p_eng->run.tm_tx_only = 1;
			p_eng->run.TM_IEEE = 1;
			break;
		default:
			printf("Error test_mode!!!\n");
			print_arg_test_mode(p_eng);
			return (1);
		}
	}

	if (0 == p_eng->run.TM_IOStrength) {
		p_eng->io.drv_upper_bond = 0;
	}
	return 0;
}

/**
 * @brief enable/disable MAC
 * @param[in] p_eng - MAC_ENGINE
 * 
 * AST2600 uses synchronous reset scheme, so the bits for reset assert and 
 * deassert are the same
 * e.g. MAC#1: SCU04[11] = 1 --> MAC#1 reset assert
 *                       = 0 --> MAC#1 reset de-assert
 * 
 * AST2600 uses asynchronous reset scheme, so the bits for reset assert and 
 * deassert are different
 * e.g. MAC#1: SCU40[11] = 1 --> MAC#1 reset assert
 *             SCU44[11] = 1 --> MAC#1 reset de-assert
 * 
 * The same design concept is also adopted on clock stop/start.
 */
void scu_disable_mac(MAC_ENGINE *p_eng) 
{
	uint32_t mac_idx = p_eng->run.mac_idx;
	const struct mac_ctrl_desc *p_mac = &mac_ctrl_lookup_tbl[mac_idx];
	uint32_t reg;

	debug("MAC%d:reset assert=0x%02x[%08x] deassert=0x%02x[%08x]\n",
	      mac_idx, p_mac->base_reset_assert, p_mac->bit_reset_assert,
	      p_mac->base_reset_deassert, p_mac->bit_reset_deassert);
	debug("MAC%d:clock stop=0x%02x[%08x] start=0x%02x[%08x]\n", mac_idx,
	      p_mac->base_clk_stop, p_mac->bit_clk_stop, p_mac->base_clk_start,
	      p_mac->bit_clk_start);

	reg = SCU_RD(p_mac->base_reset_assert);
	debug("reset reg: 0x%08x\n", reg);
	reg |= p_mac->bit_reset_assert;
	debug("reset reg: 0x%08x\n", reg);
	SCU_WR(reg, p_mac->base_reset_assert);
	/* issue a dummy read to ensure command is in order */
	reg = SCU_RD(p_mac->base_reset_assert);
	
	reg = SCU_RD(p_mac->base_clk_stop);
	debug("clock reg: 0x%08x\n", reg);
	reg |= p_mac->bit_clk_stop;
	debug("clock reg: 0x%08x\n", reg);
	SCU_WR(reg, p_mac->base_clk_stop);
	/* issue a dummy read to ensure command is in order */
	reg = SCU_RD(p_mac->base_clk_stop);
}

void scu_enable_mac(MAC_ENGINE *p_eng) 
{
	uint32_t mac_idx = p_eng->run.mac_idx;
	const struct mac_ctrl_desc *p_mac = &mac_ctrl_lookup_tbl[mac_idx];
	uint32_t reg;

	debug("MAC%d:reset assert=0x%02x[%08x] deassert=0x%02x[%08x]\n",
	      mac_idx, p_mac->base_reset_assert, p_mac->bit_reset_assert,
	      p_mac->base_reset_deassert, p_mac->bit_reset_deassert);
	debug("MAC%d:clock stop=0x%02x[%08x] start=0x%02x[%08x]\n", mac_idx,
	      p_mac->base_clk_stop, p_mac->bit_clk_stop, p_mac->base_clk_start,
	      p_mac->bit_clk_start);

#ifdef CONFIG_ASPEED_AST2600
	reg = SCU_RD(p_mac->base_reset_deassert);
	debug("reset reg: 0x%08x\n", reg);
	reg |= p_mac->bit_reset_deassert;
	debug("reset reg: 0x%08x\n", reg);
	SCU_WR(reg, p_mac->base_reset_deassert);
	/* issue a dummy read to ensure command is in order */
	reg = SCU_RD(p_mac->base_reset_deassert);
	
	reg = SCU_RD(p_mac->base_clk_start);
	debug("clock reg: 0x%08x\n", reg);
	reg |= p_mac->bit_clk_start;
	debug("clock reg: 0x%08x\n", reg);
	SCU_WR(reg, p_mac->base_clk_start);
	/* issue a dummy read to ensure command is in order */
	reg = SCU_RD(p_mac->base_clk_start);

	/* deassert MDIO reset */
	SCU_WR(BIT(3), 0x54);
#else
	reg = SCU_RD(p_mac->base_reset_assert);
	debug("reset reg: 0x%08x\n", reg);
	reg &= ~p_mac->bit_reset_assert;
	debug("reset reg: 0x%08x\n", reg);
	SCU_WR(reg, p_mac->base_reset_assert);
	
	reg = SCU_RD(p_mac->base_clk_stop);
	debug("clock reg: 0x%08x\n", reg);
	reg &= ~p_mac->bit_clk_stop;
	debug("clock reg: 0x%08x\n", reg);
	SCU_WR(reg, p_mac->base_clk_stop);
#endif
}

/**
 * @brief setup mdc/mdio pinmix
 * @todo push/pop pinmux registers
*/
void scu_set_pinmux(MAC_ENGINE *p_eng)
{
	uint32_t reg;
	nt_log_func_name();

#ifdef CONFIG_ASPEED_AST2600
	/* MDC/MDIO pinmux */
	switch (p_eng->run.mdio_idx) {
	case 0:
		reg = SCU_RD(0x430) | GENMASK(17, 16);
		SCU_WR(reg, 0x430);
		break;
	case 1:
		reg = SCU_RD(0x470) & ~GENMASK(13, 12);
		SCU_WR(reg, 0x470);
		reg = SCU_RD(0x410) | GENMASK(13, 12);
		SCU_WR(reg, 0x410);
		break;
	case 2:
		reg = SCU_RD(0x470) & ~GENMASK(1, 0);
		SCU_WR(reg, 0x470);
		reg = SCU_RD(0x410) | GENMASK(1, 0);
		SCU_WR(reg, 0x410);
		break;
	case 3:
		reg = SCU_RD(0x470) & ~GENMASK(3, 2);
		SCU_WR(reg, 0x470);
		reg = SCU_RD(0x410) | GENMASK(3, 2);
		SCU_WR(reg, 0x410);
		break;
	default:
		printf("%s:undefined MDIO idx %d\n", __func__,
		       p_eng->run.mdio_idx);
	}

	switch (p_eng->run.mac_idx) {
	case 0:
#ifdef CONFIG_FPGA_ASPEED
		setbits_le32(SCU_BASE + 0x410, BIT(4));
#else
		setbits_le32(SCU_BASE + 0x400, GENMASK(11, 0));
		setbits_le32(SCU_BASE + 0x410, BIT(4));
		clrbits_le32(SCU_BASE + 0x470, BIT(4));
#endif
		break;
	case 1:
		setbits_le32(SCU_BASE + 0x400, GENMASK(23, 12));
		setbits_le32(SCU_BASE + 0x410, BIT(5));
		clrbits_le32(SCU_BASE + 0x470, BIT(5));
		break;
	case 2:
		setbits_le32(SCU_BASE + 0x410, GENMASK(27, 16));
		setbits_le32(SCU_BASE + 0x410, BIT(6));
		clrbits_le32(SCU_BASE + 0x470, BIT(6));
		break;
	case 3:
		clrbits_le32(SCU_BASE + 0x410, GENMASK(31, 28));
		setbits_le32(SCU_BASE + 0x4b0, GENMASK(31, 28));
		clrbits_le32(SCU_BASE + 0x474, GENMASK(7, 0));
		clrbits_le32(SCU_BASE + 0x414, GENMASK(7, 0));
		setbits_le32(SCU_BASE + 0x4b4, GENMASK(7, 0));
		setbits_le32(SCU_BASE + 0x410, BIT(7));
		clrbits_le32(SCU_BASE + 0x470, BIT(7));
		break;

	}

	debug("SCU410: %08x %08x %08x %08x\n", SCU_RD(0x410), SCU_RD(0x414), SCU_RD(0x418), SCU_RD(0x41c));
	debug("SCU430: %08x %08x %08x %08x\n", SCU_RD(0x430), SCU_RD(0x434), SCU_RD(0x438), SCU_RD(0x43c));
	debug("SCU470: %08x %08x %08x %08x\n", SCU_RD(0x470), SCU_RD(0x474), SCU_RD(0x478), SCU_RD(0x47c));
	debug("SCU4b0: %08x %08x %08x %08x\n", SCU_RD(0x4b0), SCU_RD(0x4b4), SCU_RD(0x4b8), SCU_RD(0x4bc));
#else
	/* MDC/MDIO pinmux */
	if (p_eng->run.mdio_idx == 0) {
		setbits_le32(SCU_BASE + 88, GENMASK(31, 30));
	} else {
		clrsetbits_le32(SCU_BASE + 90, BIT(6), BIT(2));
	}

	/* enable MAC#nLINK pin */
	setbits_le32(SCU_BASE + 80, BIT(p_eng->run.mac_idx));
#endif
}

static uint32_t check_mac_idx(MAC_ENGINE *p_eng)
{
	/* check if legal run_idx */
	if (p_eng->arg.mac_idx > p_eng->env.mac_num - 1) {
		printf("invalid run_idx = %d\n", p_eng->arg.mac_idx);	
		return 1;
	}
	
	return 0;
}

static void calc_loop_check_num(MAC_ENGINE *p_eng)
{
	nt_log_func_name();

	if (p_eng->run.IO_MrgChk ||
	    (p_eng->arg.run_speed == SET_1G_100M_10MBPS) ||
	    (p_eng->arg.run_speed == SET_100M_10MBPS) ||
	    (p_eng->arg.run_speed == SET_1G_100MBPS) ||
	    (p_eng->arg.run_speed == SET_1G_10MBPS)) {
		p_eng->run.LOOP_CheckNum = p_eng->run.loop_max;
	} else {
		switch (p_eng->arg.run_speed) {
		case SET_1GBPS:
			p_eng->run.CheckBuf_MBSize = MOVE_DATA_MB_SEC;
			break;
		case SET_100MBPS:
			p_eng->run.CheckBuf_MBSize = (MOVE_DATA_MB_SEC >> 3);
			break;
		case SET_10MBPS:
			p_eng->run.CheckBuf_MBSize = (MOVE_DATA_MB_SEC >> 6);
			break;
		}
		p_eng->run.LOOP_CheckNum =
		    (p_eng->run.CheckBuf_MBSize /
		     (((p_eng->dat.Des_Num * DMA_PakSize) >> 20) + 1));
	}
}
static uint32_t setup_interface(MAC_ENGINE *p_eng);
static uint32_t setup_running(MAC_ENGINE *p_eng)
{
	uint32_t n_desp_min;
	int i;

	if (0 != check_mac_idx(p_eng)) {
		return 1;
	}
	p_eng->run.mac_idx = p_eng->arg.mac_idx;
	p_eng->run.mac_base = mac_base_lookup_tbl[p_eng->run.mac_idx];	
	
	p_eng->run.mdio_idx = p_eng->arg.mdio_idx;
	p_eng->run.mdio_base = mdio_base_lookup_tbl[p_eng->run.mdio_idx];

	p_eng->run.is_rgmii = p_eng->env.is_1g_valid[p_eng->run.mac_idx];

	if (p_eng->arg.run_mode == MODE_NCSI) {
#ifdef CONFIG_ASPEED_AST2600
		/**
		 * NCSI needs for 3.3V IO voltage but MAC#1 & MAC#2 only
		 * support 1.8V. So NCSI can only runs on MAC#3 or MAC#4
		 */
		if (p_eng->run.mac_idx < 2) {
			printf("\nNCSI must runs on MAC#3 or MAC#4\n");
			return 1;
		}

		if (p_eng->run.is_rgmii) {
			hw_strap2_t strap2;
			
			printf("\nNCSI must be RMII interface, force the strap value:\n");
			printf("\nbefore: SCU510=%08x\n", SCU_RD(0x510));
			strap2.w = 0;
			if (p_eng->run.mac_idx == 2) {
				strap2.b.mac3_interface = 1;
			} else if (p_eng->run.mac_idx == 3) {
				strap2.b.mac4_interface = 1;
			}
			SCU_WR(strap2.w, 0x514);
			while (SCU_RD(0x510) & strap2.w);
			printf("\nafter: SCU510=%08x\n", SCU_RD(0x510));
			/* update interface setting */
			setup_interface(p_eng);
			p_eng->run.is_rgmii = p_eng->env.is_1g_valid[p_eng->run.mac_idx];
		}
#else
		if (p_eng->run.is_rgmii) {
			printf("\nNCSI must be RMII interface\n");
			return 1;
		}
#endif
	}

	for (i = 0; i < 3; i++) {
		if (p_eng->arg.run_speed & (1 << i))
			p_eng->run.speed_cfg[i] = 1;
	}

	if (p_eng->arg.run_mode == MODE_NCSI) {
		/*
		 * [Arg]check GPackageTolNum
		 * [Arg]check GChannelTolNum
		 */
		if ((p_eng->arg.GPackageTolNum < 1) ||
		    (p_eng->arg.GPackageTolNum > 8)) {
			print_arg_package_num(p_eng);
			return (1);
		}
		if ((p_eng->arg.GChannelTolNum < 1) ||
		    (p_eng->arg.GChannelTolNum > 32)) {
			print_arg_channel_num(p_eng);
			return (1);
		}
	} else {
		/* [Arg]check ctrl */		
		if (p_eng->arg.ctrl.w & 0xfffc0000) {
			print_arg_ctrl(p_eng);
			return (1);
		}

		if (p_eng->arg.phy_addr > 31) {
			printf("Error phy_adr!!!\n");
			print_arg_phy_addr(p_eng);
			return (1);
		}

		if (0 == p_eng->arg.loop_max) {
			switch (p_eng->arg.run_speed) {
			case SET_1GBPS:
				p_eng->arg.loop_max = DEF_GLOOP_MAX * 20;
				break;
			case SET_100MBPS:
				p_eng->arg.loop_max = DEF_GLOOP_MAX * 2;
				break;
			case SET_10MBPS:
				p_eng->arg.loop_max = DEF_GLOOP_MAX;
				break;
			case SET_1G_100M_10MBPS:
			case SET_1G_100MBPS:
			case SET_1G_10MBPS:
				p_eng->arg.loop_max = DEF_GLOOP_MAX * 20;
				break;
			case SET_100M_10MBPS:
				p_eng->arg.loop_max = DEF_GLOOP_MAX * 2;
				break;
			}
		}		
	}

	if (0 != check_test_mode(p_eng)) {
		return 1;
	}

	if (p_eng->run.tm_tx_only) {
		p_eng->run.ieee_sel = p_eng->arg.ieee_sel;
		p_eng->run.delay_margin = 0;
	} else {
		p_eng->run.ieee_sel = 0;			
		p_eng->run.delay_margin = p_eng->arg.delay_scan_range;
#if 0
		if (p_eng->run.delay_margin == 0) {
			printf("Error IO margin!!!\n");
			print_arg_delay_scan_range(p_eng);
			return(1);
		}
#endif								
	}

	if (!p_eng->env.is_1g_valid[p_eng->run.mac_idx])
		p_eng->run.speed_cfg[ 0 ] = 0;
	
	p_eng->run.tdes_base = (uint32_t)(&tdes_buf[0]);
	p_eng->run.rdes_base = (uint32_t)(&rdes_buf[0]);

	if (p_eng->run.TM_IOTiming || p_eng->run.delay_margin)
		p_eng->run.IO_MrgChk = 1;
	else
		p_eng->run.IO_MrgChk = 0;

	p_eng->phy.Adr         = p_eng->arg.phy_addr;
	p_eng->phy.loopback    = p_eng->arg.ctrl.b.phy_int_loopback;
	p_eng->phy.default_phy = p_eng->run.TM_DefaultPHY;

	p_eng->run.loop_max = p_eng->arg.loop_max;
	calc_loop_check_num(p_eng);

	//------------------------------------------------------------
	// Descriptor Number
	//------------------------------------------------------------
	//------------------------------
	// [Dat]setup Des_Num
	// [Dat]setup DMABuf_Size
	// [Dat]setup DMABuf_Num
	//------------------------------
	if (p_eng->arg.run_mode == MODE_DEDICATED) {
		n_desp_min = p_eng->run.TM_IOTiming;

		if (p_eng->arg.ctrl.b.skip_phy_id_check &&
		    (p_eng->arg.test_mode == 0))
			/* for SMSC's LAN9303 issue */
			p_eng->dat.Des_Num = 114;
		else {
			switch (p_eng->arg.run_speed) {
			case SET_1GBPS:
			case SET_100MBPS:
			case SET_1G_100MBPS:
				p_eng->dat.Des_Num =
				    p_eng->run.delay_margin
					? 100
					: (n_desp_min) ? 512 : 4096;
				break;
			case SET_10MBPS:
			case SET_1G_100M_10MBPS:
			case SET_100M_10MBPS:
			case SET_1G_10MBPS:
				p_eng->dat.Des_Num =
				    p_eng->run.delay_margin
					? 100
					: (n_desp_min) ? 100 : 830;
				break;
			}
		}

		if (p_eng->arg.ctrl.b.single_packet)
			p_eng->dat.Des_Num = 1;

		/* keep in order: Des_Num -> DMABuf_Size -> DMABuf_Num */
		p_eng->dat.Des_Num_Org = p_eng->dat.Des_Num;
		p_eng->dat.DMABuf_Size = DMA_BufSize;
		p_eng->dat.DMABuf_Num = DMA_BufNum;

		if (DbgPrn_Info) {
			printf("CheckBuf_MBSize : %d\n",
			       p_eng->run.CheckBuf_MBSize);
			printf("LOOP_CheckNum   : %d\n",
			       p_eng->run.LOOP_CheckNum);
			printf("Des_Num         : %d\n", p_eng->dat.Des_Num);
			printf("DMA_BufSize     : %d bytes\n",
			       p_eng->dat.DMABuf_Size);
			printf("DMA_BufNum      : %d\n", p_eng->dat.DMABuf_Num);
			printf("DMA_PakSize     : %d\n", DMA_PakSize);
			printf("\n");
		}
		if (2 > p_eng->dat.DMABuf_Num)
			return (finish_check(p_eng, Err_Flag_DMABufNum));
	}

	return 0;
}

/**
 * @brief setup environment according to HW strap registers
*/
static uint32_t setup_interface(MAC_ENGINE *p_eng)
{
#ifdef CONFIG_ASPEED_AST2600
	hw_strap1_t strap1;
	hw_strap2_t strap2;
	
	strap1.w = SCU_RD(0x500);
	strap2.w = SCU_RD(0x510);

	p_eng->env.is_1g_valid[0] = strap1.b.mac1_interface;
	p_eng->env.is_1g_valid[1] = strap1.b.mac2_interface;
	p_eng->env.is_1g_valid[2] = strap2.b.mac3_interface;
	p_eng->env.is_1g_valid[3] = strap2.b.mac4_interface;
	
	p_eng->env.at_least_1g_valid =
	    p_eng->env.is_1g_valid[0] | p_eng->env.is_1g_valid[1] |
	    p_eng->env.is_1g_valid[2] | p_eng->env.is_1g_valid[3];
#else
	hw_strap1_t strap1;
	strap1.w = SCU_RD(0x70);
	p_eng->env.is_1g_valid[0] = strap1.b.mac1_interface;
	p_eng->env.is_1g_valid[1] = strap1.b.mac2_interface;

	p_eng->env.at_least_1g_valid =
	    p_eng->env.is_1g_valid[0] | p_eng->env.is_1g_valid[1];
#endif
	return 0;
}

/**
 * @brief setup chip compatibility accoriding to the chip ID register
*/
static uint32_t setup_chip_compatibility(MAC_ENGINE *p_eng)
{
	uint32_t reg_addr;
	uint32_t id, version;
	uint32_t is_valid;

	p_eng->env.ast2600 = 0;
	p_eng->env.ast2500 = 0;

#if defined(CONFIG_ASPEED_AST2600)
	reg_addr = 0x04;
#else
	reg_addr = 0x7c;
#endif
	is_valid = 0;
	id = (SCU_RD(reg_addr) & GENMASK(31, 24)) >> 24;
	version = (SCU_RD(reg_addr) & GENMASK(23, 16)) >> 16;

#if defined(CONFIG_FPGA_ASPEED) && defined(CONFIG_ASPEED_AST2600)
	id = 0x5;
#endif
	if (id == 0x5) {
		printf("chip: AST2600 A%d\n", version);
		p_eng->env.ast2600 = 1;
		p_eng->env.ast2500 = 1;
		p_eng->env.mac_num = 4;
		p_eng->env.is_new_mdio_reg[0] = 1;
		p_eng->env.is_new_mdio_reg[1] = 1;
		p_eng->env.is_new_mdio_reg[2] = 1;
		p_eng->env.is_new_mdio_reg[3] = 1;
		is_valid = 1;
	} else if (id == 0x4) {
		printf("chip: AST2500 A%d\n", version);
		p_eng->env.ast2500 = 1;
		p_eng->env.mac_num = 2;
		p_eng->env.is_new_mdio_reg[0] = MAC1_RD(0x40) >> 31;
		p_eng->env.is_new_mdio_reg[1] = MAC2_RD(0x40) >> 31;
		is_valid = 1;
	}

	if (0 == is_valid) {
		printf("unknown chip\n");
		return 1;
	}

	return 0;
}

/**
 * @brief setup environment accoriding to the HW strap and chip ID
*/
static uint32_t setup_env(MAC_ENGINE *p_eng)
{
	if (0 != setup_chip_compatibility(p_eng)) {
		return 1;
	}
	
	setup_interface(p_eng);
	return 0;
}

static uint32_t init_mac_engine(MAC_ENGINE *p_eng, uint32_t mode)
{
	memset(p_eng, 0, sizeof(MAC_ENGINE));

	if (0 != setup_env(p_eng)) {
		return 1;
	}
	
	p_eng->arg.run_mode = mode;
	p_eng->arg.delay_scan_range = DEF_GIOTIMINGBUND;
	p_eng->arg.test_mode = DEF_GTESTMODE;

	if (p_eng->arg.run_mode == MODE_NCSI ) {
		p_eng->arg.GARPNumCnt = DEF_GARPNUMCNT;
		p_eng->arg.GChannelTolNum = DEF_GCHANNEL2NUM;
		p_eng->arg.GPackageTolNum = DEF_GPACKAGE2NUM;
		p_eng->arg.ctrl.w = 0;
		p_eng->arg.run_speed = SET_100MBPS;        // In NCSI mode, we set to 100M bps
	} else {
		p_eng->arg.user_def_val  = DEF_GUSER_DEF_PACKET_VAL;
		p_eng->arg.phy_addr  = DEF_GPHY_ADR;
		p_eng->arg.loop_inf = 0;
		p_eng->arg.loop_max = 0;
		p_eng->arg.ctrl.w = DEF_GCTRL;
		p_eng->arg.run_speed = DEF_GSPEED;
	}

	p_eng->flg.print_en  = 1;	
	
	p_eng->run.TM_TxDataEn = 1;
	p_eng->run.TM_RxDataEn = 1;
	p_eng->run.TM_NCSI_DiSChannel = 1;

	/* setup 
	 * 1. delay control register
	 * 2. driving strength control register and upper/lower bond
	 * 3. MAC control register
	 */
#ifdef CONFIG_ASPEED_AST2600
	p_eng->io.mac12_1g_delay.addr = SCU_BASE + 0x340;
	p_eng->io.mac12_1g_delay.tx_min = 0;
	p_eng->io.mac12_1g_delay.tx_max = 63;
	p_eng->io.mac12_1g_delay.rx_min = -63;
	p_eng->io.mac12_1g_delay.rx_max = 63;
	p_eng->io.mac12_1g_delay.rmii_tx_min = 0;
	p_eng->io.mac12_1g_delay.rmii_tx_max = 1;
	p_eng->io.mac12_1g_delay.rmii_rx_min = 0;
	p_eng->io.mac12_1g_delay.rmii_rx_max = 63;

	p_eng->io.mac12_100m_delay.addr = SCU_BASE + 0x348;
	p_eng->io.mac12_100m_delay.tx_min = 0;
	p_eng->io.mac12_100m_delay.tx_max = 63;
	p_eng->io.mac12_100m_delay.rx_min = -63;
	p_eng->io.mac12_100m_delay.rx_max = 63;
	p_eng->io.mac12_10m_delay.addr = SCU_BASE + 0x34c;
	p_eng->io.mac12_10m_delay.tx_min = 0;
	p_eng->io.mac12_10m_delay.tx_max = 63;
	p_eng->io.mac12_10m_delay.rx_min = -63;
	p_eng->io.mac12_10m_delay.rx_max = 63;

	p_eng->io.mac34_1g_delay.addr = SCU_BASE + 0x350;
	p_eng->io.mac34_1g_delay.tx_min = 0;
	p_eng->io.mac34_1g_delay.tx_max = 63;
	p_eng->io.mac34_1g_delay.rx_min = -63;
	p_eng->io.mac34_1g_delay.rx_max = 63;
	p_eng->io.mac34_1g_delay.rmii_tx_min = 0;
	p_eng->io.mac34_1g_delay.rmii_tx_max = 1;
	p_eng->io.mac34_1g_delay.rmii_rx_min = 0;
	p_eng->io.mac34_1g_delay.rmii_rx_max = 63;
	p_eng->io.mac34_100m_delay.addr = SCU_BASE + 0x358;
	p_eng->io.mac34_100m_delay.tx_min = 0;
	p_eng->io.mac34_100m_delay.tx_max = 63;
	p_eng->io.mac34_100m_delay.rx_min = -63;
	p_eng->io.mac34_100m_delay.rx_max = 63;
	p_eng->io.mac34_10m_delay.addr = SCU_BASE + 0x35c;
	p_eng->io.mac34_10m_delay.tx_min = 0;
	p_eng->io.mac34_10m_delay.tx_max = 63;
	p_eng->io.mac34_10m_delay.rx_min = -63;
	p_eng->io.mac34_10m_delay.rx_max = 63;

	p_eng->io.mac34_drv_reg.addr = SCU_BASE + 0x458;
	p_eng->io.mac34_drv_reg.drv_max = 0x3;
	p_eng->io.drv_upper_bond = 0x3;
	p_eng->io.drv_lower_bond = 0;
#else
	p_eng->io.mac12_1g_delay.addr = SCU_BASE + 0x48;
	p_eng->io.mac12_1g_delay.tx_min = 0;
	p_eng->io.mac12_1g_delay.tx_max = 63;
	p_eng->io.mac12_1g_delay.rx_min = 0;
	p_eng->io.mac12_1g_delay.rx_max = 63;
	p_eng->io.mac12_1g_delay.rmii_tx_min = 0;
	p_eng->io.mac12_1g_delay.rmii_tx_max = 1;
	p_eng->io.mac12_1g_delay.rmii_rx_min = 0;
	p_eng->io.mac12_1g_delay.rmii_rx_max = 63;
	p_eng->io.mac12_100m_delay.addr = SCU_BASE + 0xb8;
	p_eng->io.mac12_100m_delay.tx_min = 0;
	p_eng->io.mac12_100m_delay.tx_max = 63;
	p_eng->io.mac12_100m_delay.rx_min = 0;
	p_eng->io.mac12_100m_delay.rx_max = 63;
	p_eng->io.mac12_10m_delay.addr = SCU_BASE + 0xbc;
	p_eng->io.mac12_10m_delay.tx_min = 0;
	p_eng->io.mac12_10m_delay.tx_max = 63;
	p_eng->io.mac12_10m_delay.rx_min = 0;
	p_eng->io.mac12_10m_delay.rx_max = 63;

	p_eng->io.mac34_1g_delay.addr = 0;
	p_eng->io.mac34_100m_delay.addr = 0;
	p_eng->io.mac34_10m_delay.addr = 0;

	p_eng->io.mac12_drv_reg.addr = SCU_BASE + 0x90;
	p_eng->io.mac12_drv_reg.drv_max = 0x1;
	p_eng->io.drv_upper_bond = 0x1;
	p_eng->io.drv_lower_bond = 0;
#endif
	return 0;
}

static uint32_t parse_arg_dedicated(int argc, char *const argv[],
				    MAC_ENGINE *p_eng) 
{
	switch (argc) {
	case 10:
		p_eng->arg.user_def_val = simple_strtol(argv[9], NULL, 16);
	case 9:
		p_eng->arg.delay_scan_range = simple_strtol(argv[8], NULL, 10);
		p_eng->arg.ieee_sel = p_eng->arg.delay_scan_range;
	case 8:
		p_eng->arg.phy_addr = simple_strtol(argv[7], NULL, 10);
	case 7:
		p_eng->arg.test_mode = simple_strtol(argv[6], NULL, 16);
		printf("test mode = %d\n", p_eng->arg.test_mode);
	case 6:		
		p_eng->arg.loop_max = simple_strtol(argv[5], NULL, 10);
		if (p_eng->arg.loop_max == -1) {
			p_eng->arg.loop_inf = 1;
		}
		printf("loop max=%d, loop_inf=%d\n", p_eng->arg.loop_max, p_eng->arg.loop_inf);
	case 5:
		p_eng->arg.ctrl.w = simple_strtol(argv[4], NULL, 16);
		printf("ctrl=0x%05x\n", p_eng->arg.ctrl.w);
	case 4:
		p_eng->arg.run_speed = simple_strtol(argv[3], NULL, 16);
		printf("speed=0x%1x\n", p_eng->arg.run_speed);
	case 3:
		p_eng->arg.mdio_idx = simple_strtol(argv[2], NULL, 10);
		printf("mdio_idx=%d\n", p_eng->arg.mdio_idx);		
	}

	return 0;
}

static uint32_t parse_arg_ncsi(int argc, char *const argv[], MAC_ENGINE *p_eng) 
{
	switch (argc) {
	case 8:
		p_eng->arg.GARPNumCnt = simple_strtol(argv[7], NULL, 10);
	case 7:
		p_eng->arg.ctrl.w = simple_strtol(argv[6], NULL, 16);
	case 6:
		p_eng->arg.delay_scan_range = simple_strtol(argv[5], NULL, 10);		
	case 5:
		p_eng->arg.test_mode = simple_strtol(argv[4], NULL, 16);
	case 4:
		p_eng->arg.GChannelTolNum  = simple_strtol(argv[3], NULL, 10);
	case 3:
		p_eng->arg.GPackageTolNum  = simple_strtol(argv[2], NULL, 10);
	}
	return 0;
}


static void disable_wdt(MAC_ENGINE *p_eng)
{
#ifdef CONFIG_ASPEED_AST2600
	writel(0, 0x1e620064);
#else
	writel(0, 0x1e78502c);
#endif
	return;
}

static uint32_t setup_data(MAC_ENGINE *p_eng)
{
	if (p_eng->arg.run_mode == MODE_DEDICATED) {
		if (p_eng->run.tm_tx_only)
			setup_arp(p_eng);
		
		p_eng->dat.FRAME_LEN =
		    (uint32_t *)malloc(p_eng->dat.Des_Num * sizeof(uint32_t));
		p_eng->dat.wp_lst =
		    (uint32_t *)malloc(p_eng->dat.Des_Num * sizeof(uint32_t));

		if (!p_eng->dat.FRAME_LEN)
			return (finish_check(p_eng, Err_Flag_MALLOC_FrmSize));
		if (!p_eng->dat.wp_lst)
			return (finish_check(p_eng, Err_Flag_MALLOC_LastWP));

		TestingSetup(p_eng);
	} else {
		if (p_eng->arg.GARPNumCnt != 0)
			setup_arp(p_eng);
	}

	p_eng->run.speed_idx = 0;
	p_eng->io.drv_curr = mac_get_driving_strength(p_eng);
	if (mac_set_scan_boundary(p_eng))
		return (finish_check(p_eng, 0));

	return 0;			
}

static uint32_t get_time_out_th(MAC_ENGINE *p_eng)
{
	uint32_t time_out;	
	
	time_out = timeout_th_tbl[p_eng->run.speed_idx];
	if (p_eng->run.TM_WaitStart)
		time_out = time_out * 10000;
	
	return time_out;		
}
uint32_t test_start(MAC_ENGINE *p_eng, PHY_ENGINE *p_phy_eng)
{
	uint32_t drv, speed;
	int td, rd, tbegin, rbegin, tend, rend;
	int tstep, rstep;

	uint32_t wrn_flag_allspeed = 0;
	uint32_t err_flag_allspeed = 0;
	uint32_t des_flag_allspeed = 0;
	uint32_t ncsi_flag_allspeed = 0;

	memset(&p_eng->io.result_history[0][0], 0,
	       sizeof(p_eng->io.result_history));

	for (speed = 0; speed < 3; speed++) {
		p_eng->flg.print_en = 1;
		p_eng->run.speed_idx = speed;
		mac_set_scan_boundary(p_eng);
		if (0 == p_eng->run.speed_sel[speed]) {
			continue;
		}

		p_eng->run.timeout_th = get_time_out_th(p_eng);
		if (p_eng->arg.run_mode == MODE_DEDICATED) {
			if ((p_eng->arg.run_speed == SET_1G_100M_10MBPS) ||
			    (p_eng->arg.run_speed == SET_100M_10MBPS) ||
			    (p_eng->arg.run_speed == SET_1G_100MBPS) ||
			    (p_eng->arg.run_speed == SET_1G_10MBPS)) {
				if (p_eng->run.speed_sel[0])
					p_eng->run.loop_max =
					    p_eng->arg.loop_max;
				else if (p_eng->run.speed_sel[1])
					p_eng->run.loop_max =
					    p_eng->arg.loop_max / 100;
				else
					p_eng->run.loop_max =
					    p_eng->arg.loop_max / 1000;

				if (0 == p_eng->run.loop_max)
					p_eng->run.loop_max = 1;

				calc_loop_check_num(p_eng);
			}
			//------------------------------
			// PHY Initial
			//------------------------------
			if (p_phy_eng->fp_set) {
				init_phy(p_eng, p_phy_eng);
			}

			if (p_eng->flg.error)
				return (finish_check(p_eng, 0));
		}

		//------------------------------
		// [Start] The loop of different IO strength
		//------------------------------
		debug("drirving scan range: %d ~ %d\n",
		       p_eng->io.drv_lower_bond, p_eng->io.drv_upper_bond);
		for (drv = p_eng->io.drv_lower_bond;
		     drv <= p_eng->io.drv_upper_bond; drv++) {
			if (p_eng->run.IO_MrgChk) {
				if (p_eng->run.TM_IOStrength) {
					mac_set_driving_strength(p_eng, drv);
					p_eng->io.drv_curr = mac_get_driving_strength(p_eng);
				}

				if (p_eng->run.delay_margin)
					PrintIO_Header(p_eng, FP_LOG);
				if (p_eng->run.TM_IOTiming)
					PrintIO_Header(p_eng, FP_IO);
				PrintIO_Header(p_eng, STD_OUT);
			} else {
				if (p_eng->arg.run_mode == MODE_DEDICATED) {
					Print_Header(p_eng, STD_OUT);
				}
			} // End if (p_eng->run.IO_MrgChk)

			//------------------------------
			// [Start] The loop of different IO out delay
			//------------------------------
			tbegin = p_eng->io.tx_delay_scan.begin;
			tend = p_eng->io.tx_delay_scan.end;
			tstep = p_eng->io.tx_delay_scan.step;

			rbegin = p_eng->io.rx_delay_scan.begin;
			rend = p_eng->io.rx_delay_scan.end;
			rstep = p_eng->io.rx_delay_scan.step;

			for (td = tbegin; td <= tend; td += tstep) {
				p_eng->io.Dly_out = td;
				p_eng->io.Dly_out_selval = td;
				if (p_eng->run.IO_MrgChk) {
					PrintIO_LineS(p_eng, STD_OUT);
				} // End if (p_eng->run.IO_MrgChk)

				//------------------------------
				// [Start] The loop of different IO in
				// delay
				//------------------------------
				for (rd = rbegin; rd <= rend; rd += rstep) {
					p_eng->io.Dly_in = rd;
					if (p_eng->run.IO_MrgChk) {
						p_eng->io.Dly_in_selval = rd;
						scu_disable_mac(p_eng);
						mac_set_delay(p_eng, rd, td);
						scu_enable_mac(p_eng);
					}
					//------------------------------
					// MAC Initial
					//------------------------------
					init_mac(p_eng);
					if (p_eng->flg.error)
						return (finish_check(p_eng, 0));

					if (p_eng->arg.run_mode == MODE_NCSI) {
						p_eng->io.result =
						    phy_ncsi(p_eng);
					} else {
						p_eng->io.result = TestingLoop(
						    p_eng,
						    p_eng->run.LOOP_CheckNum);
					}

					p_eng->io.result_history[rd + 64][td] |=
					    p_eng->io.result;

					/* Display to Log file and monitor */
					if (p_eng->run.IO_MrgChk) {
						PrintIO_Line(p_eng, STD_OUT);

						FPri_ErrFlag(p_eng, FP_LOG);

						p_eng->flg.warn = 0;
						p_eng->flg.error = 0;
						p_eng->flg.desc = 0;
						p_eng->flg.ncsi = 0;
					}
				}

				if (p_eng->run.IO_MrgChk) {
					if (p_eng->run.TM_IOTiming) {
						PRINTF(FP_IO, "\n");
					}
					printf("\n");
				}
			}

			if (!p_eng->run.tm_tx_only)
				FPri_ErrFlag(p_eng, FP_LOG);
			if (p_eng->run.TM_IOTiming)
				FPri_ErrFlag(p_eng, FP_IO);

			FPri_ErrFlag(p_eng, STD_OUT);

			wrn_flag_allspeed |= p_eng->flg.warn;
			err_flag_allspeed |= p_eng->flg.error;
			des_flag_allspeed |= p_eng->flg.error;
			ncsi_flag_allspeed |= p_eng->flg.error;
			p_eng->flg.warn = 0;
			p_eng->flg.error = 0;
			p_eng->flg.desc = 0;
			p_eng->flg.ncsi = 0;
		}

		if (p_eng->arg.run_mode == MODE_DEDICATED) {
			if (p_phy_eng->fp_clr != 0)
				recov_phy(p_eng, p_phy_eng);
		}

		p_eng->run.speed_sel[speed] = 0;
		p_eng->flg.print_en = 0;
	} // End for (speed = 0; speed < 3; speed++)

	p_eng->flg.warn = wrn_flag_allspeed;
	p_eng->flg.error = err_flag_allspeed;
	p_eng->flg.desc = des_flag_allspeed;
	p_eng->flg.ncsi = ncsi_flag_allspeed;

	return (finish_check(p_eng, 0));
}
static uint32_t ring_clk(uint32_t reg_offset, uint32_t clk_sel)
{
	uint32_t freq;

	SCU_WR(0, reg_offset);
	SCU_WR((0xf << 2) | BIT(0), reg_offset);
	udelay(1000);
	SCU_WR((clk_sel << 2) | BIT(1) | BIT(0), reg_offset);
	while ((SCU_RD(reg_offset) & BIT(6)) == 0);

	freq = (SCU_RD(reg_offset) & GENMASK(29, 16)) >> 16;
	SCU_WR(0, reg_offset);
	return ((freq + 1) * 48828);
}

void dump_setting(MAC_ENGINE *p_eng)
{
	/* dump env */
	printf("===================\n");
	printf("ast2600 compatible = %d\n", p_eng->env.ast2600);
	printf("ast2500 compatible = %d\n", p_eng->env.ast2500);
	printf("valid MAC number = %d\n", p_eng->env.mac_num);
	printf("use new MDIO register = %d %d %d %d\n",
	       p_eng->env.is_new_mdio_reg[0],
	       p_eng->env.is_new_mdio_reg[1],
	       p_eng->env.is_new_mdio_reg[2],
	       p_eng->env.is_new_mdio_reg[3]);
	printf("1G compatible = %d %d %d %d\n",
	       p_eng->env.is_1g_valid[0],
	       p_eng->env.is_1g_valid[1],
	       p_eng->env.is_1g_valid[2],
	       p_eng->env.is_1g_valid[3]);	
	printf("===================\n");


#if defined(CONFIG_ASPEED_AST2600)
	printf("RGMIICK of MAC1/2 = %d Hz\n", ring_clk(0x320, 0xf));
	printf("RGMIICK of MAC3/4 = %d Hz\n", ring_clk(0x330, 0x9));
	printf("EPLL              = %d Hz\n", ring_clk(0x320, 0x5) * 4);
	printf("HCLK              = %d Hz\n", ring_clk(0x330, 0x1));
#endif

}
/**
 * @brief nettest main function
*/
int mac_test(int argc, char * const argv[], uint32_t mode)
{
	MAC_ENGINE mac_eng;
	PHY_ENGINE phy_eng;
	uint32_t ret;

	ret = init_mac_engine(&mac_eng, mode);
	if (ret) {
		printf("init MAC engine fail\n");
		return ret;
	}
	
	if (argc <= 1) {
		print_usage(&mac_eng);
		return 1;
	}

	mac_eng.arg.mac_idx = simple_strtol(argv[1], NULL, 16);

	/* default mdio_idx = mac_idx */
	mac_eng.arg.mdio_idx = mac_eng.arg.mac_idx;
	if (MODE_DEDICATED == mode)
		parse_arg_dedicated(argc, argv, &mac_eng);
	else		
		parse_arg_ncsi(argc, argv, &mac_eng);

	ret = setup_running(&mac_eng); 
	if (ret)
		return 1;

	dump_setting(&mac_eng);

	/* init PHY engine */
	phy_eng.fp_set = NULL;
	phy_eng.fp_clr = NULL;

	if (mac_eng.arg.ctrl.b.rmii_50m_out && 0 == mac_eng.run.is_rgmii) {
		mac_set_rmii_50m_output_enable(&mac_eng);
	}

	push_reg(&mac_eng);
	disable_wdt(&mac_eng);

	mac_set_addr(&mac_eng);
	if (mac_eng.arg.ctrl.b.mac_int_loopback)
		mac_set_interal_loopback(&mac_eng);

	scu_set_pinmux(&mac_eng);

	scu_disable_mac(&mac_eng);
	scu_enable_mac(&mac_eng);
	if (mac_eng.arg.run_mode == MODE_DEDICATED) {
		if (1 == phy_find_addr(&mac_eng)) {
			phy_select(&mac_eng, &phy_eng);		
		}
	}

	/* Data Initial */
	setup_data(&mac_eng);

	mac_eng.flg.all_fail = 1;
	mac_eng.io.init_done = 1;
	for(int i = 0; i < 3; i++)
		mac_eng.run.speed_sel[i] = mac_eng.run.speed_cfg[i];

	//------------------------------
	// [Start] The loop of different speed
	//------------------------------
	print_legend();
	test_start(&mac_eng, &phy_eng);

	return 0;
}
