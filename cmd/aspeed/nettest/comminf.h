/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) ASPEED Technology Inc.
 */

#ifndef COMMINF_H
#define COMMINF_H

#include "swfunc.h"

#include "mac.h"

//---------------------------------------------------------
// Print Message
//---------------------------------------------------------
// for function
#define FP_LOG                                   0
#define FP_IO                                    1
#define STD_OUT                                  2

#define PRINTF(i, ...)                                                         \
	do {                                                                   \
		if (i == STD_OUT) {                                            \
			fprintf(stdout, __VA_ARGS__);                          \
			break;                                                 \
		}                                                              \
		if ((display_lantest_log_msg != 0) && (i == FP_LOG)) {         \
			fprintf(stdout, "[Log]:   ");                          \
			fprintf(stdout, __VA_ARGS__);                          \
		}                                                              \
	} while (0);

//---------------------------------------------------------
// Function
//---------------------------------------------------------
  #define SWAP_4B( x )                                                             \
                                                 ( ( ( ( x ) & 0xff000000 ) >> 24) \
                                                 | ( ( ( x ) & 0x00ff0000 ) >>  8) \
                                                 | ( ( ( x ) & 0x0000ff00 ) <<  8) \
                                                 | ( ( ( x ) & 0x000000ff ) << 24) \
                                                 )
  #define SWAP_2B( x )                                                             \
                                                 ( ( ( ( x ) & 0xff00     ) >>  8) \
                                                 | ( ( ( x ) & 0x00ff     ) <<  8) \
                                                 )

  #define SWAP_2B_BEDN( x )                      ( SWAP_2B ( x ) )
  #define SWAP_2B_LEDN( x )                      ( x )
  #define SWAP_4B_BEDN( x )                      ( SWAP_4B ( x ) )
  #define SWAP_4B_LEDN( x )                      ( x )

  #define SWAP_4B_BEDN_NCSI( x )                 ( SWAP_4B( x ) )
  #define SWAP_4B_LEDN_NCSI( x )                 ( x )

#if defined(ENABLE_BIG_ENDIAN_MEM)
  #define SWAP_4B_LEDN_MEM( x )                  ( SWAP_4B( x ) )
#else
  #define SWAP_4B_LEDN_MEM( x )                  ( x )
#endif
#if defined(ENABLE_BIG_ENDIAN_REG)
  #define SWAP_4B_LEDN_REG( x )                  ( SWAP_4B( x ) )
#else
  #define SWAP_4B_LEDN_REG( x )                  ( x )
#endif

#define DELAY( x )                       	udelay( ( x ) * 1000 )
#define GET_CAHR                         	getc

//---------------------------------------------------------
// Default argument
//---------------------------------------------------------
#define  DEF_GUSER_DEF_PACKET_VAL                0xaaaaaaaa     //0xff00ff00, 0xf0f0f0f0, 0xcccccccc, 0x55aa55aa, 0x5a5a5a5a, 0x66666666
#define  DEF_GIOTIMINGBUND                       2
#define  DEF_GPHY_ADR                            0
#define  DEF_GTESTMODE                           0              //[0]0: no burst mode, 1: 0xff, 2: 0x55, 3: random, 4: ARP, 5: ARP, 6: IO timing, 7: IO timing+IO Strength
#define  DEF_GLOOP_MAX                           1
#define  DEF_GCTRL                               0

#define  SET_1GBPS                               BIT(0)
#define  SET_100MBPS                             BIT(1)
#define  SET_10MBPS                              BIT(2)
#define  SET_1G_100M_10MBPS                      (SET_1GBPS | SET_100MBPS | SET_10MBPS)
#define  SET_100M_10MBPS                         (SET_100MBPS | SET_10MBPS)
#define  SET_1G_100MBPS		(SET_1GBPS | SET_100MBPS)
#define  SET_1G_10MBPS		(SET_1GBPS | SET_10MBPS)

#define  DEF_GSPEED                              SET_1G_100M_10MBPS
#define  DEF_GARPNUMCNT                          0

/* MAC information */

/* old interface - deprecated */
#define MDC_CYC_THLD				0x3f
#define MDIO_WR_CODE_OLD			BIT(27)
#define MDIO_RD_CODE_OLD			BIT(26)

#define MDIO_SET_PHY_ADDR_OLD(addr)		((addr) << 16)
#define MDIO_SET_REG_ADDR_OLD(reg)		((reg) << 21)

