// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) ASPEED Technology Inc.
 */

#define PHY_C
//#define PHY_DEBUG
//#define PHY_DEBUG_SET_CLR

#ifdef PHY_DEBUG
#undef DbgPrn_PHYRW
#define DbgPrn_PHYRW		1
#endif

#ifdef PHY_DEBUG_SET_CLR
#undef DbgPrn_PHYRW
#define DbgPrn_PHYRW		1
#endif


#include "comminf.h"
#include "swfunc.h"

#include <command.h>
#include <common.h>

#include "phy.h"

#include "phy_tbl.h"
#include "mem_io.h"

//#define RTK_DEBUG
#define RTK_DBG_GPIO		BIT(22)
#ifdef RTK_DEBUG
#define RTK_DBG_PRINTF		printf
#else
#define RTK_DBG_PRINTF(...)
#endif

static void rtk_dbg_gpio_set(void)
{
#ifdef RTK_DEBUG		
	GPIO_WR(GPIO_RD(0x20) | RTK_DBG_GPIO, 0x20);
#endif	
}

static void rtk_dbg_gpio_clr(void)
{
#ifdef RTK_DEBUG		
	GPIO_WR(GPIO_RD(0x20) & ~RTK_DBG_GPIO, 0x20);
#endif	
}

static void rtk_dbg_gpio_init(void)
{
#ifdef RTK_DEBUG
	GPIO_WR(GPIO_RD(0x24) | RTK_DBG_GPIO, 0x24);

	rtk_dbg_gpio_set();
#endif	
}

//------------------------------------------------------------
// PHY R/W basic
//------------------------------------------------------------
void phy_write (MAC_ENGINE *eng, int index, uint32_t data) 
{
	u32 wr_data;
	int timeout = 0;

	if (eng->env.is_new_mdio_reg[eng->run.mdio_idx]) {
		wr_data = MDIO_WR_CODE | MDIO_SET_WR_DATA(data) |
			  MDIO_SET_PHY_ADDR(eng->phy.Adr) |
			  MDIO_SET_REG_ADDR(index);
		writel(wr_data, eng->run.mdio_base);
		/* check time-out */
		while (readl(eng->run.mdio_base) & MDIO_FIRE_BUSY) {
			if (++timeout > TIME_OUT_PHY_RW) {
				if (!eng->run.tm_tx_only)
					PRINTF(FP_LOG,
					       "[PHY-Write] Time out: %08x\n",
					       readl(eng->run.mdio_base));

				FindErr(eng, Err_Flag_PHY_TimeOut_RW);
				break;
			}
		}
	} else {
		writel(data, eng->run.mdio_base + 0x4);
		writel(MDC_CYC_THLD | MDIO_WR_CODE_OLD |
			       MDIO_SET_PHY_ADDR_OLD(eng->phy.Adr) |
			       MDIO_SET_REG_ADDR_OLD(index),
		       eng->run.mdio_base);

		while (readl(eng->run.mdio_base) & MDIO_WR_CODE_OLD) {
			if (++timeout > TIME_OUT_PHY_RW) {
				if (!eng->run.tm_tx_only)
					PRINTF(FP_LOG,
					       "[PHY-Write] Time out: %08x\n",
					       readl(eng->run.mdio_base));

				FindErr(eng, Err_Flag_PHY_TimeOut_RW);
				break;
			}
		}
	} // End if (eng->env.new_mdio_reg)

	if (DbgPrn_PHYRW) {
		printf("[Wr ]%02d: 0x%04x (%02d:%08x)\n", index, data,
		       eng->phy.Adr, eng->run.mdio_base);
		if (!eng->run.tm_tx_only)
			PRINTF(FP_LOG, "[Wr ]%02d: 0x%04x (%02d:%08x)\n", index,
			       data, eng->phy.Adr, eng->run.mdio_base);
	}

} // End void phy_write (int adr, uint32_t data)

//------------------------------------------------------------
uint16_t phy_read (MAC_ENGINE *eng, int index) 
{
	uint32_t read_value;
	int timeout = 0;

	if (index > 0x1f) {
		printf("invalid PHY register index: 0x%02x\n", index);
		FindErr(eng, Err_Flag_PHY_TimeOut_RW);
		return 0;
	}

	if (eng->env.is_new_mdio_reg[eng->run.mdio_idx]) {
		writel(MDIO_RD_CODE | MDIO_SET_PHY_ADDR(eng->phy.Adr) |
			       MDIO_SET_REG_ADDR(index),
		       eng->run.mdio_base);

		while (readl(eng->run.mdio_base) & MDIO_FIRE_BUSY) {
			if (++timeout > TIME_OUT_PHY_RW) {
				if (!eng->run.tm_tx_only)
					PRINTF(FP_LOG,
					       "[PHY-Read] Time out: %08x\n",
					       readl(eng->run.mdio_base));

				FindErr(eng, Err_Flag_PHY_TimeOut_RW);
				break;
			}
		}

#ifdef Delay_PHYRd
		DELAY(Delay_PHYRd);
#endif
		read_value = readl(eng->run.mdio_base + 0x4) & GENMASK(15, 0);
	} else {
		writel(MDC_CYC_THLD | MDIO_RD_CODE_OLD |
			       MDIO_SET_PHY_ADDR_OLD(eng->phy.Adr) |
			       MDIO_SET_REG_ADDR_OLD(index),
		       eng->run.mdio_base);

		while (readl(eng->run.mdio_base) & MDIO_RD_CODE_OLD) {
			if (++timeout > TIME_OUT_PHY_RW) {
				if (!eng->run.tm_tx_only)
					PRINTF(FP_LOG,
					       "[PHY-Read] Time out: %08x\n",
					       readl(eng->run.mdio_base));

				FindErr(eng, Err_Flag_PHY_TimeOut_RW);
				break;
			}
		}

#ifdef Delay_PHYRd
		DELAY(Delay_PHYRd);
#endif
		read_value = readl(eng->run.mdio_base + 0x4) >> 16;
	}


	if (DbgPrn_PHYRW) {
		printf("[Rd ]%02d: 0x%04x (%02d:%08x)\n", index, read_value,
		       eng->phy.Adr, eng->run.mdio_base);
		if (!eng->run.tm_tx_only)
			PRINTF(FP_LOG, "[Rd ]%02d: 0x%04x (%02d:%08x)\n", index,
			       read_value, eng->phy.Adr, eng->run.mdio_base);
	}

	return (read_value);
} // End uint16_t phy_read (MAC_ENGINE *eng, int adr)

//------------------------------------------------------------
void phy_clrset(MAC_ENGINE *eng, int adr, uint32_t clr_mask, uint32_t set_mask)
{
	if (DbgPrn_PHYRW) {
		printf("[RW ]%02d: clr:0x%04x: set:0x%04x (%02d:%08x)\n", adr,
		       clr_mask, set_mask, eng->phy.Adr, eng->run.mdio_base);
		if (!eng->run.tm_tx_only)
			PRINTF(FP_LOG,
			       "[RW ]%02d: clr:0x%04x: set:0x%04x (%02d:%08x)\n",
			       adr, clr_mask, set_mask, eng->phy.Adr,
			       eng->run.mdio_base);
	}
	phy_write(eng, adr, ((phy_read(eng, adr) & (~clr_mask)) | set_mask));
}

//------------------------------------------------------------
void phy_dump(MAC_ENGINE *eng)
{
	int index;

	printf("[PHY%d][%d]----------------\n", eng->run.mac_idx + 1,
	       eng->phy.Adr);
	for (index = 0; index < 32; index++) {
		printf("%02d: %04x ", index, phy_read(eng, index));

		if ((index % 8) == 7)
			printf("\n");
	}
}

//------------------------------------------------------------
static void phy_scan_id(MAC_ENGINE *eng, uint8_t option)
{
	int8_t phy_addr_orig;

	phy_addr_orig = eng->phy.Adr;
	for (eng->phy.Adr = 0; eng->phy.Adr < 32; eng->phy.Adr++) {
		PRINTF(option, "[%02d] ", eng->phy.Adr);
		PRINTF(option, "%d:%04x ", 2, phy_read(eng, 2));
		PRINTF(option, "%d:%04x ", 3, phy_read(eng, 3));

		if ((eng->phy.Adr % 4) == 3)
			PRINTF(option, "\n");
	}
	eng->phy.Adr = phy_addr_orig;
}

//------------------------------------------------------------
void phy_delay (int dt) 
{
	rtk_dbg_gpio_clr();

#ifdef PHY_DEBUG
        printf("delay %d ms\n", dt);
#endif
        DELAY(dt);
	rtk_dbg_gpio_set();
}

//------------------------------------------------------------
// PHY IC basic
//------------------------------------------------------------
void phy_basic_setting(MAC_ENGINE *eng)
{
	uint32_t clr = GENMASK(14, 10) | BIT(6);
	
	phy_clrset(eng, 0, clr, eng->phy.PHY_00h);
	if (DbgPrn_PHYRW) {
		printf("[Set]00: 0x%04x (%02d:%08x)\n",
		       phy_read(eng, PHY_REG_BMCR), eng->phy.Adr,
		       eng->run.mdio_base);
		if (!eng->run.tm_tx_only)
			PRINTF(FP_LOG, "[Set]00: 0x%04x (%02d:%08x)\n",
			       phy_read(eng, PHY_REG_BMCR), eng->phy.Adr,
			       eng->run.mdio_base);
	}
}

//------------------------------------------------------------
void phy_wait_reset_done(MAC_ENGINE *eng)
{
	int timeout = 0;

	while (phy_read(eng, PHY_REG_BMCR) & 0x8000) {
		if (++timeout > TIME_OUT_PHY_Rst) {
			if (!eng->run.tm_tx_only)
				PRINTF(FP_LOG, "[PHY-Reset] Time out: %08x\n",
				       readl(eng->run.mdio_base));

			FindErr(eng, Err_Flag_PHY_TimeOut_Rst);
			break;
		}
	} //wait Rst Done

	if (DbgPrn_PHYRW) {
		printf("[Clr]00: 0x%04x (%02d:%08x)\n",
		       phy_read(eng, PHY_REG_BMCR), eng->phy.Adr,
		       eng->run.mdio_base);
		if (!eng->run.tm_tx_only)
			PRINTF(FP_LOG, "[Clr]00: 0x%04x (%02d:%08x)\n",
			       phy_read(eng, PHY_REG_BMCR), eng->phy.Adr,
			       eng->run.mdio_base);
	}
#ifdef Delay_PHYRst
	DELAY(Delay_PHYRst);
#endif
}

//------------------------------------------------------------
static void phy_reset(MAC_ENGINE *eng)
{
	phy_basic_setting(eng);

	//phy_clrset(eng, 0, 0x0000, 0x8000 | eng->phy.PHY_00h);
	phy_clrset(eng, 0, 0x7140, 0x8000 | eng->phy.PHY_00h);
	//phy_write(eng, 0, 0x8000); //clr set//Rst PHY
	phy_wait_reset_done(eng);

	phy_basic_setting(eng);
#ifdef Delay_PHYRst
	DELAY(Delay_PHYRst);
#endif
}

//------------------------------------------------------------
void phy_check_register (MAC_ENGINE *eng, uint32_t adr, uint32_t check_mask, uint32_t check_value, uint32_t hit_number, char *runname) {
        uint16_t     wait_phy_ready = 0;
        uint16_t     hit_count = 0;

        while ( wait_phy_ready < 1000 ) {
                if ( (phy_read( eng, adr ) & check_mask) == check_value ) {
                        if ( ++hit_count >= hit_number ) {
                                break;
                        }
                        else {
                                phy_delay(1);
                        }
                } else {
                        hit_count = 0;
                        wait_phy_ready++;
                        phy_delay(10);
                }
        }
        if ( hit_count < hit_number ) {
                printf("Timeout: %s\n", runname);
        }
}

