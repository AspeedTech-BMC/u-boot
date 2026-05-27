/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright ASPEED Technology Inc.
 */
#ifndef __AST2600_I2C_H_
#define __AST2600_I2C_H_

struct ast2600_i2c_regs {
	u32 fun_ctrl;
	u32 ac_timing;
	u32 trx_buff;
	u32 icr;
	u32 ier;
	u32 isr;
	u32 cmd_sts;
	u32 m_dma_len;
	u32 s_ier;
	u32 s_isr;
	u32 s_cmd;
	u32 s_dma_len;
	u32 m_dma_txa;
	u32 m_dma_rxa;
	u32 s_dma_txa;
	u32 s_dma_rxa;
	u32 s_addr_0;
	u32 s_addr_1;
	u32 m_len_sta;
	u32 s_len_sta;
	u32 dma_cfg;
	u32 dma_sta;
	u32 dummy58[2];
	u32 m_dma_tx_hi;
	u32 m_dma_rx_hi;
	u32 s_dma_tx_hi;
	u32 s_dma_rx_hi;
	u32 misc[2];
	u32 status;
	u32 hs_actime;
	u32 log[4]; //0x8c
	u32 wlock;
	u32 version_ctrl;
	u32 m_cmdq;
	u32 misc2;
	u32 dummya0[8];
	u32 m_bufftx[4];
	u32 m_buffrx[4];
	u32 s_bufftx[4];
	u32 s_buffrx[4];
};

struct ast2600_i2c_global_regs {
	u32 master_int;
	u32 slave_int;
	u32 dummy_offset8;
	u32 global_ctrl;
	u32 clk_divid;
};

enum i2c_version {
	AST2600,
	AST2700,
};

/* 0x00 : I2CC Master/Slave Function Control Register  */
#define I2CC_SLAVE_ADDR_RX_EN	BIT(20)
#define I2CC_MASTER_RETRY_MASK	GENMASK(19, 18)
#define I2CC_MASTER_RETRY(x)	(((x) & GENMASK(1, 0)) << 18)
#define I2CC_BUS_AUTO_RELEASE	BIT(17)
#define I2CC_M_SDA_LOCK_EN		BIT(16)
#define I2CC_MULTI_MASTER_DIS	BIT(15)
#define I2CC_M_SCL_DRIVE_EN		BIT(14)
#define I2CC_MSB_STS			BIT(9)
#define I2CC_SDA_DRIVE_1T_EN	BIT(8)
#define I2CC_M_SDA_DRIVE_1T_EN	BIT(7)
#define I2CC_M_HIGH_SPEED_EN	BIT(6)
/* reserved 5 : 2 */
#define I2CC_SLAVE_EN			BIT(1)
#define I2CC_MASTER_EN			BIT(0)

/* 0x04 : I2CD Clock and AC Timing Control Register #1 */
/* Base register value. These bits are always set by the driver. */
#define I2CD_CACTC_BASE		0xfff00300
#define I2CD_TCKHIGH_SHIFT	16
#define I2CD_TCKLOW_SHIFT	12
#define I2CD_THDDAT_SHIFT	10
#define I2CD_TO_DIV_SHIFT	8
#define I2CD_BASE_DIV_SHIFT	0

/* 0x08 : I2CC Master/Slave Transmit/Receive Byte Buffer Register */
#define I2CC_TX_DIR_MASK		GENMASK(31, 29)
#define I2CC_SDA_OE				BIT(28)
#define I2CC_SDA_O				BIT(27)
#define I2CC_SCL_OE				BIT(26)
#define I2CC_SCL_O				BIT(25)

#define I2CC_SCL_LINE_STS		BIT(18)
#define I2CC_SDA_LINE_STS		BIT(17)
#define I2CC_BUS_BUSY_STS		BIT(16)
#define I2CC_GET_RX_BUFF(x)		(((x) >> 8) & GENMASK(7, 0))

