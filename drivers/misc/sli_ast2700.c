// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) Aspeed Technology Inc.
 */
#include <asm/io.h>
#include <asm/arch/platform.h>
#include <asm/arch/scu_ast2700.h>
#include <common.h>
#include <dm.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/iopoll.h>

#define SLI_POLL_TIMEOUT_US	100

#define SLIM_REG_OFFSET			0x000
#define SLIH_REG_OFFSET			0x200
#define SLIV_REG_OFFSET			0x400

#define SLI_CTRL_I			0x00
#define   SLI_ALL_IN_SUSPEND            BIT(28)
#define   SLI_AUTO_CLR_OFF_DAT          BIT(23) /* No auto-clear when changing data pad delay */
#define   SLI_AUTO_CLR_OFF_CLK          BIT(22) /* No auto-clear when changing clock pad delay */
#define   SLI_SP_DOWN_PERIOD            GENMASK(21, 20)
#define   SLI_NO_RST_TXCLK_CHG          BIT(17) /* No reset when changing TX clock */
#define   SLIV_RAW_MODE			BIT(15)
#define   SLI_TX_MODE			BIT(14)
#define   SLI_RX_PHY_LAH_SEL_REV	BIT(13)
#define   SLI_RX_PHY_LAH_SEL_NEG	BIT(12)
#define   SLI_AUTO_SEND_TRN_OFF		BIT(8)
#define   SLI_CLEAR_BUS			BIT(6)
#define   SLI_TRANS_EN			BIT(5)
#define   SLI_CLEAR_RX			BIT(2)
#define   SLI_CLEAR_TX			BIT(1)
#define   SLI_RESET_TRIGGER		BIT(0)
#define SLI_CTRL_II			0x04
#define   SLIV_TX_ENT_SUSPEND		GENMASK(15, 14)
#define SLI_CTRL_III			0x08
#define   SLI_CLK_SEL			GENMASK(31, 28)
#define     SLI_CLK_500M		0x6
#define     SLI_CLK_200M		0x3
#define   SLI_PHYCLK_SEL		GENMASK(27, 24)
#define     SLI_PHYCLK_25M		0x0
#define     SLI_PHYCLK_800M		0x1
#define     SLI_PHYCLK_400M		0x2
#define     SLI_PHYCLK_200M		0x3
#define     SLI_PHYCLK_1G		0x5	/* AST2700A1 */
#define     SLI_PHYCLK_788M		0x5	/* AST2700A0 */
#define     SLI_PHYCLK_500M		0x6
#define     SLI_PHYCLK_250M		0x7
#define   SLIH_PAD_DLY_TX1		GENMASK(23, 18)
#define   SLIH_PAD_DLY_TX0		GENMASK(17, 12)
#define   SLIH_PAD_DLY_RX1		GENMASK(11, 6)
#define   SLIH_PAD_DLY_RX0		GENMASK(5, 0)
#define   SLIV_PAD_DLY_TX1		GENMASK(23, 18)
#define   SLIV_PAD_DLY_TX0		GENMASK(17, 12)
#define   SLIV_PAD_DLY_RX1		GENMASK(11, 6)
#define   SLIV_PAD_DLY_RX0		GENMASK(5, 0)
#define   SLIM_PAD_DLY_RX3		GENMASK(23, 18)
#define   SLIM_PAD_DLY_RX2		GENMASK(17, 12)
#define   SLIM_PAD_DLY_RX1		GENMASK(11, 6)
#define   SLIM_PAD_DLY_RX0		GENMASK(5, 0)
#define SLI_CTRL_IV			0x0c
#define   SLIM_PAD_DLY_TX3		GENMASK(23, 18)
#define   SLIM_PAD_DLY_TX2		GENMASK(17, 12)
#define   SLIM_PAD_DLY_TX1		GENMASK(11, 6)
#define   SLIM_PAD_DLY_TX0		GENMASK(5, 0)
#define SLI_INTR_EN			0x10
#define SLI_INTR_STATUS			0x14
#define   SLI_INTR_RX_SYNC		BIT(15)
#define   SLI_INTR_RX_ERR		BIT(13)
#define   SLI_INTR_RX_NACK		BIT(12)
#define   SLI_INTR_RX_TRAIN_PKT		BIT(10)
#define   SLI_INTR_RX_DISCONN		BIT(6)
#define   SLI_INTR_TX_SUSPEND		BIT(4)
#define   SLI_INTR_TX_TRAIN		BIT(3)
#define   SLI_INTR_TX_IDLE		BIT(2)
#define   SLI_INTR_RX_SUSPEND		BIT(1)
#define   SLI_INTR_RX_IDLE		BIT(0)
#define   SLI_INTR_RX_ERRORS                                                     \
	  (SLI_INTR_RX_ERR | SLI_INTR_RX_NACK | SLI_INTR_RX_DISCONN)

#define SLIM_MARB_FUNC_I		0x60
#define   SLIM_SLI_MARB_CLR		BIT(4)
#define   SLIM_SLI_MARB_RR		BIT(0)

struct sli_config {
	uintptr_t slim; /* SLI MBUS */
	uintptr_t slih; /* SLI AHB */
	uintptr_t sliv; /* SLI VIDEO */
	int eng_clk_freq;
	int phy_clk_freq;
};