#ifdef CONFIG_ASPEED_AST2600
#define MDIO_FIRE_BUSY				BIT(31)
#define MDIO_WR_CODE				(MDIO_FIRE_BUSY | BIT(28) | (0x1 << 26))
#define MDIO_RD_CODE				(MDIO_FIRE_BUSY | BIT(28) | (0x2 << 26))
#define MDIO_SET_WR_DATA(x)			((x) << 0)
#define MDIO_GET_WR_DATA(x)			((x) & GENMASK(15, 0))
#define MDIO_SET_PHY_ADDR(addr)			((addr) << 21)
#define MDIO_SET_REG_ADDR(reg)			((reg) << 16)
#else
#define MDIO_FIRE_BUSY				BIT(15)
#define MDIO_WR_CODE				(MDIO_FIRE_BUSY | BIT(12) | (0x1 << 10))
#define MDIO_RD_CODE				(MDIO_FIRE_BUSY | BIT(12) | (0x2 << 10))
#define MDIO_SET_WR_DATA(x)			((x) << 16)
#define MDIO_GET_WR_DATA(x)			(((x) & GENMASK(31, 16)) >> 16)
#define MDIO_SET_PHY_ADDR(addr)			((addr) << 5)
#define MDIO_SET_REG_ADDR(reg)			((reg) << 0)
#endif

#define MAC_048_def				0x000002F1 /* default 0xf1 */

//---------------------------------------------------------
// Data information
//---------------------------------------------------------
#define ZeroCopy_OFFSET                    (( eng->run.tm_tx_only ) ? 0 : 2)

//      --------------------------------- DRAM_MapAdr            = tdes_base
//              | TX descriptor ring    |
//              ------------------------- DRAM_MapAdr + 0x040000 = rdes_base
//              | RX descriptor ring    |
//              -------------------------
//              | Reserved              |
//              -------------------------
//              | Reserved              |
//      --------------------------------- DRAM_MapAdr + 0x100000 = DMA_BASE    -------------------------
//              |   #1                  |  \                                   |     #1     Tx         |
//  DMA buffer  |                       |   DMA_BufSize                        |      LOOP = 0         |
// ( Tx/Rx )    -------------------------  /                                   --------------------------------------------------
//              |   #2                  |                                      |     #2     Rx         |  #2     Tx             |
//              |                       |                                      |      LOOP = 0         |   LOOP = 1             |
//              -------------------------                                      --------------------------------------------------
//              |   #3                  |                                                              |  #3     Rx             |
//              |                       |                                                              |   LOOP = 1             |
//              -------------------------                                                              -------------------------
//              |   #4                  |                                                                                     ..........
//              |                       |
//              -------------------------
//              |   #5                  |
//              |                       |
//              -------------------------
//              |   #6                  |
//              |                       |
//              -------------------------
//                           .
//                           .
//              -------------------------
//              |   #n, n = DMA_BufNum  |
//              |                       |
//      ---------------------------------

#define BUF_SIZE			0x04000000
#define TDES_SIZE			0x00040000
#define RDES_SIZE			0x00040000
#define RESV_SIZE			0x00000000		/* reserved */

#define TDES_IniVal (0xb0000000 + eng->dat.FRAME_LEN_Cur)
#define RDES_IniVal (0x00000fff)
#define EOR_IniVal (0x40000000)
#define HWOwnTx(dat) (dat & 0x80000000)
#define HWOwnRx(dat) ((dat & 0x80000000) == 0)
#define HWEOR(dat) (dat & 0x40000000)

#define AT_MEMRW_BUF(x) ((x) - ASPEED_DRAM_BASE)
#define AT_BUF_MEMRW(x) ((x) + ASPEED_DRAM_BASE)

//---------------------------------------------------------
// Error Flag Bits
//---------------------------------------------------------
#define Err_Flag_MACMode                              ( 1 <<  0 )   // MAC interface mode mismatch
#define Err_Flag_PHY_Type                             ( 1 <<  1 )   // Unidentifiable PHY
#define Err_Flag_MALLOC_FrmSize                       ( 1 <<  2 )   // Malloc fail at frame size buffer
#define Err_Flag_MALLOC_LastWP                        ( 1 <<  3 )   // Malloc fail at last WP buffer
#define Err_Flag_Check_Buf_Data                       ( 1 <<  4 )   // Received data mismatch
#define Err_Flag_Check_Des                            ( 1 <<  5 )   // Descriptor error
#define ERR_FLAG_NCSI_LINKFAIL			(1 << 6)	// NCSI packet retry number over flows
#define Err_Flag_NCSI_Check_TxOwnTimeOut              ( 1 <<  7 )   // Time out of checking Tx owner bit in NCSI packet
#define Err_Flag_NCSI_Check_RxOwnTimeOut              ( 1 <<  8 )   // Time out of checking Rx owner bit in NCSI packet
#define Err_Flag_NCSI_Check_ARPOwnTimeOut             ( 1 <<  9 )   // Time out of checking ARP owner bit in NCSI packet
#define Err_Flag_NCSI_No_PHY                          ( 1 << 10 )   // Can not find NCSI PHY
#define Err_Flag_NCSI_Channel_Num                     ( 1 << 11 )   // NCSI Channel Number Mismatch
#define Err_Flag_NCSI_Package_Num                     ( 1 << 12 )   // NCSI Package Number Mismatch
#define Err_Flag_PHY_TimeOut_RW                       ( 1 << 13 )   // Time out of read/write PHY register
#define Err_Flag_PHY_TimeOut_Rst                      ( 1 << 14 )   // Time out of reset PHY register
#define Err_Flag_RXBUF_UNAVA                          ( 1 << 15 )   // MAC00h[2]:Receiving buffer unavailable
#define Err_Flag_RPKT_LOST                            ( 1 << 16 )   // MAC00h[3]:Received packet lost due to RX FIFO full
#define Err_Flag_NPTXBUF_UNAVA                        ( 1 << 17 )   // MAC00h[6]:Normal priority transmit buffer unavailable
#define Err_Flag_TPKT_LOST                            ( 1 << 18 )   // MAC00h[7]:Packets transmitted to Ethernet lost
#define Err_Flag_DMABufNum                            ( 1 << 19 )   // DMA Buffer is not enough
#define Err_Flag_IOMargin                             ( 1 << 20 )   // IO timing margin is not enough
#define Err_Flag_IOMarginOUF                          ( 1 << 21 )   // IO timing testing out of boundary
#define Err_Flag_MHCLK_Ratio                          ( 1 << 22 )   // Error setting of MAC AHB bus clock (SCU08[18:16])

