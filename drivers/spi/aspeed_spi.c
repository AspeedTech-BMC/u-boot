// SPDX-License-Identifier: GPL-2.0+
/*
 * ASPEED AST2500 FMC/SPI Controller driver
 *
 * Copyright (c) 2015-2018, IBM Corporation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <common.h>
#include <clk.h>
#include <dm.h>
#include <spi.h>
#include <spi_flash.h>
#include <asm/io.h>
#include <linux/ioport.h>
#include <malloc.h>

#define ASPEED_SPI_MAX_CS		3
#define FLASH_CALIBRATION_LEN   0x400

struct aspeed_spi_regs {
	u32 conf;			/* 0x00 CE Type Setting */
	u32 ctrl;			/* 0x04 Control */
	u32 intr_ctrl;			/* 0x08 Interrupt Control and Status */
	u32 cmd_ctrl;			/* 0x0c Command Control */
	u32 ce_ctrl[ASPEED_SPI_MAX_CS];	/* 0x10 .. 0x18 CEx Control */
	u32 _reserved0[5];		/* .. */
	u32 segment_addr[ASPEED_SPI_MAX_CS];
					/* 0x30 .. 0x38 Segment Address */
	u32 _reserved1[5];		/* .. */
	u32 soft_rst_cmd_ctrl;	/* 0x50 Auto Soft-Reset Command Control */
	u32 _reserved2[11];		/* .. */
	u32 dma_ctrl;			/* 0x80 DMA Control/Status */
	u32 dma_flash_addr;		/* 0x84 DMA Flash Side Address */
	u32 dma_dram_addr;		/* 0x88 DMA DRAM Side Address */
	u32 dma_len;			/* 0x8c DMA Length Register */
	u32 dma_checksum;		/* 0x90 Checksum Calculation Result */
	u32 timings;			/* 0x94 Read Timing Compensation */
	u32 _reserved3[1];
	/* not used */
	u32 soft_strap_status;		/* 0x9c Software Strap Status */
	u32 write_cmd_filter_ctrl;	/* 0xa0 Write Command Filter Control */
	u32 write_addr_filter_ctrl;	/* 0xa4 Write Address Filter Control */
	u32 lock_ctrl_reset;		/* 0xa8 Lock Control (SRST#) */
	u32 lock_ctrl_wdt;		/* 0xac Lock Control (Watchdog) */
	u32 write_addr_filter[8];	/* 0xb0 Write Address Filter */
	u32 _reserved4[12];
	u32 fully_qualified_cmd[20];	/* 0x100 Fully Qualified Command */
	u32 addr_qualified_cmd[12];	/* 0x150 Address Qualified Command */
};

/* CE Type Setting Register */
#define CONF_ENABLE_W2			BIT(18)
#define CONF_ENABLE_W1			BIT(17)
#define CONF_ENABLE_W0			BIT(16)
#define CONF_FLASH_TYPE2		4
#define CONF_FLASH_TYPE1		2	/* Hardwired to SPI */
#define CONF_FLASH_TYPE0		0	/* Hardwired to SPI */
#define	  CONF_FLASH_TYPE_NOR		0x0
#define	  CONF_FLASH_TYPE_SPI		0x2

/* CE Control Register */
#define CTRL_EXTENDED2			BIT(2)	/* 32 bit addressing for SPI */
#define CTRL_EXTENDED1			BIT(1)	/* 32 bit addressing for SPI */
#define CTRL_EXTENDED0			BIT(0)	/* 32 bit addressing for SPI */

/* Interrupt Control and Status Register */
#define INTR_CTRL_DMA_STATUS		BIT(11)
#define INTR_CTRL_CMD_ABORT_STATUS	BIT(10)
#define INTR_CTRL_WRITE_PROTECT_STATUS	BIT(9)
#define INTR_CTRL_DMA_EN		BIT(3)
#define INTR_CTRL_CMD_ABORT_EN		BIT(2)
#define INTR_CTRL_WRITE_PROTECT_EN	BIT(1)

/* CEx Control Register */
#define CE_CTRL_IO_MODE_MASK		GENMASK(31, 28)
#define CE_CTRL_IO_QPI_DATA			BIT(31)
#define CE_CTRL_IO_DUAL_DATA		BIT(29)
#define CE_CTRL_IO_SINGLE			0
#define CE_CTRL_IO_DUAL_ADDR_DATA	(BIT(29) | BIT(28))
#define CE_CTRL_IO_QUAD_DATA		BIT(30)
#define CE_CTRL_IO_QUAD_ADDR_DATA	(BIT(30) | BIT(28))
#define CE_CTRL_CMD_SHIFT		16
#define CE_CTRL_CMD_MASK		0xff
#define CE_CTRL_CMD(cmd)					\
	(((cmd) & CE_CTRL_CMD_MASK) << CE_CTRL_CMD_SHIFT)
#define CE_CTRL_DUMMY_HIGH_SHIFT	14
#define CE_CTRL_DUMMY_HIGH_MASK		0x1
#define CE_CTRL_CLOCK_FREQ_SHIFT	8
#define CE_CTRL_CLOCK_FREQ_MASK		0xf
#define CE_CTRL_CLOCK_FREQ(div)						\
	(((div) & CE_CTRL_CLOCK_FREQ_MASK) << CE_CTRL_CLOCK_FREQ_SHIFT)
#define CE_G6_CTRL_CLOCK_FREQ(div)						\
	((((div) & CE_CTRL_CLOCK_FREQ_MASK) << CE_CTRL_CLOCK_FREQ_SHIFT) | (((div) & 0xf0) << 20))
#define CE_CTRL_DUMMY_LOW_SHIFT		6 /* 2 bits [7:6] */
#define CE_CTRL_DUMMY_LOW_MASK		0x3
#define CE_CTRL_DUMMY(dummy)						\
	(((((dummy) >> 2) & CE_CTRL_DUMMY_HIGH_MASK)			\
	  << CE_CTRL_DUMMY_HIGH_SHIFT) |				\
	 (((dummy) & CE_CTRL_DUMMY_LOW_MASK) << CE_CTRL_DUMMY_LOW_SHIFT))
#define CE_CTRL_STOP_ACTIVE		BIT(2)
#define CE_CTRL_MODE_MASK		0x3
#define	CE_CTRL_READMODE		0x0
#define	CE_CTRL_FREADMODE		0x1
#define	CE_CTRL_WRITEMODE		0x2
#define	CE_CTRL_USERMODE		0x3
#define CE_CTRL_FREQ_MASK		0xf0fff0ff

#define SPI_READ_FROM_FLASH		0x00000001
#define SPI_WRITE_TO_FLASH		0x00000002

/* Auto Soft-Reset Command Control */
#define SOFT_RST_CMD_EN     GENMASK(1, 0)

/*
 * The Segment Register uses a 8MB unit to encode the start address
 * and the end address of the AHB window of a SPI flash device.
 * Default segment addresses are :
 *
 *   CE0  0x20000000 - 0x2fffffff  128MB
 *   CE1  0x28000000 - 0x29ffffff   32MB
 *   CE2  0x2a000000 - 0x2bffffff   32MB
 *
 * The full address space of the AHB window of the controller is
 * covered and CE0 start address and CE2 end addresses are read-only.
 */
#define SEGMENT_ADDR_START(reg)		((((reg) >> 16) & 0xff) << 23)
#define SEGMENT_ADDR_END(reg)		((((reg) >> 24) & 0xff) << 23)
#define SEGMENT_ADDR_VALUE(start, end)					\
	(((((start) >> 23) & 0xff) << 16) | ((((end) >> 23) & 0xff) << 24))

#define G6_SEGMENT_ADDR_START(reg)		(((reg) << 16) & 0x0ff00000)
#define G6_SEGMENT_ADDR_END(reg)		(((reg) & 0x0ff00000) + 0x100000)
#define G6_SEGMENT_ADDR_VALUE(start, end)					\
	((((start) & 0x0ff00000) >> 16) | (((end) - 0x100000) & 0xffff0000))

/* DMA Control/Status Register */
#define DMA_CTRL_DELAY_SHIFT		8
#define DMA_CTRL_DELAY_MASK		0xf
#define G6_DMA_CTRL_DELAY_MASK		0xff
#define DMA_CTRL_FREQ_SHIFT		4
#define G6_DMA_CTRL_FREQ_SHIFT		16

#define DMA_CTRL_FREQ_MASK		0xf
#define TIMING_MASK(div, delay)					   \
	(((delay & DMA_CTRL_DELAY_MASK) << DMA_CTRL_DELAY_SHIFT) | \
	 ((div & DMA_CTRL_FREQ_MASK) << DMA_CTRL_FREQ_SHIFT))
#define G6_TIMING_MASK(div, delay)					   \
	(((delay & G6_DMA_CTRL_DELAY_MASK) << DMA_CTRL_DELAY_SHIFT) | \
	 ((div & DMA_CTRL_FREQ_MASK) << G6_DMA_CTRL_FREQ_SHIFT))
#define DAM_CTRL_REQUEST		BIT(31)
#define DAM_CTRL_GRANT			BIT(30)
#define DMA_CTRL_CALIB			BIT(3)
#define DMA_CTRL_CKSUM			BIT(2)
#define DMA_CTRL_WRITE			BIT(1)
#define DMA_CTRL_ENABLE			BIT(0)

#define DMA_GET_REQ_MAGIC		0xaeed0000
#define DMA_DISCARD_REQ_MAGIC	0xdeea0000