struct sli_data {
	struct sli_config die0;	/* CPU die */
	struct sli_config die1;	/* IO die */
	struct ast2700_scu0 *scu0;
	struct ast2700_scu1 *scu1;

#define SLI_FLAG_AST2700A0		BIT(0)
#define SLI_FLAG_RX_LAH_NEG_IO_SLIH	BIT(1)
#define SLI_FLAG_RX_LAH_NEG_IO_SLIM	BIT(2)
#define SLI_FLAG_RX_LAH_NEG_IO_SLIV	BIT(3)
	uint32_t flags;
};

#define SLIH_COARSE_D_BEGIN		6
#define SLIH_COARSE_D_END		28

#define SLIM_COARSE_D_BEGIN		0
#define SLIM_COARSE_D_END		28
#define SLIM_FINE_MARGIN		5

#define SLIV_COARSE_D_BEGIN		0
#define SLIV_COARSE_D_END		28

#define SCU1_SCRATCH31_SLI0_READY	BIT(0)
#define SCU1_SCRATCH31_SLI_SKIP_CALI	BIT(1)	/* skip calibration */
#define SCU0_SCRATCH31_SLI1_READY	BIT(0)
#define AHBC_MAX_TIMEOUT		0x1ff

static void ast2700_set_ahbc_timeouts(uintptr_t base, u32 value, u8 count)
{
	static const u32 offsets[] = {
		0x034, 0x074, 0x0b4, 0x0f4,
		0x134, 0x174, 0x1b4, 0x1f4,
	};
	int i;

	for (i = 0; i < count; i++)
		writel(value, (void *)base + offsets[i]);
}

static void sli_clear_interrupt_status(uintptr_t base)
{
	writel(0xfffff, (void *)base + SLI_INTR_STATUS);
}

static int sli_wait(uintptr_t base, uint32_t mask)
{
	uint32_t value;

	sli_clear_interrupt_status(base);

	do {
		value = readl((void *)base + SLI_INTR_STATUS);
		if (value & SLI_INTR_RX_ERRORS)
			return -1;
	} while ((value & mask) != mask);

	return 0;
}

static int sli_wait_suspend(uintptr_t base)
{
	return sli_wait(base, SLI_INTR_TX_SUSPEND | SLI_INTR_RX_SUSPEND);
}

static int is_sli_suspend(uintptr_t base)
{
	uint32_t value;
	uint32_t suspend = SLI_INTR_TX_SUSPEND | SLI_INTR_RX_SUSPEND;

	value = readl((void *)base + SLI_INTR_STATUS);
	if (value & SLI_INTR_RX_ERRORS)
		return -1;
	else if ((value & suspend) == suspend)
		return 1;
	else
		return 0;
}

static int sli_wait_clear_done(uintptr_t base, uint32_t mask)
{
	uint32_t value;

	return readl_poll_timeout((void *)base + SLI_CTRL_I, value, (value & mask) == 0,
				  SLI_POLL_TIMEOUT_US);
}

static int sli_clear(uintptr_t base, uint32_t clr)
{
	setbits_le32(base + SLI_CTRL_I, clr);

	return sli_wait_clear_done(base, clr & ~SLI_CLEAR_BUS);
}

static void __maybe_unused sli_set_ahb_rx_delay_single(uintptr_t base, int index, int d)
{
	uint32_t offset = index * 6;
	uint32_t mask = SLIH_PAD_DLY_RX0 << offset;

	clrsetbits_le32(base + SLI_CTRL_III, mask, d << offset);
	readl((void *)base + SLI_CTRL_III);
	udelay(8);
}

static void sli_set_ahb_rx_delay(uintptr_t base, int d0, int d1)
{
	uint32_t value;

	value = FIELD_PREP(SLIH_PAD_DLY_RX1, d1) | FIELD_PREP(SLIH_PAD_DLY_RX0, d0);
	clrsetbits_le32(base + SLI_CTRL_III, SLIH_PAD_DLY_RX1 | SLIH_PAD_DLY_RX0, value);
	readl((void *)base + SLI_CTRL_III);
	udelay(8);
}

static void sli_log_ahb_pad_delay(struct sli_data *data, int first, int last)
{
	clrsetbits_le32(&data->scu1->scratch[30], 0xffff, ((last & 0xff) << 8) | (first & 0xff));
}

static void sli_get_ahb_pad_delay(struct sli_data *data, int *first, int *last)
{
	uint32_t value;

	value = readl((void *)&data->scu1->scratch[30]);
	*first = (value & 0xff);
	*last = (value >> 8) & 0xff;
}