//------------------------------------------------------------
// PHY IC
//------------------------------------------------------------
void recov_phy_marvell (MAC_ENGINE *eng) {//88E1111
        if ( eng->run.tm_tx_only ) {
        }
        else if ( eng->phy.loopback ) {
        }
        else {
		if (eng->run.speed_sel[0]) {
			phy_write(eng, 9, eng->phy.PHY_09h);

			phy_reset(eng);

			phy_write(eng, 29, 0x0007);
			phy_clrset(eng, 30, 0x0008, 0x0000); //clr set
			phy_write(eng, 29, 0x0010);
			phy_clrset(eng, 30, 0x0002, 0x0000); //clr set
			phy_write(eng, 29, 0x0012);
			phy_clrset(eng, 30, 0x0001, 0x0000); //clr set

			phy_write(eng, 18, eng->phy.PHY_12h);
		}
	}
}

//------------------------------------------------------------
void phy_marvell (MAC_ENGINE *eng) 
{//88E1111
        if ( eng->run.tm_tx_only ) {
                phy_reset( eng );
        }
        else if ( eng->phy.loopback ) {
                phy_reset( eng );
        }
        else {
                if ( eng->run.speed_sel[ 0 ] ) {
                        eng->phy.PHY_09h = phy_read( eng, PHY_GBCR );
                        eng->phy.PHY_12h = phy_read( eng, PHY_INER );
                        phy_write( eng, 18, 0x0000 );
                        phy_clrset( eng,  9, 0x0000, 0x1800 );//clr set
                }

                phy_reset( eng );

                if ( eng->run.speed_sel[ 0 ] ) {
                        phy_write( eng, 29, 0x0007 );
                        phy_clrset( eng, 30, 0x0000, 0x0008 );//clr set
                        phy_write( eng, 29, 0x0010 );
                        phy_clrset( eng, 30, 0x0000, 0x0002 );//clr set
                        phy_write( eng, 29, 0x0012 );
                        phy_clrset( eng, 30, 0x0000, 0x0001 );//clr set
                }
        }

        if ( !eng->phy.loopback )
                phy_check_register ( eng, 17, 0x0400, 0x0400, 1, "wait 88E1111 link-up");
//      Retry = 0;
//      do {
//              eng->phy.PHY_11h = phy_read( eng, PHY_SR );
//      } while ( !( ( eng->phy.PHY_11h & 0x0400 ) | eng->phy.loopback | ( Retry++ > 20 ) ) );
}

//------------------------------------------------------------
void recov_phy_marvell0 (MAC_ENGINE *eng) {//88E1310
        if ( eng->run.tm_tx_only ) {
        }
        else if ( eng->phy.loopback ) {
        }
        else {
                if ( eng->run.speed_sel[ 0 ] ) {
                        phy_write( eng, 22, 0x0006 );
                        phy_clrset( eng, 16, 0x0020, 0x0000 );//clr set
                        phy_write( eng, 22, 0x0000 );
                }
        }
}

//------------------------------------------------------------
void phy_marvell0 (MAC_ENGINE *eng) {//88E1310
//      int        Retry;

        phy_write( eng, 22, 0x0002 );

        eng->phy.PHY_15h = phy_read( eng, 21 );
        if ( eng->phy.PHY_15h & 0x0030 ) {
                printf("\n\n[Warning] Page2, Register 21, bit 4~5 must be 0 [Reg15_2:%04x]\n\n", eng->phy.PHY_15h);
                if ( eng->run.TM_IOTiming ) PRINTF( FP_IO, "\n\n[Warning] Page2, Register 21, bit 4~5 must be 0 [Reg15_2:%04x]\n\n", eng->phy.PHY_15h );
                if ( !eng->run.tm_tx_only ) PRINTF( FP_LOG, "\n\n[Warning] Page2, Register 21, bit 4~5 must be 0 [Reg15_2:%04x]\n\n", eng->phy.PHY_15h );

                phy_write( eng, 21, eng->phy.PHY_15h & 0xffcf ); // Set [5]Rx Dly, [4]Tx Dly to 0
        }
phy_read( eng, 21 ); // v069
        phy_write( eng, 22, 0x0000 );

        if ( eng->run.tm_tx_only ) {
                phy_reset( eng );
        }
        else if ( eng->phy.loopback ) {
                phy_write( eng, 22, 0x0002 );

                if ( eng->run.speed_sel[ 0 ] ) {
                        phy_clrset( eng, 21, 0x6040, 0x0040 );//clr set
                }
                else if ( eng->run.speed_sel[ 1 ] ) {
                        phy_clrset( eng, 21, 0x6040, 0x2000 );//clr set
                }
                else {
                        phy_clrset( eng, 21, 0x6040, 0x0000 );//clr set
                }
                phy_write( eng, 22, 0x0000 );
                phy_reset(  eng  );
        }
        else {
                if ( eng->run.speed_sel[ 0 ] ) {
                        phy_write( eng, 22, 0x0006 );
                        phy_clrset( eng, 16, 0x0000, 0x0020 );//clr set
phy_read( eng, 16 ); // v069
                        phy_write( eng, 22, 0x0000 );
                }

                phy_reset( eng );
phy_read( eng, 0 ); // v069
        }

        if ( !eng->phy.loopback )
                phy_check_register ( eng, 17, 0x0400, 0x0400, 1, "wait 88E1310 link-up");
//      Retry = 0;
//      do {
//              eng->phy.PHY_11h = phy_read( eng, PHY_SR );
//      } while ( !( ( eng->phy.PHY_11h & 0x0400 ) | eng->phy.loopback | ( Retry++ > 20 ) ) );
}

//------------------------------------------------------------
void recov_phy_marvell1 (MAC_ENGINE *eng) {//88E6176
        int8_t       phy_addr_org;

        phy_addr_org = eng->phy.Adr;
        for ( eng->phy.Adr = 16; eng->phy.Adr <= 22; eng->phy.Adr++ ) {
                if ( eng->run.tm_tx_only ) {
                }
                else {
                        phy_write( eng,  6, eng->phy.PHY_06hA[eng->phy.Adr-16] );//06h[5]P5 loopback, 06h[6]P6 loopback
                }
        }
        for ( eng->phy.Adr = 21; eng->phy.Adr <= 22; eng->phy.Adr++ ) {
                phy_write( eng,  1, 0x0003 ); //01h[1:0]00 = 10 Mbps, 01 = 100 Mbps, 10 = 1000 Mbps, 11 = Speed is not forced.
        }
        eng->phy.Adr = phy_addr_org;
}

//------------------------------------------------------------
void phy_marvell1 (MAC_ENGINE *eng) {//88E6176
//      uint32_t      PHY_01h;
        int8_t       phy_addr_org;

        if ( eng->run.tm_tx_only ) {
                printf("This mode doesn't support in 88E6176.\n");
        } else {
                //The 88E6176 is switch with 7 Port(P0~P6) and the PHYAdr will be fixed at 0x10~0x16, and only P5/P6 can be connected to the MAC.
                //Therefor, the 88E6176 only can run the internal loopback.
                phy_addr_org = eng->phy.Adr;
                for ( eng->phy.Adr = 16; eng->phy.Adr <= 20; eng->phy.Adr++ ) {
                        eng->phy.PHY_06hA[eng->phy.Adr-16] = phy_read( eng, PHY_ANER );
                        phy_write( eng,  6, 0x0000 );//06h[5]P5 loopback, 06h[6]P6 loopback
                }

                for ( eng->phy.Adr = 21; eng->phy.Adr <= 22; eng->phy.Adr++ ) {
//                      PHY_01h = phy_read( eng, PHY_REG_BMSR );
//                      if      ( eng->run.speed_sel[ 0 ] ) phy_write( eng,  1, (PHY_01h & 0xfffc) | 0x0002 );//[1:0]00 = 10 Mbps, 01 = 100 Mbps, 10 = 1000 Mbps, 11 = Speed is not forced.
//                      else if ( eng->run.speed_sel[ 1 ] ) phy_write( eng,  1, (PHY_01h & 0xfffc) | 0x0001 );//[1:0]00 = 10 Mbps, 01 = 100 Mbps, 10 = 1000 Mbps, 11 = Speed is not forced.
//                      else                              phy_write( eng,  1, (PHY_01h & 0xfffc)          );//[1:0]00 = 10 Mbps, 01 = 100 Mbps, 10 = 1000 Mbps, 11 = Speed is not forced.
                        if      ( eng->run.speed_sel[ 0 ] ) phy_write( eng,  1, 0x0002 );//01h[1:0]00 = 10 Mbps, 01 = 100 Mbps, 10 = 1000 Mbps, 11 = Speed is not forced.
                        else if ( eng->run.speed_sel[ 1 ] ) phy_write( eng,  1, 0x0001 );//01h[1:0]00 = 10 Mbps, 01 = 100 Mbps, 10 = 1000 Mbps, 11 = Speed is not forced.
                        else                                phy_write( eng,  1, 0x0000 );//01h[1:0]00 = 10 Mbps, 01 = 100 Mbps, 10 = 1000 Mbps, 11 = Speed is not forced.

                        eng->phy.PHY_06hA[eng->phy.Adr-16] = phy_read( eng, PHY_ANER );
                        if ( eng->phy.Adr == 21 ) phy_write( eng,  6, 0x0020 );//06h[5]P5 loopback, 06h[6]P6 loopback
                        else                      phy_write( eng,  6, 0x0040 );//06h[5]P5 loopback, 06h[6]P6 loopback
                }
                eng->phy.Adr = phy_addr_org;
        }
}

//------------------------------------------------------------
void recov_phy_marvell2 (MAC_ENGINE *eng) {//88E1512//88E15 10/12/14/18
        if ( eng->run.tm_tx_only ) {
        }
        else if ( eng->phy.loopback ) {
        }
        else {
                if ( eng->run.speed_sel[ 0 ] ) {
                        // Enable Stub Test
                        // switch page 6
                        phy_write( eng, 22, 0x0006 );
                        phy_clrset( eng, 18, 0x0008, 0x0000 );//clr set
                        phy_write( eng, 22, 0x0000 );
                }
        }
}