/* for ast2600 setting */
#define SPI_3B_AUTO_CLR_REG   0x1e6e2510
#define SPI_3B_AUTO_CLR       BIT(9)


/*
 * flash related info
 */
struct aspeed_spi_flash {
	u8 cs;
	/* Initialized when the SPI bus is
	 * first claimed
	 */
	bool init;
	void __iomem *ahb_base; /* AHB Window for this device */
	u32 ahb_size; /* AHB Window segment size */
	u32 ce_ctrl_user; /* CE Control Register for USER mode */
	u32 ce_ctrl_fread; /* CE Control Register for FREAD mode */
	u32 read_iomode;
	u32 write_iomode;
	u32 max_freq;
	struct spi_flash *spi; /* Associated SPI Flash device */
};

enum aspeed_spi_dir {
	ASPEED_SPI_DIR_IN,
	ASPEED_SPI_DIR_OUT,
};

#define ASPEED_SPI_OP_CMD(__opcode)				\
	{							\
		.opcode = __opcode,				\
	}

#define ASPEED_SPI_OP_ADDR(__nbytes, __val)			\
	{							\
		.nbytes = __nbytes,				\
		.val = __val,					\
	}

#define ASPEED_SPI_OP_NO_ADDR	{ }

#define ASPEED_SPI_OP_DUMMY(__nbytes)				\
	{							\
		.nbytes = __nbytes,				\
	}

#define ASPEED_SPI_OP_NO_DUMMY	{ }

#define ASPEED_SPI_OP_DATA_IN(__nbytes, __buf)			\
	{							\
		.dir = ASPEED_SPI_DIR_IN,				\
		.nbytes = __nbytes,				\
		.buf.in = __buf,				\
	}

#define ASPEED_SPI_OP_DATA_OUT(__nbytes, __buf)			\
	{							\
		.dir = ASPEED_SPI_DIR_OUT,				\
		.nbytes = __nbytes,				\
		.buf.out = __buf,				\
	}

#define ASPEED_SPI_OP_NO_DATA	{ }

#define ASPEED_SPI_OP(__io_mode, __cmd, __addr, __dummy, __data)	\
	{							\
		.io_mode = __io_mode,				\
		.cmd = __cmd,					\
		.addr = __addr,					\
		.dummy = __dummy,				\
		.data = __data,					\
	}

struct aspeed_spi_op {
	u32 io_mode;

	struct {
		u16 opcode;
	} cmd;

	struct {
		u8 nbytes;
		u32 val;
	} addr;

	struct {
		u8 nbytes;
	} dummy;

	struct {
		enum aspeed_spi_dir dir;
		unsigned int nbytes;
		union {
			void *in;
			const void *out;
		} buf;
	} data;
};

struct aspeed_spi_priv {
	struct aspeed_spi_regs *regs;
	void __iomem *ahb_base; /* AHB Window for all flash devices */
	int new_ver;
	u32 ahb_size; /* AHB Window segments size */
	ulong hclk_rate; /* AHB clock rate */
	u8 num_cs;
	bool is_fmc;

	struct aspeed_spi_flash flashes[ASPEED_SPI_MAX_CS];
	u32 flash_count;

	u8 cmd_buf[16]; /* SPI command in progress */
	size_t cmd_len;
	u8 *tmp_buf;
	int (*spi_exec_op_cmd)(struct aspeed_spi_priv *priv,
			       struct aspeed_spi_flash *flash,
			       struct aspeed_spi_op *op);
	bool pure_spi_mode_only;
};

static u32 aspeed_spi_flash_to_addr(struct aspeed_spi_flash *flash,
				    const u8 *cmdbuf, unsigned int cmdlen);

static struct aspeed_spi_flash *aspeed_spi_get_flash(struct udevice *dev)
{
	struct dm_spi_slave_platdata *slave_plat = dev_get_parent_platdata(dev);
	struct aspeed_spi_priv *priv = dev_get_priv(dev->parent);
	u8 cs = slave_plat->cs;

	if (cs >= priv->flash_count) {
		pr_err("invalid CS %u\n", cs);
		return NULL;
	}

	return &priv->flashes[cs];
}

static u32 aspeed_g6_spi_hclk_divisor(struct aspeed_spi_priv *priv, u32 max_hz)
{
	u32 hclk_rate = priv->hclk_rate;
	/* HCLK/1 ..	HCLK/16 */
	const u8 hclk_masks[] = {
		15, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 0
	};
	u8 hclk_div = 0x4; /* default value */
	bool found = false;
	u32 i, j = 0;

	/* FMC/SPIR10[27:24] */
	for (j = 0; j < 0xf; j++) {
		for (i = 0; i < ARRAY_SIZE(hclk_masks); i++) {
			if (i == 0 && j == 0)
				continue;

			if ((hclk_rate / ((i + 1) + j * 16)) <= max_hz) {
				found = 1;
				break;
			}
		}

		if (found)
			break;
	}

	debug("hclk=%d required=%d h_div %d, divisor is %d (mask %x) speed=%d\n",
		  hclk_rate, max_hz, j, i + 1, hclk_masks[i], hclk_rate / (i + 1 + j * 16));

	hclk_div = ((j << 4) | hclk_masks[i]);

	return hclk_div;
}

static u32 aspeed_spi_hclk_divisor(struct aspeed_spi_priv *priv, u32 max_hz)
{
	u32 hclk_rate = priv->hclk_rate;
	/* HCLK/1 ..	HCLK/16 */
	const u8 hclk_masks[] = {
		15, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 0
	};
	u32 i;
	u32 hclk_div_setting = 0;

	for (i = 0; i < ARRAY_SIZE(hclk_masks); i++) {
		if (max_hz >= (hclk_rate / (i + 1)))
			break;
	}
	debug("hclk=%d required=%d divisor is %d (mask %x) speed=%d\n",
	      hclk_rate, max_hz, i + 1, hclk_masks[i], hclk_rate / (i + 1));

	hclk_div_setting = hclk_masks[i];

	return hclk_div_setting;
}

/*
 * Use some address/size under the first flash device CE0
 */
static u32 aspeed_spi_fmc_checksum(struct aspeed_spi_priv *priv,
				   struct aspeed_spi_flash *flash,
				   u8 div, u8 delay)
{
	u32 flash_addr = (u32)flash->ahb_base + 0x10000;
	u32 dma_ctrl;
	u32 checksum;

	writel(flash_addr, &priv->regs->dma_flash_addr);
	writel(FLASH_CALIBRATION_LEN,  &priv->regs->dma_len);

	/*
	 * When doing calibration, the SPI clock rate in the CE0
	 * Control Register and the data input delay cycles in the
	 * Read Timing Compensation Register are replaced by bit[11:4].
	 */
	dma_ctrl = DMA_CTRL_ENABLE | DMA_CTRL_CKSUM | DMA_CTRL_CALIB |
		TIMING_MASK(div, delay);

	writel(dma_ctrl, &priv->regs->dma_ctrl);
	while (!(readl(&priv->regs->intr_ctrl) & INTR_CTRL_DMA_STATUS))
		;

	writel(0x0, &priv->regs->intr_ctrl);

	checksum = readl(&priv->regs->dma_checksum);

	writel(0x0, &priv->regs->dma_ctrl);
	return checksum;
}

/*
 * Use some address/size under the first flash device CE0
 */
static u32 aspeed_g6_spi_fmc_checksum(struct aspeed_spi_priv *priv,
				      struct aspeed_spi_flash *flash,
				      u8 div, u8 delay)
{
	u32 flash_addr = (u32)flash->ahb_base;
	u32 dma_ctrl;
	u32 checksum;

	writel(DMA_GET_REQ_MAGIC, &priv->regs->dma_ctrl);
	if (readl(&priv->regs->dma_ctrl) & DAM_CTRL_REQUEST) {
		while (!(readl(&priv->regs->dma_ctrl) & DAM_CTRL_GRANT))
			;
	}

	writel(flash_addr, &priv->regs->dma_flash_addr);
	writel(FLASH_CALIBRATION_LEN,  &priv->regs->dma_len);

	/*
	 * When doing calibration, the SPI clock rate in the control
	 * register and the data input delay cycles in the
	 * read timing compensation register are replaced by bit[11:4].
	 */
	dma_ctrl = DMA_CTRL_ENABLE | DMA_CTRL_CKSUM | DMA_CTRL_CALIB |
		G6_TIMING_MASK(div, delay);

	writel(dma_ctrl, &priv->regs->dma_ctrl);
	while (!(readl(&priv->regs->intr_ctrl) & INTR_CTRL_DMA_STATUS))
		;

	checksum = readl(&priv->regs->dma_checksum);

	writel(0x0, &priv->regs->intr_ctrl);
	writel(0x0, &priv->regs->dma_ctrl);
	writel(DMA_DISCARD_REQ_MAGIC, &priv->regs->dma_ctrl);

	return checksum;
}

static u32 aspeed_spi_read_checksum(struct aspeed_spi_priv *priv,
				    struct aspeed_spi_flash *flash,
				    u8 div, u8 delay)
{
	if (priv->new_ver)
		return aspeed_g6_spi_fmc_checksum(priv, flash, div, delay);

	/* for AST2500, */
	if (!priv->is_fmc) {
		pr_warn("No timing calibration support for SPI controllers");
		return 0xbadc0de;
	}

	return aspeed_spi_fmc_checksum(priv, flash, div, delay);
}

#define TIMING_DELAY_DI_4NS         BIT(3)
#define TIMING_DELAY_HCYCLE_MAX     5