static void sli_calibrate_ahb_delay(struct sli_data *data)
{
	int dc;
	int d_first_pass = -1;
	int d_last_pass = -1;
	int win_size = 0;

	setbits_le32(data->die1.slih + SLI_CTRL_I, SLI_AUTO_SEND_TRN_OFF);

	if (data->flags & SLI_FLAG_RX_LAH_NEG_IO_SLIH)
		setbits_le32(data->die1.slih + SLI_CTRL_I, SLI_RX_PHY_LAH_SEL_NEG);
	else
		clrbits_le32(data->die1.slih + SLI_CTRL_I, SLI_RX_PHY_LAH_SEL_NEG);

	for (dc = SLIH_COARSE_D_BEGIN; dc < SLIH_COARSE_D_END; dc++) {
		sli_set_ahb_rx_delay(data->die1.slih, dc, dc);
		sli_clear(data->die1.slih, SLI_CLEAR_RX | SLI_CLEAR_BUS);

		/* Check result */
		sli_clear_interrupt_status(data->die1.slih);
		udelay(200);
		if (is_sli_suspend(data->die1.slih) > 0) {
			if (d_first_pass == -1)
				d_first_pass = dc;

			d_last_pass = dc;
		} else if (d_last_pass != -1) {
			if ((d_last_pass - d_first_pass) > win_size) {
				win_size = d_last_pass - d_first_pass;
				sli_log_ahb_pad_delay(data, d_first_pass, d_last_pass);
				debug("IOD SLIH DS coarse win: {%d, %d}\n", d_first_pass, d_last_pass);
			}
			d_first_pass = -1;
			d_last_pass = -1;
		}
	}

	if (d_last_pass != -1 && (d_last_pass - d_first_pass) > win_size) {
		win_size = d_last_pass - d_first_pass;
		sli_log_ahb_pad_delay(data, d_first_pass, d_last_pass);
		debug("IOD SLIH DS coarse win: {%d, %d}\n", d_first_pass, d_last_pass);
	}

	sli_get_ahb_pad_delay(data, &d_first_pass, &d_last_pass);
	dc = (d_first_pass + d_last_pass) >> 1;
	debug("IOD SLIH DS coarse win: {%d, %d} -> select %d\n", d_first_pass, d_last_pass, dc);

	sli_set_ahb_rx_delay(data->die1.slih, dc, dc);

	/* Reset IOD SLIH Bus (to reset the counters) and RX */
	sli_clear(data->die1.slih, SLI_CLEAR_RX | SLI_CLEAR_BUS);

	/* Turn on the hardware training and wait suspend state */
	clrbits_le32(data->die1.slih + SLI_CTRL_I, SLI_AUTO_SEND_TRN_OFF);
	sli_wait_suspend(data->die1.slih);

	/* SLI-H is available now */
}

static void sli_set_mbus_delay_single(uintptr_t base, int index, int d, bool is_rx)
{
	uint32_t offset = index * 6;
	uint32_t mask = SLIM_PAD_DLY_RX0 << offset;
	void *reg_base = (is_rx) ?
			 (void *)(base + SLI_CTRL_III) :
			 (void *)(base + SLI_CTRL_IV);

	clrsetbits_le32(reg_base, mask, d << offset);
	readl(reg_base);
	udelay(8);
}

static void sli_set_mbus_delay(uintptr_t base, int d0, int d1, int d2, int d3, bool is_rx)
{
	uint32_t clr, set;
	void *reg_base = (is_rx) ?
			 (void *)(base + SLI_CTRL_III) :
			 (void *)(base + SLI_CTRL_IV);

	clr = SLIM_PAD_DLY_RX3 | SLIM_PAD_DLY_RX2 | SLIM_PAD_DLY_RX1 | SLIM_PAD_DLY_RX0;
	set = FIELD_PREP(SLIM_PAD_DLY_RX3, d3) | FIELD_PREP(SLIM_PAD_DLY_RX2, d2) |
	      FIELD_PREP(SLIM_PAD_DLY_RX1, d1) | FIELD_PREP(SLIM_PAD_DLY_RX0, d0);
	clrsetbits_le32(reg_base, clr, set);
	readl(reg_base);
	udelay(8);
}

static void sli_log_mbus_pad_delay(struct sli_data *data, int index, int first, int last)
{
	uintptr_t addr;
	uint32_t bit_offset;

	if (index > 1)
		addr = (uintptr_t)&data->scu1->scratch[29];
	else
		addr = (uintptr_t)&data->scu1->scratch[28];

	if (index & 1)
		bit_offset = 16;
	else
		bit_offset = 0;

	clrsetbits_le32(addr, 0xffff << bit_offset,
			(last << (bit_offset + 8)) | (first << bit_offset));
}

static void sli_get_mbus_pad_delay(struct sli_data *data, int index, int *first, int *last)
{
	uintptr_t addr;
	uint32_t value;
	uint32_t bit_offset;

	if (index > 1)
		addr = (uintptr_t)&data->scu1->scratch[29];
	else
		addr = (uintptr_t)&data->scu1->scratch[28];

	if (index & 1)
		bit_offset = 16;
	else
		bit_offset = 0;

	value = readl((void *)addr);
	*first = (value >> bit_offset) & 0xff;
	*last = (value >> (bit_offset + 8)) & 0xff;
}

static int sli_calibrate_mbus_pad_delay(struct sli_data *data, int index, int begin, int end, bool is_k_rx)
{
	int d;
	int d_first_pass = -1;
	int d_last_pass = -1;
	int d_def = (begin + end) / 2;
	int count;
	char *die_name = (is_k_rx) ? "IOD" : "CPUD";
	uintptr_t kx = (is_k_rx) ? data->die1.slim : data->die0.slim;

	for (count = 0; count < 50; count++) {
		for (d = begin; d < end; d++) {
			sli_set_mbus_delay_single(kx, index, d, is_k_rx);

			/* Reset CPU-die TX and IO-die RX */
			sli_clear(data->die0.slim, SLI_RESET_TRIGGER);
			sli_clear(data->die1.slim, SLI_RESET_TRIGGER);

			/* Check result */
			sli_clear_interrupt_status(data->die1.slim);
			udelay(200);
			if (is_sli_suspend(data->die1.slim) > 0) {
				if (d_first_pass == -1)
					d_first_pass = d;

				d_last_pass = d;
			} else if (d_last_pass != -1) {
				break;
			}
		}

		if ((d_last_pass - d_first_pass) >= 3)
			break;
		debug("%s SLIM[%d] DS win: {%d, %d} retry %d\n", die_name, index, d_first_pass, d_last_pass, count);
		d_first_pass = -1;
		d_last_pass = -1;
	}

	if (d_first_pass == -1)
		d = d_def;
	else
		d = (d_first_pass + d_last_pass) >> 1;

	debug("%s SLIM[%d] DS win: {%d, %d} -> select %d\n", die_name, index, d_first_pass, d_last_pass, d);
	sli_log_mbus_pad_delay(data, index, d_first_pass, d_last_pass);

	return d;
}