//------------------------------------------------------------
//88E1512//88E15 10/12/14/18
void phy_marvell2 (MAC_ENGINE *eng) 
{
	/* switch to page 2 */
	phy_write(eng, 22, 0x0002);
	eng->phy.PHY_15h = phy_read(eng, 21);
	eng->phy.PHY_15h &= ~GENMASK(5, 4);
	if (eng->arg.ctrl.b.phy_tx_delay_en)
		eng->phy.PHY_15h |= BIT(4);
	if (eng->arg.ctrl.b.phy_rx_delay_en)
		eng->phy.PHY_15h |= BIT(5);

	phy_write(eng, 21, eng->phy.PHY_15h);

	/* switch to page 0 */
	phy_write(eng, 22, 0x0000);

        if ( eng->run.tm_tx_only ) {
                phy_reset( eng );
        }
        else if ( eng->phy.loopback ) {
                // Internal loopback funciton only support in copper mode
                // switch page 18
                phy_write( eng, 22, 0x0012 );
                eng->phy.PHY_14h = phy_read( eng, 20 );
                // Change mode to Copper mode
//              if ( eng->phy.PHY_14h & 0x0020 ) {
                if ( ( eng->phy.PHY_14h & 0x003f ) != 0x0010 ) {
                        printf("\n\n[Warning] Internal loopback funciton only support in copper mode[%04x]\n\n", eng->phy.PHY_14h);
                        if ( eng->run.TM_IOTiming ) PRINTF( FP_IO, "\n\n[Warning] Internal loopback funciton only support in copper mode[%04x]\n\n", eng->phy.PHY_14h);
                        if ( !eng->run.tm_tx_only ) PRINTF( FP_LOG, "\n\n[Warning] Internal loopback funciton only support in copper mode[%04x]\n\n", eng->phy.PHY_14h);

                        phy_write( eng, 20, ( eng->phy.PHY_14h & 0xffc0 ) | 0x8010 );
                        // do software reset
                        phy_check_register ( eng, 20, 0x8000, 0x0000, 1, "wait 88E15 10/12/14/18 mode reset");
//                      do {
//                              temp_reg = phy_read( eng, 20 );
//                      } while ( ( (temp_reg & 0x8000) == 0x8000 ) & (Retry++ < 20) );
                }

                // switch page 2
                phy_write( eng, 22, 0x0002 );
                if ( eng->run.speed_sel[ 0 ] ) {
                        phy_clrset( eng, 21, 0x2040, 0x0040 );//clr set
                }
                else if ( eng->run.speed_sel[ 1 ] ) {
                        phy_clrset( eng, 21, 0x2040, 0x2000 );//clr set
                }
                else {
                        phy_clrset( eng, 21, 0x2040, 0x0000 );//clr set
                }
                phy_write( eng, 22, 0x0000 );

                phy_reset( eng );

                //Internal loopback at 100Mbps need delay 400~500 ms
//              DELAY( 400 );//Still fail at 100Mbps
//              DELAY( 500 );//All Pass
                if ( !eng->run.speed_sel[ 0 ] ) {
                        phy_check_register ( eng, 17, 0x0040, 0x0040, 10, "wait 88E15 10/12/14/18 link-up");
                        phy_check_register ( eng, 17, 0x0040, 0x0000, 10, "wait 88E15 10/12/14/18 link-up");
                        phy_check_register ( eng, 17, 0x0040, 0x0040, 10, "wait 88E15 10/12/14/18 link-up");
                }
        }
        else {
                if ( eng->run.speed_sel[ 0 ] ) {
                        // Enable Stub Test
                        // switch page 6
                        phy_write( eng, 22, 0x0006 );
                        phy_clrset( eng, 18, 0x0000, 0x0008 );//clr set
                        phy_write( eng, 22, 0x0000 );
                }

                phy_reset( eng );
                phy_check_register ( eng, 17, 0x0400, 0x0400, 10, "wait 88E15 10/12/14/18 link-up");
        }

//      if ( !eng->phy.loopback )
////    if ( !eng->run.tm_tx_only )
//              phy_check_register ( eng, 17, 0x0400, 0x0400, 10, "wait 88E15 10/12/14/18 link-up");
////    Retry = 0;
////    do {
////            eng->phy.PHY_11h = phy_read( eng, PHY_SR );
////    } while ( !( ( eng->phy.PHY_11h & 0x0400 ) | eng->phy.loopback | ( Retry++ > 20 ) ) );
}

//------------------------------------------------------------
void phy_marvell3 (MAC_ENGINE *eng) 
{//88E3019

        //Reg1ch[11:10]: MAC Interface Mode
        // 00 => RGMII where receive clock trnasitions when data transitions
        // 01 => RGMII where receive clock trnasitions when data is stable
        // 10 => RMII
        // 11 => MII
        eng->phy.PHY_1ch = phy_read( eng, 28 );
        if (eng->run.is_rgmii) {
                if ( ( eng->phy.PHY_1ch & 0x0c00 ) != 0x0000 ) {
                        printf("\n\n[Warning] Register 28, bit 10~11 must be 0 (RGMIIRX Edge-align Mode)[Reg1ch:%04x]\n\n", eng->phy.PHY_1ch);
                        eng->phy.PHY_1ch = ( eng->phy.PHY_1ch & 0xf3ff ) | 0x0000;
                        phy_write( eng, 28, eng->phy.PHY_1ch );
                }
        } else {
                if ( ( eng->phy.PHY_1ch & 0x0c00 ) != 0x0800 ) {
                        printf("\n\n[Warning] Register 28, bit 10~11 must be 2 (RMII Mode)[Reg1ch:%04x]\n\n", eng->phy.PHY_1ch);
                        eng->phy.PHY_1ch = ( eng->phy.PHY_1ch & 0xf3ff ) | 0x0800;
                        phy_write( eng, 28, eng->phy.PHY_1ch );
                }
        }

        if ( eng->run.tm_tx_only ) {
                phy_reset( eng );
        }
        else if ( eng->phy.loopback ) {
                phy_reset( eng );
        }
        else {
                phy_reset( eng );
        }

        phy_check_register ( eng, 17, 0x0400, 0x0400, 1, "wait 88E3019 link-up");
}

//------------------------------------------------------------
void phy_broadcom (MAC_ENGINE *eng) 
{//BCM5221
	uint32_t      reg;

        phy_reset( eng );

        if ( eng->run.TM_IEEE ) {
                if ( eng->run.ieee_sel == 0 ) {
                        phy_write( eng, 25, 0x1f01 );//Force MDI  //Measuring from channel A
                }
                else {
                        phy_clrset( eng, 24, 0x0000, 0x4000 );//clr set//Force Link
//                      phy_write( eng,  0, eng->phy.PHY_00h );
//                      phy_write( eng, 30, 0x1000 );
                }
        }
        else
        {
                // we can check link status from register 0x18
                if ( eng->run.speed_sel[ 1 ] ) {
                        do {
                                reg = phy_read( eng, 0x18 ) & 0xF;
                        } while ( reg != 0x7 );
                }
                else {
                        do {
                        reg = phy_read( eng, 0x18 ) & 0xF;
                        } while ( reg != 0x1 );
                }
        }
}

//------------------------------------------------------------
void recov_phy_broadcom0 (MAC_ENGINE *eng) {//BCM54612
        phy_write( eng,  0, eng->phy.PHY_00h );
        phy_write( eng,  9, eng->phy.PHY_09h );
//      phy_write( eng, 24, eng->phy.PHY_18h | 0xf007 );//write reg 18h, shadow value 111
//      phy_write( eng, 28, eng->phy.PHY_1ch | 0x8c00 );//write reg 1Ch, shadow value 00011

        if ( eng->run.tm_tx_only ) {
        }
        else if ( eng->phy.loopback ) {
                phy_write( eng,  0, eng->phy.PHY_00h );
        }
        else {
        }
}

//------------------------------------------------------------
//internal loop 1G  : no  loopback stub
//internal loop 100M: Don't support(?)
//internal loop 10M : Don't support(?)
void phy_broadcom0 (MAC_ENGINE *eng)
{
	uint32_t PHY_new;

	phy_reset(eng);

        eng->phy.PHY_00h = phy_read( eng, PHY_REG_BMCR );
        eng->phy.PHY_09h = phy_read( eng, PHY_GBCR );

	phy_write( eng, 0, eng->phy.PHY_00h & ~BIT(10));

	/*
	 * RX interface delay: reg 0x18, shadow value b'0111: misc control
	 * bit[8] RGMII RXD to RXC skew
	 */
	phy_write(eng, 0x18, (0x7 << 12) | 0x7);
	eng->phy.PHY_18h = phy_read(eng, 0x18);
	PHY_new = eng->phy.PHY_18h & ~((0x7 << 12) | 0x7 | BIT(8));
	PHY_new |= (0x7 << 12) | 0x7 | BIT(15);
	if (eng->arg.ctrl.b.phy_rx_delay_en)
		PHY_new |= BIT(8);
	phy_write(eng, 0x18, PHY_new);

	/*
	 * TX interface delay: reg 0x1c, shadow value b'0011: clock alignment 
	 * control
	 * bit[9] GTXCLK clock delay enable
	 */
	phy_write(eng, 0x1c, 0x3 << 10);
	eng->phy.PHY_1ch = phy_read(eng, 0x1c);
	PHY_new = eng->phy.PHY_1ch & ~((0x1f << 10) | BIT(9));
	PHY_new |= (0x3 << 10) | BIT(15);
	if (eng->arg.ctrl.b.phy_tx_delay_en)
		PHY_new |= BIT(9);
	phy_write(eng, 0x1c, PHY_new);

        if ( eng->run.tm_tx_only ) {
                phy_basic_setting(eng);
        } else if (eng->phy.loopback) {
		phy_basic_setting(eng);
		/* reg1E[12]: force-link */
		if (strncmp((char *)eng->phy.phy_name, "BCM5421x", strlen("BCM5421x")) == 0)
			phy_write(eng, 0x1e, BIT(12));
	} else {
		if (eng->run.speed_sel[0]) {
			phy_write(eng, 0x9, 0x1800);
			phy_write(eng, 0x0, 0x0140);
			phy_write(eng, 0x18, 0x8400);
		} else if (eng->run.speed_sel[1]) {
			phy_write(eng, 0x0, 0x2100);
			phy_write(eng, 0x18, 0x8400);
		} else {
			phy_write(eng, 0x0, 0x0100);
			phy_write(eng, 0x18, 0x8400);
		}
	}
	mdelay(100);
}

//------------------------------------------------------------
void phy_realtek (MAC_ENGINE *eng)
{//RTL8201N

        phy_reset( eng );
}

//------------------------------------------------------------
//internal loop 100M: Don't support
//internal loop 10M : no  loopback stub
void phy_realtek0 (MAC_ENGINE *eng)
{//RTL8201E

        eng->phy.RMIICK_IOMode |= PHY_Flag_RMIICK_IOMode_RTL8201E;

        phy_reset( eng );

        eng->phy.PHY_19h = phy_read( eng, 25 );
        //Check RMII Mode
        if ( ( eng->phy.PHY_19h & 0x0400 ) == 0x0 ) {
                phy_write( eng, 25, eng->phy.PHY_19h | 0x0400 );
                printf("\n\n[Warning] Register 25, bit 10 must be 1 [Reg19h:%04x]\n\n", eng->phy.PHY_19h);
                if ( eng->run.TM_IOTiming ) PRINTF( FP_IO, "\n\n[Warning] Register 25, bit 10 must be 1 [Reg19h:%04x]\n\n", eng->phy.PHY_19h );
                if ( !eng->run.tm_tx_only ) PRINTF( FP_LOG, "\n\n[Warning] Register 25, bit 10 must be 1 [Reg19h:%04x]\n\n", eng->phy.PHY_19h );
        }
        //Check TXC Input/Output Direction
        if ( eng->arg.ctrl.b.rmii_phy_in == 0 ) {
                if ( ( eng->phy.PHY_19h & 0x0800 ) == 0x0800 ) {
                        phy_write( eng, 25, eng->phy.PHY_19h & 0xf7ff );
                        printf("\n\n[Warning] Register 25, bit 11 must be 0 (TXC should be output mode)[Reg19h:%04x]\n\n", eng->phy.PHY_19h);
                        if ( eng->run.TM_IOTiming ) PRINTF( FP_IO, "\n\n[Warning] Register 25, bit 11 must be 0 (TXC should be output mode)[Reg19h:%04x]\n\n", eng->phy.PHY_19h );
                        if ( !eng->run.tm_tx_only ) PRINTF( FP_LOG, "\n\n[Warning] Register 25, bit 11 must be 0 (TXC should be output mode)[Reg19h:%04x]\n\n", eng->phy.PHY_19h );
                }
        } else {
                if ( ( eng->phy.PHY_19h & 0x0800 ) == 0x0000 ) {
                        phy_write( eng, 25, eng->phy.PHY_19h | 0x0800 );
                        printf("\n\n[Warning] Register 25, bit 11 must be 1 (TXC should be input mode)[Reg19h:%04x]\n\n", eng->phy.PHY_19h);
                        if ( eng->run.TM_IOTiming ) PRINTF( FP_IO, "\n\n[Warning] Register 25, bit 11 must be 1 (TXC should be input mode)[Reg19h:%04x]\n\n", eng->phy.PHY_19h );
                        if ( !eng->run.tm_tx_only ) PRINTF( FP_LOG, "\n\n[Warning] Register 25, bit 11 must be 1 (TXC should be input mode)[Reg19h:%04x]\n\n", eng->phy.PHY_19h );
                }
        }

        if ( eng->run.TM_IEEE ) {
                phy_write( eng, 31, 0x0001 );
                if ( eng->run.ieee_sel == 0 ) {
                        phy_write( eng, 25, 0x1f01 );//Force MDI  //Measuring from channel A
                }
                else {
                        phy_write( eng, 25, 0x1f00 );//Force MDIX //Measuring from channel B
                }
                phy_write( eng, 31, 0x0000 );
        }
}