/*
 * Check whether the data is not all 0 or 1 in order to
 * avoid calibriate umount spi-flash.
 */
static bool aspeed_spi_calibriation_enable(const u8 *buf, u32 sz)
{
	const u32 *buf_32 = (const u32 *)buf;
	u32 i;
	u32 valid_count = 0;

	for (i = 0; i < (sz / 4); i++) {
		if (buf_32[i] != 0 && buf_32[i] != 0xffffffff)
			valid_count++;
		if (valid_count > 100)
			return true;
	}

	return false;
}

static int get_mid_point_of_longest_one(u8 *buf, u32 len)
{
	int i;
	int start = 0, mid_point = 0;
	int max_cnt = 0, cnt = 0;

	for (i = 0; i < len; i++) {
		if (buf[i] == 1) {
			cnt++;
		} else {
			cnt = 0;
			start = i;
		}

		if (max_cnt < cnt) {
			max_cnt = cnt;
			mid_point = start + (cnt / 2);
		}
	}

	/*
	 * In order to get a stable SPI read timing,
	 * abandon the result if the length of longest
	 * consecutive good points is too short.
	 */
	if (max_cnt < 4)
		return -1;

	return mid_point;
}

static int aspeed_spi_timing_calibration(struct aspeed_spi_priv *priv,
					 struct aspeed_spi_flash *flash)
{
	u32 cs = flash->cs;
	/* HCLK/5 .. HCLK/1 */
	const u8 hclk_masks[] = {13, 6, 14, 7, 15};
	u32 timing_reg;
	u32 checksum, gold_checksum;
	int i;
	u32 hcycle, delay_ns;
	u32 final_delay = 0;
	u32 hclk_div = 0;
	u32 max_freq = flash->max_freq;
	u32 reg_val;
	u8 *tmp_buf = NULL;
	u8 *calib_res = NULL;
	int calib_point;
	bool pass;

	if (priv->new_ver) {
		timing_reg = readl(&priv->regs->timings + cs);
		if (timing_reg != 0)
			return 0;

		/*
		 * use the related low frequency to get check calibration data
		 * and get golden data.
		 */
		reg_val = flash->ce_ctrl_fread & CE_CTRL_FREQ_MASK;
		writel(reg_val, &priv->regs->ce_ctrl[cs]);
		tmp_buf = (u8 *)malloc(FLASH_CALIBRATION_LEN);
		if (!tmp_buf)
			return -ENOMEM;

		memcpy_fromio(tmp_buf, flash->ahb_base, FLASH_CALIBRATION_LEN);
		if (!aspeed_spi_calibriation_enable(tmp_buf, FLASH_CALIBRATION_LEN)) {
			debug("flash data is monotonous, skip calibration.\n");
			goto no_calib;
		}

		/* Compute reference checksum at lowest freq HCLK/16 */
		gold_checksum = aspeed_spi_read_checksum(priv, flash, 0, 0);

		/*
		 * allocate a space to record calibration result for
		 * different timing compensation with fixed
		 * HCLK division.
		 */
		calib_res = (u8 *)malloc(6 * 17);
		if (!calib_res) {
			free(tmp_buf);
			return -ENOMEM;
		}

		/* from HCLK/2 to HCLK/5 */
		for (i = 0; i < ARRAY_SIZE(hclk_masks) - 1; i++) {
			if (priv->hclk_rate / (i + 2) > max_freq) {
				debug("skipping freq %ld\n", priv->hclk_rate / (i + 2));
				continue;
			}

			max_freq = (u32)priv->hclk_rate / (i + 2);

			memset(calib_res, 0x0, 6 * 17);
			for (hcycle = 0; hcycle <= 5; hcycle++) {
				/* increase DI delay by the step of 0.5ns */
				debug("Delay Enable : hcycle %x\n", hcycle);
				for (delay_ns = 0; delay_ns <= 0xf; delay_ns++) {
					checksum = aspeed_g6_spi_fmc_checksum(priv, flash,
									      hclk_masks[3 - i],
						TIMING_DELAY_DI_4NS | hcycle | (delay_ns << 4));
					pass = (checksum == gold_checksum);
					calib_res[hcycle * 17 + delay_ns] = pass;
					debug("HCLK/%d, %d HCLK cycle, %d delay_ns : %s\n",
					      i + 2, hcycle, delay_ns, pass ? "PASS" : "FAIL");
				}
			}

			calib_point = get_mid_point_of_longest_one(calib_res, 6 * 17);
			if (calib_point < 0) {
				debug("cannot get good calibration point.\n");
				continue;
			}

			hcycle = calib_point / 17;
			delay_ns = calib_point % 17;
			debug("final hcycle: %d, delay_ns: %d\n", hcycle, delay_ns);

			final_delay = (TIMING_DELAY_DI_4NS | hcycle | (delay_ns << 4)) << (i * 8);
			writel(final_delay, &priv->regs->timings + cs);
			break;
		}

no_calib:
		hclk_div = aspeed_g6_spi_hclk_divisor(priv, max_freq);
		/* configure SPI clock frequency */
		reg_val = readl(&priv->regs->ce_ctrl[cs]);
		reg_val = (reg_val & CE_CTRL_FREQ_MASK) | CE_G6_CTRL_CLOCK_FREQ(hclk_div);
		writel(reg_val, &priv->regs->ce_ctrl[cs]);

		/* add clock setting info for CE ctrl setting */
		flash->ce_ctrl_user =
			(flash->ce_ctrl_user & CE_CTRL_FREQ_MASK) | CE_G6_CTRL_CLOCK_FREQ(hclk_div);
		flash->ce_ctrl_fread =
			(flash->ce_ctrl_fread & CE_CTRL_FREQ_MASK) | CE_G6_CTRL_CLOCK_FREQ(hclk_div);

		debug("cs: %d, freq: %dMHz\n", cs, max_freq / 1000000);

		if (tmp_buf)
			free(tmp_buf);
		if (calib_res)
			free(calib_res);
	} else {
		/* Use the ctrl setting in aspeed_spi_flash_init() to
		 * implement calibration process.
		 */
		timing_reg = readl(&priv->regs->timings);
		if (timing_reg != 0)
			return 0;

		/* Compute reference checksum at lowest freq HCLK/16 */
		gold_checksum = aspeed_spi_read_checksum(priv, flash, 0, 0);

		for (i = 0; i < ARRAY_SIZE(hclk_masks); i++) {
			u32 hdiv = 5 - i;
			u32 hshift = (hdiv - 1) << 2;
			bool pass = false;
			u8 delay;

			if (priv->hclk_rate / hdiv > flash->max_freq) {
				debug("skipping freq %ld\n", priv->hclk_rate / hdiv);
				continue;
			}

			/* Increase HCLK cycles until read succeeds */
			for (hcycle = 0; hcycle <= TIMING_DELAY_HCYCLE_MAX; hcycle++) {
				/* Try first with a 4ns DI delay */
				delay = TIMING_DELAY_DI_4NS | hcycle;
				checksum = aspeed_spi_read_checksum(priv, flash, hclk_masks[i],
								    delay);
				pass = (checksum == gold_checksum);
				debug(" HCLK/%d, 4ns DI delay, %d HCLK cycle : %s\n",
				      hdiv, hcycle, pass ? "PASS" : "FAIL");

				/* Try again with more HCLK cycles */
				if (!pass)
					continue;

				/* Try without the 4ns DI delay */
				delay = hcycle;
				checksum = aspeed_spi_read_checksum(priv, flash, hclk_masks[i],
								    delay);
				pass = (checksum == gold_checksum);
				debug(" HCLK/%d,  no DI delay, %d HCLK cycle : %s\n",
				      hdiv, hcycle, pass ? "PASS" : "FAIL");

				/* All good for this freq  */
				if (pass)
					break;
			}

			if (pass) {
				timing_reg &= ~(0xfu << hshift);
				timing_reg |= delay << hshift;
			}
		}

		debug("Read Timing Compensation set to 0x%08x\n", timing_reg);
		writel(timing_reg, &priv->regs->timings);
	}

	return 0;
}

static int aspeed_spi_controller_init(struct aspeed_spi_priv *priv)
{
	int cs;

	/*
	 * Enable write on all flash devices as USER command mode
	 * requires it.
	 */
	setbits_le32(&priv->regs->conf,
		     CONF_ENABLE_W2 | CONF_ENABLE_W1 | CONF_ENABLE_W0);

	/*
	 * Set safe default settings for each device. These will be
	 * tuned after the SPI flash devices are probed.
	 */
	if (priv->new_ver) {
		for (cs = 0; cs < priv->flash_count; cs++) {
			struct aspeed_spi_flash *flash = &priv->flashes[cs];
			u32 addr_config = 0;
			switch(cs) {
			case 0:
				flash->ahb_base = priv->ahb_base;
				debug("cs0 mem-map : %x\n", (u32)flash->ahb_base);
				break;
			case 1:
				flash->ahb_base = priv->flashes[0].ahb_base + 0x4000000; /* cs0 + 64MB */
				debug("cs1 mem-map : %x end %x\n",
				      (u32)flash->ahb_base, (u32)flash->ahb_base + 0x4000000);
				break;
			case 2:
				flash->ahb_base = priv->flashes[0].ahb_base + 0x4000000 * 2; /* cs0 + 128MB : use 64MB */
				debug("cs2 mem-map : %x end %x\n",
				      (u32)flash->ahb_base, (u32)flash->ahb_base + 0x4000000);
				break;
			}
			addr_config =
				G6_SEGMENT_ADDR_VALUE((u32)flash->ahb_base, (u32)flash->ahb_base + 0x4000000);
			writel(addr_config, &priv->regs->segment_addr[cs]);
			flash->cs = cs;
			flash->ce_ctrl_user = CE_CTRL_USERMODE;
			flash->ce_ctrl_fread = CE_CTRL_READMODE;
		}
	} else {
		for (cs = 0; cs < priv->flash_count; cs++) {
			struct aspeed_spi_flash *flash = &priv->flashes[cs];
			u32 seg_addr = readl(&priv->regs->segment_addr[cs]);
			/*
			 * The start address of the AHB window of CE0 is
			 * read-only and is the same as the address of the
			 * overall AHB window of the controller for all flash
			 * devices.
			 */
			flash->ahb_base = cs ? (void *)SEGMENT_ADDR_START(seg_addr) :
				priv->ahb_base;

			flash->cs = cs;
			flash->ce_ctrl_user = CE_CTRL_USERMODE;
			flash->ce_ctrl_fread = CE_CTRL_READMODE;
		}
	}
	return 0;
}