static void sli_calibrate_mbus_delay(struct sli_data *data, bool is_k_rx)
{
	int dc, d0, d1, d2, d3;
	int begin, end;
	int d_first_pass = -1;
	int d_last_pass = -1;
	int d_def = 12;
	int win_size = 0;
	int count;
	char *die_name = (is_k_rx) ? "IOD" : "CPUD";
	uintptr_t kx = (is_k_rx) ? data->die1.slim : data->die0.slim;

	setbits_le32(data->die1.slim + SLI_CTRL_I, SLI_AUTO_SEND_TRN_OFF);

	if (data->die0.phy_clk_freq == SLI_PHYCLK_800M ||
	    data->die0.phy_clk_freq == SLI_PHYCLK_788M)
		d_def = 5;

	if (data->flags & SLI_FLAG_RX_LAH_NEG_IO_SLIM)
		setbits_le32(kx + SLI_CTRL_I, SLI_RX_PHY_LAH_SEL_NEG);
	else
		clrbits_le32(kx + SLI_CTRL_I, SLI_RX_PHY_LAH_SEL_NEG);

	/* Find coarse delay */
	for (count = 0; count < 50; count++) {
		for (dc = SLIM_COARSE_D_BEGIN; dc < SLIM_COARSE_D_END; dc++) {
			sli_set_mbus_delay(kx, dc, dc, dc, dc, is_k_rx);

			/* Reset CPU-die TX and IO-die RX */
			sli_clear(data->die0.slim, SLI_RESET_TRIGGER);
			sli_clear(data->die1.slim, SLI_RESET_TRIGGER);

			/* Check result */
			sli_clear_interrupt_status(data->die1.slim);
			udelay(200);
			if (is_sli_suspend(data->die1.slim) > 0) {
				if (d_first_pass == -1)
					d_first_pass = dc;

				d_last_pass = dc;
				debug("%s SLIM DS detect win: {%d, %d}\n", die_name, d_first_pass, d_last_pass);
			} else if (d_last_pass != -1) {
				if ((d_last_pass - d_first_pass) > win_size) {
					win_size = d_last_pass - d_first_pass;
					sli_log_mbus_pad_delay(data, 0, d_first_pass, d_last_pass);
					debug("%s SLIM DS coarse win: {%d, %d}\n", die_name, d_first_pass, d_last_pass);
				}
				d_first_pass = -1;
				d_last_pass = -1;
			}
		}

		if (d_last_pass != -1 && (d_last_pass - d_first_pass) > win_size) {
			win_size = d_last_pass - d_first_pass;
			sli_log_mbus_pad_delay(data, 0, d_first_pass, d_last_pass);
			debug("%s SLIM DS coarse win: {%d, %d}\n", die_name, d_first_pass, d_last_pass);
		}

		sli_get_mbus_pad_delay(data, 0, &d_first_pass, &d_last_pass);
		if ((d_last_pass - d_first_pass) >= 3)
			break;
		debug("%s SLIM DS retry %d\n", die_name, count);
		d_first_pass = -1;
		d_last_pass = -1;
	}

	dc = (d_first_pass + d_last_pass) >> 1;
	if (dc == 0)
		dc = d_def;

	debug("%s SLIM DS coarse win: {%d, %d} -> select %d\n", die_name, d_first_pass, d_last_pass, dc);

	sli_set_mbus_delay(kx, dc, dc, dc, dc, is_k_rx);

	begin = max(dc - SLIM_FINE_MARGIN, 0);
	end = min(dc + SLIM_FINE_MARGIN, 31);

	if (win_size) {
		/* Fine-tune per-PAD delay */
		d0 = sli_calibrate_mbus_pad_delay(data, 0, begin, end, is_k_rx);
		sli_set_mbus_delay_single(kx, 0, d0, is_k_rx);

		d1 = sli_calibrate_mbus_pad_delay(data, 1, begin, end, is_k_rx);
		sli_set_mbus_delay_single(kx, 1, d1, is_k_rx);

		d2 = sli_calibrate_mbus_pad_delay(data, 2, begin, end, is_k_rx);
		sli_set_mbus_delay_single(kx, 2, d2, is_k_rx);

		d3 = sli_calibrate_mbus_pad_delay(data, 3, begin, end, is_k_rx);
		sli_set_mbus_delay_single(kx, 3, d3, is_k_rx);
	}

	/* Reset CPU-die TX and IO-die RX */
	sli_clear(data->die0.slim, SLI_RESET_TRIGGER);
	sli_clear(data->die1.slim, SLI_RESET_TRIGGER);

	/* Turn on the hardware training and wait suspend state */
	clrbits_le32(data->die1.slim + SLI_CTRL_I, SLI_AUTO_SEND_TRN_OFF);
	sli_wait_suspend(data->die1.slim);

	/* Enable the MARB RR mode for AST2700A0 */
	setbits_le32(data->die1.slim + SLIM_MARB_FUNC_I, SLIM_SLI_MARB_RR);
}