//------------------------------------------------------------
void recov_phy_realtek1 (MAC_ENGINE *eng) {//RTL8211D
        if ( eng->run.tm_tx_only ) {
                if ( eng->run.TM_IEEE ) {
                        if ( eng->run.speed_sel[ 0 ] ) {
                                if ( eng->run.ieee_sel == 0 ) {//Test Mode 1
                                        //Rev 1.2
                                        phy_write( eng, 31, 0x0002 );
                                        phy_write( eng,  2, 0xc203 );
                                        phy_write( eng, 31, 0x0000 );
                                        phy_write( eng,  9, 0x0000 );
                                }
                                else {//Test Mode 4
                                        //Rev 1.2
                                        phy_write( eng, 31, 0x0000 );
                                        phy_write( eng,  9, 0x0000 );
                                }
                        }
                        else if ( eng->run.speed_sel[ 1 ] ) {
                                //Rev 1.2
                                phy_write( eng, 23, 0x2100 );
                                phy_write( eng, 16, 0x016e );
                        }
                        else {
                                //Rev 1.2
                                phy_write( eng, 31, 0x0006 );
                                phy_write( eng,  0, 0x5a00 );
                                phy_write( eng, 31, 0x0000 );
                        }
                } else {
                        phy_reset( eng );
                } // End if ( eng->run.TM_IEEE )
        }
        else if ( eng->phy.loopback ) {
                if ( eng->run.speed_sel[ 0 ] ) {
                        phy_write( eng, 31, 0x0000 ); // new in Rev. 1.6
                        phy_write( eng,  0, 0x1140 ); // new in Rev. 1.6
                        phy_write( eng, 20, 0x8040 ); // new in Rev. 1.6
                }
        }
        else {
                if ( eng->run.speed_sel[ 0 ] ) {
                        phy_write( eng, 31, 0x0001 );
                        phy_write( eng,  3, 0xdf41 );
                        phy_write( eng,  2, 0xdf20 );
                        phy_write( eng,  1, 0x0140 );
                        phy_write( eng,  0, 0x00bb );
                        phy_write( eng,  4, 0xb800 );
                        phy_write( eng,  4, 0xb000 );

                        phy_write( eng, 31, 0x0000 );
//                      phy_write( eng, 26, 0x0020 ); // Rev. 1.2
                        phy_write( eng, 26, 0x0040 ); // new in Rev. 1.6
                        phy_write( eng,  0, 0x1140 );
//                      phy_write( eng, 21, 0x0006 ); // Rev. 1.2
                        phy_write( eng, 21, 0x1006 ); // new in Rev. 1.6
                        phy_write( eng, 23, 0x2100 );
//              }
//              else if ( eng->run.speed_sel[ 1 ] ) {//option
//                      phy_write( eng, 31, 0x0000 );
//                      phy_write( eng,  9, 0x0200 );
//                      phy_write( eng,  0, 0x1200 );
//              }
//              else if ( eng->run.speed_sel[ 2 ] ) {//option
//                      phy_write( eng, 31, 0x0000 );
//                      phy_write( eng,  9, 0x0200 );
//                      phy_write( eng,  4, 0x05e1 );
//                      phy_write( eng,  0, 0x1200 );
                }
                phy_reset( eng );
                phy_delay(2000);
        } // End if ( eng->run.tm_tx_only )
} // End void recov_phy_realtek1 (MAC_ENGINE *eng)

//------------------------------------------------------------
//internal loop 1G  : no  loopback stub
//internal loop 100M: no  loopback stub
//internal loop 10M : no  loopback stub
void phy_realtek1 (MAC_ENGINE *eng)
{//RTL8211D

        if ( eng->run.tm_tx_only ) {
                if ( eng->run.TM_IEEE ) {
                        if ( eng->run.speed_sel[ 0 ] ) {
                                if ( eng->run.ieee_sel == 0 ) {//Test Mode 1
                                        //Rev 1.2
                                        phy_write( eng, 31, 0x0002 );
                                        phy_write( eng,  2, 0xc22b );
                                        phy_write( eng, 31, 0x0000 );
                                        phy_write( eng,  9, 0x2000 );
                                }
                                else {//Test Mode 4
                                        //Rev 1.2
                                        phy_write( eng, 31, 0x0000 );
                                        phy_write( eng,  9, 0x8000 );
                                }
                        }
                        else if ( eng->run.speed_sel[ 1 ] ) {
                                if ( eng->run.ieee_sel == 0 ) {//From Channel A
                                        //Rev 1.2
                                        phy_write( eng, 23, 0xa102 );
                                        phy_write( eng, 16, 0x01ae );//MDI
                                }
                                else {//From Channel B
                                        //Rev 1.2
                                        phy_clrset( eng, 17, 0x0008, 0x0000 ); // clr set
                                        phy_write( eng, 23, 0xa102 );         // MDI
                                        phy_write( eng, 16, 0x010e );
                                }
                        }
                        else {
                                if ( eng->run.ieee_sel == 0 ) {//Diff. Voltage/TP-IDL/Jitter: Pseudo-random pattern
                                        phy_write( eng, 31, 0x0006 );
                                        phy_write( eng,  0, 0x5a21 );
                                        phy_write( eng, 31, 0x0000 );
                                }
                                else if ( eng->run.ieee_sel == 1 ) {//Harmonic: pattern
                                        phy_write( eng, 31, 0x0006 );
                                        phy_write( eng,  2, 0x05ee );
                                        phy_write( eng,  0, 0xff21 );
                                        phy_write( eng, 31, 0x0000 );
                                }
                                else {//Harmonic: �00� pattern
                                        phy_write( eng, 31, 0x0006 );
                                        phy_write( eng,  2, 0x05ee );
                                        phy_write( eng,  0, 0x0021 );
                                        phy_write( eng, 31, 0x0000 );
                                }
                        }
                }
                else {
                        phy_reset( eng );
                }
        }
        else if ( eng->phy.loopback ) {
                phy_reset( eng );

                if ( eng->run.speed_sel[ 0 ] ) {
                        phy_write( eng, 20, 0x0042 );//new in Rev. 1.6
                }
        }
        else {
        // refer to RTL8211D Register for Manufacture Test_V1.6.pdf
        // MDI loop back
                if ( eng->run.speed_sel[ 0 ] ) {
                        phy_write( eng, 31, 0x0001 );
                        phy_write( eng,  3, 0xff41 );
                        phy_write( eng,  2, 0xd720 );
                        phy_write( eng,  1, 0x0140 );
                        phy_write( eng,  0, 0x00bb );
                        phy_write( eng,  4, 0xb800 );
                        phy_write( eng,  4, 0xb000 );

                        phy_write( eng, 31, 0x0007 );
                        phy_write( eng, 30, 0x0040 );
                        phy_write( eng, 24, 0x0008 );

                        phy_write( eng, 31, 0x0000 );
                        phy_write( eng,  9, 0x0300 );
                        phy_write( eng, 26, 0x0020 );
                        phy_write( eng,  0, 0x0140 );
                        phy_write( eng, 23, 0xa101 );
                        phy_write( eng, 21, 0x0200 );
                        phy_write( eng, 23, 0xa121 );
                        phy_write( eng, 23, 0xa161 );
                        phy_write( eng,  0, 0x8000 );
                        phy_wait_reset_done( eng );

//                      phy_delay(200); // new in Rev. 1.6
                        phy_delay(5000); // 20150504
//              }
//              else if ( eng->run.speed_sel[ 1 ] ) {//option
//                      phy_write( eng, 31, 0x0000 );
//                      phy_write( eng,  9, 0x0000 );
//                      phy_write( eng,  4, 0x0061 );
//                      phy_write( eng,  0, 0x1200 );
//                      phy_delay(5000);
//              }
//              else if ( eng->run.speed_sel[ 2 ] ) {//option
//                      phy_write( eng, 31, 0x0000 );
//                      phy_write( eng,  9, 0x0000 );
//                      phy_write( eng,  4, 0x05e1 );
//                      phy_write( eng,  0, 0x1200 );
//                      phy_delay(5000);
                }
                else {
                        phy_reset( eng );
                }
        }
} // End void phy_realtek1 (MAC_ENGINE *eng)

//------------------------------------------------------------
void recov_phy_realtek2 (MAC_ENGINE *eng)
{
	RTK_DBG_PRINTF("\nClear RTL8211E [Start] =====>\n");

        if ( eng->run.tm_tx_only ) {
                if ( eng->run.TM_IEEE ) {
                        if ( eng->run.speed_sel[ 0 ] ) {
                                //Rev 1.2
                                phy_write( eng, 31, 0x0000 );
                                phy_write( eng,  9, 0x0000 );
                        }
                        else if ( eng->run.speed_sel[ 1 ] ) {
                                //Rev 1.2
                                phy_write( eng, 31, 0x0007 );
                                phy_write( eng, 30, 0x002f );
                                phy_write( eng, 23, 0xd88f );
                                phy_write( eng, 30, 0x002d );
                                phy_write( eng, 24, 0xf050 );
                                phy_write( eng, 31, 0x0000 );
                                phy_write( eng, 16, 0x006e );
                        }
                        else {
                                //Rev 1.2
                                phy_write( eng, 31, 0x0006 );
                                phy_write( eng,  0, 0x5a00 );
                                phy_write( eng, 31, 0x0000 );
                        }
                        //Rev 1.2
                        phy_write( eng, 31, 0x0005 );
                        phy_write( eng,  5, 0x8b86 );
                        phy_write( eng,  6, 0xe201 );
                        phy_write( eng, 31, 0x0007 );
                        phy_write( eng, 30, 0x0020 );
                        phy_write( eng, 21, 0x1108 );
                        phy_write( eng, 31, 0x0000 );
                }
                else {
                }
        }
        else if ( eng->phy.loopback ) {
        }
        else {
                if ( eng->run.speed_sel[ 0 ] ) {
                        //Rev 1.6
                        phy_write( eng, 31, 0x0000 );
                        phy_write( eng,  0, 0x8000 );
#ifdef RTK_DEBUG
#else
                        phy_wait_reset_done( eng );
                        phy_delay(30);
#endif

                        phy_write( eng, 31, 0x0007 );
                        phy_write( eng, 30, 0x0042 );
                        phy_write( eng, 21, 0x0500 );
                        phy_write( eng, 31, 0x0000 );
                        phy_write( eng,  0, 0x1140 );
                        phy_write( eng, 26, 0x0040 );
                        phy_write( eng, 31, 0x0007 );
                        phy_write( eng, 30, 0x002f );
                        phy_write( eng, 23, 0xd88f );
                        phy_write( eng, 30, 0x0023 );
                        phy_write( eng, 22, 0x0300 );
                        phy_write( eng, 31, 0x0000 );
                        phy_write( eng, 21, 0x1006 );
                        phy_write( eng, 23, 0x2100 );
                }
//              else if ( eng->run.speed_sel[ 1 ] ) {//option
//                      phy_write( eng, 31, 0x0000 );
//                      phy_write( eng,  9, 0x0200 );
//                      phy_write( eng,  0, 0x1200 );
//              }
//              else if ( eng->run.speed_sel[ 2 ] ) {//option
//                      phy_write( eng, 31, 0x0000 );
//                      phy_write( eng,  9, 0x0200 );
//                      phy_write( eng,  4, 0x05e1 );
//                      phy_write( eng,  0, 0x1200 );
//              }
                else {
                        phy_write( eng, 31, 0x0000 );
                        phy_write( eng,  0, 0x1140 );
                }
#ifdef RTK_DEBUG
#else
                // Check register 0x11 bit10 Link OK or not OK
                phy_check_register ( eng, 17, 0x0c02, 0x0000, 10, "clear RTL8211E");
#endif
        }

	RTK_DBG_PRINTF("\nClear RTL8211E [End] =====>\n");
} // End void recov_phy_realtek2 (MAC_ENGINE *eng)