#define Wrn_Flag_IOMarginOUF                          ( 1 <<  0 )   // IO timing testing out of boundary
#define Wrn_Flag_RxErFloatting                        ( 1 <<  1 )   // NCSI RXER pin may be floatting to the MAC
//#define Wrn_Flag_RMIICK_IOMode                        ( 1 <<  2 )   // The PHY's RMII refreence clock input/output mode

#define PHY_Flag_RMIICK_IOMode_RTL8201E               ( 1 <<  0 )
#define PHY_Flag_RMIICK_IOMode_RTL8201F               ( 1 <<  1 )

#define Des_Flag_TxOwnTimeOut                         ( 1 <<  0 )   // Time out of checking Tx owner bit
#define Des_Flag_RxOwnTimeOut                         ( 1 <<  1 )   // Time out of checking Rx owner bit
#define Des_Flag_FrameLen                             ( 1 <<  2 )   // Frame length mismatch
#define Des_Flag_RxErr                                ( 1 <<  3 )   // Input signal RxErr
#define Des_Flag_CRC                                  ( 1 <<  4 )   // CRC error of frame
#define Des_Flag_FTL                                  ( 1 <<  5 )   // Frame too long
#define Des_Flag_Runt                                 ( 1 <<  6 )   // Runt packet
#define Des_Flag_OddNibble                            ( 1 <<  7 )   // Nibble bit happen
#define Des_Flag_RxFIFOFull                           ( 1 <<  8 )   // Rx FIFO full

#define NCSI_Flag_Get_Version_ID                      ( 1 <<  0 )   // Time out when Get Version ID
#define NCSI_Flag_Get_Capabilities                    ( 1 <<  1 )   // Time out when Get Capabilities
#define NCSI_Flag_Select_Active_Package               ( 1 <<  2 )   // Time out when Select Active Package
#define NCSI_Flag_Enable_Set_MAC_Address              ( 1 <<  3 )   // Time out when Enable Set MAC Address
#define NCSI_Flag_Enable_Broadcast_Filter             ( 1 <<  4 )   // Time out when Enable Broadcast Filter
#define NCSI_Flag_Enable_Network_TX                   ( 1 <<  5 )   // Time out when Enable Network TX
#define NCSI_Flag_Enable_Channel                      ( 1 <<  6 )   // Time out when Enable Channel
#define NCSI_Flag_Disable_Network_TX                  ( 1 <<  7 )   // Time out when Disable Network TX
#define NCSI_Flag_Disable_Channel                     ( 1 <<  8 )   // Time out when Disable Channel
#define NCSI_Flag_Select_Package                      ( 1 <<  9 )   // Time out when Select Package
#define NCSI_Flag_Deselect_Package                    ( 1 << 10 )   // Time out when Deselect Package
#define NCSI_Flag_Set_Link                            ( 1 << 11 )   // Time out when Set Link
#define NCSI_Flag_Get_Controller_Packet_Statistics    ( 1 << 12 )   // Time out when Get Controller Packet Statistics
#define NCSI_Flag_Reset_Channel                       ( 1 << 13 )   // Time out when Reset Channel

//---------------------------------------------------------
// DMA Buffer information
//---------------------------------------------------------
#define DMA_BUF_SIZE				(56 * 1024 * 1024)
extern uint8_t dma_buf[DMA_BUF_SIZE];

#define DMA_BASE				((uint32_t)(&dma_buf[0]))
/* The size of one LAN packet */
#define DMA_PakSize 				(2 * 1024)

#ifdef SelectSimpleBoundary
#define DMA_BufSize (((((p_eng->dat.Des_Num + 15) * DMA_PakSize) >> 2) << 2))
#else
#define DMA_BufSize                                                            \
	(4 + ((((p_eng->dat.Des_Num + 15) * DMA_PakSize) >> 2) << 2))
#endif
#define DMA_BufNum (DMA_BUF_SIZE / (p_eng->dat.DMABuf_Size))

/* get DMA buffer address according to the loop counter */
#define GET_DMA_BASE(p_eng, x)                                                 \
	(DMA_BASE + ((((x) % p_eng->dat.DMABuf_Num)) * p_eng->dat.DMABuf_Size))