static void sli_set_video_rx_delay(uint32_t base, int d0, int d1, bool is_k_rx)
{
	uint32_t value;
	uint32_t mask = SLIV_PAD_DLY_RX1 | SLIV_PAD_DLY_RX0;
	u8 offset = (is_k_rx) ? 0 : 12;

	value = FIELD_PREP(SLIV_PAD_DLY_RX1, d1) | FIELD_PREP(SLIV_PAD_DLY_RX0, d0);
	clrsetbits_le32(base + SLI_CTRL_III,
			mask << offset,
			value << offset);
	readl((void *)base + SLI_CTRL_III);
	udelay(8);
}

static void sli_log_video_pad_delay(uintptr_t scu, int first, int last)
{
	clrsetbits_le32(scu, 0xffff0000, ((last & 0xff) << 24) | ((first & 0xff) << 16));
}

static void sli_get_video_pad_delay(uintptr_t scu, int *first, int *last)
{
	uint32_t value;

	value = readl((void *)scu);
	*first = (value >> 16) & 0xff;
	*last = (value >> 24) & 0xff;
}

static void sli_calibrate_video_delay(struct sli_data *data, bool is_DS, bool is_k_rx)
{
	int d;
	int d_first_pass = -1;
	int d_last_pass = -1;
	int d_def = 12;
	int win_size = 0;
	uintptr_t tx, rx, kx, scu;
	char *die_name = (is_DS ^ is_k_rx) ? "CPUD" : "IOD";
	char *dir_name = (is_DS) ? "DS" : "US";

	if (is_DS) {
		tx = data->die0.sliv;
		rx = data->die1.sliv;
		scu = (uintptr_t)&data->scu0->cpu_scratch[30];
	} else {
		tx = data->die1.sliv;
		rx = data->die0.sliv;
		scu = (uintptr_t)&data->scu1->scratch[30];
	}

	kx = (is_k_rx) ? rx : tx;

	setbits_le32(rx + SLI_CTRL_I, SLI_AUTO_SEND_TRN_OFF);
	setbits_le32(tx + SLI_CTRL_I, SLI_AUTO_SEND_TRN_OFF);

	if (data->flags & SLI_FLAG_RX_LAH_NEG_IO_SLIV)
		setbits_le32(rx + SLI_CTRL_I, SLI_RX_PHY_LAH_SEL_NEG);
	else
		clrbits_le32(rx + SLI_CTRL_I, SLI_RX_PHY_LAH_SEL_NEG);

	/* Set RX SLIV to receiver */
	clrsetbits_le32(rx + SLI_CTRL_I, SLI_TX_MODE, SLIV_RAW_MODE);

	/* Set TX SLIV to transmitter */
	setbits_le32(tx + SLI_CTRL_I, SLIV_RAW_MODE | SLI_TX_MODE);

	/* set max wait count */
	setbits_le32(tx + SLI_CTRL_II, SLIV_TX_ENT_SUSPEND);

	for (d = SLIV_COARSE_D_BEGIN; d < SLIV_COARSE_D_END; d++) {
		sli_set_video_rx_delay(kx, d, d, is_k_rx);

		/* reset SLIV */
		sli_clear(rx, SLI_CLEAR_BUS | SLI_RESET_TRIGGER);
		sli_clear(tx, SLI_CLEAR_BUS | SLI_RESET_TRIGGER);

		/* check interrupt status */
		sli_clear_interrupt_status(rx);
		udelay(200);
		if (is_sli_suspend(rx) > 0) {
			if (d_first_pass == -1)
				d_first_pass = d;

			d_last_pass = d;
		} else if (d_last_pass != -1) {
			if (d_last_pass - d_first_pass > win_size) {
				win_size = d_last_pass - d_first_pass;
				sli_log_video_pad_delay(scu, d_first_pass, d_last_pass);
				debug("%s SLIV %s coarse win: {%d, %d}\n", die_name, dir_name, d_first_pass, d_last_pass);
			}
			d_first_pass = -1;
			d_last_pass = -1;
		}
	}

	if (d_last_pass != -1 && (d_last_pass - d_first_pass) > win_size) {
		win_size = d_last_pass - d_first_pass;
		sli_log_video_pad_delay(scu, d_first_pass, d_last_pass);
		debug("%s SLIV %s coarse win: {%d, %d}\n", die_name, dir_name, d_first_pass, d_last_pass);
	}

	sli_get_video_pad_delay(scu, &d_first_pass, &d_last_pass);
	if (d_first_pass < 0 || (d_last_pass - d_first_pass) < 4)
		printf("%s SLIV %s margin not enough! {%d, %d}\n", die_name, dir_name, d_first_pass, d_last_pass);

	d = (d_first_pass + d_last_pass) >> 1;
	if (d == 0)
		d = d_def;
	debug("%s SLIV %s coarse win: {%d, %d} -> select %d\n", die_name, dir_name, d_first_pass, d_last_pass, d);

	sli_set_video_rx_delay(kx, d, d, is_k_rx);

	sli_clear(rx, SLI_CLEAR_BUS | SLI_RESET_TRIGGER);
	sli_clear(tx, SLI_CLEAR_BUS | SLI_RESET_TRIGGER);
	udelay(200);
	clrbits_le32(rx + SLI_CTRL_I, SLI_AUTO_SEND_TRN_OFF);
	clrbits_le32(tx + SLI_CTRL_I, SLI_AUTO_SEND_TRN_OFF);
	sli_wait_suspend(rx);
}