static int aspeed_spi_read_from_ahb(void __iomem *ahb_base, void *buf,
				    size_t len)
{
	size_t offset = 0;

	if (!((uintptr_t)buf % 4)) {
		readsl(ahb_base, buf, len >> 2);
		offset = len & ~0x3;
		len -= offset;
	}
	readsb(ahb_base, (u8 *)buf + offset, len);

	return 0;
}

static int aspeed_spi_write_to_ahb(void __iomem *ahb_base, const void *buf,
				   size_t len)
{
	size_t offset = 0;

	if (!((uintptr_t)buf % 4)) {
		writesl(ahb_base, buf, len >> 2);
		offset = len & ~0x3;
		len -= offset;
	}
	writesb(ahb_base, (u8 *)buf + offset, len);

	return 0;
}

static void aspeed_spi_start_user(struct aspeed_spi_priv *priv,
				  struct aspeed_spi_flash *flash)
{
	u32 ctrl_reg = flash->ce_ctrl_user | CE_CTRL_STOP_ACTIVE;

	/* Deselect CS and set USER command mode */
	writel(ctrl_reg, &priv->regs->ce_ctrl[flash->cs]);

	/* Select CS */
	clrbits_le32(&priv->regs->ce_ctrl[flash->cs], CE_CTRL_STOP_ACTIVE);
}

static void aspeed_spi_stop_user(struct aspeed_spi_priv *priv,
				 struct aspeed_spi_flash *flash)
{
	/* Deselect CS first */
	setbits_le32(&priv->regs->ce_ctrl[flash->cs], CE_CTRL_STOP_ACTIVE);

	/* Restore default command mode */
	writel(flash->ce_ctrl_fread, &priv->regs->ce_ctrl[flash->cs]);
}

static int aspeed_spi_read_reg(struct aspeed_spi_priv *priv,
			       struct aspeed_spi_flash *flash,
			       u8 opcode, u8 *read_buf, int len)
{
	struct aspeed_spi_op op =
		ASPEED_SPI_OP(0,
			      ASPEED_SPI_OP_CMD(opcode),
			      ASPEED_SPI_OP_ADDR(0, 0),
			      ASPEED_SPI_OP_DUMMY(0),
			      ASPEED_SPI_OP_DATA_IN(len, read_buf));

	if (priv->spi_exec_op_cmd) {
		priv->spi_exec_op_cmd(priv, flash, &op);
		return 0;
	}

	aspeed_spi_start_user(priv, flash);
	aspeed_spi_write_to_ahb(flash->ahb_base, &opcode, 1);
	aspeed_spi_read_from_ahb(flash->ahb_base, read_buf, len);
	aspeed_spi_stop_user(priv, flash);

	return 0;
}

static int aspeed_spi_write_reg(struct aspeed_spi_priv *priv,
				struct aspeed_spi_flash *flash,
				u8 opcode, const u8 *write_buf, int len)
{
	int i;
	struct aspeed_spi_op op =
		ASPEED_SPI_OP(0,
			      ASPEED_SPI_OP_CMD(opcode),
			      ASPEED_SPI_OP_ADDR(0, 0),
			      ASPEED_SPI_OP_DUMMY(0),
			      ASPEED_SPI_OP_DATA_OUT(len, write_buf));

	if (priv->spi_exec_op_cmd) {
		if (opcode == SPINOR_OP_BE_4K || opcode == SPINOR_OP_BE_4K_4B ||
		    opcode == SPINOR_OP_BE_32K || opcode == SPINOR_OP_BE_32K_4B ||
		    opcode == SPINOR_OP_SE || opcode == SPINOR_OP_SE_4B) {
			op.addr.nbytes = len;
			for (i = 0; i < len; i++) {
				op.addr.val <<= 8;
				op.addr.val |= (u32)write_buf[i];
			}
			op.data.nbytes = 0;
		}

		priv->spi_exec_op_cmd(priv, flash, &op);
		return 0;
	}

	aspeed_spi_start_user(priv, flash);
	aspeed_spi_write_to_ahb(flash->ahb_base, &opcode, 1);
	aspeed_spi_write_to_ahb(flash->ahb_base, write_buf, len);
	aspeed_spi_stop_user(priv, flash);

	debug("=== write opcode [%x] ==== \n", opcode);
	switch(opcode) {
		case SPINOR_OP_EN4B:
			/* For ast2600, if 2 chips ABR mode is enabled,
			 * turn on 3B mode auto clear in order to avoid
			 * the scenario where spi controller is at 4B mode
			 * and flash site is at 3B mode after 3rd switch.
			 */
			if (priv->new_ver == 1 && (readl(SPI_3B_AUTO_CLR_REG) & SPI_3B_AUTO_CLR))
				writel(readl(&priv->regs->soft_rst_cmd_ctrl) | SOFT_RST_CMD_EN,
						&priv->regs->soft_rst_cmd_ctrl);

			writel(readl(&priv->regs->ctrl) | (0x11 << flash->cs), &priv->regs->ctrl);
			break;
		case SPINOR_OP_EX4B:
			writel(readl(&priv->regs->ctrl) & ~(0x11 << flash->cs), &priv->regs->ctrl);
			break;
	}
	return 0;
}

static void aspeed_spi_send_cmd_addr(struct aspeed_spi_priv *priv,
				     struct aspeed_spi_flash *flash,
				     const u8 *cmdbuf, unsigned int cmdlen, uint32_t flag)
{
	int i;

	/* First, send the opcode */
	aspeed_spi_write_to_ahb(flash->ahb_base, &cmdbuf[0], 1);

	if(flash->write_iomode == CE_CTRL_IO_QUAD_ADDR_DATA && (flag & SPI_WRITE_TO_FLASH))
		writel(flash->ce_ctrl_user | flash->write_iomode, &priv->regs->ce_ctrl[flash->cs]);
	else if(flash->read_iomode == CE_CTRL_IO_QUAD_ADDR_DATA && (flag & SPI_READ_FROM_FLASH))
		writel(flash->ce_ctrl_user | flash->read_iomode, &priv->regs->ce_ctrl[flash->cs]);

	/* Then the address */
	for (i = 1 ; i < cmdlen; i++)
		aspeed_spi_write_to_ahb(flash->ahb_base, &cmdbuf[i], 1);
}

static ssize_t aspeed_spi_read_user(struct aspeed_spi_priv *priv,
				    struct aspeed_spi_flash *flash,
				    unsigned int cmdlen, const u8 *cmdbuf,
				    unsigned int len, u8 *read_buf)
{
	u8 dummy = 0x00;
	int i;
	struct aspeed_spi_op op =
		ASPEED_SPI_OP(flash->read_iomode,
			      ASPEED_SPI_OP_CMD(cmdbuf[0]),
			      ASPEED_SPI_OP_ADDR(0, 0),
			      ASPEED_SPI_OP_DUMMY(flash->spi->read_dummy / 8),
			      ASPEED_SPI_OP_DATA_IN(len, read_buf));

	if (priv->spi_exec_op_cmd) {
		op.addr.nbytes = cmdlen - 1 - op.dummy.nbytes;
		op.addr.val = aspeed_spi_flash_to_addr(flash, cmdbuf, op.addr.nbytes + 1);
		priv->spi_exec_op_cmd(priv, flash, &op);
		return 0;
	}

	aspeed_spi_start_user(priv, flash);

	/* cmd buffer = cmd + addr + dummies */
	aspeed_spi_send_cmd_addr(priv, flash, cmdbuf,
				 cmdlen - (flash->spi->read_dummy / 8), SPI_READ_FROM_FLASH);

	for (i = 0; i < (flash->spi->read_dummy / 8); i++)
		aspeed_spi_write_to_ahb(flash->ahb_base, &dummy, 1);

	if (flash->read_iomode) {
		clrbits_le32(&priv->regs->ce_ctrl[flash->cs],
			     CE_CTRL_IO_MODE_MASK);
		setbits_le32(&priv->regs->ce_ctrl[flash->cs], flash->read_iomode);
	}

	aspeed_spi_read_from_ahb(flash->ahb_base, read_buf, len);
	aspeed_spi_stop_user(priv, flash);

	return 0;
}