//------------------------------------------------------------
//internal loop 1G  : no  loopback stub
//internal loop 100M: no  loopback stub
//internal loop 10M : no  loopback stub
// for RTL8211E
void phy_realtek2 (MAC_ENGINE *eng) 
{
        uint16_t     check_value;

	RTK_DBG_PRINTF("\nSet RTL8211E [Start] =====>\n");

	rtk_dbg_gpio_init();

#ifdef RTK_DEBUG
#else
        phy_write( eng, 31, 0x0000 );
        phy_clrset( eng,  0, 0x0000, 0x8000 | eng->phy.PHY_00h ); // clr set // Rst PHY
        phy_wait_reset_done( eng );
        phy_delay(30);
#endif

        if ( eng->run.tm_tx_only ) {
                if ( eng->run.TM_IEEE ) {
                        //Rev 1.2
                        phy_write( eng, 31, 0x0005 );
                        phy_write( eng,  5, 0x8b86 );
                        phy_write( eng,  6, 0xe200 );
                        phy_write( eng, 31, 0x0007 );
                        phy_write( eng, 30, 0x0020 );
                        phy_write( eng, 21, 0x0108 );
                        phy_write( eng, 31, 0x0000 );

                        if ( eng->run.speed_sel[ 0 ] ) {
                                //Rev 1.2
                                phy_write( eng, 31, 0x0000 );

                                if ( eng->run.ieee_sel == 0 ) {
                                        phy_write( eng,  9, 0x2000 );//Test Mode 1
                                }
                                else {
                                        phy_write( eng,  9, 0x8000 );//Test Mode 4
                                }
                        }
                        else if ( eng->run.speed_sel[ 1 ] ) {
                                //Rev 1.2
                                phy_write( eng, 31, 0x0007 );
                                phy_write( eng, 30, 0x002f );
                                phy_write( eng, 23, 0xd818 );
                                phy_write( eng, 30, 0x002d );
                                phy_write( eng, 24, 0xf060 );
                                phy_write( eng, 31, 0x0000 );

                                if ( eng->run.ieee_sel == 0 ) {
                                        phy_write( eng, 16, 0x00ae );//From Channel A
                                }
                                else {
                                        phy_write( eng, 16, 0x008e );//From Channel B
                                }
                        }
                        else {
                                //Rev 1.2
                                phy_write( eng, 31, 0x0006 );
                                if ( eng->run.ieee_sel == 0 ) {//Diff. Voltage/TP-IDL/Jitter
                                        phy_write( eng,  0, 0x5a21 );
                                }
                                else if ( eng->run.ieee_sel == 1 ) {//Harmonic: �FF� pattern
                                        phy_write( eng,  2, 0x05ee );
                                        phy_write( eng,  0, 0xff21 );
                                }
                                else {//Harmonic: �00� pattern
                                        phy_write( eng,  2, 0x05ee );
                                        phy_write( eng,  0, 0x0021 );
                                }
                                phy_write( eng, 31, 0x0000 );
                        }
                }
                else {
                        phy_basic_setting( eng );
                        phy_delay(30);
                }
        }
        else if ( eng->phy.loopback ) {
#ifdef RTK_DEBUG
                phy_write( eng,  0, 0x0000 );
                phy_write( eng,  0, 0x8000 );
                phy_delay(60);
                phy_write( eng,  0, eng->phy.PHY_00h );
                phy_delay(60);
#else
                phy_basic_setting( eng );

                phy_clrset( eng,  0, 0x0000, 0x8000 | eng->phy.PHY_00h );//clr set//Rst PHY
                phy_wait_reset_done( eng );
                phy_delay(30);

                phy_basic_setting( eng );
                phy_delay(30);
#endif
        }
        else {
#ifdef Enable_Dual_Mode
                if ( eng->run.speed_sel[ 0 ] ) {
                        check_value = 0x0c02 | 0xa000;
                }
                else if ( eng->run.speed_sel[ 1 ] ) {
                        check_value = 0x0c02 | 0x6000;
                }
                else if ( eng->run.speed_sel[ 2 ] ) {
                        check_value = 0x0c02 | 0x2000;
                }
#else
                if ( eng->run.speed_sel[ 0 ] ) {
                        check_value = 0x0c02 | 0xa000;                
#ifdef RTK_DEBUG
                        phy_write( eng, 31, 0x0000 );
                        phy_write( eng,  0, 0x8000 );
                        phy_delay(60);
  #endif

                        phy_write( eng, 31, 0x0007 );
                        phy_write( eng, 30, 0x0042 );
                        phy_write( eng, 21, 0x2500 );
                        phy_write( eng, 30, 0x0023 );
                        phy_write( eng, 22, 0x0006 );
                        phy_write( eng, 31, 0x0000 );
                        phy_write( eng,  0, 0x0140 );
                        phy_write( eng, 26, 0x0060 );
                        phy_write( eng, 31, 0x0007 );
                        phy_write( eng, 30, 0x002f );
                        phy_write( eng, 23, 0xd820 );
                        phy_write( eng, 31, 0x0000 );
                        phy_write( eng, 21, 0x0206 );
                        phy_write( eng, 23, 0x2120 );
                        phy_write( eng, 23, 0x2160 );
  #ifdef RTK_DEBUG
                        phy_delay(600);
  #else
                        phy_delay(300);
  #endif
                }
//              else if ( eng->run.speed_sel[ 1 ] ) {//option
//                      check_value = 0x0c02 | 0x6000;
//                      phy_write( eng, 31, 0x0000 );
//                      phy_write( eng,  9, 0x0000 );
//                      phy_write( eng,  4, 0x05e1 );
//                      phy_write( eng,  0, 0x1200 );
//                      phy_delay(6000);
//              }
//              else if ( eng->run.speed_sel[ 2 ] ) {//option
//                      check_value = 0x0c02 | 0x2000;
//                      phy_write( eng, 31, 0x0000 );
//                      phy_write( eng,  9, 0x0000 );
//                      phy_write( eng,  4, 0x0061 );
//                      phy_write( eng,  0, 0x1200 );
//                      phy_delay(6000);
//              }
                else {
                        if ( eng->run.speed_sel[ 1 ] )
                                check_value = 0x0c02 | 0x6000;
                        else
                                check_value = 0x0c02 | 0x2000;
                        phy_write( eng, 31, 0x0000 );
                        phy_write( eng,  0, eng->phy.PHY_00h );
  #ifdef RTK_DEBUG
                        phy_delay(300);
  #else
                        phy_delay(150);
  #endif
                }
#endif
#ifdef RTK_DEBUG
#else
                // Check register 0x11 bit10 Link OK or not OK
                phy_check_register ( eng, 17, 0x0c02 | 0xe000, check_value, 10, "set RTL8211E");
#endif
        }

	RTK_DBG_PRINTF("\nSet RTL8211E [End] =====>\n");
} // End void phy_realtek2 (MAC_ENGINE *eng)

//------------------------------------------------------------
void recov_phy_realtek3 (MAC_ENGINE *eng) {//RTL8211C
        if ( eng->run.tm_tx_only ) {
                if ( eng->run.TM_IEEE ) {
                        if ( eng->run.speed_sel[ 0 ] ) {
                                phy_write( eng,  9, 0x0000 );
                        }
                        else if ( eng->run.speed_sel[ 1 ] ) {
                                phy_write( eng, 17, eng->phy.PHY_11h );
                                phy_write( eng, 14, 0x0000 );
                                phy_write( eng, 16, 0x00a0 );
                        }
                        else {
//                              phy_write( eng, 31, 0x0006 );
//                              phy_write( eng,  0, 0x5a00 );
//                              phy_write( eng, 31, 0x0000 );
                        }
                }
                else {
                }
        }
        else if ( eng->phy.loopback ) {
                if ( eng->run.speed_sel[ 0 ] ) {
                        phy_write( eng, 11, 0x0000 );
                }
                phy_write( eng, 12, 0x1006 );
        }
        else {
                if ( eng->run.speed_sel[ 0 ] ) {
                        phy_write( eng, 31, 0x0001 );
                        phy_write( eng,  4, 0xb000 );
                        phy_write( eng,  3, 0xff41 );
                        phy_write( eng,  2, 0xdf20 );
                        phy_write( eng,  1, 0x0140 );
                        phy_write( eng,  0, 0x00bb );
                        phy_write( eng,  4, 0xb800 );
                        phy_write( eng,  4, 0xb000 );

                        phy_write( eng, 31, 0x0000 );
                        phy_write( eng, 25, 0x8c00 );
                        phy_write( eng, 26, 0x0040 );
                        phy_write( eng,  0, 0x1140 );
                        phy_write( eng, 14, 0x0000 );
                        phy_write( eng, 12, 0x1006 );
                        phy_write( eng, 23, 0x2109 );
                }
        }
}

//------------------------------------------------------------
void phy_realtek3 (MAC_ENGINE *eng)
{//RTL8211C

        if ( eng->run.tm_tx_only ) {
                if ( eng->run.TM_IEEE ) {
                        if ( eng->run.speed_sel[ 0 ] ) {
                                if ( eng->run.ieee_sel == 0 ) {   //Test Mode 1
                                        phy_write( eng,  9, 0x2000 );
                                }
                                else if ( eng->run.ieee_sel == 1 ) {//Test Mode 2
                                        phy_write( eng,  9, 0x4000 );
                                }
                                else if ( eng->run.ieee_sel == 2 ) {//Test Mode 3
                                        phy_write( eng,  9, 0x6000 );
                                }
                                else {                           //Test Mode 4
                                        phy_write( eng,  9, 0x8000 );
                                }
                        }
                        else if ( eng->run.speed_sel[ 1 ] ) {
                                eng->phy.PHY_11h = phy_read( eng, PHY_SR );
                                phy_write( eng, 17, eng->phy.PHY_11h & 0xfff7 );
                                phy_write( eng, 14, 0x0660 );

                                if ( eng->run.ieee_sel == 0 ) {
                                        phy_write( eng, 16, 0x00a0 );//MDI  //From Channel A
                                }
                                else {
                                        phy_write( eng, 16, 0x0080 );//MDIX //From Channel B
                                }
                        }
                        else {
//                              if ( eng->run.ieee_sel == 0 ) {//Pseudo-random pattern
//                                      phy_write( eng, 31, 0x0006 );
//                                      phy_write( eng,  0, 0x5a21 );
//                                      phy_write( eng, 31, 0x0000 );
//                              }
//                              else if ( eng->run.ieee_sel == 1 ) {//�FF� pattern
//                                      phy_write( eng, 31, 0x0006 );
//                                      phy_write( eng,  2, 0x05ee );
//                                      phy_write( eng,  0, 0xff21 );
//                                      phy_write( eng, 31, 0x0000 );
//                              }
//                              else {//�00� pattern
//                                      phy_write( eng, 31, 0x0006 );
//                                      phy_write( eng,  2, 0x05ee );
//                                      phy_write( eng,  0, 0x0021 );
//                                      phy_write( eng, 31, 0x0000 );
//                              }
                        }
                }
                else {
                        phy_reset( eng );
                }
        }
        else if ( eng->phy.loopback ) {
                phy_write( eng,  0, 0x9200 );
                phy_wait_reset_done( eng );
                phy_delay(30);

                phy_write( eng, 17, 0x401c );
                phy_write( eng, 12, 0x0006 );

                if ( eng->run.speed_sel[ 0 ] ) {
                        phy_write( eng, 11, 0x0002 );
                }
                else {
                        phy_basic_setting( eng );
                }
        }
        else {
                if ( eng->run.speed_sel[ 0 ] ) {
                        phy_write( eng, 31, 0x0001 );
                        phy_write( eng,  4, 0xb000 );
                        phy_write( eng,  3, 0xff41 );
                        phy_write( eng,  2, 0xd720 );
                        phy_write( eng,  1, 0x0140 );
                        phy_write( eng,  0, 0x00bb );
                        phy_write( eng,  4, 0xb800 );
                        phy_write( eng,  4, 0xb000 );

                        phy_write( eng, 31, 0x0000 );
                        phy_write( eng, 25, 0x8400 );
                        phy_write( eng, 26, 0x0020 );
                        phy_write( eng,  0, 0x0140 );
                        phy_write( eng, 14, 0x0210 );
                        phy_write( eng, 12, 0x0200 );
                        phy_write( eng, 23, 0x2109 );
                        phy_write( eng, 23, 0x2139 );
                }
                else {
                        phy_reset( eng );
                }
        }
} // End void phy_realtek3 (MAC_ENGINE *eng)