static void __maybe_unused sli_switch_video_dir(struct sli_data *data, bool is_DS)
{
	uintptr_t tx, rx, scu;

	if (is_DS) {
		tx = data->die0.sliv;
		rx = data->die1.sliv;
		scu = (uintptr_t)&data->scu0->cpu_scratch[30];
	} else {
		tx = data->die1.sliv;
		rx = data->die0.sliv;
		scu = (uintptr_t)&data->scu1->scratch[30];
	}

	setbits_le32(rx + SLI_CTRL_I, SLI_AUTO_SEND_TRN_OFF);
	setbits_le32(tx + SLI_CTRL_I, SLI_AUTO_SEND_TRN_OFF);

	/* Set RX SLIV to receiver */
	clrsetbits_le32(rx + SLI_CTRL_I, SLI_TX_MODE, SLIV_RAW_MODE);

	/* Set TX SLIV to transmitter */
	setbits_le32(tx + SLI_CTRL_I, SLIV_RAW_MODE | SLI_TX_MODE);

	sli_clear(rx, SLI_CLEAR_BUS | SLI_RESET_TRIGGER);
	sli_clear(tx, SLI_CLEAR_BUS | SLI_RESET_TRIGGER);
	udelay(200);
	clrbits_le32(rx + SLI_CTRL_I, SLI_AUTO_SEND_TRN_OFF);
	clrbits_le32(tx + SLI_CTRL_I, SLI_AUTO_SEND_TRN_OFF);
	sli_wait_suspend(rx);
}

#if SUPPORT_A0
/* To be deprecated */
static void sli_set_cpu_die_hpll(void)
{
	uint32_t value;

	/* Switch CPU-die HPLL to 1575M */
	value = readl((void *)ASPEED_CPU_HPLL);
	value &= ~(SCU_CPU_HPLL_P | SCU_CPU_HPLL_N | SCU_CPU_HPLL_M);
	value |= FIELD_PREP(SCU_CPU_HPLL_P, 0x0) |
		 FIELD_PREP(SCU_CPU_HPLL_N, 0x0) |
		 FIELD_PREP(SCU_CPU_HPLL_M, 0x7d);
	writel(value, (void *)ASPEED_CPU_HPLL);

	value = readl((void *)ASPEED_CPU_HPLL2);
	value &= ~SCU_CPU_HPLL2_BWADJ;
	value |= FIELD_PREP(SCU_CPU_HPLL2_BWADJ, 0x3e);
	writel(value, (void *)ASPEED_CPU_HPLL2);
	do {
		value = readl((void *)ASPEED_CPU_HPLL2);
	} while ((value & SCU_CPU_HPLL2_LOCK) == 0);
}
#endif

static int get_phandle_dev_regs(ofnode node, const char *propname, uint32_t *regs)
{
	ofnode prop_node;
	uint32_t phandle, value;
	int rc;

	rc = ofnode_read_u32(node, propname, &phandle);
	if (rc) {
		debug("cannot get %s phandle\n", propname);
		return -ENODEV;
	}

	prop_node = ofnode_get_by_phandle(phandle);
	if (!ofnode_valid(prop_node)) {
		debug("cannot get %s device node\n", propname);
		return -ENODEV;
	}

	value = (uint32_t)ofnode_get_addr(prop_node);
	if (value == (uint32_t)FDT_ADDR_T_NONE) {
		debug("cannot map %s registers\n", propname);
		return -ENODEV;
	}

	*regs = value;

	return 0;
}

/*
 * CPU die  --- downstream pads ---> I/O die
 * CPU die  <--- upstream pads ----- I/O die
 *
 * US/DS PAD[3:0] : SLIM[3:0]
 * US/DS PAD[5:4] : SLIH[1:0]
 * US/DS PAD[7:6] : SLIV[1:0]
 */