static ssize_t aspeed_spi_read_sfdp(struct aspeed_spi_priv *priv,
				    struct aspeed_spi_flash *flash,
				    unsigned int cmdlen, const u8 *cmdbuf,
				    unsigned int len, u8 *read_buf)
{
	u8 dummy = 0x00;
	int i;
	struct aspeed_spi_op op =
		ASPEED_SPI_OP(flash->read_iomode,
			      ASPEED_SPI_OP_CMD(cmdbuf[0]),
			      ASPEED_SPI_OP_ADDR(0, 3),
			      ASPEED_SPI_OP_DUMMY(flash->spi->read_dummy / 8),
			      ASPEED_SPI_OP_DATA_IN(len, read_buf));

	if (priv->spi_exec_op_cmd) {
		op.addr.val = aspeed_spi_flash_to_addr(flash, cmdbuf, op.addr.nbytes + 1);
		priv->spi_exec_op_cmd(priv, flash, &op);
		return 0;
	}

	/* only 1-1-1 mode is used to read SFDP */
	aspeed_spi_start_user(priv, flash);

	/* cmd buffer = cmd + addr + dummies */
	aspeed_spi_send_cmd_addr(priv, flash, cmdbuf,
				 cmdlen - (flash->spi->read_dummy / 8), 0);

	for (i = 0; i < (flash->spi->read_dummy / 8); i++)
		aspeed_spi_write_to_ahb(flash->ahb_base, &dummy, 1);

	aspeed_spi_read_from_ahb(flash->ahb_base, read_buf, len);
	aspeed_spi_stop_user(priv, flash);

	return 0;
}

static ssize_t aspeed_spi_write_user(struct aspeed_spi_priv *priv,
				     struct aspeed_spi_flash *flash,
				     unsigned int cmdlen, const u8 *cmdbuf,
				     unsigned int len,	const u8 *write_buf)
{
	struct aspeed_spi_op op =
		ASPEED_SPI_OP(flash->write_iomode,
			      ASPEED_SPI_OP_CMD(cmdbuf[0]),
			      ASPEED_SPI_OP_ADDR(0, 0),
			      ASPEED_SPI_OP_DUMMY(0),
			      ASPEED_SPI_OP_DATA_OUT(len, write_buf));

	if (priv->spi_exec_op_cmd) {
		op.addr.nbytes = cmdlen - 1;
		op.addr.val = aspeed_spi_flash_to_addr(flash, cmdbuf, op.addr.nbytes + 1);
		priv->spi_exec_op_cmd(priv, flash, &op);
		return 0;
	}

	aspeed_spi_start_user(priv, flash);

	/* cmd buffer = cmd + addr : normally cmd is use signle mode*/
	aspeed_spi_send_cmd_addr(priv, flash, cmdbuf, cmdlen, SPI_WRITE_TO_FLASH);

	/* data will use io mode */
	if(flash->write_iomode == CE_CTRL_IO_QUAD_DATA)
		writel(flash->ce_ctrl_user | flash->write_iomode, &priv->regs->ce_ctrl[flash->cs]);

	aspeed_spi_write_to_ahb(flash->ahb_base, write_buf, len);

	aspeed_spi_stop_user(priv, flash);

	return 0;
}

static u32 aspeed_spi_flash_to_addr(struct aspeed_spi_flash *flash,
				    const u8 *cmdbuf, unsigned int cmdlen)
{
	u8 addrlen = cmdlen - 1;
	u32 addr = (cmdbuf[1] << 16) | (cmdbuf[2] << 8) | cmdbuf[3];

	/*
	 * U-Boot SPI Flash layer uses 3 bytes addresses, but it might
	 * change one day
	 */
	if (addrlen == 4)
		addr = (addr << 8) | cmdbuf[4];

	return addr;
}

/* TODO(clg@kaod.org): add support for XFER_MMAP instead ? */
static ssize_t aspeed_spi_read(struct aspeed_spi_priv *priv,
			       struct aspeed_spi_flash *flash,
			       unsigned int cmdlen, const u8 *cmdbuf,
			       unsigned int len, u8 *read_buf)
{
	/* cmd buffer = cmd + addr + dummies */
	u32 offset = aspeed_spi_flash_to_addr(flash, cmdbuf,
					      cmdlen - (flash->spi->read_dummy/8));
	struct aspeed_spi_op op =
		ASPEED_SPI_OP(flash->read_iomode,
			      ASPEED_SPI_OP_CMD(cmdbuf[0]),
			      ASPEED_SPI_OP_ADDR(0, 0),
			      ASPEED_SPI_OP_DUMMY(flash->spi->read_dummy / 8),
			      ASPEED_SPI_OP_DATA_IN(len, read_buf));

	if (priv->spi_exec_op_cmd) {
		op.addr.nbytes = cmdlen - 1 - op.dummy.nbytes;
		op.addr.val = aspeed_spi_flash_to_addr(flash, cmdbuf, op.addr.nbytes + 1);
		priv->spi_exec_op_cmd(priv, flash, &op);
		return 0;
	}

	/*
	 * Switch to USER command mode:
	 * - if read SFDP content.
	 * - if the AHB window configured for the device is
	 *   too small for the read operation
	 * - if read offset is smaller than the decoded start address
	 *   and the decoded range is not multiple of flash size.
	 */
	if (priv->pure_spi_mode_only || (offset + len >= flash->ahb_size) ||
	    (offset < ((int)flash->ahb_base & 0x0FFFFFFF) &&
	     (((int)flash->ahb_base & 0x0FFFFFFF) % flash->spi->size) != 0)) {
		return aspeed_spi_read_user(priv, flash, cmdlen, cmdbuf,
					    len, read_buf);
	}

	memcpy_fromio(read_buf, flash->ahb_base + offset, len);

	return 0;
}

int aspeed_spi_exec_op_cmd_mode(struct aspeed_spi_priv *priv,
				struct aspeed_spi_flash *flash,
				struct aspeed_spi_op *op)
{
	uint32_t cs = flash->cs;
	uint32_t ctrl_val;
	uint32_t addr_mode_reg, addr_mode_reg_backup;
	uint32_t addr_data_mask = 0;
	void __iomem *op_addr;
	const void *data_buf;
	uint32_t data_byte = 0;
	uint32_t dummy_data = 0;

	debug("iomode: %08x, cmd:%02x, addr:%08x, dummy:%d, data_len:%x, dir: %s\n",
	      op->io_mode, op->cmd.opcode, op->addr.val, op->dummy.nbytes,
	      op->data.nbytes, op->data.dir == ASPEED_SPI_DIR_IN ? "in" : "out");

	addr_mode_reg = readl(&priv->regs->ctrl);
	addr_mode_reg_backup = addr_mode_reg;
	addr_data_mask = readl(&priv->regs->cmd_ctrl);

	ctrl_val = flash->ce_ctrl_fread & (~0xf0ff40c7);
	ctrl_val |= op->io_mode;
	/* configure opcode */
	ctrl_val |= op->cmd.opcode << 16;

	/* configure operation address, address length and address mask */
	if (op->addr.nbytes != 0) {
		if (op->addr.nbytes == 3)
			addr_mode_reg &= ~(0x11 << cs);
		else
			addr_mode_reg |= (0x11 << cs);

		addr_data_mask &= 0x0f;
		op_addr = flash->ahb_base + op->addr.val;
	} else {
		addr_data_mask |= 0xf0;
		op_addr = flash->ahb_base;
	}

	if (op->dummy.nbytes != 0) {
		ctrl_val |= ((op->dummy.nbytes & 0x3) << 6 |
			     ((op->dummy.nbytes & 0x4) >> 2) << 14);
	}

	/* configure data io mode and data mask */
	if (op->data.nbytes != 0) {
		addr_data_mask &= 0xF0;
		if (op->data.nbytes < 4)
			addr_data_mask |= ~((1 << op->data.nbytes) - 1);

		data_byte = op->data.nbytes;
		if (op->data.dir == ASPEED_SPI_DIR_OUT) {
			if (data_byte % 4 != 0) {
				memset(priv->tmp_buf, 0xff, ((data_byte / 4) + 1) * 4);
				memcpy(priv->tmp_buf, op->data.buf.out, data_byte);
				data_buf = priv->tmp_buf;
				data_byte = ((data_byte / 4) + 1) * 4;
			} else {
				data_buf = op->data.buf.out;
			}
		} else {
			data_buf = op->data.buf.in;
		}
	} else {
		addr_data_mask |= 0x0f;
		data_byte = 1;
		data_buf = &dummy_data;
	}

	/* configure command mode */
	if (op->data.dir == ASPEED_SPI_DIR_OUT)
		ctrl_val |= CE_CTRL_WRITEMODE;
	else
		ctrl_val |= CE_CTRL_FREADMODE;

	/* set controller registers */
	writel(ctrl_val, &priv->regs->ce_ctrl[cs]);
	writel(addr_mode_reg, &priv->regs->ctrl);
	writel(addr_data_mask, &priv->regs->cmd_ctrl);

	debug("ctrl: 0x%08x, addr_mode: 0x%x, mask: 0x%x, addr:0x%08x\n",
	      ctrl_val, addr_mode_reg, addr_data_mask, (uint32_t)op_addr);

	/* trigger spi transmission or reception sequence */
	if (op->data.dir == ASPEED_SPI_DIR_OUT)
		memcpy_toio(op_addr, data_buf, data_byte);
	else
		memcpy_fromio((void *)data_buf, op_addr, data_byte);

	/* restore controller setting */
	writel(flash->ce_ctrl_fread, &priv->regs->ce_ctrl[cs]);
	writel(addr_mode_reg_backup, &priv->regs->ctrl);
	writel(0x0, &priv->regs->cmd_ctrl);