//------------------------------------------------------------
//external loop 100M: OK
//external loop 10M : OK
//internal loop 100M: no  loopback stub
//internal loop 10M : no  loopback stub
void phy_realtek4 (MAC_ENGINE *eng) {//RTL8201F

        eng->phy.RMIICK_IOMode |= PHY_Flag_RMIICK_IOMode_RTL8201F;

        phy_write( eng, 31, 0x0007 );
        eng->phy.PHY_10h = phy_read( eng, 16 );
        //Check RMII Mode
        if ( ( eng->phy.PHY_10h & 0x0008 ) == 0x0 ) {
                phy_write( eng, 16, eng->phy.PHY_10h | 0x0008 );
                printf("\n\n[Warning] Page 7 Register 16, bit 3 must be 1 [Reg10h_7:%04x]\n\n", eng->phy.PHY_10h);
                if ( eng->run.TM_IOTiming ) PRINTF( FP_IO, "\n\n[Warning] Page 7 Register 16, bit 3 must be 1 [Reg10h_7:%04x]\n\n", eng->phy.PHY_10h );
                if ( !eng->run.tm_tx_only ) PRINTF( FP_LOG, "\n\n[Warning] Page 7 Register 16, bit 3 must be 1 [Reg10h_7:%04x]\n\n", eng->phy.PHY_10h );
        }
        //Check TXC Input/Output Direction
        if ( eng->arg.ctrl.b.rmii_phy_in == 0 ) {
                if ( ( eng->phy.PHY_10h & 0x1000 ) == 0x1000 ) {
                        phy_write( eng, 16, eng->phy.PHY_10h & 0xefff );
                        printf("\n\n[Warning] Page 7 Register 16, bit 12 must be 0 (TXC should be output mode)[Reg10h_7:%04x]\n\n", eng->phy.PHY_10h);
                        if ( eng->run.TM_IOTiming ) PRINTF( FP_IO, "\n\n[Warning] Page 7 Register 16, bit 12 must be 0 (TXC should be output mode)[Reg10h_7:%04x]\n\n", eng->phy.PHY_10h );
                        if ( !eng->run.tm_tx_only ) PRINTF( FP_LOG, "\n\n[Warning] Page 7 Register 16, bit 12 must be 0 (TXC should be output mode)[Reg10h_7:%04x]\n\n", eng->phy.PHY_10h );
                }
        } else {
                if ( ( eng->phy.PHY_10h & 0x1000 ) == 0x0000 ) {
                        phy_write( eng, 16, eng->phy.PHY_10h | 0x1000 );
                        printf("\n\n[Warning] Page 7 Register 16, bit 12 must be 1 (TXC should be input mode)[Reg10h_7:%04x]\n\n", eng->phy.PHY_10h);
                        if ( eng->run.TM_IOTiming ) PRINTF( FP_IO, "\n\n[Warning] Page 7 Register 16, bit 12 must be 1 (TXC should be input mode)[Reg10h_7:%04x]\n\n", eng->phy.PHY_10h );
                        if ( !eng->run.tm_tx_only ) PRINTF( FP_LOG, "\n\n[Warning] Page 7 Register 16, bit 12 must be 1 (TXC should be input mode)[Reg10h_7:%04x]\n\n", eng->phy.PHY_10h );
                }
        }
        phy_write( eng, 31, 0x0000 );

        if ( eng->run.tm_tx_only ) {
                if ( eng->run.TM_IEEE ) {
                        //Rev 1.0
                        phy_write( eng, 31, 0x0004 );
                        phy_write( eng, 16, 0x4077 );
                        phy_write( eng, 21, 0xc5a0 );
                        phy_write( eng, 31, 0x0000 );

                        if ( eng->run.speed_sel[ 1 ] ) {
                                phy_write( eng,  0, 0x8000 ); // Reset PHY
                                phy_wait_reset_done( eng );
                                phy_write( eng, 24, 0x0310 ); // Disable ALDPS

                                if ( eng->run.ieee_sel == 0 ) {//From Channel A (RJ45 pair 1, 2)
                                        phy_write( eng, 28, 0x40c2 ); //Force MDI
                                }
                                else {//From Channel B (RJ45 pair 3, 6)
                                        phy_write( eng, 28, 0x40c0 ); //Force MDIX
                                }
                                phy_write( eng,  0, 0x2100 );       //Force 100M/Full Duplex)
                        } else {
                        }
                }
                else {
                        phy_reset( eng );
                }
        }
        else if ( eng->phy.loopback ) {
                // Internal loopback
                if ( eng->run.speed_sel[ 1 ] ) {
                        // Enable 100M PCS loop back; RTL8201(F_FL_FN)-VB-CG_DataSheet_1.6.pdf
                        phy_write( eng, 31, 0x0000 );
                        phy_write( eng,  0, 0x6100 );
                        phy_write( eng, 31, 0x0007 );
                        phy_write( eng, 16, 0x1FF8 );
                        phy_write( eng, 16, 0x0FF8 );
                        phy_write( eng, 31, 0x0000 );
                        phy_delay(20);
                } else if ( eng->run.speed_sel[ 2 ] ) {
                        // Enable 10M PCS loop back; RTL8201(F_FL_FN)-VB-CG_DataSheet_1.6.pdf
                        phy_write( eng, 31, 0x0000 );
                        phy_write( eng,  0, 0x4100 );
                        phy_write( eng, 31, 0x0007 );
                        phy_write( eng, 16, 0x1FF8 );
                        phy_write( eng, 16, 0x0FF8 );
                        phy_write( eng, 31, 0x0000 );
                        phy_delay(20);
                }
        }
        else {
                // External loopback
                if ( eng->run.speed_sel[ 1 ] ) {
                        // Enable 100M MDI loop back Nway option; RTL8201(F_FL_FN)-VB-CG_DataSheet_1.6.pdf
                        phy_write( eng, 31, 0x0000 );
                        phy_write( eng,  4, 0x01E1 );
                        phy_write( eng,  0, 0x1200 );
                } else if ( eng->run.speed_sel[ 2 ] ) {
                        // Enable 10M MDI loop back Nway option; RTL8201(F_FL_FN)-VB-CG_DataSheet_1.6.pdf
                        phy_write( eng, 31, 0x0000 );
                        phy_write( eng,  4, 0x0061 );
                        phy_write( eng,  0, 0x1200 );
                }
//              phy_write( eng,  0, 0x8000 );
//              while ( phy_read( eng, 0 ) != 0x3100 ) {}
//              while ( phy_read( eng, 0 ) != 0x3100 ) {}
//              phy_write( eng,  0, eng->phy.PHY_00h );
////            phy_delay(100);
//              phy_delay(400);

                // Check register 0x1 bit2 Link OK or not OK
                phy_check_register ( eng, 1, 0x0004, 0x0004, 10, "set RTL8201F");
                phy_delay(300);
        }
}

//------------------------------------------------------------
/* for RTL8211F */
void recov_phy_realtek5 (MAC_ENGINE *eng) 
{
	RTK_DBG_PRINTF("\nClear RTL8211F [Start] =====>\n");
        if ( eng->run.tm_tx_only ) {
                if ( eng->run.TM_IEEE ) {
                        if ( eng->run.speed_sel[ 0 ] ) {
                                //Rev 1.0
                                phy_write( eng, 31, 0x0000 );
                                phy_write( eng,  9, 0x0000 );
                        }
                        else if ( eng->run.speed_sel[ 1 ] ) {
                                //Rev 1.0
                                phy_write( eng, 31, 0x0000 );
                                phy_write( eng, 24, 0x2118 );//RGMII
                                phy_write( eng,  9, 0x0200 );
                                phy_write( eng,  0, 0x9200 );
                                phy_wait_reset_done( eng );
                        }
                        else {
                                //Rev 1.0
                                phy_write( eng, 31, 0x0c80 );
                                phy_write( eng, 16, 0x5a00 );
                                phy_write( eng, 31, 0x0000 );
                                phy_write( eng,  4, 0x01e1 );
                                phy_write( eng,  9, 0x0200 );
                                phy_write( eng,  0, 0x9200 );
                                phy_wait_reset_done( eng );
                        }
                }
                else {
                }
        }
        else if ( eng->phy.loopback ) {
        }
        else {
                if ( eng->run.speed_sel[ 0 ] ) {
                        //Rev 1.1
                        phy_write( eng, 31, 0x0a43 );
                        phy_write( eng, 24, 0x2118 );
                        phy_write( eng,  0, 0x1040 );
                }
//              else if ( eng->run.speed_sel[ 1 ] ) {//option
//                      phy_write( eng, 31, 0x0000 );
//                      phy_write( eng,  9, 0x0200 );
//                      phy_write( eng,  0, 0x1200 );
//              }
//              else if ( eng->run.speed_sel[ 2 ] ) {//option
//                      phy_write( eng, 31, 0x0000 );
//                      phy_write( eng,  9, 0x0200 );
//                      phy_write( eng,  4, 0x01e1 );
//                      phy_write( eng,  0, 0x1200 );
//              }
                else {
                        phy_write( eng, 31, 0x0000 );
                        phy_write( eng,  0, 0x1040 );
                }

#ifdef RTK_DEBUG
#else
                // Check register 0x1A bit2 Link OK or not OK
                phy_write( eng, 31, 0x0a43 );
                phy_check_register ( eng, 26, 0x0004, 0x0000, 10, "clear RTL8211F");
                phy_write( eng, 31, 0x0000 );
#endif
        }

	RTK_DBG_PRINTF("\nClear RTL8211F [End] =====>\n");
}