#define SEED_START                               8
#define DATA_SEED(seed)                          ( ( seed ) | (( seed + 1 ) << 16 ) )
#define DATA_IncVal                              0x00020001
#define PktByteSize                              ( ( ( ( ZeroCopy_OFFSET + eng->dat.FRAME_LEN_Cur - 1 ) >> 2 ) + 1) << 2 )

//---------------------------------------------------------
// Delay (ms)
//---------------------------------------------------------
//#define Delay_DesGap                             1    //off
//#define Delay_CntMax                             40
//#define Delay_CntMax                             1000
//#define Delay_CntMax                             8465
//#define Delay_CntMaxIncVal                       50000
#define Delay_CntMaxIncVal                       47500


//#define Delay_ChkRxOwn                           1
//#define Delay_ChkTxOwn                           1

#define Delay_PHYRst                             100
//#define Delay_PHYRd                              5
#define Delay_PHYRd                              1         //20150423

#define Delay_MACRst                             1
#define Delay_MACDump                            1

//#define Delay_DES                                1

//---------------------------------------------------------
// Time Out
//---------------------------------------------------------
#define TIME_OUT_Des_1G                      10000     //400
#define TIME_OUT_Des_100M                    20000     //4000
#define TIME_OUT_Des_10M                     50000     //20000
#define TIME_OUT_NCSI                        100000    //40000
#define TIME_OUT_PHY_RW                      2000000   //100000
#define TIME_OUT_PHY_Rst                     20000     //1000

//---------------------------------------------------------
// Chip memory MAP
//---------------------------------------------------------
typedef union {
	uint32_t w;
	struct {
		uint32_t txdma_en		: 1;	/* bit[0] */
		uint32_t rxdma_en		: 1;	/* bit[1] */
		uint32_t txmac_en		: 1;	/* bit[2] */
		uint32_t rxmac_en		: 1;	/* bit[3] */
		uint32_t rm_vlan		: 1;	/* bit[4] */
		uint32_t hptxr_en		: 1;	/* bit[5] */
		uint32_t phy_link_sts_dtct	: 1;	/* bit[6] */
		uint32_t enrx_in_halftx		: 1;	/* bit[7] */
		uint32_t fulldup		: 1;	/* bit[8] */
		uint32_t gmac_mode		: 1;	/* bit[9] */
		uint32_t crc_apd		: 1;	/* bit[10] */
#ifdef CONFIG_ASPEED_AST2600
		uint32_t reserved_1		: 1;	/* bit[11] */
#else		
		uint32_t phy_link_lvl_dtct	: 1;	/* bit[11] */
#endif		
		uint32_t rx_runt		: 1;	/* bit[12] */
		uint32_t jumbo_lf		: 1;	/* bit[13] */
		uint32_t rx_alladr		: 1;	/* bit[14] */
		uint32_t rx_ht_en		: 1;	/* bit[15] */
		uint32_t rx_multipkt_en		: 1;	/* bit[16] */
		uint32_t rx_broadpkt_en		: 1;	/* bit[17] */
		uint32_t discard_crcerr		: 1;	/* bit[18] */
		uint32_t speed_100		: 1;	/* bit[19] */
		uint32_t reserved_0		: 11;	/* bit[30:20] */
		uint32_t sw_rst			: 1;	/* bit[31] */
	}b;
} mac_cr_t;
// ========================================================
// For ncsi.c

#define DEF_GPACKAGE2NUM                         1         // Default value
#define DEF_GCHANNEL2NUM                         1         // Default value

//---------------------------------------------------------
// Variable
//---------------------------------------------------------
//NC-SI Command Packet
typedef struct {
//Ethernet Header
	unsigned char        DA[6];                        // Destination Address
	unsigned char        SA[6];                        // Source Address
	uint16_t       EtherType;                    // DMTF NC-SI, it should be 0x88F8
//NC-SI Control Packet
	unsigned char        MC_ID;                        // Management Controller should set this field to 0x00
	unsigned char        Header_Revision;              // For NC-SI 1.0 spec, this field has to set 0x01
	unsigned char        Reserved_1;                   // Reserved has to set to 0x00
	unsigned char        IID;                          // Instance ID
	unsigned char        Command;
//	unsigned char        Channel_ID;
	unsigned char        ChID;
	uint16_t	Payload_Length;               // Payload Length = 12 bits, 4 bits are reserved
	uint32_t	Reserved_2;
	uint32_t	Reserved_3;

	uint16_t	Reserved_4;
	uint16_t	Reserved_5;
	uint16_t	Response_Code;
	uint16_t       Reason_Code;
	unsigned char        Payload_Data[64];
#if !defined(SLT_UBOOT)
}  NCSI_Command_Packet;
#else
}  __attribute__ ((__packed__)) NCSI_Command_Packet;
#endif