	return 0;
}

static int aspeed_spi_xfer(struct udevice *dev, unsigned int bitlen,
			   const void *dout, void *din, unsigned long flags)
{
	struct udevice *bus = dev->parent;
	struct aspeed_spi_priv *priv = dev_get_priv(bus);
	struct aspeed_spi_flash *flash;
	u8 *cmd_buf = priv->cmd_buf;
	size_t data_bytes;
	int err = 0;
	u32 iomode;

	flash = aspeed_spi_get_flash(dev);
	if (!flash)
		return -ENXIO;

	if (flags & SPI_XFER_BEGIN) {
		/* save command in progress */
		priv->cmd_len = bitlen / 8;
		memcpy(cmd_buf, dout, priv->cmd_len);
	}

	if (flags == (SPI_XFER_BEGIN | SPI_XFER_END)) {
		/* if start and end bit are set, the data bytes is 0. */
		data_bytes = 0;
	} else {
		data_bytes = bitlen / 8;
	}

	debug("CS%u: %s cmd %zu bytes data %zu bytes\n", flash->cs,
	      din ? "read" : "write", priv->cmd_len, data_bytes);

	if ((flags & SPI_XFER_END) || flags == 0) {
		if (priv->cmd_len == 0) {
			pr_err("No command is progress !\n");
			return -1;
		}

		if (din && data_bytes) {
			if (priv->cmd_len == 1) {
				err = aspeed_spi_read_reg(priv, flash,
							  cmd_buf[0],
							  din, data_bytes);
			} else if (cmd_buf[0] == SPINOR_OP_RDSFDP) {
				err = aspeed_spi_read_sfdp(priv, flash,
							   priv->cmd_len,
							   cmd_buf, data_bytes,
							   din);
			} else if (cmd_buf[0] == SPINOR_OP_RDAR) {
				/* only for Cypress flash */
				iomode = flash->read_iomode;
				flash->read_iomode = 0;
				err = aspeed_spi_read_user(priv, flash,
							   priv->cmd_len,
							   cmd_buf, data_bytes,
							   din);
				flash->read_iomode = iomode;
			} else {
				err = aspeed_spi_read(priv, flash,
						      priv->cmd_len,
						      cmd_buf, data_bytes,
						      din);
			}
		} else if (dout) {
			if (priv->cmd_len == 1) {
				err = aspeed_spi_write_reg(priv, flash,
							   cmd_buf[0],
							   dout, data_bytes);
			} else {
				err = aspeed_spi_write_user(priv, flash,
							    priv->cmd_len,
							    cmd_buf, data_bytes,
							    dout);
			}
		}

		if (flags & SPI_XFER_END) {
			/* clear command */
			memset(cmd_buf, 0, sizeof(priv->cmd_buf));
			priv->cmd_len = 0;
		}
	}

	return err;
}

#ifdef CONFIG_ASPEED_SPI_FLASH_WRITE_PROTECTION
static void aspeed_spi_fill_FQCD(struct aspeed_spi_priv *priv, u8 cmd)
{
	u32 reg_val;
	u32 i;

	for (i = 0; i < 20; i++) {
		reg_val = readl(&priv->regs->fully_qualified_cmd[i]);
		if ((u8)(reg_val & 0xff) == cmd ||
		    (u8)((reg_val & 0xff00) >> 8) == cmd) {
			if ((reg_val & 0x80000000) == 0x80000000) {
				debug("cmd: %02x already exists in FQCD.\n", cmd);
				return;
			}
		}
	}

	for (i = 0; i < 20; i++) {
		reg_val = readl(&priv->regs->fully_qualified_cmd[i]);
		if ((reg_val & 0x80000000) == 0x80000000) {
			if ((u8)(reg_val & 0xff) == 0x0) {
				reg_val |= (u32)cmd;
				debug("[%d]fill %02x cmd in FQCD%02d.\n", __LINE__, cmd, i);
				writel(reg_val, &priv->regs->fully_qualified_cmd[i]);
				return;
			} else if ((u8)((reg_val & 0xff00) >> 8) == 0x0) {
				reg_val |= ((u32)cmd) << 8;
				debug("[%d]fill %02x cmd in FQCD%02d.\n", __LINE__, cmd, i);
				writel(reg_val, &priv->regs->fully_qualified_cmd[i]);
				return;
			}
		}
	}

	for (i = 0; i < 20; i++) {
		reg_val = readl(&priv->regs->fully_qualified_cmd[i]);
		if (reg_val == 0) {
			reg_val = 0x80000000 | (u32)cmd;
			debug("[%d]fill %02x cmd in FQCD%02d.\n", __LINE__, cmd, i);
			writel(reg_val, &priv->regs->fully_qualified_cmd[i]);
			return;
		}
	}
}

static void aspeed_spi_fill_AQCD(struct aspeed_spi_priv *priv, u8 cmd, u8 addr_width)
{
	u32 reg_val;
	u32 i;
	u32 bit_offset;

	if (addr_width != 3 && addr_width != 4) {
		printf("wrong address width: %d.\n", addr_width);
		return;
	}

	bit_offset = (addr_width - 3) * 8;

	for (i = 0; i < 12; i++) {
		reg_val = readl(&priv->regs->addr_qualified_cmd[i]);
		if ((reg_val & 0x80000000) == 0x80000000) {
			if ((u8)((reg_val & (0xff << bit_offset)) >> bit_offset) == cmd) {
				debug("cmd: %02x already exists in AQCD.\n", cmd);
				return;
			}
		}
	}

	for (i = 0; i < 12; i++) {
		reg_val = readl(&priv->regs->addr_qualified_cmd[i]);
		if ((reg_val & 0x80000000) == 0x80000000) {
			if ((u8)((reg_val & (0xff << bit_offset)) >> bit_offset) == 0x0) {
				reg_val |= ((u32)cmd << bit_offset);
				debug("fill %02x cmd in AQCD%02d.\n", cmd, i);
				writel(reg_val, &priv->regs->addr_qualified_cmd[i]);
				return;
			}
		}

		if (reg_val == 0) {
			reg_val = 0x80000000 | ((u32)cmd << bit_offset);
			debug("fill %02x cmd in AQCD%02d.\n", cmd, i);
			writel(reg_val, &priv->regs->addr_qualified_cmd[i]);
			return;
		}
	}
}

static void aspeed_spi_cmd_filter_config(struct aspeed_spi_priv *priv,
					 u32 cs, bool enable)
{
	u32 reg_val;

	reg_val = readl(&priv->regs->write_cmd_filter_ctrl);

	if (enable)
		reg_val |= BIT(cs);
	else
		reg_val &= ~BIT(cs);

	writel(reg_val, &priv->regs->write_cmd_filter_ctrl);
}

static int aspeed_spi_write_addr_ftr_sanity(struct aspeed_spi_priv *priv,
					    u32 offset, size_t len)
{
	u32 addr_ftr_ctrl;
	u32 reg_val;
	u32 start;
	u32 end;
	u32 i;

	addr_ftr_ctrl = readl(&priv->regs->write_addr_filter_ctrl);
	for (i = 0; i < 8; i++) {
		if ((addr_ftr_ctrl & (0x3 << (i * 2))) == 0)
			continue;
		reg_val = readl(&priv->regs->write_addr_filter[i]);
		start = (reg_val & 0xffff) << 12;
		end = (((reg_val & 0xffff0000) >> 16) << 12) | 0xFFF;

		if (offset >= start && offset < end)
			return -1;
		else if ((offset + len) > start && (offset + len) < end)
			return -1;
	}

	return 0;
}

static int aspeed_add_write_addr_ftr(struct aspeed_spi_priv *priv,
				     u32 offset, size_t len)
{
	u32 addr_ftr_ctrl;
	u32 reg_val;
	u32 start;
	u32 end;
	u32 i;

	if ((offset & 0xfff) != 0) {
		offset &= 0xfffff000;
		printf("protected start address will be entend to 0x%08x.\n",
		       offset);
	}

	if ((len & 0xfff) != 0) {
		len &= 0xfff;
		printf("protected len will be trimed to 0x%x.\n", len);
	}

	if (len == 0) {
		printf("invalid protect len: 0x%x.\n", len);
		return -1;
	}

	addr_ftr_ctrl = readl(&priv->regs->write_addr_filter_ctrl);
	for (i = 0; i < 8; i++) {
		if ((addr_ftr_ctrl & (0x3 << (i * 2))) == 0) {
			start = offset;
			end = offset + len - 1;

			reg_val = (start >> 12) | ((end >> 12) << 16);

			debug("start: 0x%08x, end: 0x%08x, val: 0x%08x.\n",
			      start, end, reg_val);

			writel(reg_val, &priv->regs->write_addr_filter[i]);
			addr_ftr_ctrl |= 0x3 << (i * 2);
			writel(addr_ftr_ctrl, &priv->regs->write_addr_filter_ctrl);

			printf("apply write lock from offset, 0x%08x, with len, 0x%08x.\n",
			       offset, (u32)len);

			break;
		}
	}

	if (i == 8) {
		printf("insufficient write address filter register.\n");
		return -1;
	}

	return 0;
}