//------------------------------------------------------------
void phy_realtek5 (MAC_ENGINE *eng) {//RTL8211F
	uint16_t check_value;
	uint16_t reg;

	RTK_DBG_PRINTF("\nSet RTL8211F [Start] =====>\n");

	/* select page 0xd08 to configure TX and RX delay */
	phy_write(eng, 0x1f, 0xd08);

	/* page 0xd08, reg 0x11[8] TX delay enable */
	reg = phy_read(eng, 0x11);
	if (eng->arg.ctrl.b.phy_tx_delay_en)
		reg |= BIT(8);
	else
		reg &= ~BIT(8);
	phy_write(eng, 0x11, reg);

	/* page 0xd08, reg 0x15[3] RX delay enable */
	reg = phy_read(eng, 0x15);
	if (eng->arg.ctrl.b.phy_rx_delay_en)
		reg |= BIT(3);
	else
		reg &= ~BIT(3);
	phy_write(eng, 0x15, reg);

	/* restore page 0 */
	phy_write(eng, 0x1f, 0x0);

	if (eng->run.tm_tx_only) {
		if (eng->run.TM_IEEE) {
			if (eng->run.speed_sel[0]) {
				// Rev 1.0
				phy_write(eng, 31, 0x0000);
				if (eng->run.ieee_sel == 0) { // Test Mode 1
					phy_write(eng, 9, 0x0200);
				} else if (eng->run.ieee_sel ==
					   1) { // Test Mode 2
					phy_write(eng, 9, 0x0400);
				} else { // Test Mode 4
					phy_write(eng, 9, 0x0800);
				}
			} else if (eng->run.speed_sel[1]) { // option
				// Rev 1.0
				phy_write(eng, 31, 0x0000);
				if (eng->run.ieee_sel ==
				    0) { // Output MLT-3 from Channel A
					phy_write(eng, 24, 0x2318);
				} else { // Output MLT-3 from Channel B
					phy_write(eng, 24, 0x2218);
				}
				phy_write(eng, 9, 0x0000);
				phy_write(eng, 0, 0x2100);
			} else {
				// Rev 1.0
				// 0: For Diff. Voltage/TP-IDL/Jitter with EEE
				// 1: For Diff. Voltage/TP-IDL/Jitter without
				// EEE 2: For Harmonic (all "1" patten) with EEE
				// 3: For Harmonic (all "1" patten) without EEE
				// 4: For Harmonic (all "0" patten) with EEE
				// 5: For Harmonic (all "0" patten) without EEE
				phy_write(eng, 31, 0x0000);
				phy_write(eng, 9, 0x0000);
				phy_write(eng, 4, 0x0061);
				if ((eng->run.ieee_sel & 0x1) == 0) { // with
								      // EEE
					phy_write(eng, 25, 0x0853);
				} else { // without EEE
					phy_write(eng, 25, 0x0843);
				}
				phy_write(eng, 0, 0x9200);
				phy_wait_reset_done(eng);

				if ((eng->run.ieee_sel & 0x6) ==
				    0) { // For Diff. Voltage/TP-IDL/Jitter
					phy_write(eng, 31, 0x0c80);
					phy_write(eng, 18, 0x0115);
					phy_write(eng, 16, 0x5a21);
				} else if ((eng->run.ieee_sel & 0x6) ==
					   0x2) { // For Harmonic (all "1"
						  // patten)
					phy_write(eng, 31, 0x0c80);
					phy_write(eng, 18, 0x0015);
					phy_write(eng, 16, 0xff21);
				} else { // For Harmonic (all "0" patten)
					phy_write(eng, 31, 0x0c80);
					phy_write(eng, 18, 0x0015);
					phy_write(eng, 16, 0x0021);
				}
				phy_write(eng, 31, 0x0000);
			}
		} else {
			phy_reset(eng);
		}
	} else if (eng->phy.loopback) {
		phy_reset(eng);
	} else {
		if (eng->run.speed_sel[0]) {
			check_value = 0x0004 | 0x0028;
			// Rev 1.1
			phy_write(eng, 31, 0x0a43);
			phy_write(eng, 0, 0x8000);
#ifdef RTK_DEBUG
			phy_delay(60);
#else
			phy_wait_reset_done(eng);
			phy_delay(30);
#endif

			phy_write(eng, 0, 0x0140);
			phy_write(eng, 24, 0x2d18);
#ifdef RTK_DEBUG
			phy_delay(600);
#else
			phy_delay(300);
#endif
		} else {
			if (eng->run.speed_sel[1])
				check_value = 0x0004 | 0x0018;
			else
				check_value = 0x0004 | 0x0008;
#ifdef RTK_DEBUG
#else
			phy_write(eng, 31, 0x0a43);
			phy_write(eng, 0, 0x8000);
			phy_wait_reset_done(eng);
			phy_delay(30);
#endif

			phy_write(eng, 31, 0x0000);
			phy_write(eng, 0, eng->phy.PHY_00h);
#ifdef RTK_DEBUG
			phy_delay(300);
#else
			phy_delay(150);
#endif
		}

#ifdef RTK_DEBUG
#else
		// Check register 0x1A bit2 Link OK or not OK
		phy_write(eng, 31, 0x0a43);
		phy_check_register(eng, 26, 0x0004 | 0x0038, check_value, 10,
				   "set RTL8211F");
		phy_write(eng, 31, 0x0000);
#endif
	}

	/* additional delay for PHY PCS loopback mode */
	phy_delay(500);
	RTK_DBG_PRINTF("\nSet RTL8211F [End] =====>\n");
}

//------------------------------------------------------------
//It is a LAN Switch, only support 1G internal loopback test.
void phy_realtek6 (MAC_ENGINE *eng) 
{//RTL8363S

	if (eng->run.tm_tx_only) {
		printf("This mode doesn't support in RTL8363S.\n");
	} else if (eng->phy.loopback) {

		// RXDLY2 and TXDLY2 of RTL8363S should set to LOW
		phy_basic_setting(eng);

		phy_clrset(eng, 0, 0x0000,
			       0x8000 | eng->phy.PHY_00h); // clr set//Rst PHY
		phy_wait_reset_done(eng);
		phy_delay(30);

		phy_basic_setting(eng);
		phy_delay(30);
	} else {
		printf("This mode doesn't support in RTL8363S\n");
	}
} // End void phy_realtek6 (MAC_ENGINE *eng)

//------------------------------------------------------------
void phy_smsc (MAC_ENGINE *eng) 
{//LAN8700
	phy_reset(eng);
}

//------------------------------------------------------------
void phy_micrel (MAC_ENGINE *eng) 
{//KSZ8041

        phy_reset( eng );

//      phy_write( eng, 24, 0x0600 );
}

//------------------------------------------------------------
void phy_micrel0 (MAC_ENGINE *eng)
{//KSZ8031/KSZ8051

        //For KSZ8051RNL only
        //Reg1Fh[7] = 0(default): 25MHz Mode, XI, XO(pin 9, 8) is 25MHz(crystal/oscilator).
        //Reg1Fh[7] = 1         : 50MHz Mode, XI(pin 9) is 50MHz(oscilator).
        eng->phy.PHY_1fh = phy_read( eng, 31 );
        if ( eng->phy.PHY_1fh & 0x0080 ) sprintf((char *)eng->phy.phy_name, "%s-50MHz Mode", eng->phy.phy_name);
        else                             sprintf((char *)eng->phy.phy_name, "%s-25MHz Mode", eng->phy.phy_name);

        if ( eng->run.TM_IEEE ) {
                phy_clrset( eng,  0, 0x0000, 0x8000 | eng->phy.PHY_00h );//clr set//Rst PHY
                phy_wait_reset_done( eng );

                phy_clrset( eng, 31, 0x0000, 0x2000 );//clr set//1Fh[13] = 1: Disable auto MDI/MDI-X
                phy_basic_setting( eng );
                phy_clrset( eng, 31, 0x0000, 0x0800 );//clr set//1Fh[11] = 1: Force link pass

//              phy_delay(2500);//2.5 sec
        }
        else {
                phy_reset( eng );

                //Reg16h[6] = 1         : RMII B-to-B override
                //Reg16h[1] = 1(default): RMII override
                phy_clrset( eng, 22, 0x0000, 0x0042 );//clr set
        }

        if ( eng->phy.PHY_1fh & 0x0080 )
                phy_clrset( eng, 31, 0x0000, 0x0080 );//clr set//Reset PHY will clear Reg1Fh[7]
}

//------------------------------------------------------------
//external loop 1G  : NOT Support
//external loop 100M: OK
//external loop 10M : OK
//internal loop 1G  : no  loopback stub
//internal loop 100M: no  loopback stub
//internal loop 10M : no  loopback stub
void phy_micrel1 (MAC_ENGINE *eng) 
{//KSZ9031
//      int        temp;

/*
        phy_write( eng, 13, 0x0002 );
        phy_write( eng, 14, 0x0004 );
        phy_write( eng, 13, 0x4002 );
        temp = phy_read( eng, 14 );
        //Reg2.4[ 7: 4]: RXDV Pad Skew
        phy_write( eng, 14, temp & 0xff0f | 0x0000 );
//      phy_write( eng, 14, temp & 0xff0f | 0x00f0 );
printf("Reg2.4 = %04x -> %04x\n", temp, phy_read( eng, 14 ));

        phy_write( eng, 13, 0x0002 );
        phy_write( eng, 14, 0x0005 );
        phy_write( eng, 13, 0x4002 );
        temp = phy_read( eng, 14 );
        //Reg2.5[15:12]: RXD3 Pad Skew
        //Reg2.5[11: 8]: RXD2 Pad Skew
        //Reg2.5[ 7: 4]: RXD1 Pad Skew
        //Reg2.5[ 3: 0]: RXD0 Pad Skew
        phy_write( eng, 14, 0x0000 );
//      phy_write( eng, 14, 0xffff );
printf("Reg2.5 = %04x -> %04x\n", temp, phy_read( eng, 14 ));

        phy_write( eng, 13, 0x0002 );
        phy_write( eng, 14, 0x0008 );
        phy_write( eng, 13, 0x4002 );
        temp = phy_read( eng, 14 );
        //Reg2.8[9:5]: GTX_CLK Pad Skew
        //Reg2.8[4:0]: RX_CLK Pad Skew
//      phy_write( eng, 14, temp & 0xffe0 | 0x0000 );
        phy_write( eng, 14, temp & 0xffe0 | 0x001f );
printf("Reg2.8 = %04x -> %04x\n", temp, phy_read( eng, 14 ));
*/

        if ( eng->run.tm_tx_only ) {
                if ( eng->run.TM_IEEE ) {
                        phy_reset( eng );
                }
                else {
                        phy_reset( eng );
                }
        }
        else if ( eng->phy.loopback ) {
                phy_reset( eng );
        }
        else {
                if ( eng->run.speed_sel[ 0 ] ) {
                        phy_reset( eng );//DON'T support for 1G external loopback testing
                }
                else {
                        phy_reset( eng );
                }
        }
}

//------------------------------------------------------------
//external loop 100M: OK
//external loop 10M : OK
//internal loop 100M: no  loopback stub
//internal loop 10M : no  loopback stub
void phy_micrel2 (MAC_ENGINE *eng) 
{//KSZ8081

        if ( eng->run.tm_tx_only ) {
                if ( eng->run.TM_IEEE ) {
                        phy_reset( eng );
                }
                else {
                        phy_reset( eng );
                }
        }
        else if ( eng->phy.loopback ) {
                phy_reset( eng );
        }
        else {
                if ( eng->run.speed_sel[ 1 ] )
                        phy_reset( eng );
                else
                        phy_reset( eng );
        }
}

//------------------------------------------------------------
void recov_phy_vitesse (MAC_ENGINE *eng) {//VSC8601
        if ( eng->run.tm_tx_only ) {
//              if ( eng->run.TM_IEEE ) {
//              }
//              else {
//              }
        }
        else if ( eng->phy.loopback ) {
        }
        else {
                if ( eng->run.speed_sel[ 0 ] ) {
                        phy_write( eng, 24, eng->phy.PHY_18h );
                        phy_write( eng, 18, eng->phy.PHY_12h );
                }
        }
}

//------------------------------------------------------------
void phy_vitesse (MAC_ENGINE *eng)
{//VSC8601

        if ( eng->run.tm_tx_only ) {
                if ( eng->run.TM_IEEE ) {
                        phy_reset( eng );
                }
                else {
                        phy_reset( eng );
                }
        }
        else if ( eng->phy.loopback ) {
                phy_reset( eng );
        }
        else {
                if ( eng->run.speed_sel[ 0 ] ) {
                        eng->phy.PHY_18h = phy_read( eng, 24 );
                        eng->phy.PHY_12h = phy_read( eng, PHY_INER );

                        phy_reset( eng );

                        phy_write( eng, 24, eng->phy.PHY_18h | 0x0001 );
                        phy_write( eng, 18, eng->phy.PHY_12h | 0x0020 );
                }
                else {
                        phy_reset( eng );
                }
        }
}

//------------------------------------------------------------
void recov_phy_atheros (MAC_ENGINE *eng) {//AR8035
	if (eng->run.tm_tx_only) {
		if (eng->run.TM_IEEE) {
		} else {
		}
	} else if (eng->phy.loopback) {
	} else {
		phy_clrset(
		    eng, 11, 0x0000,
		    0x8000); // clr set//Disable hibernate: Reg0Bh[15] = 0
		phy_clrset(
		    eng, 17, 0x0001,
		    0x0000); // clr set//Enable external loopback: Reg11h[0] = 1
	}
}