//NC-SI Response Packet
typedef struct {
	unsigned char        DA[6];
	unsigned char        SA[6];
	uint16_t       EtherType;                    //DMTF NC-SI
//NC-SI Control Packet
	unsigned char        MC_ID;                        //Management Controller should set this field to 0x00
	unsigned char        Header_Revision;              //For NC-SI 1.0 spec, this field has to set 0x01
	unsigned char        Reserved_1;                   //Reserved has to set to 0x00
	unsigned char        IID;                          //Instance ID
	unsigned char        Command;
//	unsigned char        Channel_ID;
	unsigned char        ChID;
	uint16_t       Payload_Length;               //Payload Length = 12 bits, 4 bits are reserved
	uint16_t       Reserved_2;
	uint16_t       Reserved_3;
	uint16_t       Reserved_4;
	uint16_t       Reserved_5;

	uint16_t       Response_Code;
	uint16_t       Reason_Code;
	unsigned char        Payload_Data[64];
#if !defined(SLT_UBOOT)
}  NCSI_Response_Packet;
#else
}  __attribute__ ((__packed__)) NCSI_Response_Packet;
#endif

typedef struct {
	unsigned char        All_ID                                   ;
	unsigned char        Package_ID                               ;
	unsigned char        Channel_ID                               ;
	uint32_t Capabilities_Flags                       ;
	uint32_t Broadcast_Packet_Filter_Capabilities     ;
	uint32_t Multicast_Packet_Filter_Capabilities     ;
	uint32_t Buffering_Capabilities                   ;
	uint32_t AEN_Control_Support                      ;
	unsigned char        VLAN_Filter_Count                        ;
	unsigned char        Mixed_Filter_Count                       ;
	unsigned char        Multicast_Filter_Count                   ;
	unsigned char        Unicast_Filter_Count                     ;
	unsigned char        VLAN_Mode_Support                        ;
	unsigned char        Channel_Count                            ;
	uint32_t PCI_DID_VID                              ;
	uint32_t manufacturer_id                           ;
} NCSI_Capability;

typedef struct {
	mac_cr_t maccr;
	uint32_t mac_madr;
	uint32_t mac_ladr;
	uint32_t mac_fear;

	uint32_t WDT_00c                       ;
	uint32_t WDT_02c                       ;
	uint32_t WDT_04c                       ;

	int8_t                 SCU_oldvld;
} mac_reg_t;
typedef struct {
	uint8_t ast2600;
	uint8_t ast2500;
	uint8_t mac_num;
	uint8_t is_new_mdio_reg[4];

	uint8_t is_1g_valid[4];
	uint8_t at_least_1g_valid;
	uint8_t MHCLK_Ratio;
} mac_env_t;

typedef union {
	uint32_t w;
	struct {
		uint32_t skip_phy_init	: 1;	/* bit[0] */
		uint32_t skip_phy_deinit: 1;	/* bit[1] */
		uint32_t skip_phy_id_check	: 1;	/* bit[2] */
		uint32_t reserved_0	: 1;	/* bit[3] */
		uint32_t phy_int_loopback : 1;	/* bit[4] */
		uint32_t mac_int_loopback : 1;	/* bit[5] */
		uint32_t phy_rx_delay_en: 1;	/* bit[6] */
		uint32_t phy_tx_delay_en: 1;	/* bit[7] */
		uint32_t rmii_50m_out	: 1;	/* bit[8] */
		uint32_t rmii_phy_in	: 1;	/* bit[9] */
		uint32_t inv_rgmii_rxclk: 1;	/* bit[10] */
		uint32_t reserved_2	: 1;	/* bit[11] */
		uint32_t single_packet	: 1;	/* bit[12] */
		uint32_t full_range	: 1;	/* bit[13] */
		uint32_t reserved_3	: 2;	/* bit[15:14] */
		uint32_t print_ncsi	: 1;	/* bit[16] */
		uint32_t skip_rx_err	: 1;	/* bit[17] */
	} b;
} mac_arg_ctrl_t;
typedef struct {
	uint32_t run_mode;		/* select dedicated or NCSI */
	uint32_t mac_idx;		/* argv[1] */
	uint32_t mdio_idx;
	uint32_t run_speed;		/* argv[2] for dedicated */
	mac_arg_ctrl_t ctrl;		/* argv[3] for dedicated 
					   argv[6] for ncsi */
	uint32_t loop_max;		/* argv[4] for dedicated */
	uint32_t loop_inf;		/* argv[4] for dedicated */

	uint32_t GPackageTolNum;	/* argv[2] for ncsi */
	uint32_t GChannelTolNum;	/* argv[3] for ncsi */
	
	uint32_t test_mode;		/* argv[5] for dedicated
					   argv[4] for ncsi */

	uint32_t phy_addr;		/* argv[6] for dedicated */
	uint32_t delay_scan_range;	/* argv[7] for dedicated
					   argv[5] for ncsi */	
	uint32_t ieee_sel;		/* argv[7] for dedicated */

	uint32_t GARPNumCnt;		/* argv[7] for ncsi */
	uint32_t user_def_val;		/* argv[8] for dedicated */
} mac_arg_t;
typedef struct {
	uint32_t mac_idx;
	uint32_t mac_base;
	uint32_t mdio_idx;
	uint32_t mdio_base;
	uint32_t is_rgmii;
	uint32_t ieee_sel;		/* derived from delay_scan_range */

	uint32_t tdes_base;
	uint32_t rdes_base;

	uint32_t ncsi_tdes_base;
	uint32_t ncsi_rdes_base;

	uint32_t LOOP_CheckNum                 ;
	uint32_t CheckBuf_MBSize               ;
	uint32_t timeout_th;	/* time out threshold (varies with run-speed) */

	uint32_t loop_max;
	uint32_t loop_of_cnt;
	uint32_t loop_cnt;
	uint32_t speed_idx;
	int                  NCSI_RxTimeOutScale           ;

	uint8_t speed_cfg[3];
	uint8_t speed_sel[3];

	/* test mode */
	uint8_t delay_margin;
	uint8_t tm_tx_only;
	int8_t                 TM_IEEE                       ;//test_mode
	int8_t                 TM_IOTiming                   ;//test_mode
	int8_t                 TM_IOStrength                 ;//test_mode
	int8_t                 TM_TxDataEn                   ;//test_mode
	int8_t                 TM_RxDataEn                   ;//test_mode
	int8_t                 TM_WaitStart                  ;//test_mode
	int8_t                 TM_DefaultPHY                 ;//test_mode
	int8_t                 TM_NCSI_DiSChannel            ;//test_mode

	int8_t                 IO_MrgChk                     ;


} MAC_Running;