/* 0x10 : I2CM Master Interrupt Control Register */
/* 0x14 : I2CM Master Interrupt Status Register  */
#define I2CM_PKT_TIMEOUT		BIT(18)
#define I2CM_PKT_ERROR			BIT(17)
#define I2CM_PKT_DONE			BIT(16)

#define I2CM_BUS_RECOVER_FAIL	BIT(15)
#define I2CM_SDA_DL_TO			BIT(14)
#define I2CM_BUS_RECOVER		BIT(13)
#define I2CM_SMBUS_ALT			BIT(12)

#define I2CM_SCL_LOW_TO			BIT(6)
#define I2CM_ABNORMAL			BIT(5)
#define I2CM_NORMAL_STOP		BIT(4)
#define I2CM_ARBIT_LOSS			BIT(3)
#define I2CM_RX_DONE			BIT(2)
#define I2CM_TX_NAK				BIT(1)
#define I2CM_TX_ACK				BIT(0)

/* 0x18 : I2CM Master Command/Status Register   */
#define I2CM_PKT_ADDR(x)		(((x) & GENMASK(6, 0)) << 24)
#define I2CM_PKT_EN				BIT(16)
#define I2CM_SDA_OE_OUT_DIR		BIT(15)
#define I2CM_SDA_O_OUT_DIR		BIT(14)
#define I2CM_SCL_OE_OUT_DIR		BIT(13)
#define I2CM_SCL_O_OUT_DIR		BIT(12)
#define I2CM_RECOVER_CMD_EN		BIT(11)

#define I2CM_RX_DMA_EN			BIT(9)
#define I2CM_TX_DMA_EN			BIT(8)
/* Command Bit */
#define I2CM_RX_BUFF_EN			BIT(7)
#define I2CM_TX_BUFF_EN			BIT(6)
#define I2CM_STOP_CMD			BIT(5)
#define I2CM_RX_CMD_LAST		BIT(4)
#define I2CM_RX_CMD				BIT(3)

#define I2CM_TX_CMD				BIT(1)
#define I2CM_START_CMD			BIT(0)

#define I2CM_SET_RX_DMA_LEN(x)	((((x) & GENMASK(11, 0)) << 16) | BIT(31))
#define I2CM_SET_TX_DMA_LEN(x)	(((x) & GENMASK(11, 0)) | BIT(15))
#define I2CM_SET_DMA_BASE_L(x)	((x) & GENMASK(31, 0))
#define I2CM_SET_DMA_BASE_H(x)	(((x) >> 32) & GENMASK(8, 0))

/* 0x94 : I2CC Version Control   */
#define I2CC_DMA_MODE			BIT(2)

#define I2CC_BUFF_SIZE			16

#define I2C_TIMEOUT_US 100000

/* I2C Global Register */
#define I2CG_ISR			0x00
#define I2CG_SLAVE_ISR		0x04
#define I2CG_OWNER		    0x08
#define I2CG_CTRL		    0x0C
#define I2CG_CLK_DIV_CTRL	0x10

#define I2CG_SLAVE_PKT_NAK	    BIT(4)
#define I2CG_M_S_SEPARATE_INTR	BIT(3)
#define I2CG_CTRL_NEW_REG	    BIT(2)
#define I2CG_CTRL_NEW_CLK_DIV	BIT(1)

#define GLOBAL_INIT					\
			(I2CG_SLAVE_PKT_NAK |	\
			I2CG_CTRL_NEW_REG |		\
			I2CG_CTRL_NEW_CLK_DIV)
#define AST2600_I2CCG_DIV_CTRL 0xC6411208
#define AST2700_I2CCG_DIV_CTRL 0xC6220904

#define GET_CLK1_DIV(x) ((x) & 0xff)
#define GET_CLK2_DIV(x) (((x) >> 8) & 0xff)
#define GET_CLK3_DIV(x) (((x) >> 16) & 0xff)
#define GET_CLK4_DIV(x) (((x) >> 24) & 0xff)

#endif				/* __AST2600_I2C_H_ */