//------------------------------------------------------------
void phy_atheros (MAC_ENGINE *eng) 
{
	// Reg0b[15]: Power saving
	phy_write(eng, 29, 0x000b);
	eng->phy.PHY_1eh = phy_read(eng, 30);
	if (eng->phy.PHY_1eh & 0x8000) {
		printf("\n\n[Warning] Debug register offset = 11, bit 15 must "
		       "be 0 [%04x]\n\n",
		       eng->phy.PHY_1eh);
		if (eng->run.TM_IOTiming)
			PRINTF(FP_IO,
			       "\n\n[Warning] Debug register offset = 11, bit "
			       "15 must be 0 [%04x]\n\n",
			       eng->phy.PHY_1eh);
		if (!eng->run.tm_tx_only)
			PRINTF(FP_LOG,
			       "\n\n[Warning] Debug register offset = 11, bit "
			       "15 must be 0 [%04x]\n\n",
			       eng->phy.PHY_1eh);

		phy_write(eng, 30, eng->phy.PHY_1eh & 0x7fff);
	}
	//      phy_write( eng, 30, (eng->phy.PHY_1eh & 0x7fff) | 0x8000 );

	// Check RGMIIRXCK delay (Sel_clk125m_dsp)
	phy_write(eng, 29, 0x0000);
	eng->phy.PHY_1eh = phy_read(eng, 30);
	if (eng->phy.PHY_1eh & 0x8000) {
		printf("\n\n[Warning] Debug register offset = 0, bit 15 must "
		       "be 0 [%04x]\n\n",
		       eng->phy.PHY_1eh);
		if (eng->run.TM_IOTiming)
			PRINTF(FP_IO,
			       "\n\n[Warning] Debug register offset = 0, bit "
			       "15 must be 0 [%04x]\n\n",
			       eng->phy.PHY_1eh);
		if (!eng->run.tm_tx_only)
			PRINTF(FP_LOG,
			       "\n\n[Warning] Debug register offset = 0, bit "
			       "15 must be 0 [%04x]\n\n",
			       eng->phy.PHY_1eh);

		phy_write(eng, 30, eng->phy.PHY_1eh & 0x7fff);
	}
	//      phy_write( eng, 30, (eng->phy.PHY_1eh & 0x7fff) | 0x8000 );

	// Check RGMIITXCK delay (rgmii_tx_clk_dly)
	phy_write(eng, 29, 0x0005);
	eng->phy.PHY_1eh = phy_read(eng, 30);
	if (eng->phy.PHY_1eh & 0x0100) {
		printf("\n\n[Warning] Debug register offset = 5, bit 8 must be "
		       "0 [%04x]\n\n",
		       eng->phy.PHY_1eh);
		if (eng->run.TM_IOTiming)
			PRINTF(FP_IO,
			       "\n\n[Warning] Debug register offset = 5, bit 8 "
			       "must be 0 [%04x]\n\n",
			       eng->phy.PHY_1eh);
		if (!eng->run.tm_tx_only)
			PRINTF(FP_LOG,
			       "\n\n[Warning] Debug register offset = 5, bit 8 "
			       "must be 0 [%04x]\n\n",
			       eng->phy.PHY_1eh);

		phy_write(eng, 30, eng->phy.PHY_1eh & 0xfeff);
	}
	//      phy_write( eng, 30, (eng->phy.PHY_1eh & 0xfeff) | 0x0100 );

	// Check CLK_25M output (Select_clk125m)
	phy_write(eng, 13, 0x0007);
	phy_write(eng, 14, 0x8016);
	phy_write(eng, 13, 0x4007);
	eng->phy.PHY_0eh = phy_read(eng, 14);
	if ((eng->phy.PHY_0eh & 0x0018) != 0x0018) {
		printf("\n\n[Warning] Device addrress = 7, Addrress ofset = "
		       "0x8016, bit 4~3 must be 3 [%04x]\n\n",
		       eng->phy.PHY_0eh);
		if (eng->run.TM_IOTiming)
			PRINTF(FP_IO,
			       "\n\n[Warning] Device addrress = 7, Addrress "
			       "ofset = 0x8016, bit 4~3 must be 3 [%04x]\n\n",
			       eng->phy.PHY_0eh);
		if (!eng->run.tm_tx_only)
			PRINTF(FP_LOG,
			       "\n\n[Warning] Device addrress = 7, Addrress "
			       "ofset = 0x8016, bit 4~3 must be 3 [%04x]\n\n",
			       eng->phy.PHY_0eh);
		printf("          The CLK_25M don't ouput 125MHz clock for the "
		       "RGMIICK !!!\n\n");

		phy_write(eng, 14, (eng->phy.PHY_0eh & 0xffe7) | 0x0018);
	}

	if (eng->run.tm_tx_only) {
		if (eng->run.TM_IEEE) {
			phy_write(eng, 0, eng->phy.PHY_00h);
		} else {
			phy_write(eng, 0, eng->phy.PHY_00h);
		}
	} else if (eng->phy.loopback) {
		phy_write(eng, 0, eng->phy.PHY_00h);
	} else {
		phy_clrset(
		    eng, 11, 0x8000,
		    0x0000); // clr set//Disable hibernate: Reg0Bh[15] = 0
		phy_clrset(
		    eng, 17, 0x0000,
		    0x0001); // clr set//Enable external loopback: Reg11h[0] = 1

		phy_write(eng, 0, eng->phy.PHY_00h | 0x8000);
#ifdef Delay_PHYRst
		phy_delay(Delay_PHYRst);
#endif
	}
}

//------------------------------------------------------------
void phy_motor_comm(MAC_ENGINE *eng)
{
	uint32_t reg;

	// RX internal delay
	phy_write(eng, 0x1e, 0xA001);
	reg = phy_read(eng, 0x1f);
	if (eng->arg.ctrl.b.phy_rx_delay_en)
		reg |= BIT(8);
	else
		reg &= ~BIT(8);
	phy_write(eng, 0x1F, reg);

	if (eng->phy.loopback) {
		if (eng->run.speed_sel[0])
			phy_write(eng, 0, 0X4140);
		else if (eng->run.speed_sel[1])
			phy_write(eng, 0, 0x6100);
		else
			phy_write(eng, 0, 0x4100);
	} else {
		// Disable sleep bode
		phy_write(eng, 0x1e, 0x27);
		reg = phy_read(eng, 0x1f) | BIT(15);
		phy_write(eng, 0x1F, reg);
		// Enable External loopback
		phy_write(eng, 0x1e, 0x0A);
		reg = phy_read(eng, 0x1f) | BIT(4);
		phy_write(eng, 0x1F, reg);

		if (eng->run.speed_sel[0])
			phy_write(eng, 0, 0x8140);
		else if (eng->run.speed_sel[1])
			phy_write(eng, 0, 0xA100);
		else
			phy_write(eng, 0, 0x8100);
	}
	DELAY(200);
}

void recov_phy_motor_comm(MAC_ENGINE *eng)
{
	uint32_t reg;

	// Disable sleep bode
	phy_write(eng, 0x1e, 0x27);
	reg = phy_read(eng, 0x1f) | BIT(15);
	phy_write(eng, 0x1F, reg);
	// Disable External loopback
	phy_write(eng, 0x1e, 0x0A);
	reg = phy_read(eng, 0x1f) & ~BIT(4);
	phy_write(eng, 0x1F, reg);

	DELAY(200);
}

//------------------------------------------------------------
void phy_default (MAC_ENGINE *eng) 
{
	nt_log_func_name();

	phy_reset(eng);
}

//------------------------------------------------------------
// PHY Init
//------------------------------------------------------------
/**
 * @return	1->addr found,  0->else
*/
uint32_t phy_find_addr(MAC_ENGINE *eng)
{
	uint32_t value, value2;
	uint32_t ret = 0;
	int8_t phy_addr_org;

	nt_log_func_name();
        
        phy_addr_org = eng->phy.Adr;
        value = phy_read(eng, PHY_REG_ID_1);
	
        ret = PHY_IS_VALID(value);
        if ((ret == 0) && (eng->arg.ctrl.b.skip_phy_id_check)) {
		value = phy_read(eng, PHY_REG_BMCR);
		if ((value & BIT(15)) && (0 == eng->arg.ctrl.b.skip_phy_init)) {
                        /* user wants to skip PHY init but PHY is in reset state
                         * this case should not be allowed.
                         */
		} else {
			ret = 1;
		}
	}

	if (ret == 0) {
		for (eng->phy.Adr = 0; eng->phy.Adr < 32; eng->phy.Adr++) {
			value = phy_read(eng, PHY_REG_ID_1);
			value2 = phy_read(eng, PHY_REG_ID_2);
			// YT8521SH ID1 is zero, so need to check ID2
			if (value == 0 && value2 == 0x11A) {
				ret = 1;
				break;
			}
			if (PHY_IS_VALID(value)) {
				ret = 1;
				break;
			}
		}
	}

	if (ret == 0)
		eng->phy.Adr = eng->arg.phy_addr;

	if (0 == eng->arg.ctrl.b.skip_phy_init) {
		if (ret == 1) {
			if (phy_addr_org != eng->phy.Adr) {
				phy_scan_id(eng, STD_OUT);
			}
		} else {
			phy_scan_id(eng, STD_OUT);
			FindErr(eng, Err_Flag_PHY_Type);
		}
	}


	eng->phy.id1 = phy_read(eng, PHY_REG_ID_1);
	eng->phy.id2 = phy_read(eng, PHY_REG_ID_2);
        value = (eng->phy.id2 << 16) | eng->phy.id1;
        
	if (0 == eng->arg.ctrl.b.skip_phy_id_check) {
                if ((value == 0) || (value == 0xffffffff)) {
                        sprintf((char *)eng->phy.phy_name, "--");
                        if (0 == eng->arg.ctrl.b.skip_phy_init)
			        FindErr(eng, Err_Flag_PHY_Type);
                }
        }

        return ret;
}

//------------------------------------------------------------
void phy_set00h (MAC_ENGINE *eng) 
{
	nt_log_func_name();

	eng->phy.PHY_00h = BIT(8);

	if (eng->run.speed_sel[0])
		eng->phy.PHY_00h |= BIT(6);
	else if (eng->run.speed_sel[1])
		eng->phy.PHY_00h |= BIT(13);

	if (eng->phy.loopback)
		eng->phy.PHY_00h |= BIT(14);
}

static uint32_t phy_check_id(MAC_ENGINE *p_eng, const struct phy_desc *p_phy) 
{
        uint32_t value, id;

        value  = (p_eng->phy.id1 << 16) | (p_eng->phy.id2 & p_phy->id2_mask);
        id  = (p_phy->id1 << 16) | (p_phy->id2 & p_phy->id2_mask);
        debug("%s:value %04x, id %04x\n", __func__, value, id);

        if (value == id)
                return 1;
	
        return 0;
}

void phy_select(MAC_ENGINE *eng, PHY_ENGINE *phyeng)
{
	int i;
	const struct phy_desc *p_phy;
	nt_log_func_name();

	/* set default before lookup */
	sprintf((char *)eng->phy.phy_name, "default");
	phyeng->fp_set = phy_default;
	phyeng->fp_clr = NULL;

	if (eng->phy.default_phy) {
		debug("use default PHY\n");
	} else {
		for (i = 0; i < PHY_LOOKUP_N; i++) {
			p_phy = &phy_lookup_tbl[i];
			if (phy_check_id(eng, p_phy)) {
				sprintf((char *)eng->phy.phy_name,
					(char *)p_phy->name);
				phyeng->fp_set = p_phy->cfg.fp_set;
				phyeng->fp_clr = p_phy->cfg.fp_clr;
				break;
			}
		}
	}

	if (eng->arg.ctrl.b.skip_phy_init) {
		phyeng->fp_set = NULL;
		phyeng->fp_clr = NULL;
	} else if (eng->arg.ctrl.b.skip_phy_deinit) {
		phyeng->fp_clr = NULL;
	}
}

//------------------------------------------------------------
void recov_phy (MAC_ENGINE *eng, PHY_ENGINE *phyeng) 
{
	nt_log_func_name();

	if (phyeng->fp_clr != NULL)
        	(*phyeng->fp_clr)( eng );
}

//------------------------------------------------------------
void init_phy (MAC_ENGINE *eng, PHY_ENGINE *phyeng) 
{
	nt_log_func_name();

	if (DbgPrn_PHYInit)
		phy_dump(eng);

	phy_set00h(eng);
	if (phyeng->fp_set != NULL)
		(*phyeng->fp_set)(eng);

	if (DbgPrn_PHYInit)
		phy_dump(eng);
}