typedef struct {
	uint8_t SA[6];
} MAC_Information;

typedef struct {
	uint32_t mdio_base;
	uint32_t loopback;
	uint8_t phy_name[64];
	int8_t                 default_phy                   ;
	int8_t                 Adr                           ;

	uint16_t id2                       ;
	uint16_t id1                       ;

	uint32_t PHY_00h                       ;
	uint32_t PHY_06h                       ;
	uint32_t PHY_09h                       ;
	uint32_t PHY_0eh                       ;
	uint32_t PHY_10h                       ;
	uint32_t PHY_11h                       ;
	uint32_t PHY_12h                       ;
	uint32_t PHY_14h                       ;
	uint32_t PHY_15h                       ;
	uint32_t PHY_18h                       ;
	uint32_t PHY_19h                       ;
	uint32_t PHY_1ch                       ;
	uint32_t PHY_1eh                       ;
	uint32_t PHY_1fh                       ;
	uint32_t PHY_06hA[7]                   ;

	uint32_t RMIICK_IOMode                 ;
} MAC_PHY;

#ifdef CONFIG_ASPEED_AST2600
typedef union {
	uint32_t w;
	struct {
		uint32_t tx_delay_1		: 6;	/* bit[5:0] */
		uint32_t tx_delay_2		: 6;	/* bit[11:6] */
		uint32_t rx_delay_1		: 6;	/* bit[17:12] */
		uint32_t rx_delay_2		: 6;	/* bit[23:18] */
		uint32_t rx_clk_inv_1 		: 1;	/* bit[24] */
		uint32_t rx_clk_inv_2 		: 1;	/* bit[25] */
		uint32_t rmii_tx_data_at_falling_1 : 1; /* bit[26] */
		uint32_t rmii_tx_data_at_falling_2 : 1; /* bit[27] */
		uint32_t rgmiick_pad_dir	: 1;	/* bit[28] */
		uint32_t rmii_50m_oe_1 		: 1;	/* bit[29] */
		uint32_t rmii_50m_oe_2		: 1;	/* bit[30] */
		uint32_t rgmii_125m_o_sel 	: 1;	/* bit[31] */
	}b;
} mac_delay_1g_t;

typedef union {
	uint32_t w;
	struct {
		uint32_t tx_delay_1		: 6;	/* bit[5:0] */
		uint32_t tx_delay_2		: 6;	/* bit[11:6] */
		uint32_t rx_delay_1		: 6;	/* bit[17:12] */
		uint32_t rx_delay_2		: 6;	/* bit[23:18] */
		uint32_t rx_clk_inv_1 		: 1;	/* bit[24] */
		uint32_t rx_clk_inv_2 		: 1;	/* bit[25] */
		uint32_t reserved_0 		: 6;	/* bit[31:26] */
	}b;
} mac_delay_100_10_t;
#else
typedef union {
	uint32_t w;
	struct {
		uint32_t tx_delay_1		: 6;	/* bit[5:0] */
		uint32_t tx_delay_2		: 6;	/* bit[11:6] */
		uint32_t rx_delay_1		: 6;	/* bit[17:12] */
		uint32_t rx_delay_2		: 6;	/* bit[23:18] */		
		uint32_t rmii_tx_data_at_falling_1 : 1; /* bit[24] */
		uint32_t rmii_tx_data_at_falling_2 : 1; /* bit[25] */
		uint32_t reserved		: 3;	/* bit[28:26] */
		uint32_t rmii_50m_oe_1 		: 1;	/* bit[29] */
		uint32_t rmii_50m_oe_2		: 1;	/* bit[30] */
		uint32_t rgmii_125m_o_sel 	: 1;	/* bit[31] */
	}b;
} mac_delay_1g_t;