static int aspeed_remove_write_addr_ftr(struct aspeed_spi_priv *priv,
					u32 offset, size_t len)
{
	u32 addr_ftr_ctrl;
	u32 reg_val;
	u32 bit_mask;
	u32 start;
	u32 end;
	u32 i;

	if ((offset & 0xfff) != 0) {
		printf("start address should be aligned to 0x1000.\n");
		return -1;
	}

	if ((len & 0xfff) != 0) {
		printf("removed length should be aligned to 0x1000.\n");
		return -1;
	}

	if (len == 0) {
		printf("invalid removed length!\n");
		return -1;
	}

	addr_ftr_ctrl = readl(&priv->regs->write_addr_filter_ctrl);
	for (i = 0; i < 8; i++) {
		bit_mask = 0x3 << (i * 2);
		if ((addr_ftr_ctrl & bit_mask) != bit_mask)
			continue;

		reg_val = readl(&priv->regs->write_addr_filter[i]);
		start = (reg_val & 0xffff) << 12;
		end = (((reg_val & 0xffff0000) >> 16) << 12) + 0x1000;

		if (offset != start || offset + len != end)
			continue;

		addr_ftr_ctrl &= ~(0x3 << (i * 2));
		writel(addr_ftr_ctrl, &priv->regs->write_addr_filter_ctrl);
		writel(0x0, &priv->regs->write_addr_filter[i]);
		printf("remove write lock from offset, 0x%08x, with len, 0x%08x.\n",
		       offset, (u32)len);
		break;
	}

	if (i == 8) {
		printf("cannot find expected removed region.\n");
		return -1;
	}

	return 0;
}

static int aspeed_spi_mem_wlock(struct udevice *dev, u32 offset, size_t len)
{
	struct udevice *bus = dev->parent;
	struct aspeed_spi_priv *priv = dev_get_priv(bus);
	struct aspeed_spi_flash *flash;
	struct spi_nor *nor;
	int ret;

	debug("%s offset: 0x%08x, len: 0x%08x.\n", __func__, offset, (u32)len);

	flash = aspeed_spi_get_flash(dev);
	if (!flash)
		return -ENXIO;

	nor = flash->spi;

	debug("name: %s, read cmd: %02x, erase cmd: %02x, write cmd: %02x.\n",
	      nor->name, nor->read_opcode, nor->erase_opcode, nor->program_opcode);

	/* enable address filter */
	aspeed_spi_fill_FQCD(priv, nor->read_opcode);
	aspeed_spi_fill_AQCD(priv, nor->erase_opcode, nor->addr_width);
	aspeed_spi_fill_AQCD(priv, nor->program_opcode, nor->addr_width);
	aspeed_spi_cmd_filter_config(priv, flash->cs, true);

	ret = aspeed_spi_write_addr_ftr_sanity(priv, offset, len);
	if (ret < 0) {
		printf("The expected protect region overlays with the existed regions!\n");
		return ret;
	}

	ret = aspeed_add_write_addr_ftr(priv, offset, len);
	if (ret < 0)
		return -1;

	return 0;
}

static int aspeed_spi_mem_wunlock(struct udevice *dev, u32 offset, size_t len)
{
	struct udevice *bus = dev->parent;
	struct aspeed_spi_priv *priv = dev_get_priv(bus);
	int ret;

	ret = aspeed_remove_write_addr_ftr(priv, offset, len);
	if (ret < 0)
		return -1;

	return 0;
}
#endif

static int aspeed_spi_child_pre_probe(struct udevice *dev)
{
	struct dm_spi_slave_platdata *slave_plat = dev_get_parent_platdata(dev);

	debug("pre_probe slave device on CS%u, max_hz %u, mode 0x%x.\n",
	      slave_plat->cs, slave_plat->max_hz, slave_plat->mode);

	if (!aspeed_spi_get_flash(dev))
		return -ENXIO;

	return 0;
}

/*
 * AST2600 SPI memory controllers support multiple chip selects.
 * The start address of a decode range should be multiple
 * of its related flash size. Namely, the total decoded size
 * from flash 0 to flash N should be multiple of (N + 1) flash size.
 */
void aspeed_g6_adjust_decode_sz(u32 decode_sz_arr[], int len)
{
	int cs, j;
	u32 sz;

	for (cs = len - 1; cs >= 0; cs--) {
		sz = 0;
		for (j = 0; j < cs; j++)
			sz += decode_sz_arr[j];

		if (sz % decode_sz_arr[cs] != 0)
			decode_sz_arr[0] += (sz % decode_sz_arr[cs]);
	}
}

/*
 * It is possible to automatically define a contiguous address space
 * on top of all CEs in the AHB window of the controller but it would
 * require much more work. Let's start with a simple mapping scheme
 * which should work fine for a single flash device.
 *
 * More complex schemes should probably be defined with the device
 * tree.
 */
static int aspeed_spi_flash_set_segment(struct aspeed_spi_priv *priv,
					struct aspeed_spi_flash *flash)
{
	u32 seg_addr;
	u32 decode_sz_arr[ASPEED_SPI_MAX_CS];
	u32 reg_val;
	u32 cs;
	u32 total_decode_sz = 0;
	u32 cur_offset = 0;

	/* could be configured through the device tree */
	flash->ahb_size = flash->spi->size;

	if (priv->new_ver) {
		if (priv->pure_spi_mode_only) {
			flash->ahb_size = priv->ahb_size / priv->num_cs;
			flash->ahb_size &= 0xffe00000;
		}

		for (cs = 0; cs < ASPEED_SPI_MAX_CS; cs++) {
			reg_val = readl(&priv->regs->segment_addr[cs]);
			if (reg_val != 0 &&
			    G6_SEGMENT_ADDR_END(reg_val) > G6_SEGMENT_ADDR_START(reg_val)) {
				decode_sz_arr[cs] =
					G6_SEGMENT_ADDR_END(reg_val) - G6_SEGMENT_ADDR_START(reg_val);
			} else {
				decode_sz_arr[cs] = 0;
			}
		}

		decode_sz_arr[flash->cs] = flash->ahb_size;
		aspeed_g6_adjust_decode_sz(decode_sz_arr, flash->cs + 1);

		for (cs = 0; cs < ASPEED_SPI_MAX_CS; cs++)
			total_decode_sz += decode_sz_arr[cs];

		if (total_decode_sz > priv->ahb_size) {
			printf("err: Total decoded size, 0x%x, is too large.\n", total_decode_sz);
			return -ENOMEM;
		}

		for (cs = 0; cs < ASPEED_SPI_MAX_CS; cs++) {
			struct aspeed_spi_flash *flash = &priv->flashes[cs];

			flash->ahb_base = (void __iomem *)((u32)priv->ahb_base + cur_offset);

			if (decode_sz_arr[cs] != 0) {
				seg_addr = G6_SEGMENT_ADDR_VALUE((u32)flash->ahb_base,
								 (u32)flash->ahb_base + decode_sz_arr[cs]);
			} else {
				seg_addr = 0;
			}

			writel(seg_addr, &priv->regs->segment_addr[cs]);
			flash->ahb_size = decode_sz_arr[cs];
			cur_offset += decode_sz_arr[cs];
		}
	} else {
		seg_addr = SEGMENT_ADDR_VALUE((u32)flash->ahb_base,
						  (u32)flash->ahb_base + flash->ahb_size);
		writel(seg_addr, &priv->regs->segment_addr[flash->cs]);
	}

	return 0;
}

static int aspeed_spi_flash_init(struct aspeed_spi_priv *priv,
				 struct aspeed_spi_flash *flash,
				 struct udevice *dev)
{
	int ret;
	struct spi_flash *spi_flash = dev_get_uclass_priv(dev);
	struct spi_slave *slave = dev_get_parent_priv(dev);
	struct udevice *bus = dev->parent;
	u32 read_hclk;

	flash->spi = spi_flash;

	/*
	 * The flash device has not been probed yet. Initial transfers
	 * to read the JEDEC of the device will use the initial
	 * default settings of the registers.
	 */
	if (!spi_flash->name)
		return 0;

	/*
	 * The SPI flash device slave should not change, so initialize
	 * it only once.
	 */
	if (flash->init)
		return 0;

	debug("CS%u: init %s flags:%x size:%d page:%d sector:%d erase:%d",
	      flash->cs,
	      spi_flash->name, spi_flash->flags, spi_flash->size,
	      spi_flash->page_size, spi_flash->sector_size,
	      spi_flash->erase_size);
	debug(" cmds [ erase:%x read=%x write:%x ] dummy:%d, speed:%d\n",
	      spi_flash->erase_opcode,
	      spi_flash->read_opcode, spi_flash->program_opcode,
	      spi_flash->read_dummy, slave->speed);

	flash->ce_ctrl_user = CE_CTRL_USERMODE;
	flash->max_freq = slave->speed;

	if(priv->new_ver)
		read_hclk = aspeed_g6_spi_hclk_divisor(priv, slave->speed);
	else
		read_hclk = aspeed_spi_hclk_divisor(priv, slave->speed);

	switch(flash->spi->read_opcode) {
	case SPINOR_OP_READ:
	case SPINOR_OP_READ_4B:
		flash->read_iomode = CE_CTRL_IO_SINGLE;
		break;
	case SPINOR_OP_READ_1_1_2:
	case SPINOR_OP_READ_1_1_2_4B:
		flash->read_iomode = CE_CTRL_IO_DUAL_DATA;
		break;
	case SPINOR_OP_READ_1_1_4:
	case SPINOR_OP_READ_1_1_4_4B:
		flash->read_iomode = CE_CTRL_IO_QUAD_DATA;
		break;
	case SPINOR_OP_READ_1_4_4:
	case SPINOR_OP_READ_1_4_4_4B:
		flash->read_iomode = CE_CTRL_IO_QUAD_ADDR_DATA;
		printf("need modify dummy for 3 bytes\n");
		break;
	}