int ast2700_sli1_probe(struct udevice *dev)
{
	struct sli_data ast2700_sli_data[1];
	struct sli_data *data = ast2700_sli_data;
	ofnode node;
	uint32_t sli1_regs, sli0_regs, scu1_regs;
	uint32_t reg_val;
	int ret;

	__maybe_unused int phyclk_lookup[8] = {
		25, 800, 400, 200, 2000, 1000, 500, 250,
	};

	sli1_regs = (uint32_t)devfdt_get_addr_index(dev, 0);
	if (sli1_regs == (uint32_t)FDT_ADDR_T_NONE) {
		debug("cannot get SLI1 base\n");
		return -ENODEV;
	};

	node = dev_ofnode(dev);
	ret = get_phandle_dev_regs(node, "aspeed,sli0", &sli0_regs);
	if (ret < 0)
		return ret;
	ret = get_phandle_dev_regs(node, "aspeed,scu1", &scu1_regs);
	if (ret < 0)
		return ret;

	/* CPU die */
	data->die0.slim = sli0_regs + SLIM_REG_OFFSET;
	data->die0.slih = sli0_regs + SLIH_REG_OFFSET;
	data->die0.sliv = sli0_regs + SLIV_REG_OFFSET;
	data->die0.eng_clk_freq = SLI_CLK_500M;
	if (IS_ENABLED(CONFIG_SLI_TARGET_PHYCLK_1GHZ))
		data->die0.phy_clk_freq = SLI_PHYCLK_1G;
	else if (IS_ENABLED(CONFIG_SLI_TARGET_PHYCLK_800MHZ))
		data->die0.phy_clk_freq = SLI_PHYCLK_800M;
	else if (IS_ENABLED(CONFIG_SLI_TARGET_PHYCLK_500MHZ))
		data->die0.phy_clk_freq = SLI_PHYCLK_500M;
	else if (IS_ENABLED(CONFIG_SLI_TARGET_PHYCLK_400MHZ))
		data->die0.phy_clk_freq = SLI_PHYCLK_400M;
	else
		data->die0.phy_clk_freq = SLI_PHYCLK_25M;

	/* IO die */
	data->die1.slim = sli1_regs + SLIM_REG_OFFSET;
	data->die1.slih = sli1_regs + SLIH_REG_OFFSET;
	data->die1.sliv = sli1_regs + SLIV_REG_OFFSET;
	data->die1.eng_clk_freq = SLI_CLK_500M;
	data->die1.phy_clk_freq = SLI_PHYCLK_25M;
	data->flags = 0;

	data->scu1 = (struct ast2700_scu1 *)scu1_regs;
#if SUPPORT_A0
	reg_val = readl((void *)&data->scu1->chip_id1);
	if (FIELD_GET(SCU_CPU_REVISION_ID_HW, reg_val) == 0)
		data->flags |= SLI_FLAG_AST2700A0;

	if (data->flags & SLI_FLAG_AST2700A0) {
		/* Return if SLI had been calibrated */
		reg_val = readl((void *)data->die1.slih + SLI_CTRL_III);
		reg_val = FIELD_GET(SLI_CLK_SEL, reg_val);
		if (reg_val) {
			debug("SLI has been initialized\n");
			return 0;
		}

		/* AST2700A0 workaround for 25MHz */
		reg_val = SLI_RX_PHY_LAH_SEL_NEG | SLI_TRANS_EN | SLI_CLEAR_BUS;
		writel(reg_val, (void *)data->die1.slih + SLI_CTRL_I);
		writel(reg_val, (void *)data->die1.slim + SLI_CTRL_I);
		writel(reg_val | SLIV_RAW_MODE, (void *)data->die1.sliv + SLI_CTRL_I);
		sli_wait_suspend(data->die1.slih);
		sli_wait_suspend(data->die0.slih);
		debug("SLI US/DS @ 25MHz init done\n");

		/* AST2700A0 workaround to save SD waveform */
		sli_set_cpu_die_hpll();
		phyclk_lookup[5] = 788;

		if (data->die0.phy_clk_freq == SLI_PHYCLK_800M ||
		    data->die0.phy_clk_freq == SLI_PHYCLK_788M)
			data->flags |= SLI_FLAG_RX_LAH_NEG_IO_SLIM;
	} else {
#else
	{
#endif
		/* Return if SLI had been calibrated */
		reg_val = readl((void *)&data->scu1->scratch[31]);
		if (reg_val & SCU1_SCRATCH31_SLI_SKIP_CALI) {
			debug("SLI1 has been initialized\n");
			return 0;
		}
	}

	if (IS_ENABLED(CONFIG_SLI_TARGET_PHYCLK_25MHZ) ||
	    IS_ENABLED(CONFIG_ASPEED_FPGA))
		return 0;

	if (!(data->flags & SLI_FLAG_AST2700A0)) {
		/* Disable AHBC timeout before calibration */
		ast2700_set_ahbc_timeouts((uintptr_t)ASPEED_AHBC1_BASE, 0, 8);
		ast2700_set_ahbc_timeouts((uintptr_t)ASPEED_AHBC0_BASE, 0, 4);
	}

	/* Speed up engine clock before adjusting PHY TX clock and delay */
	reg_val = FIELD_PREP(SLI_CLK_SEL, data->die1.eng_clk_freq);
	clrsetbits_le32(data->die1.slih + SLI_CTRL_III, SLI_CLK_SEL, reg_val);
	reg_val = FIELD_PREP(SLI_CLK_SEL, data->die0.eng_clk_freq);
	clrsetbits_le32(data->die0.slih + SLI_CTRL_III, SLI_CLK_SEL, reg_val);

	/* Turn off auto-clear for AST2700A1 */
	if (!(data->flags & SLI_FLAG_AST2700A0)) {
		setbits_le32(data->die1.slih + SLI_CTRL_I,
			     SLI_AUTO_CLR_OFF_DAT | SLI_AUTO_CLR_OFF_CLK | SLI_NO_RST_TXCLK_CHG);
		setbits_le32(data->die0.slih + SLI_CTRL_I,
			     SLI_AUTO_CLR_OFF_DAT | SLI_AUTO_CLR_OFF_CLK | SLI_NO_RST_TXCLK_CHG);
	}

	/* Speed up CPU die PHY TX clock and clear TX PAD delay */
	reg_val = FIELD_PREP(SLI_PHYCLK_SEL, data->die0.phy_clk_freq);
	clrsetbits_le32(data->die0.slih + SLI_CTRL_III,
			SLI_PHYCLK_SEL | SLIH_PAD_DLY_TX1 | SLIH_PAD_DLY_TX0, reg_val);

	/* Calibrate SLIH DS delay */
	sli_calibrate_ahb_delay(data);
	if (IS_ENABLED(CONFIG_SLI_K_ON_CPU)) {
		writel(0, (void *)data->die1.slim + SLI_CTRL_III);
		sli_calibrate_mbus_delay(data, false);
	} else {
		sli_calibrate_mbus_delay(data, true);
	}

	debug("SLI DS @ %dMHz init done\n", phyclk_lookup[data->die0.phy_clk_freq]);

	/* Clear remote SLI controller */
	sli_clear(data->die0.slih, SLI_CLEAR_BUS);
	sli_wait_suspend(data->die0.slih);

	return 0;
}

static void _mac_hotfix(struct sli_data *data)
{
	u32 val = readl((void *)data->die1.slim + 0xb8) & 0xe00;

	if (!val)
		return;

	writel(val, (void *)data->die1.slim + 0x68);
	setbits_le32(data->die1.slim + 0x60, BIT(5));
}

int ast2700_sli0_probe(struct udevice *dev)
{
	struct sli_data ast2700_sli_data[1];
	struct sli_data *data = ast2700_sli_data;
	struct ast2700_scu0 *scu0;
	struct ast2700_scu1 *scu1;
	ofnode node;
	uint32_t scu0_regs, scu1_regs, sli1_regs, sli0_regs;
	uint32_t reg_val;
	int ret, retry = 100;
	bool sli0_ready = false;

	sli0_regs = (uint32_t)devfdt_get_addr_index(dev, 0);
	if (sli0_regs == (uint32_t)FDT_ADDR_T_NONE) {
		debug("cannot get SLI0 base\n");
		return -ENODEV;
	};

	node = dev_ofnode(dev);
	ret = get_phandle_dev_regs(node, "aspeed,scu1", &scu1_regs);
	if (ret < 0)
		return ret;

	ret = get_phandle_dev_regs(node, "aspeed,scu0", &scu0_regs);
	if (ret < 0)
		return ret;

	ret = get_phandle_dev_regs(node, "aspeed,sli1", &sli1_regs);
	if (ret < 0)
		return ret;

	scu0 = (struct ast2700_scu0 *)scu0_regs;
	scu1 = (struct ast2700_scu1 *)scu1_regs;

	data->die0.slim = sli0_regs + SLIM_REG_OFFSET;
	data->die0.sliv = sli0_regs + SLIV_REG_OFFSET;
	data->die1.slim = sli1_regs + SLIM_REG_OFFSET;
	data->die1.sliv = sli1_regs + SLIV_REG_OFFSET;
	data->flags = 0;

	data->scu0 = scu0;
	data->scu1 = scu1;

	/*
	 * On AST2700A0, SLI0 RX calibration is handled by ATF. SPL does
	 * not need to wait for its completion.
	 */
	reg_val = readl((void *)&scu1->chip_id1);
	if (FIELD_GET(SCU_CPU_REVISION_ID_HW, reg_val) == 0)
		return 0;

	if (IS_ENABLED(CONFIG_SLI_TARGET_PHYCLK_25MHZ) ||
	    IS_ENABLED(CONFIG_ASPEED_FPGA)) {
		debug("AST2700 SLI0 ready, 25MHz\n");
		return 0;
	}

	reg_val = readl((void *)&scu1->scratch[31]);
	if (reg_val & SCU1_SCRATCH31_SLI_SKIP_CALI) {
		_mac_hotfix(data);
		printf("SLI0 has been initialized\n");
		return 0;
	}

	while (--retry > 0) {
		reg_val = readl((void *)&scu1->scratch[31]);
		if (reg_val & SCU1_SCRATCH31_SLI0_READY) {
			sli0_ready = true;
			break;
		}

		mdelay(100);
	}

	if (sli0_ready) {
		sli_clear(sli1_regs + SLIH_REG_OFFSET,
			  SLI_CLEAR_RX | SLI_CLEAR_BUS);
		ast2700_set_ahbc_timeouts((uintptr_t)ASPEED_AHBC1_BASE,
					  AHBC_MAX_TIMEOUT, 8);
		udelay(200);
		setbits_le32((void *)&scu0->cpu_scratch[31],
			     SCU0_SCRATCH31_SLI1_READY);
		udelay(100);
		ast2700_set_ahbc_timeouts((uintptr_t)ASPEED_AHBC0_BASE,
					  AHBC_MAX_TIMEOUT, 4);
		debug("SLI0 calibration completed\n");

		setbits_le32((void *)&scu1->scratch[31],
			     SCU1_SCRATCH31_SLI_SKIP_CALI);

		/* Reset SLIM MARB before using the SLIM */
		setbits_le32(sli1_regs + SLIM_REG_OFFSET + SLIM_MARB_FUNC_I, SLIM_SLI_MARB_CLR);

		/* Clear the INTC reset interrupt status. */
		reg_val = readl((void *)ASPEED_IO_INTC_BASE + 0x14);
		writel(reg_val, (void *)ASPEED_IO_INTC_BASE + 0x14);

		sli_calibrate_video_delay(data, false, true);
		if (IS_ENABLED(CONFIG_SLI_K_ON_CPU)) {
			writel(0, (void *)data->die1.sliv + SLI_CTRL_III);
			sli_calibrate_video_delay(data, true, false);
		} else {
			sli_calibrate_video_delay(data, true, true);
		}

		return 0;
	}

	debug("Timeout to wait SLI0 calibration\n");
	return -1;
}

static const struct udevice_id aspeed_sli0_match[] = {
	{ .compatible = "aspeed,ast2700-sli0" },
	{ /* sentinel */ }
};

static const struct udevice_id aspeed_sli1_match[] = {
	{ .compatible = "aspeed,ast2700-sli1" },
	{ /* sentinel */ }
};

U_BOOT_DRIVER(aspeed_sli0_driver) = {
	.name = "ast2700-sli0",
	.id = UCLASS_MISC,
	.of_match = aspeed_sli0_match,
	.probe = ast2700_sli0_probe,
};

U_BOOT_DRIVER(aspeed_sli1_driver) = {
	.name = "ast2700-sli1",
	.id = UCLASS_MISC,
	.of_match = aspeed_sli1_match,
	.probe = ast2700_sli1_probe,
};