typedef union {
	uint32_t w;
	struct {
		uint32_t tx_delay_1		: 6;	/* bit[5:0] */
		uint32_t tx_delay_2		: 6;	/* bit[11:6] */
		uint32_t rx_delay_1		: 6;	/* bit[17:12] */
		uint32_t rx_delay_2		: 6;	/* bit[23:18] */
		uint32_t enable 		: 1;	/* bit[24] */
		uint32_t reserved_0 		: 7;	/* bit[31:25] */
	}b;
} mac_delay_100_10_t;
#endif

typedef struct mac_delay_1g_reg_s {
	uint32_t addr;
	int32_t tx_min;
	int32_t tx_max;
	int32_t rx_min;
	int32_t rx_max;
	int32_t rmii_tx_min;
	int32_t rmii_tx_max;
	int32_t rmii_rx_min;
	int32_t rmii_rx_max;
	mac_delay_1g_t value;	/* backup register value */
} mac_delay_1g_reg_t;

typedef struct mac_delay_100_10_reg_s {
	uint32_t addr;
	int32_t tx_min;
	int32_t tx_max;
	int32_t rx_min;
	int32_t rx_max;
	mac_delay_100_10_t value;
} mac_delay_100_10_reg_t;

#ifdef CONFIG_ASPEED_AST2600
typedef union {
	uint32_t w;
	struct {
		uint32_t mac3_tx_drv		: 2;	/* bit[1:0] */
		uint32_t mac4_tx_drv		: 2;	/* bit[3:2] */
		uint32_t reserved_0		: 28;	/* bit[31:4] */
	}b;
} mac34_drv_t;
typedef struct mac34_drv_reg_s {
	uint32_t addr;
	uint32_t drv_max;
	mac34_drv_t value;
} mac34_drv_reg_t;

#else
typedef union {
	uint32_t w;
	struct {
		uint32_t reserved_0		: 8;	/* bit[7:0] */
		uint32_t mac1_rmii_tx_drv	: 1;	/* bit[8] */
		uint32_t mac1_rgmii_tx_drv	: 1;	/* bit[9] */
		uint32_t mac2_rmii_tx_drv	: 1;	/* bit[10] */
		uint32_t mac2_rgmii_tx_drv	: 1;	/* bit[11] */
		uint32_t reserved_1		: 20;	/* bit[31:12] */
	}b;
} mac12_drv_t;

typedef struct mac12_drv_reg_s {
	uint32_t addr;
	uint32_t drv_max;
	mac12_drv_t value;
} mac12_drv_reg_t;
#endif

typedef struct delay_scan_s {
	int8_t begin;
	int8_t end;
	int8_t step;
	int8_t orig;
} delay_scan_t;
typedef struct {
	/* driving strength */
#ifdef CONFIG_ASPEED_AST2600
	mac34_drv_reg_t mac34_drv_reg;
#else	
	mac12_drv_reg_t mac12_drv_reg;
#endif
	uint32_t drv_upper_bond;
	uint32_t drv_lower_bond;
	uint32_t drv_curr;

	mac_delay_1g_reg_t mac12_1g_delay;
	mac_delay_1g_reg_t mac34_1g_delay;
	mac_delay_100_10_reg_t mac12_100m_delay;
	mac_delay_100_10_reg_t mac34_100m_delay;
	mac_delay_100_10_reg_t mac12_10m_delay;
	mac_delay_100_10_reg_t mac34_10m_delay;

	delay_scan_t tx_delay_scan;
	delay_scan_t rx_delay_scan;	

	uint8_t                 Dly_in_reg_idx;
	int8_t                Dly_in_min                    ;
	uint8_t                 Dly_in_max                    ;
	uint8_t                 Dly_out_reg_idx               ;
	int8_t                Dly_out_min                   ;
	uint8_t                 Dly_out_max                   ;	
	
	uint8_t                 Dly_in                        ;
	uint8_t                 Dly_in_selval                 ;
	uint8_t                 Dly_out                       ;
	uint8_t                 Dly_out_selval                ;
	int8_t result;
	int8_t result_history[128][64];
	uint32_t init_done;
} MAC_IO;
typedef struct {
#ifdef Enable_ShowBW
	double               Total_frame_len               ;//__attribute__ ((aligned (8)));
#endif
	uint32_t Des_Num                       ;
	uint32_t Des_Num_Org                   ;
	uint32_t DMABuf_Size                   ;
	uint32_t DMABuf_Num                    ;

	uint32_t *FRAME_LEN                    ;
	uint32_t FRAME_LEN_Cur                 ;
	uint32_t *wp_lst                       ;
	uint32_t wp_fir                        ;

	uint32_t DMA_Base_Setup                 ;
	uint32_t DMA_Base_Tx                  ;
	uint32_t DMA_Base_Rx                   ;

	uint32_t ARP_data[16]                  ;
	uint32_t TxDes0DW                      ;
	uint32_t RxDes0DW                      ;
	uint32_t RxDes3DW                      ;

	uint8_t                 number_chl                    ;
	uint8_t                 number_pak                    ;
	char                 NCSI_RxEr                     ;
	uint32_t NCSI_TxDWBUF[512]             ;
	uint32_t NCSI_RxDWBUF[512]             ;
	char                 NCSI_CommandStr[512]          ;
	unsigned char        *NCSI_TxByteBUF               ;
	unsigned char        *NCSI_RxByteBUF               ;
	unsigned char        NCSI_Payload_Data[16]         ;
	uint32_t Payload_Checksum_NCSI         ;
} MAC_Data;