	switch(flash->spi->program_opcode) {
	case SPINOR_OP_PP:
	case SPINOR_OP_PP_4B:
		flash->write_iomode = CE_CTRL_IO_SINGLE;
		break;
	case SPINOR_OP_PP_1_1_4:
	case SPINOR_OP_PP_1_1_4_4B:
		flash->write_iomode = CE_CTRL_IO_QUAD_DATA;
		break;
	case SPINOR_OP_PP_1_4_4:
	case SPINOR_OP_PP_1_4_4_4B:
		flash->write_iomode = CE_CTRL_IO_QUAD_ADDR_DATA;
		printf("need modify dummy for 3 bytes");
		break;
	}

	if(priv->new_ver) {
		flash->ce_ctrl_fread = CE_G6_CTRL_CLOCK_FREQ(read_hclk) |
			flash->read_iomode |
			CE_CTRL_CMD(flash->spi->read_opcode) |
			CE_CTRL_DUMMY((flash->spi->read_dummy/8)) |
			CE_CTRL_FREADMODE;
		flash->ce_ctrl_user |= CE_G6_CTRL_CLOCK_FREQ(read_hclk);
	} else {
		flash->ce_ctrl_fread = CE_CTRL_CLOCK_FREQ(read_hclk) |
			flash->read_iomode |
			CE_CTRL_CMD(flash->spi->read_opcode) |
			CE_CTRL_DUMMY((flash->spi->read_dummy/8)) |
			CE_CTRL_FREADMODE;
	}

	if (flash->spi->addr_width == 4)
		writel(readl(&priv->regs->ctrl) | 0x11 << flash->cs, &priv->regs->ctrl);

	debug("CS%u: USER mode 0x%08x FREAD mode 0x%08x\n", flash->cs,
	      flash->ce_ctrl_user, flash->ce_ctrl_fread);

	/* Set the CE Control Register default (FAST READ) */
	writel(flash->ce_ctrl_fread, &priv->regs->ce_ctrl[flash->cs]);

	/* Set Address Segment Register for direct AHB accesses */
	ret = aspeed_spi_flash_set_segment(priv, flash);
	if (ret != 0)
		return ret;

	/*
	 * Set the Read Timing Compensation Register. This setting
	 * applies to all devices.
	 */
	if (!dev_read_bool(bus, "timing-calibration-disabled")) {
		ret = aspeed_spi_timing_calibration(priv, flash);
		if (ret != 0)
			return ret;
	}

	/* All done */
	flash->init = true;

	return 0;
}

static int aspeed_spi_claim_bus(struct udevice *dev)
{
	struct udevice *bus = dev->parent;
	struct aspeed_spi_priv *priv = dev_get_priv(bus);
	struct dm_spi_slave_platdata *slave_plat = dev_get_parent_platdata(dev);
	struct aspeed_spi_flash *flash;
	struct spi_slave *slave = dev_get_parent_priv(dev);
	u32 read_hclk;

	debug("%s: claim bus CS%u\n", bus->name, slave_plat->cs);

	flash = aspeed_spi_get_flash(dev);
	if (!flash)
		return -ENODEV;

	if (priv->new_ver) {
		if (dev_read_bool(bus, "timing-calibration-disabled")) {
			read_hclk = aspeed_g6_spi_hclk_divisor(priv, slave->speed);
			flash->ce_ctrl_user &= CE_CTRL_FREQ_MASK;
			flash->ce_ctrl_user |= CE_G6_CTRL_CLOCK_FREQ(read_hclk);
			flash->ce_ctrl_fread &= CE_CTRL_FREQ_MASK;
			flash->ce_ctrl_fread |= CE_G6_CTRL_CLOCK_FREQ(read_hclk);
		}
	}

	return aspeed_spi_flash_init(priv, flash, dev);
}

static int aspeed_spi_release_bus(struct udevice *dev)
{
	struct udevice *bus = dev->parent;
	struct dm_spi_slave_platdata *slave_plat = dev_get_parent_platdata(dev);

	debug("%s: release bus CS%u\n", bus->name, slave_plat->cs);

	if (!aspeed_spi_get_flash(dev))
		return -ENODEV;

	return 0;
}

static int aspeed_spi_set_mode(struct udevice *bus, uint mode)
{
	debug("%s: setting mode to %x\n", bus->name, mode);

	if (mode & (SPI_RX_QUAD | SPI_TX_QUAD)) {
#ifndef CONFIG_ASPEED_AST2600
		pr_err("%s invalid QUAD IO mode\n", bus->name);
		return -EINVAL;
#endif
	}

	/* The CE Control Register is set in claim_bus() */
	return 0;
}

static int aspeed_spi_set_speed(struct udevice *bus, uint hz)
{
	debug("%s: setting speed to %u\n", bus->name, hz);

	/* The CE Control Register is set in claim_bus() */
	return 0;
}

static int aspeed_spi_count_flash_devices(struct udevice *bus)
{
	ofnode node;
	int count = 0;

	dev_for_each_subnode(node, bus) {
		if (ofnode_is_available(node) &&
		    (ofnode_device_is_compatible(node, "spi-flash") ||
		     ofnode_device_is_compatible(node, "jedec,spi-nor")))
			count++;
	}

	return count;
}

static int aspeed_spi_bind(struct udevice *bus)
{
	debug("%s assigned req_seq=%d seq=%d\n", bus->name, bus->req_seq,
	      bus->seq);

	return 0;
}

static int aspeed_spi_probe(struct udevice *bus)
{
	struct resource res_regs, res_ahb;
	struct aspeed_spi_priv *priv = dev_get_priv(bus);
	struct clk hclk;
	int ret;

	ret = dev_read_resource(bus, 0, &res_regs);
	if (ret < 0)
		return ret;

	priv->regs = (void __iomem *)res_regs.start;

	ret = dev_read_resource(bus, 1, &res_ahb);
	if (ret < 0)
		return ret;

	priv->ahb_base = (void __iomem *)res_ahb.start;
	priv->ahb_size = res_ahb.end - res_ahb.start + 1;

	ret = clk_get_by_index(bus, 0, &hclk);
	if (ret < 0) {
		pr_err("%s could not get clock: %d\n", bus->name, ret);
		return ret;
	}

	priv->hclk_rate = clk_get_rate(&hclk);
	clk_free(&hclk);

	priv->num_cs = dev_read_u32_default(bus, "num-cs", ASPEED_SPI_MAX_CS);

	priv->flash_count = aspeed_spi_count_flash_devices(bus);
	if (priv->flash_count > priv->num_cs) {
		pr_err("%s has too many flash devices: %d\n", bus->name,
		       priv->flash_count);
		return -EINVAL;
	}

	if (!priv->flash_count) {
		pr_err("%s has no flash devices ?!\n", bus->name);
		return -ENODEV;
	}

	if (device_is_compatible(bus, "aspeed,ast2600-fmc") || 
			device_is_compatible(bus, "aspeed,ast2600-spi")) {
		priv->new_ver = 1;
	}

	if (dev_read_bool(bus, "aspeed-spi-command-mode")) {
		debug("adopt command mode\n");
		priv->tmp_buf = memalign(4, 512);
		priv->spi_exec_op_cmd = aspeed_spi_exec_op_cmd_mode;
	} else {
		priv->spi_exec_op_cmd = NULL;
	}

	priv->pure_spi_mode_only = dev_read_bool(bus, "pure-spi-mode-only");

	/*
	 * There are some slight differences between the FMC and the
	 * SPI controllers
	 */
	priv->is_fmc = dev_get_driver_data(bus);

	ret = aspeed_spi_controller_init(priv);
	if (ret)
		return ret;

	debug("%s probed regs=%p ahb_base=%p cs_num=%d seq=%d\n",
	      bus->name, priv->regs, priv->ahb_base, priv->flash_count, bus->seq);

	return 0;
}

static const struct dm_spi_ops aspeed_spi_ops = {
	.claim_bus	= aspeed_spi_claim_bus,
	.release_bus	= aspeed_spi_release_bus,
	.set_mode	= aspeed_spi_set_mode,
	.set_speed	= aspeed_spi_set_speed,
	.xfer		= aspeed_spi_xfer,
#ifdef CONFIG_ASPEED_SPI_FLASH_WRITE_PROTECTION
	.mem_ctrl_wlock = aspeed_spi_mem_wlock,
	.mem_ctrl_wunlock = aspeed_spi_mem_wunlock,
#endif
};

static const struct udevice_id aspeed_spi_ids[] = {
	{ .compatible = "aspeed,ast2600-fmc", .data = 1 },
	{ .compatible = "aspeed,ast2600-spi", .data = 0 },
	{ .compatible = "aspeed,ast2500-fmc", .data = 1 },
	{ .compatible = "aspeed,ast2500-spi", .data = 0 },
	{ .compatible = "aspeed,ast2400-fmc", .data = 1 },
	{ }
};

U_BOOT_DRIVER(aspeed_spi) = {
	.name = "aspeed_spi",
	.id = UCLASS_SPI,
	.of_match = aspeed_spi_ids,
	.ops = &aspeed_spi_ops,
	.priv_auto_alloc_size = sizeof(struct aspeed_spi_priv),
	.child_pre_probe = aspeed_spi_child_pre_probe,
	.bind  = aspeed_spi_bind,
	.probe = aspeed_spi_probe,
};
