/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _ABI_MACH_ASPEED_AST2400_RESET_H_
#define _ABI_MACH_ASPEED_AST2400_RESET_H_

/*
 * The values are intentionally layed out as flags in
 * WDT reset parameter.
 */
#define ASPEED_RESET_RESERVED31	(31)
#define ASPEED_RESET_RESERVED30	(30)
#define ASPEED_RESET_RESERVED29	(29)
#define ASPEED_RESET_RESERVED28	(28)
#define ASPEED_RESET_RESERVED27	(27)
#define ASPEED_RESET_RESERVED26	(26)
#define ASPEED_RESET_XDMA		(25)
#define ASPEED_RESET_MCTP		(24)
#define ASPEED_RESET_ADC		(23)
#define ASPEED_RESET_JTAG_MASTER	(22)
#define ASPEED_RESET_RESERVED21	(21)
#define ASPEED_RESET_RESERVED20	(20)
#define ASPEED_RESET_RESERVED19	(19)
#define ASPEED_RESET_MIC		(18)
#define ASPEED_RESET_RESERVED17	(17)
#define ASEPPD_RESET_SDIO		(16)
#define ASPEED_RESET_UHCI		(15)
#define ASPEED_RESET_EHCI_P1	(14)
#define ASPEED_RESET_CRT		(13)
#define ASPEED_RESET_MAC2		(12)
#define ASPEED_RESET_MAC1		(11)
#define ASPEED_RESET_PECI		(10)
#define ASPEED_RESET_PWM		(9)
#define ASPEED_RESET_PCI_VGA	(8)
#define ASPEED_RESET_2D			(7)
#define ASPEED_RESET_VIDEO		(6)
#define ASPEED_RESET_LPC		(5)
#define ASPEED_RESET_HACE		(4)
#define ASPEED_RESET_EHCI_P2	(3)
#define ASPEED_RESET_I2C		(2)
#define ASPEED_RESET_AHB		(1)
#define ASPEED_RESET_SDRAM		(0)

#endif  /* _ABI_MACH_ASPEED_AST2400_RESET_H_ */