typedef struct {
	uint32_t warn;
	uint32_t error;
	uint32_t desc;
	uint32_t ncsi;
	uint32_t error_backup;
	uint32_t ncsi_backup;
	uint32_t n_desc_fail;
	uint8_t print_en;
	uint8_t all_fail;
} MAC_Flag;

typedef struct {
	mac_reg_t reg;
	mac_env_t env;
	mac_arg_t arg;	
	MAC_Running          run;
	MAC_Information      inf;
	MAC_PHY              phy;
	MAC_IO               io;
	MAC_Data             dat;
	MAC_Flag             flg;
	NCSI_Command_Packet  ncsi_req;
	NCSI_Response_Packet ncsi_rsp;
	NCSI_Capability      ncsi_cap;	
} MAC_ENGINE;
typedef void (* PHY_SETTING) (MAC_ENGINE *);
typedef struct {
	PHY_SETTING          fp_set;
	PHY_SETTING          fp_clr;
} PHY_ENGINE;

#undef GLOBAL
#ifdef NCSI_C
#define GLOBAL
#else
#define GLOBAL    extern
#endif

GLOBAL  char phy_ncsi (MAC_ENGINE *eng);

// ========================================================
// For mactest

#undef GLOBAL
#ifdef MACTEST_C
#define GLOBAL
#else
#define GLOBAL    extern
#endif

#define MODE_DEDICATED                           0x01
#define MODE_NCSI                                0x02

GLOBAL  uint8_t            *mmiobase;
GLOBAL  uint32_t ulPCIBaseAddress;
GLOBAL  uint32_t ulMMIOBaseAddress;

GLOBAL  uint8_t             display_lantest_log_msg;

// ========================================================
// For mac.c
#undef GLOBAL
#ifdef MAC_C
#define GLOBAL
#else
#define GLOBAL    extern
#endif

#if defined(MAC_C)
static  const  char version_name[] = VER_NAME;
static  const  uint8_t IOValue_Array_A0[16] = {8,1, 10,3, 12,5, 14,7, 0,9, 2,11, 4,13, 6,15}; // AST2300-A0
#endif

GLOBAL void    debug_pause (void);
GLOBAL uint32_t Read_Mem_Dat_NCSI_DD (uint32_t addr);
GLOBAL uint32_t Read_Mem_Des_NCSI_DD (uint32_t addr);







GLOBAL void Write_Mem_Dat_NCSI_DD (uint32_t addr, uint32_t data);
GLOBAL void Write_Mem_Des_NCSI_DD (uint32_t addr, uint32_t data);


GLOBAL void Write_Reg_TIMER_DD (uint32_t addr, uint32_t data);






GLOBAL void    PrintTest (MAC_ENGINE *eng);
GLOBAL void    PrintIOTimingBund (MAC_ENGINE *eng);



GLOBAL void    PrintPHYAdr (MAC_ENGINE *eng);





GLOBAL void    setup_arp (MAC_ENGINE *eng);
GLOBAL void    TestingSetup (MAC_ENGINE *eng);



GLOBAL void    init_mac (MAC_ENGINE *eng);
GLOBAL char TestingLoop (MAC_ENGINE *eng, uint32_t loop_checknum);

GLOBAL void    init_phy (MAC_ENGINE *eng, PHY_ENGINE *phyeng);


GLOBAL void    phy_select (MAC_ENGINE *eng, PHY_ENGINE *phyeng);
GLOBAL void    recov_phy (MAC_ENGINE *eng, PHY_ENGINE *phyeng);
GLOBAL int     FindErr (MAC_ENGINE *eng, int value);
GLOBAL int     FindErr_Des (MAC_ENGINE *eng, int value);
GLOBAL void    PrintIO_Header (MAC_ENGINE *eng, uint8_t option);



GLOBAL void    FPri_ErrFlag (MAC_ENGINE *eng, uint8_t option);

GLOBAL void init_hwtimer( void );
GLOBAL void delay_hwtimer(uint16_t msec);

// ========================================================
// For PHYGPIO.c
#undef GLOBAL
#ifdef PHYGPIO_C
#define GLOBAL
#else
#define GLOBAL    extern
#endif


// ========================================================
// For PHYSPECIAL.c
#undef GLOBAL
#ifdef PHYMISC_C
#define GLOBAL
#else
#define GLOBAL    extern
#endif


#endif // End COMMINF_H
