// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 ASPEED Technology Inc.
 */

#include <asm/io.h>
#include <dm.h>
#include <dm/pinctrl.h>
#include <errno.h>
#include <vsprintf.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>

#define AST27X5_SOC1_MUX_BASE		0x400
#define AST27X5_SOC1_PCIE_REG		0x908
#define AST27X5_SOC1_USB_MODE_REG	0x3b0
#define AST27X5_SOC1_SGMII_REG		0x46c
#define AST27X5_SOC1_MUX_MASK		0x7
#define AST27X5_SOC1_MUX_BITS_PER_PIN	4
#define AST27X5_SOC1_MUX_PINS_PER_REG	8

#define AST27X5_SOC1_BIAS_BASE		0x470
#define AST27X5_SOC1_PULL_DOWN_MASK	(0x1 << 8)
#define AST27X5_SOC1_PULL_UP_MASK	(0x7 << 4)
#define AST27X5_SOC1_PULL_UP_SHIFT	4

#define AST27X5_SOC1_DRV_BASE		0x490
#define AST27X5_SOC1_DRV_MASK		0x3
#define AST27X5_SOC1_DRV_BITS_PER_PIN	16
#define AST27X5_SOC1_DRV_PINS_PER_REG	2

enum {
	Y22,
	U22,
	U16,
	U15,
	U23,
	U19,
	V18,
	V19,
	G10,
	G9,
	G8,
	F9,
	F10,
	H11,
	E10,
	G11,
	F12,
	D10,
	H7,
	H5,
	G4,
	F8,
	G5,
	Y24,
	P13,
	G12,
	H12,
	T17,
	T19,
	Y26,
	G15,
	V26,
	E9,
	D9,
	C9,
	H13,
	G13,
	AA24,
	Y23,
	M12,
	N13,
	M13,
	L12,
	A6,
	B7,
	A7,
	A8,
	B8,
	U26,
	E16,
	P26,
	E8,
	C12,
	L25,
	M25,
	N25,
	M26,
	N26,
	B19,
	B20,
	HOLE0,
	HOLE1,
	HOLE2,
	HOLE3,
	AD25,
	AF24,
	AC25,
	AE24,
	AC24,
	AD24,
	V24,
	AD19,
	B13,
	C15,
	A12,
	B14,
	C16,
	D17,
	A14,
	A15,
	F13,
	R15,
	E12,
	J11,
	F14,
	T18,
	E13,
	AA25,
	D15,
	D14,
	R26,
	P25,
	D13,
	F17,
	D16,
	B10,
	AD17,
	AC22,
	AC20,
	AC19,
	AA19,
	Y20,
	W23,
	AB20,
	AA22,
	T14,
	U24,
	U21,
	V22,
	U20,
	V20,
	U17,
	W22,
	AA15,
	AA16,
	AA17,
	AA18,
	AA21,
	W20,
	W18,
	AF25,
	W25,
	AD23,
	V25,
	V23,
	AD20,
	AD21,
	AE25,
	A9,
	B9,
	J10,
	H10,
	F11,
	J9,
	E11,
	AE26,
	AC23,
	AD18,
	AD22,
	AC17,
	AC18,
	AC21,
	W24,
	AA20,
	Y19,
	AB21,
	AB22,
	AB19,
	V16,
	U14,
	C10,
	G7,
	F15,
	T25,
	E14,
	T24,
	Y25,
	U25,
	E15,
	D12,
	D11,
	C11,
	F16,
	T26,
	W26,
	R25,
	AD26,
	AC26,
	B16,
	C18,
	B18,
	A18,
	A19,
	C22,
	D22,
	C21,
	B11,
	B12,
	A11,
	C13,
	A10,
	C14,
	B15,
	A13,
	C20,
	J26,
	E22,
	E23,
	F26,
	G26,
	G25,
	H24,
	W14,
	W16,
	W15,
	W19,
	Y15,
	Y16,
	AA13,
	AA14,
	AB18,
	AB15,
	V15,
	V14,
	Y13,
	Y14,
	AB16,
	AB17,
	C17,
	A16,
	B17,
	A17,
	J25,
	C19,
	L26,
	K26,
	SGMII0,
	PCIERC2_PERST,
	PORTC_MODE,
	PORTD_MODE,
};

struct ast27x5_soc1_field {
	u32 reg;
	u32 shift;
	u32 mask;
};

struct ast27x5_soc1_group {
	const char *name;
	const unsigned int *pins;
	unsigned int npins;
};

struct ast27x5_soc1_function {
	const char *name;
	const char *const *groups;
	const u8 *muxvals;
	unsigned int ngroups;
};

struct ast27x5_soc1_pinctrl_priv {
	void __iomem *scu;
};

#define PIN(n) \
	[n] = #n

static const char *const ast27x5_soc1_pin_names[] = {
	PIN(Y22),
	PIN(U22),
	PIN(U16),
	PIN(U15),
	PIN(U23),
	PIN(U19),
	PIN(V18),
	PIN(V19),
	PIN(G10),
	PIN(G9),
	PIN(G8),
	PIN(F9),
	PIN(F10),
	PIN(H11),
	PIN(E10),
	PIN(G11),
	PIN(F12),
	PIN(D10),
	PIN(H7),
	PIN(H5),
	PIN(G4),
	PIN(F8),
	PIN(G5),
	PIN(Y24),
	PIN(P13),
	PIN(G12),
	PIN(H12),
	PIN(T17),
	PIN(T19),
	PIN(Y26),
	PIN(G15),
	PIN(V26),
	PIN(E9),
	PIN(D9),
	PIN(C9),
	PIN(H13),
	PIN(G13),
	PIN(AA24),
	PIN(Y23),
	PIN(M12),
	PIN(N13),
	PIN(M13),
	PIN(L12),
	PIN(A6),
	PIN(B7),
	PIN(A7),
	PIN(A8),
	PIN(B8),
	PIN(U26),
	PIN(E16),
	PIN(P26),
	PIN(E8),
	PIN(C12),
	PIN(L25),
	PIN(M25),
	PIN(N25),
	PIN(M26),
	PIN(N26),
	PIN(B19),
	PIN(B20),
	PIN(HOLE0),
	PIN(HOLE1),
	PIN(HOLE2),
	PIN(HOLE3),
	PIN(AD25),
	PIN(AF24),
	PIN(AC25),
	PIN(AE24),
	PIN(AC24),
	PIN(AD24),
	PIN(V24),
	PIN(AD19),
	PIN(B13),
	PIN(C15),
	PIN(A12),
	PIN(B14),
	PIN(C16),
	PIN(D17),
	PIN(A14),
	PIN(A15),
	PIN(F13),
	PIN(R15),
	PIN(E12),
	PIN(J11),
	PIN(F14),
	PIN(T18),
	PIN(E13),
	PIN(AA25),
	PIN(D15),
	PIN(D14),
	PIN(R26),
	PIN(P25),
	PIN(D13),
	PIN(F17),
	PIN(D16),
	PIN(B10),
	PIN(AD17),
	PIN(AC22),
	PIN(AC20),
	PIN(AC19),
	PIN(AA19),
	PIN(Y20),
	PIN(W23),
	PIN(AB20),
	PIN(AA22),
	PIN(T14),
	PIN(U24),
	PIN(U21),
	PIN(V22),
	PIN(U20),
	PIN(V20),
	PIN(U17),
	PIN(W22),
	PIN(AA15),
	PIN(AA16),
	PIN(AA17),
	PIN(AA18),
	PIN(AA21),
	PIN(W20),
	PIN(W18),
	PIN(AF25),
	PIN(W25),
	PIN(AD23),
	PIN(V25),
	PIN(V23),
	PIN(AD20),
	PIN(AD21),
	PIN(AE25),
	PIN(A9),
	PIN(B9),
	PIN(J10),
	PIN(H10),
	PIN(F11),
	PIN(J9),
	PIN(E11),
	PIN(AE26),
	PIN(AC23),
	PIN(AD18),
	PIN(AD22),
	PIN(AC17),
	PIN(AC18),
	PIN(AC21),
	PIN(W24),
	PIN(AA20),
	PIN(Y19),
	PIN(AB21),
	PIN(AB22),
	PIN(AB19),
	PIN(V16),
	PIN(U14),
	PIN(C10),
	PIN(G7),
	PIN(F15),
	PIN(T25),
	PIN(E14),
	PIN(T24),
	PIN(Y25),
	PIN(U25),
	PIN(E15),
	PIN(D12),
	PIN(D11),
	PIN(C11),
	PIN(F16),
	PIN(T26),
	PIN(W26),
	PIN(R25),
	PIN(AD26),
	PIN(AC26),
	PIN(B16),
	PIN(C18),
	PIN(B18),
	PIN(A18),
	PIN(A19),
	PIN(C22),
	PIN(D22),
	PIN(C21),
	PIN(B11),
	PIN(B12),
	PIN(A11),
	PIN(C13),
	PIN(A10),
	PIN(C14),
	PIN(B15),
	PIN(A13),
	PIN(C20),
	PIN(J26),
	PIN(E22),
	PIN(E23),
	PIN(F26),
	PIN(G26),
	PIN(G25),
	PIN(H24),
	PIN(W14),
	PIN(W16),
	PIN(W15),
	PIN(W19),
	PIN(Y15),
	PIN(Y16),
	PIN(AA13),
	PIN(AA14),
	PIN(AB18),
	PIN(AB15),
	PIN(V15),
	PIN(V14),
	PIN(Y13),
	PIN(Y14),
	PIN(AB16),
	PIN(AB17),
	PIN(C17),
	PIN(A16),
	PIN(B17),
	PIN(A17),
	PIN(J25),
	PIN(C19),
	PIN(L26),
	PIN(K26),
	PIN(SGMII0),
	PIN(PCIERC2_PERST),
	PIN(PORTC_MODE),
	PIN(PORTD_MODE),
};

#define PIN_GROUP(name, ...) \
	static const unsigned int name##_pins[] = { __VA_ARGS__ }

/* Pin groups and functions */
PIN_GROUP(ESPI1, Y22, U22, U16, U15, U23, U19, V18, V19);
PIN_GROUP(LPC1, Y22, U22, U16, U15, U23, U19, V18, V19);
PIN_GROUP(TACH0, G10);
PIN_GROUP(THRU0, G10, G9);
PIN_GROUP(SCMGPO0, G10);
PIN_GROUP(TACH1, G9);
PIN_GROUP(SCMGPO1, G9);
PIN_GROUP(TACH2, G8);
PIN_GROUP(THRU1, G8, F9);
PIN_GROUP(SCMGPO2, G8);
PIN_GROUP(TACH3, F9);
PIN_GROUP(SCMGPO3, F9);
PIN_GROUP(TACH4, F10);
PIN_GROUP(NCTS5, F10);
PIN_GROUP(SCMGPO4, F10);
PIN_GROUP(TACH5, H11);
PIN_GROUP(NDCD5, H11);
PIN_GROUP(SCMGPO5, H11);
PIN_GROUP(TACH6, E10);
PIN_GROUP(NDSR5, E10);
PIN_GROUP(SCMGPO6, E10);
PIN_GROUP(TACH7, G11);
PIN_GROUP(NRI5, G11);
PIN_GROUP(SCMGPO7, G11);
PIN_GROUP(TACH8, F12);
PIN_GROUP(NDTR5, F12);
PIN_GROUP(SCMGPO8, F12);
PIN_GROUP(TACH9, D10);
PIN_GROUP(NRTS5, D10);
PIN_GROUP(SCMGPO9, D10);
PIN_GROUP(TACH10, H7);
PIN_GROUP(SALT12, H7);
PIN_GROUP(NCTS6, H7);
PIN_GROUP(SCMGPO10, H7);
PIN_GROUP(TACH11, H5);
PIN_GROUP(SALT13, H5);
PIN_GROUP(NDCD6, H5);
PIN_GROUP(SCMGPO11, H5);
PIN_GROUP(TACH12, G4);
PIN_GROUP(SALT14, G4);
PIN_GROUP(NDSR6, G4);
PIN_GROUP(SCMGPI0, G4);
PIN_GROUP(TACH13, F8);
PIN_GROUP(SALT15, F8);
PIN_GROUP(NRI6, F8);
PIN_GROUP(SCMGPI1, F8);
PIN_GROUP(TACH14, G5);
PIN_GROUP(PME, G5);
PIN_GROUP(PME1, M26);
PIN_GROUP(NDTR6, G5);
PIN_GROUP(SCMGPI2, G5);
PIN_GROUP(TACH15, Y24);
PIN_GROUP(SMI, Y24);
PIN_GROUP(SMI1, N26);
PIN_GROUP(NRTS6, Y24);
PIN_GROUP(SCMGPI3, Y24);
PIN_GROUP(PWM0, P13);
PIN_GROUP(SIOPBON0, P13);
PIN_GROUP(PWM1, G12);
PIN_GROUP(SIOPBIN0, G12);
PIN_GROUP(PWM2, H12);
PIN_GROUP(SIOSCIN0, H12);
PIN_GROUP(PWM3, T17);
PIN_GROUP(SIOS3N0, T17);
PIN_GROUP(PWM4, T19);
PIN_GROUP(SIOS5N0, T19);
PIN_GROUP(PWM5, Y26);
PIN_GROUP(SIOPWREQN0, Y26);
PIN_GROUP(PWM6, G15);
PIN_GROUP(SIOONCTRLN0, G15);
PIN_GROUP(PWM7, V26);
PIN_GROUP(SIOPWRGD0, V26);
PIN_GROUP(NCTS0, E9);
PIN_GROUP(NDCD0, D9);
PIN_GROUP(NDSR0, C9);
PIN_GROUP(NRI0, H13);
PIN_GROUP(NDTR0, G13);
PIN_GROUP(NRTS0, AA24);
PIN_GROUP(SIOPBON1, E9);
PIN_GROUP(SIOPBIN1, D9);
PIN_GROUP(SIOSCIN1, C9);
PIN_GROUP(SIOS3N1, H13);
PIN_GROUP(SIOS5N1, G13);
PIN_GROUP(SIOPWREQN1, AA24);
PIN_GROUP(TXD0, Y23);
PIN_GROUP(RXD0, M12);
PIN_GROUP(NCTS1, N13);
PIN_GROUP(SD, N13, M13, L12, A6, B7, A7, A8, B8);
PIN_GROUP(NDCD1, M13);
PIN_GROUP(NDSR1, L12);
PIN_GROUP(SALT2, L12);
PIN_GROUP(NRI1, A6);
PIN_GROUP(SALT3, A6);
PIN_GROUP(NDTR1, B7);
PIN_GROUP(NRTS1, A7);
PIN_GROUP(TXD1, A8);
PIN_GROUP(RXD1, B8);
PIN_GROUP(TXD2, U26);
PIN_GROUP(RXD2, E16);
PIN_GROUP(DI3C12, E16, E8);
PIN_GROUP(I2C12, E16, E8);
PIN_GROUP(TXD3, P26);
PIN_GROUP(WDTRST0N, P26);
PIN_GROUP(RXD3, E8);
PIN_GROUP(WDTRST1N, E8);
PIN_GROUP(TXD5, C12);
PIN_GROUP(WDTRST2N, C12);
PIN_GROUP(DI3C13, C12, L25);
PIN_GROUP(I2C13, C12, L25);
PIN_GROUP(RXD5, L25);
PIN_GROUP(WDTRST3N, L25);
PIN_GROUP(TXD6, M25);
PIN_GROUP(SALT0, M25);
PIN_GROUP(DI3C14, M25, N25);
PIN_GROUP(I2C14, M25, N25);
PIN_GROUP(RXD6, N25);
PIN_GROUP(SALT1, N25);
PIN_GROUP(TXD7, M26);
PIN_GROUP(I2C15, M26, N26);
PIN_GROUP(DI3C15, M26, N26);
PIN_GROUP(RXD7, N26);
PIN_GROUP(SGPM1, B19, B20, C19, L26);
PIN_GROUP(SGPM1LD_R, K26);
PIN_GROUP(WDTRST7N, B19);
PIN_GROUP(SFF8485B, B19, B20, C19, L26);
PIN_GROUP(I3C12, AD25, AF24);
PIN_GROUP(I3C13, AC25, AE24);
PIN_GROUP(I3C14, AC24, AD24);
PIN_GROUP(I3C15, V24, AD19);
PIN_GROUP(I3C4, B13, C15);
PIN_GROUP(DI2C0, B13, C15);
PIN_GROUP(I3C5, A12, B14);
PIN_GROUP(DI2C1, A12, B14);
PIN_GROUP(I3C6, C16, D17);
PIN_GROUP(DI2C2, C16, D17);
PIN_GROUP(I3C7, A14, A15);
PIN_GROUP(DI2C3, A14, A15);
PIN_GROUP(I3C8, F13, R15);
PIN_GROUP(FSI0, F13, R15);
PIN_GROUP(DI2C4, F13, R15);
PIN_GROUP(I3C9, E12, J11);
PIN_GROUP(FSI1, E12, J11);
PIN_GROUP(DI2C5, E12, J11);
PIN_GROUP(I3C10, F14, T18);
PIN_GROUP(FSI2, F14, T18);
PIN_GROUP(I3C11, E13, AA25);
PIN_GROUP(FSI3, E13, AA25);
PIN_GROUP(I3C0, D15, D14);
PIN_GROUP(I3C1, R26, P25);
PIN_GROUP(I3C2, D13, F17);
PIN_GROUP(I3C3, D16, B10);
PIN_GROUP(ESPI0, AD17, AC22, AC20, AC19, AA19, Y20, W23, AB20);
PIN_GROUP(LPC0, AD17, AC22, AC20, AC19, AA19, Y20, W23, AB20);
PIN_GROUP(OSCCLK, AA19);
PIN_GROUP(SPI0, AA22, T14, U24);
PIN_GROUP(QSPI0, U21, V22);
PIN_GROUP(SPI0CS1, U20);
PIN_GROUP(SPI0ABR, V20);
PIN_GROUP(SIOONCTRLN1, V20);
PIN_GROUP(TXD8, V20);
PIN_GROUP(SPI0WPN, U17);
PIN_GROUP(SIOPWRGD1, U17);
PIN_GROUP(RXD8, U17);
PIN_GROUP(SPI1, W22, AA15, AA16);
PIN_GROUP(TXD9, W22);
PIN_GROUP(RXD9, AA15);
PIN_GROUP(TXD10, AA16);
PIN_GROUP(QSPI1, AA17, AA18);
PIN_GROUP(RXD10, AA17);
PIN_GROUP(TXD11, AA18);
PIN_GROUP(SPI1CS1, AA21);
PIN_GROUP(RXD11, AA21);
PIN_GROUP(SPI1ABR, W20);
PIN_GROUP(RECOVERY, W20, W18);
PIN_GROUP(I3C, W20, W18);
PIN_GROUP(THRU2, W20, W18);
PIN_GROUP(SPI1WPN, W18);
PIN_GROUP(SPI2, AF25, W25, AD23, V25);
PIN_GROUP(QSPI2, V23, AD20);
PIN_GROUP(THRU3, V23, AD20);
PIN_GROUP(SPI2CS1, AD21);
PIN_GROUP(FWSPIABR, AE25);
PIN_GROUP(MDIO2, A9, B9);
PIN_GROUP(PE2SGRSTN, PCIERC2_PERST, A9);
PIN_GROUP(JTAGM1TRST, J10);
PIN_GROUP(JTAGM1, H10, F11, J9, E11);
PIN_GROUP(CPTRA, H10, F11, J9, E11);
PIN_GROUP(CPTRA_TRSTN, J10);
PIN_GROUP(FWSPIWPN, AE26);
PIN_GROUP(RGMII0, AC23, AD18, AD22, AC17, AC18, AC21, W24, AA20, Y19, AB21, AB22, AB19);
PIN_GROUP(RMII0, AC23, AD22, AC17, AC18, AC21, AA20, Y19, AB21);
PIN_GROUP(RMII0RCLKO, W24);
PIN_GROUP(MDIO0, V16, U14);
PIN_GROUP(VGA, C10, G7);
PIN_GROUP(RGMII1, F15, T25, E14, T24, Y25, U25, E15, D12, D11, C11, F16, T26);
PIN_GROUP(RMII1, F15, E14, T24, Y25, U25, D12, D11, C11);
PIN_GROUP(RMII1RCLKO, E15);
PIN_GROUP(SGPS, T25, E15, F16, T26);
PIN_GROUP(TXD3_ALT, E14);
PIN_GROUP(RXD3_ALT, T24);
PIN_GROUP(MDIO1, W26, R25);
PIN_GROUP(FWQSPI, AD26, AC26);
PIN_GROUP(I2C0, B16, C18);
PIN_GROUP(DI3C0, B16, C18);
PIN_GROUP(I2C1, B18, A18);
PIN_GROUP(DI3C1, B18, A18);
PIN_GROUP(I2C2, A19, C22);
PIN_GROUP(DI3C2, A19, C22);
PIN_GROUP(I2C3, D22, C21);
PIN_GROUP(DI3C3, D22, C21);
PIN_GROUP(I2C4, B11, B12);
PIN_GROUP(LTPI_PS_I2C4, B11, B12);
PIN_GROUP(DI3C4, B11, B12);
PIN_GROUP(I2C5, A11, C13);
PIN_GROUP(LTPI_PS_I2C5, A11, C13);
PIN_GROUP(DI3C5, A11, C13);
PIN_GROUP(I2C6, A10, C14);
PIN_GROUP(DI3C6, A10, C14);
PIN_GROUP(I2C7, B15, A13);
PIN_GROUP(DI3C7, B15, A13);
PIN_GROUP(I2C8, C20, J26);
PIN_GROUP(DI3C8, C20, J26);
PIN_GROUP(I2C9, E22, E23);
PIN_GROUP(DI3C9, E22, E23);
PIN_GROUP(I2C10, F26, G26);
PIN_GROUP(CANBUS, F26, G26);
PIN_GROUP(DI3C10, F26, G26);
PIN_GROUP(I2C11, G25, H24);
PIN_GROUP(DI3C11, G25, H24);
PIN_GROUP(ADC0, W14);
PIN_GROUP(GPIY0, W14);
PIN_GROUP(SALT4, W14);
PIN_GROUP(ADC1, W16);
PIN_GROUP(GPIY1, W16);
PIN_GROUP(SALT5, W16);
PIN_GROUP(ADC2, W15);
PIN_GROUP(GPIY2, W15);
PIN_GROUP(SALT6, W15);
PIN_GROUP(ADC3, W19);
PIN_GROUP(GPIY3, W19);
PIN_GROUP(SALT7, W19);
PIN_GROUP(ADC4, Y15);
PIN_GROUP(GPIY4, Y15);
PIN_GROUP(SALT8, Y15);
PIN_GROUP(ADC5, Y16);
PIN_GROUP(GPIY5, Y16);
PIN_GROUP(SALT9, Y16);
PIN_GROUP(ADC6, AA13);
PIN_GROUP(GPIY6, AA13);
PIN_GROUP(SALT10, AA13);
PIN_GROUP(ADC7, AA14);
PIN_GROUP(GPIY7, AA14);
PIN_GROUP(SALT11, AA14);
PIN_GROUP(ADC8, AB18);
PIN_GROUP(GPIZ0, AB18);
PIN_GROUP(ADC9, AB15);
PIN_GROUP(GPIZ1, AB15);
PIN_GROUP(ADC10, V15);
PIN_GROUP(GPIZ2, V15);
PIN_GROUP(ADC11, V14);
PIN_GROUP(GPIZ3, V14);
PIN_GROUP(ADC12, Y13);
PIN_GROUP(GPIZ4, Y13);
PIN_GROUP(ADC13, Y14);
PIN_GROUP(GPIZ5, Y14);
PIN_GROUP(AUXPWRGOOD0, Y14);
PIN_GROUP(AUXPWRGOOD1, AB16);
PIN_GROUP(ADC14, AB16);
PIN_GROUP(GPIZ6, AB16);
PIN_GROUP(ADC15, AB17);
PIN_GROUP(GPIZ7, AB17);
PIN_GROUP(SGPM0, C17, A16, A17, J25);
PIN_GROUP(SGPM0LD_R, B17);
PIN_GROUP(SFF8485A, C17, A16, A17, J25);
PIN_GROUP(NCTS2, C17);
PIN_GROUP(MACLINK0, C17);
PIN_GROUP(NDCD2, A16);
PIN_GROUP(MACLINK2, A16);
PIN_GROUP(HBLEDN, B17);
PIN_GROUP(NDSR2, A17);
PIN_GROUP(NRI2, J25);
PIN_GROUP(WDTRST4N, C19);
PIN_GROUP(NDTR2, C19);
PIN_GROUP(WDTRST5N, L26);
PIN_GROUP(NRTS2, L26);
PIN_GROUP(WDTRST6N, K26);
PIN_GROUP(MACLINK1, K26);
PIN_GROUP(SGMII, SGMII0);
PIN_GROUP(USB2CUD, PORTC_MODE);
PIN_GROUP(USB2CD, PORTC_MODE);
PIN_GROUP(USB2CH, PORTC_MODE);
PIN_GROUP(USB2CU, PORTC_MODE);
PIN_GROUP(USB2DD, PORTD_MODE);
PIN_GROUP(USB2DH, PORTD_MODE);

#define GROUP(n) \
	{ .name = #n, .pins = n##_pins, .npins = ARRAY_SIZE(n##_pins) }

static const struct ast27x5_soc1_group ast27x5_soc1_groups[] = {
	GROUP(ESPI1),
	GROUP(LPC1),
	GROUP(TACH0),
	GROUP(THRU0),
	GROUP(SCMGPO0),
	GROUP(TACH1),
	GROUP(SCMGPO1),
	GROUP(TACH2),
	GROUP(THRU1),
	GROUP(SCMGPO2),
	GROUP(TACH3),
	GROUP(SCMGPO3),
	GROUP(TACH4),
	GROUP(NCTS5),
	GROUP(SCMGPO4),
	GROUP(TACH5),
	GROUP(NDCD5),
	GROUP(SCMGPO5),
	GROUP(TACH6),
	GROUP(NDSR5),
	GROUP(SCMGPO6),
	GROUP(TACH7),
	GROUP(NRI5),
	GROUP(SCMGPO7),
	GROUP(TACH8),
	GROUP(NDTR5),
	GROUP(SCMGPO8),
	GROUP(TACH9),
	GROUP(NRTS5),
	GROUP(SCMGPO9),
	GROUP(TACH10),
	GROUP(SALT12),
	GROUP(NCTS6),
	GROUP(SCMGPO10),
	GROUP(TACH11),
	GROUP(SALT13),
	GROUP(NDCD6),
	GROUP(SCMGPO11),
	GROUP(TACH12),
	GROUP(SALT14),
	GROUP(NDSR6),
	GROUP(SCMGPI0),
	GROUP(TACH13),
	GROUP(SALT15),
	GROUP(NRI6),
	GROUP(SCMGPI1),
	GROUP(TACH14),
	GROUP(PME),
	GROUP(PME1),
	GROUP(NDTR6),
	GROUP(SCMGPI2),
	GROUP(TACH15),
	GROUP(SMI),
	GROUP(SMI1),
	GROUP(NRTS6),
	GROUP(SCMGPI3),
	GROUP(PWM0),
	GROUP(SIOPBON0),
	GROUP(PWM1),
	GROUP(SIOPBIN0),
	GROUP(PWM2),
	GROUP(SIOSCIN0),
	GROUP(PWM3),
	GROUP(SIOS3N0),
	GROUP(PWM4),
	GROUP(SIOS5N0),
	GROUP(PWM5),
	GROUP(SIOPWREQN0),
	GROUP(PWM6),
	GROUP(SIOONCTRLN0),
	GROUP(PWM7),
	GROUP(SIOPWRGD0),
	GROUP(NCTS0),
	GROUP(NDCD0),
	GROUP(NDSR0),
	GROUP(NRI0),
	GROUP(NDTR0),
	GROUP(NRTS0),
	GROUP(SIOPBON1),
	GROUP(SIOPBIN1),
	GROUP(SIOSCIN1),
	GROUP(SIOS3N1),
	GROUP(SIOS5N1),
	GROUP(SIOPWREQN1),
	GROUP(TXD0),
	GROUP(RXD0),
	GROUP(NCTS1),
	GROUP(SD),
	GROUP(NDCD1),
	GROUP(NDSR1),
	GROUP(SALT2),
	GROUP(NRI1),
	GROUP(SALT3),
	GROUP(NDTR1),
	GROUP(NRTS1),
	GROUP(TXD1),
	GROUP(RXD1),
	GROUP(TXD2),
	GROUP(RXD2),
	GROUP(DI3C12),
	GROUP(I2C12),
	GROUP(TXD3),
	GROUP(WDTRST0N),
	GROUP(RXD3),
	GROUP(WDTRST1N),
	GROUP(TXD5),
	GROUP(WDTRST2N),
	GROUP(DI3C13),
	GROUP(I2C13),
	GROUP(RXD5),
	GROUP(WDTRST3N),
	GROUP(TXD6),
	GROUP(SALT0),
	GROUP(DI3C14),
	GROUP(I2C14),
	GROUP(RXD6),
	GROUP(SALT1),
	GROUP(TXD7),
	GROUP(I2C15),
	GROUP(DI3C15),
	GROUP(RXD7),
	GROUP(SGPM1),
	GROUP(SGPM1LD_R),
	GROUP(WDTRST7N),
	GROUP(SFF8485B),
	GROUP(I3C12),
	GROUP(I3C13),
	GROUP(I3C14),
	GROUP(I3C15),
	GROUP(I3C4),
	GROUP(DI2C0),
	GROUP(I3C5),
	GROUP(DI2C1),
	GROUP(I3C6),
	GROUP(DI2C2),
	GROUP(I3C7),
	GROUP(DI2C3),
	GROUP(I3C8),
	GROUP(FSI0),
	GROUP(DI2C4),
	GROUP(I3C9),
	GROUP(FSI1),
	GROUP(DI2C5),
	GROUP(I3C10),
	GROUP(FSI2),
	GROUP(I3C11),
	GROUP(FSI3),
	GROUP(I3C0),
	GROUP(I3C1),
	GROUP(I3C2),
	GROUP(I3C3),
	GROUP(ESPI0),
	GROUP(LPC0),
	GROUP(OSCCLK),
	GROUP(SPI0),
	GROUP(QSPI0),
	GROUP(SPI0CS1),
	GROUP(SPI0ABR),
	GROUP(SIOONCTRLN1),
	GROUP(TXD8),
	GROUP(SPI0WPN),
	GROUP(SIOPWRGD1),
	GROUP(RXD8),
	GROUP(SPI1),
	GROUP(TXD9),
	GROUP(RXD9),
	GROUP(TXD10),
	GROUP(QSPI1),
	GROUP(RXD10),
	GROUP(TXD11),
	GROUP(SPI1CS1),
	GROUP(RXD11),
	GROUP(SPI1ABR),
	GROUP(RECOVERY),
	GROUP(I3C),
	GROUP(THRU2),
	GROUP(SPI1WPN),
	GROUP(SPI2),
	GROUP(QSPI2),
	GROUP(THRU3),
	GROUP(SPI2CS1),
	GROUP(FWSPIABR),
	GROUP(MDIO2),
	GROUP(PE2SGRSTN),
	GROUP(JTAGM1TRST),
	GROUP(JTAGM1),
	GROUP(CPTRA),
	GROUP(CPTRA_TRSTN),
	GROUP(FWSPIWPN),
	GROUP(RGMII0),
	GROUP(RMII0),
	GROUP(RMII0RCLKO),
	GROUP(MDIO0),
	GROUP(VGA),
	GROUP(RGMII1),
	GROUP(RMII1),
	GROUP(RMII1RCLKO),
	GROUP(SGPS),
	GROUP(TXD3_ALT),
	GROUP(RXD3_ALT),
	GROUP(MDIO1),
	GROUP(FWQSPI),
	GROUP(I2C0),
	GROUP(DI3C0),
	GROUP(I2C1),
	GROUP(DI3C1),
	GROUP(I2C2),
	GROUP(DI3C2),
	GROUP(I2C3),
	GROUP(DI3C3),
	GROUP(I2C4),
	GROUP(LTPI_PS_I2C4),
	GROUP(DI3C4),
	GROUP(I2C5),
	GROUP(LTPI_PS_I2C5),
	GROUP(DI3C5),
	GROUP(I2C6),
	GROUP(DI3C6),
	GROUP(I2C7),
	GROUP(DI3C7),
	GROUP(I2C8),
	GROUP(DI3C8),
	GROUP(I2C9),
	GROUP(DI3C9),
	GROUP(I2C10),
	GROUP(CANBUS),
	GROUP(DI3C10),
	GROUP(I2C11),
	GROUP(DI3C11),
	GROUP(ADC0),
	GROUP(GPIY0),
	GROUP(SALT4),
	GROUP(ADC1),
	GROUP(GPIY1),
	GROUP(SALT5),
	GROUP(ADC2),
	GROUP(GPIY2),
	GROUP(SALT6),
	GROUP(ADC3),
	GROUP(GPIY3),
	GROUP(SALT7),
	GROUP(ADC4),
	GROUP(GPIY4),
	GROUP(SALT8),
	GROUP(ADC5),
	GROUP(GPIY5),
	GROUP(SALT9),
	GROUP(ADC6),
	GROUP(GPIY6),
	GROUP(SALT10),
	GROUP(ADC7),
	GROUP(GPIY7),
	GROUP(SALT11),
	GROUP(ADC8),
	GROUP(GPIZ0),
	GROUP(ADC9),
	GROUP(GPIZ1),
	GROUP(ADC10),
	GROUP(GPIZ2),
	GROUP(ADC11),
	GROUP(GPIZ3),
	GROUP(ADC12),
	GROUP(GPIZ4),
	GROUP(ADC13),
	GROUP(GPIZ5),
	GROUP(AUXPWRGOOD0),
	GROUP(AUXPWRGOOD1),
	GROUP(ADC14),
	GROUP(GPIZ6),
	GROUP(ADC15),
	GROUP(GPIZ7),
	GROUP(SGPM0),
	GROUP(SGPM0LD_R),
	GROUP(SFF8485A),
	GROUP(NCTS2),
	GROUP(MACLINK0),
	GROUP(NDCD2),
	GROUP(MACLINK2),
	GROUP(HBLEDN),
	GROUP(NDSR2),
	GROUP(NRI2),
	GROUP(WDTRST4N),
	GROUP(NDTR2),
	GROUP(WDTRST5N),
	GROUP(NRTS2),
	GROUP(WDTRST6N),
	GROUP(MACLINK1),
	GROUP(SGMII),
	GROUP(USB2CUD),
	GROUP(USB2CD),
	GROUP(USB2CH),
	GROUP(USB2CU),
	GROUP(USB2DD),
	GROUP(USB2DH),
};

/**
 * VM() - Helper macro to unwrap a parenthesized list of arguments.
 * @...: The parenthesized list to be unwrapped.
 *
 * Since the C preprocessor treats commas inside braces {} as argument
 * separators for macros, wrap mux value lists in parentheses and strip
 * them here when initializing the compound literal.
 */
#define VM(...) __VA_ARGS__

/**
 * FUNC() - Initialize an ast27x5_soc1_function entry.
 * @n: Name of the pin function.
 * @m: Parenthesized list of mux values mapped 1:1 to the groups list.
 * @...: Variable list of pin group names associated with this function.
 */
#define FUNC(n, m, ...) \
	{ \
		.name = #n, \
		.groups = (const char *const[]){ __VA_ARGS__ }, \
		.muxvals = (const u8[]){ VM m }, \
		.ngroups = sizeof((const char *const[]){ __VA_ARGS__ }) / \
			   sizeof(char *), \
	}

static const struct ast27x5_soc1_function ast27x5_soc1_functions[] = {
	FUNC(ESPI1, (1), "ESPI1"),
	FUNC(LPC1, (2), "LPC1"),
	FUNC(TACH0, (1), "TACH0"),
	FUNC(THRU0, (2), "THRU0"),
	FUNC(SCMGPO0, (5), "SCMGPO0"),
	FUNC(TACH1, (1), "TACH1"),
	FUNC(SCMGPO1, (5), "SCMGPO1"),
	FUNC(TACH2, (1), "TACH2"),
	FUNC(THRU1, (2), "THRU1"),
	FUNC(SCMGPO2, (5), "SCMGPO2"),
	FUNC(TACH3, (1), "TACH3"),
	FUNC(SCMGPO3, (5), "SCMGPO3"),
	FUNC(TACH4, (1), "TACH4"),
	FUNC(UART5, (4, 4, 4, 4, 4, 4, 1, 1), "NCTS5", "NDCD5", "NDSR5", "NRI5", "NDTR5",
	     "NRTS5", "TXD5", "RXD5"),
	FUNC(SCMGPO4, (5), "SCMGPO4"),
	FUNC(TACH5, (1), "TACH5"),
	FUNC(SCMGPO5, (5), "SCMGPO5"),
	FUNC(TACH6, (1), "TACH6"),
	FUNC(SCMGPO6, (5), "SCMGPO6"),
	FUNC(TACH7, (1), "TACH7"),
	FUNC(SCMGPO7, (5), "SCMGPO7"),
	FUNC(TACH8, (1), "TACH8"),
	FUNC(SCMGPO8, (5), "SCMGPO8"),
	FUNC(TACH9, (1), "TACH9"),
	FUNC(SCMGPO9, (5), "SCMGPO9"),
	FUNC(TACH10, (1), "TACH10"),
	FUNC(SALT12, (2), "SALT12"),
	FUNC(UART6, (4, 4, 4, 4, 4, 4, 1, 1), "NCTS6", "NDCD6", "NDSR6", "NRI6", "NDTR6",
	     "NRTS6", "TXD6", "RXD6"),
	FUNC(SCMGPO10, (5), "SCMGPO10"),
	FUNC(TACH11, (1), "TACH11"),
	FUNC(SALT13, (2), "SALT13"),
	FUNC(SCMGPO11, (5), "SCMGPO11"),
	FUNC(TACH12, (1), "TACH12"),
	FUNC(SALT14, (2), "SALT14"),
	FUNC(SCMGPI0, (5), "SCMGPI0"),
	FUNC(TACH13, (1), "TACH13"),
	FUNC(SALT15, (2), "SALT15"),
	FUNC(SCMGPI1, (5), "SCMGPI1"),
	FUNC(TACH14, (1), "TACH14"),
	FUNC(PME, (2), "PME"),
	FUNC(PME1, (4), "PME1"),
	FUNC(SCMGPI2, (5), "SCMGPI2"),
	FUNC(TACH15, (1), "TACH15"),
	FUNC(SMI, (2), "SMI"),
	FUNC(SMI1, (4), "SMI1"),
	FUNC(SCMGPI3, (5), "SCMGPI3"),
	FUNC(PWM0, (1), "PWM0"),
	FUNC(SIOPBON0, (2), "SIOPBON0"),
	FUNC(PWM1, (1), "PWM1"),
	FUNC(SIOPBIN0, (2), "SIOPBIN0"),
	FUNC(PWM2, (1), "PWM2"),
	FUNC(SIOSCIN0, (2), "SIOSCIN0"),
	FUNC(PWM3, (1), "PWM3"),
	FUNC(SIOS3N0, (2), "SIOS3N0"),
	FUNC(PWM4, (1), "PWM4"),
	FUNC(SIOS5N0, (2), "SIOS5N0"),
	FUNC(PWM5, (1), "PWM5"),
	FUNC(SIOPWREQN0, (2), "SIOPWREQN0"),
	FUNC(PWM6, (1), "PWM6"),
	FUNC(SIOONCTRLN0, (2), "SIOONCTRLN0"),
	FUNC(PWM7, (1), "PWM7"),
	FUNC(SIOPWRGD0, (2), "SIOPWRGD0"),
	FUNC(UART0, (1, 1, 1, 1, 1, 1, 1, 1), "NCTS0", "NDCD0", "NDSR0", "NRI0", "NDTR0",
	     "NRTS0", "TXD0", "RXD0"),
	FUNC(SIOPBON1, (2), "SIOPBON1"),
	FUNC(SIOPBIN1, (2), "SIOPBIN1"),
	FUNC(SIOSCIN1, (2), "SIOSCIN1"),
	FUNC(SIOS3N1, (2), "SIOS3N1"),
	FUNC(SIOS5N1, (2), "SIOS5N1"),
	FUNC(SIOPWREQN1, (2), "SIOPWREQN1"),
	FUNC(UART1, (1, 1, 1, 1, 1, 1, 1, 1), "NCTS1", "NDCD1", "NDSR1", "NRI1", "NDTR1",
	     "NRTS1", "TXD1", "RXD1"),
	FUNC(SD, (3), "SD"),
	FUNC(SALT2, (2), "SALT2"),
	FUNC(SALT3, (2), "SALT3"),
	FUNC(UART2, (1, 1, 3, 3, 3, 3, 3, 3), "TXD2", "RXD2", "NCTS2", "NDCD2", "NDSR2",
	     "NRI2", "NDTR2", "NRTS2"),
	FUNC(I3C12, (1, 3), "I3C12", "DI3C12"),
	FUNC(I2C12, (4), "I2C12"),
	FUNC(UART3, (1, 1, 4, 4), "TXD3", "RXD3", "TXD3_ALT", "RXD3_ALT"),
	FUNC(WDTRST0N, (2), "WDTRST0N"),
	FUNC(WDTRST1N, (2), "WDTRST1N"),
	FUNC(WDTRST2N, (2), "WDTRST2N"),
	FUNC(I3C13, (1, 3), "I3C13", "DI3C13"),
	FUNC(I2C13, (4), "I2C13"),
	FUNC(WDTRST3N, (2), "WDTRST3N"),
	FUNC(SALT0, (2), "SALT0"),
	FUNC(I3C14, (1, 3), "I3C14", "DI3C14"),
	FUNC(I2C14, (4), "I2C14"),
	FUNC(SALT1, (2), "SALT1"),
	FUNC(UART7, (1, 1), "TXD7", "RXD7"),
	FUNC(I2C15, (2), "I2C15"),
	FUNC(I3C15, (1, 3), "I3C15", "DI3C15"),
	FUNC(SGPM1, (1), "SGPM1"),
	FUNC(SGPM1LD_R, (1), "SGPM1LD_R"),
	FUNC(WDTRST7N, (2), "WDTRST7N"),
	FUNC(SFF8485B, (4), "SFF8485B"),
	FUNC(I3C4, (1, 3), "I3C4", "DI3C4"),
	FUNC(I2C0, (1, 3), "I2C0", "DI2C0"),
	FUNC(I3C5, (1, 3), "I3C5", "DI3C5"),
	FUNC(I2C1, (1, 3), "I2C1", "DI2C1"),
	FUNC(I3C6, (1, 3), "I3C6", "DI3C6"),
	FUNC(I2C2, (1, 3), "I2C2", "DI2C2"),
	FUNC(I3C7, (1, 3), "I3C7", "DI3C7"),
	FUNC(I2C3, (1, 3), "I2C3", "DI2C3"),
	FUNC(I3C8, (1, 3), "I3C8", "DI3C8"),
	FUNC(FSI0, (2), "FSI0"),
	FUNC(I2C4, (1, 3), "I2C4", "DI2C4"),
	FUNC(I3C9, (1, 3), "I3C9", "DI3C9"),
	FUNC(FSI1, (2), "FSI1"),
	FUNC(I2C5, (1, 3), "I2C5", "DI2C5"),
	FUNC(I3C10, (1, 3), "I3C10", "DI3C10"),
	FUNC(FSI2, (2), "FSI2"),
	FUNC(I3C11, (1, 3), "I3C11", "DI3C11"),
	FUNC(FSI3, (2), "FSI3"),
	FUNC(I3C0, (1, 3), "I3C0", "DI3C0"),
	FUNC(I3C1, (1, 3), "I3C1", "DI3C1"),
	FUNC(I3C2, (1, 3), "I3C2", "DI3C2"),
	FUNC(I3C3, (1, 3), "I3C3", "DI3C3"),
	FUNC(ESPI0, (1), "ESPI0"),
	FUNC(LPC0, (2), "LPC0"),
	FUNC(OSCCLK, (3), "OSCCLK"),
	FUNC(SPI0, (1), "SPI0"),
	FUNC(QSPI0, (1), "QSPI0"),
	FUNC(SPI0CS1, (1), "SPI0CS1"),
	FUNC(SPI0ABR, (1), "SPI0ABR"),
	FUNC(SIOONCTRLN1, (2), "SIOONCTRLN1"),
	FUNC(UART8, (3, 3), "TXD8", "RXD8"),
	FUNC(SPI0WPN, (1), "SPI0WPN"),
	FUNC(SIOPWRGD1, (2), "SIOPWRGD1"),
	FUNC(SPI1, (1), "SPI1"),
	FUNC(UART9, (3, 3), "TXD9", "RXD9"),
	FUNC(UART10, (3, 3), "TXD10", "RXD10"),
	FUNC(QSPI1, (1), "QSPI1"),
	FUNC(UART11, (3, 3), "TXD11", "RXD11"),
	FUNC(SPI1CS1, (1), "SPI1CS1"),
	FUNC(SPI1ABR, (1), "SPI1ABR"),
	FUNC(RECOVERY, (2), "RECOVERY"),
	FUNC(I3C, (3), "I3C"),
	FUNC(THRU2, (4), "THRU2"),
	FUNC(SPI1WPN, (1), "SPI1WPN"),
	FUNC(SPI2, (1), "SPI2"),
	FUNC(QSPI2, (1), "QSPI2"),
	FUNC(THRU3, (4), "THRU3"),
	FUNC(SPI2CS1, (1), "SPI2CS1"),
	FUNC(FWSPIABR, (1), "FWSPIABR"),
	FUNC(MDIO2, (1), "MDIO2"),
	FUNC(PCIERC, (2), "PE2SGRSTN"),
	FUNC(JTAGM1TRST, (1), "JTAGM1TRST"),
	FUNC(JTAGM1, (1), "JTAGM1"),
	FUNC(CPTRA, (3), "CPTRA"),
	FUNC(CPTRA_TRSTN, (3), "CPTRA_TRSTN"),
	FUNC(FWSPIWPN, (1), "FWSPIWPN"),
	FUNC(RGMII0, (1), "RGMII0"),
	FUNC(RMII0, (2), "RMII0"),
	FUNC(RMII0RCLKO, (2), "RMII0RCLKO"),
	FUNC(MDIO0, (1), "MDIO0"),
	FUNC(VGA, (1), "VGA"),
	FUNC(RGMII1, (1), "RGMII1"),
	FUNC(RMII1, (2), "RMII1"),
	FUNC(RMII1RCLKO, (2), "RMII1RCLKO"),
	FUNC(SGPS, (5), "SGPS"),
	FUNC(MDIO1, (1), "MDIO1"),
	FUNC(FWQSPI, (1), "FWQSPI"),
	FUNC(LTPI_PS_I2C4, (2), "LTPI_PS_I2C4"),
	FUNC(LTPI_PS_I2C5, (2), "LTPI_PS_I2C5"),
	FUNC(I2C6, (1), "I2C6"),
	FUNC(I2C7, (1), "I2C7"),
	FUNC(I2C8, (1), "I2C8"),
	FUNC(I2C9, (1), "I2C9"),
	FUNC(I2C10, (1), "I2C10"),
	FUNC(CANBUS, (2), "CANBUS"),
	FUNC(I2C11, (1), "I2C11"),
	FUNC(ADC0, (0), "ADC0"),
	FUNC(GPIY0, (1), "GPIY0"),
	FUNC(SALT4, (2), "SALT4"),
	FUNC(ADC1, (0), "ADC1"),
	FUNC(GPIY1, (1), "GPIY1"),
	FUNC(SALT5, (2), "SALT5"),
	FUNC(ADC2, (0), "ADC2"),
	FUNC(GPIY2, (1), "GPIY2"),
	FUNC(SALT6, (2), "SALT6"),
	FUNC(ADC3, (0), "ADC3"),
	FUNC(GPIY3, (1), "GPIY3"),
	FUNC(SALT7, (2), "SALT7"),
	FUNC(ADC4, (0), "ADC4"),
	FUNC(GPIY4, (1), "GPIY4"),
	FUNC(SALT8, (2), "SALT8"),
	FUNC(ADC5, (0), "ADC5"),
	FUNC(GPIY5, (1), "GPIY5"),
	FUNC(SALT9, (2), "SALT9"),
	FUNC(ADC6, (0), "ADC6"),
	FUNC(GPIY6, (1), "GPIY6"),
	FUNC(SALT10, (2), "SALT10"),
	FUNC(ADC7, (0), "ADC7"),
	FUNC(GPIY7, (1), "GPIY7"),
	FUNC(SALT11, (2), "SALT11"),
	FUNC(ADC8, (0), "ADC8"),
	FUNC(GPIZ0, (1), "GPIZ0"),
	FUNC(ADC9, (0), "ADC9"),
	FUNC(GPIZ1, (1), "GPIZ1"),
	FUNC(ADC10, (0), "ADC10"),
	FUNC(GPIZ2, (1), "GPIZ2"),
	FUNC(ADC11, (0), "ADC11"),
	FUNC(GPIZ3, (1), "GPIZ3"),
	FUNC(ADC12, (0), "ADC12"),
	FUNC(GPIZ4, (1), "GPIZ4"),
	FUNC(ADC13, (0), "ADC13"),
	FUNC(GPIZ5, (1), "GPIZ5"),
	FUNC(AUXPWRGOOD0, (2), "AUXPWRGOOD0"),
	FUNC(AUXPWRGOOD1, (2), "AUXPWRGOOD1"),
	FUNC(ADC14, (0), "ADC14"),
	FUNC(GPIZ6, (1), "GPIZ6"),
	FUNC(ADC15, (0), "ADC15"),
	FUNC(GPIZ7, (1), "GPIZ7"),
	FUNC(SGPM0, (1), "SGPM0"),
	FUNC(SGPM0LD_R, (1), "SGPM0LD_R"),
	FUNC(SFF8485A, (2), "SFF8485A"),
	FUNC(MACLINK0, (4), "MACLINK0"),
	FUNC(MACLINK2, (4), "MACLINK2"),
	FUNC(HBLEDN, (2), "HBLEDN"),
	FUNC(WDTRST4N, (2), "WDTRST4N"),
	FUNC(WDTRST5N, (2), "WDTRST5N"),
	FUNC(WDTRST6N, (2), "WDTRST6N"),
	FUNC(MACLINK1, (3), "MACLINK1"),
	FUNC(SGMII, (1), "SGMII"),
	FUNC(USB2C, (0, 1, 2, 3), "USB2CUD", "USB2CD", "USB2CH", "USB2CU"),
	FUNC(USB2D, (1, 2), "USB2DD", "USB2DH"),
};

static struct ast27x5_soc1_field
ast27x5_soc1_pinmux_field_from_pin(unsigned int pin)
{
	return (struct ast27x5_soc1_field){
		.reg = AST27X5_SOC1_MUX_BASE +
		       (pin / AST27X5_SOC1_MUX_PINS_PER_REG) * sizeof(u32),
		.shift = (pin % AST27X5_SOC1_MUX_PINS_PER_REG) *
			 AST27X5_SOC1_MUX_BITS_PER_PIN,
		.mask = AST27X5_SOC1_MUX_MASK,
	};
}

static void __iomem *ast27x5_soc1_reg(struct ast27x5_soc1_pinctrl_priv *priv,
				      unsigned int offset)
{
	return (u8 __iomem *)priv->scu + offset;
}

static struct ast27x5_soc1_field
ast27x5_soc1_field_from_pin(unsigned int pin)
{
	switch (pin) {
	case PCIERC2_PERST:
		return (struct ast27x5_soc1_field){
			.reg = AST27X5_SOC1_PCIE_REG,
			.shift = 1,
			.mask = 0x1,
		};
	case PORTC_MODE:
		return (struct ast27x5_soc1_field){
			.reg = AST27X5_SOC1_USB_MODE_REG,
			.shift = 0,
			.mask = 0x3,
		};
	case PORTD_MODE:
		return (struct ast27x5_soc1_field){
			.reg = AST27X5_SOC1_USB_MODE_REG,
			.shift = 2,
			.mask = 0x3,
		};
	case SGMII0:
		return (struct ast27x5_soc1_field){
			.reg = AST27X5_SOC1_SGMII_REG,
			.shift = 0,
			.mask = 0x1,
		};
	default:
		return ast27x5_soc1_pinmux_field_from_pin(pin);
	}
}

static void
ast27x5_soc1_set_muxval(struct ast27x5_soc1_pinctrl_priv *priv,
			unsigned int pin, unsigned int muxval)
{
	struct ast27x5_soc1_field field = ast27x5_soc1_field_from_pin(pin);

	/*
	 * PCIERC2_PERST is controlled by a 1-bit enable instead of the
	 * function mux value.
	 */
	if (pin == PCIERC2_PERST)
		muxval = 1;

	clrsetbits_le32(ast27x5_soc1_reg(priv, field.reg),
			field.mask << field.shift,
			(muxval & field.mask) << field.shift);
}

static int ast27x5_soc1_pinctrl_get_pins_count(struct udevice *dev)
{
	return ARRAY_SIZE(ast27x5_soc1_pin_names);
}

static const char *
ast27x5_soc1_pinctrl_get_pin_name(struct udevice *dev, unsigned int selector)
{
	if (selector >= ARRAY_SIZE(ast27x5_soc1_pin_names))
		return NULL;

	return ast27x5_soc1_pin_names[selector];
}

static const struct ast27x5_soc1_group *
ast27x5_soc1_find_group(const char *name)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ast27x5_soc1_groups); i++) {
		if (!strcmp(ast27x5_soc1_groups[i].name, name))
			return &ast27x5_soc1_groups[i];
	}

	return NULL;
}

static int ast27x5_soc1_pinctrl_get_pin_muxing(struct udevice *dev,
					       unsigned int selector,
					       char *buf, int size)
{
	struct ast27x5_soc1_pinctrl_priv *priv = dev_get_priv(dev);
	const struct ast27x5_soc1_function *func;
	const struct ast27x5_soc1_group *group;
	struct ast27x5_soc1_field field;
	unsigned int muxval, want, f, g, i;
	bool adc_pin;

	if (selector >= ARRAY_SIZE(ast27x5_soc1_pin_names))
		return -EINVAL;

	field = ast27x5_soc1_field_from_pin(selector);
	muxval = (readl(ast27x5_soc1_reg(priv, field.reg)) >> field.shift) &
		 field.mask;

	/* Mux value 0 selects GPIO, except on the ADC balls where it is 1 */
	adc_pin = selector >= W14 && selector <= AB17;
	if (selector <= K26 && muxval == (adc_pin ? 1 : 0)) {
		snprintf(buf, size, "GPIO");
		return 0;
	}

	for (f = 0; f < ARRAY_SIZE(ast27x5_soc1_functions); f++) {
		func = &ast27x5_soc1_functions[f];
		for (g = 0; g < func->ngroups; g++) {
			want = func->muxvals[g];
			/* PCIERC2_PERST is a 1-bit enable, not a mux value */
			if (selector == PCIERC2_PERST)
				want = 1;
			if (want != muxval)
				continue;
			group = ast27x5_soc1_find_group(func->groups[g]);
			if (!group)
				continue;
			for (i = 0; i < group->npins; i++) {
				if (group->pins[i] != selector)
					continue;
				snprintf(buf, size, "%s (%s)", func->name,
					 group->name);
				return 0;
			}
		}
	}

	snprintf(buf, size, "unknown (mux 0x%x)", muxval);

	return 0;
}

static int ast27x5_soc1_pinctrl_get_groups_count(struct udevice *dev)
{
	return ARRAY_SIZE(ast27x5_soc1_groups);
}

static const char *
ast27x5_soc1_pinctrl_get_group_name(struct udevice *dev,
				    unsigned int selector)
{
	if (selector >= ARRAY_SIZE(ast27x5_soc1_groups))
		return NULL;

	return ast27x5_soc1_groups[selector].name;
}

static int ast27x5_soc1_pinctrl_get_functions_count(struct udevice *dev)
{
	return ARRAY_SIZE(ast27x5_soc1_functions);
}

static const char *
ast27x5_soc1_pinctrl_get_function_name(struct udevice *dev,
				       unsigned int selector)
{
	if (selector >= ARRAY_SIZE(ast27x5_soc1_functions))
		return NULL;

	return ast27x5_soc1_functions[selector].name;
}

static int
ast27x5_soc1_pinctrl_group_set(struct udevice *dev,
			       unsigned int group_selector,
			       unsigned int func_selector)
{
	struct ast27x5_soc1_pinctrl_priv *priv = dev_get_priv(dev);
	const struct ast27x5_soc1_function *func;
	const struct ast27x5_soc1_group *group;
	unsigned int muxval = 0;
	unsigned int i;

	if (group_selector >= ARRAY_SIZE(ast27x5_soc1_groups) ||
	    func_selector >= ARRAY_SIZE(ast27x5_soc1_functions))
		return -EINVAL;

	group = &ast27x5_soc1_groups[group_selector];
	func = &ast27x5_soc1_functions[func_selector];

	for (i = 0; i < func->ngroups; i++) {
		if (!strcmp(group->name, func->groups[i])) {
			muxval = func->muxvals[i];
			break;
		}
	}

	if (i == func->ngroups)
		return -EINVAL;

	for (i = 0; i < group->npins; i++)
		ast27x5_soc1_set_muxval(priv, group->pins[i], muxval);

	return 0;
}

static int
ast27x5_soc1_pinctrl_gpio_request_enable(struct udevice *dev,
					 unsigned int selector)
{
	struct ast27x5_soc1_pinctrl_priv *priv = dev_get_priv(dev);
	unsigned int muxval = 0;

	if (selector > K26 || (selector >= HOLE0 && selector <= HOLE3))
		return -ENOTSUPP;

	if (selector >= W14 && selector <= AB17)
		muxval = 1;

	ast27x5_soc1_set_muxval(priv, selector, muxval);

	return 0;
}

/*
 * Drive strength is controlled by 2-bit fields in 16-bit lanes at
 * DRV_BASE. GPIOY/Z use a separate pull-down register and have no
 * drive strength or pull-up fields; GPIOAA follows GPIOX in DRV_BASE.
 * The table stores (field index + 1) so that the implicit zero
 * initialization marks the unsupported pins.
 */
static const u8 ast27x5_soc1_drv_map[] = {
	[Y22] = 1, [U22] = 2, [U16] = 3, [U15] = 4, [U23] = 5,
	[U19] = 6, [V18] = 7, [V19] = 8, [G10] = 9, [G9] = 10,
	[G8] = 11, [F9] = 12, [F10] = 13, [H11] = 14, [E10] = 15,
	[G11] = 16, [F12] = 17, [D10] = 18, [H7] = 19, [H5] = 20,
	[G4] = 21, [F8] = 22, [G5] = 23, [Y24] = 24, [P13] = 25,
	[G12] = 26, [H12] = 27, [T17] = 28, [T19] = 29, [Y26] = 30,
	[G15] = 31, [V26] = 32, [E9] = 33, [D9] = 34, [C9] = 35,
	[H13] = 36, [G13] = 37, [AA24] = 38, [Y23] = 39, [M12] = 40,
	[N13] = 41, [M13] = 42, [L12] = 43, [A6] = 44, [B7] = 45,
	[A7] = 46, [A8] = 47, [B8] = 48, [U26] = 49, [E16] = 50,
	[P26] = 51, [E8] = 52, [C12] = 53, [L25] = 54, [M25] = 55,
	[N25] = 56, [M26] = 57, [N26] = 58, [B19] = 59, [B20] = 60,
	[AD25] = 65, [AF24] = 66, [AC25] = 67, [AE24] = 68, [AC24] = 69,
	[AD24] = 70, [V24] = 71, [AD19] = 72, [B13] = 73, [C15] = 74,
	[A12] = 75, [B14] = 76, [C16] = 77, [D17] = 78, [A14] = 79,
	[A15] = 80, [F13] = 81, [R15] = 82, [E12] = 83, [J11] = 84,
	[F14] = 85, [T18] = 86, [E13] = 87, [AA25] = 88, [D15] = 89,
	[D14] = 90, [R26] = 91, [P25] = 92, [D13] = 93, [F17] = 94,
	[D16] = 95, [B10] = 96, [AD17] = 97, [AC22] = 98, [AC20] = 99,
	[AC19] = 100, [AA19] = 101, [Y20] = 102, [W23] = 103, [AB20] = 104,
	[AA22] = 105, [T14] = 106, [U24] = 107, [U21] = 108, [V22] = 109,
	[U20] = 110, [V20] = 111, [U17] = 112, [W22] = 113, [AA15] = 114,
	[AA16] = 115, [AA17] = 116, [AA18] = 117, [AA21] = 118, [W20] = 119,
	[W18] = 120, [AF25] = 121, [W25] = 122, [AD23] = 123, [V25] = 124,
	[V23] = 125, [AD20] = 126, [AD21] = 127, [AE25] = 128, [A9] = 129,
	[B9] = 130, [J10] = 131, [H10] = 132, [F11] = 133, [J9] = 134,
	[E11] = 135, [AE26] = 136, [AC23] = 137, [AD18] = 138, [AD22] = 139,
	[AC17] = 140, [AC18] = 141, [AC21] = 142, [W24] = 143, [AA20] = 144,
	[Y19] = 145, [AB21] = 146, [AB22] = 147, [AB19] = 148, [V16] = 149,
	[U14] = 150, [C10] = 151, [G7] = 152, [F15] = 153, [T25] = 154,
	[E14] = 155, [T24] = 156, [Y25] = 157, [U25] = 158, [E15] = 159,
	[D12] = 160, [D11] = 161, [C11] = 162, [F16] = 163, [T26] = 164,
	[W26] = 165, [R25] = 166, [AD26] = 167, [AC26] = 168, [B16] = 169,
	[C18] = 170, [B18] = 171, [A18] = 172, [A19] = 173, [C22] = 174,
	[D22] = 175, [C21] = 176, [B11] = 177, [B12] = 178, [A11] = 179,
	[C13] = 180, [A10] = 181, [C14] = 182, [B15] = 183, [A13] = 184,
	[C20] = 185, [J26] = 186, [E22] = 187, [E23] = 188, [F26] = 189,
	[G26] = 190, [G25] = 191, [H24] = 192, [C17] = 193, [A16] = 194,
	[B17] = 195, [A17] = 196, [J25] = 197, [C19] = 198, [L26] = 199,
	[K26] = 200,
};

static bool ast27x5_soc1_is_adc_alt(unsigned int pin)
{
	return pin >= W14 && pin <= AB17;
}

static struct ast27x5_soc1_field
ast27x5_soc1_bias_field_from_pin(unsigned int pin)
{
	unsigned int idx;

	if (ast27x5_soc1_is_adc_alt(pin))
		return (struct ast27x5_soc1_field){
			.reg = AST27X5_SOC1_BIAS_BASE,
			.shift = pin - W14,
			.mask = 0x1,
		};

	idx = ast27x5_soc1_drv_map[pin] - 1;
	return (struct ast27x5_soc1_field){
		.reg = AST27X5_SOC1_DRV_BASE +
		       (idx / AST27X5_SOC1_DRV_PINS_PER_REG) * sizeof(u32),
		.shift = (idx % AST27X5_SOC1_DRV_PINS_PER_REG) *
			 AST27X5_SOC1_DRV_BITS_PER_PIN,
		.mask = AST27X5_SOC1_PULL_DOWN_MASK | AST27X5_SOC1_PULL_UP_MASK,
	};
}

static struct ast27x5_soc1_field
ast27x5_soc1_drv_field_from_idx(unsigned int idx)
{
	return (struct ast27x5_soc1_field){
		.reg = AST27X5_SOC1_DRV_BASE +
		       (idx / AST27X5_SOC1_DRV_PINS_PER_REG) * sizeof(u32),
		.shift = (idx % AST27X5_SOC1_DRV_PINS_PER_REG) *
			 AST27X5_SOC1_DRV_BITS_PER_PIN,
		.mask = AST27X5_SOC1_DRV_MASK,
	};
}

/*
 * Pull-up arguments are in ohms, matching the kernel's ABI; the hardware
 * uses a one-hot encoding. ohm == 0 is a valid argument here (not an
 * error): it means "no pull-up", same as the kernel driver. Since
 * pull-up and pull-down share one mutually-exclusive field, writing it
 * has the same effect on the register as bias-disable.
 */
static int ast27x5_soc1_pullup_ohm_to_raw(unsigned int ohm, unsigned int *raw)
{
	switch (ohm) {
	case 0:
		*raw = 0;
		break;
	case 750:
		*raw = 1;
		break;
	case 550:
		*raw = 2;
		break;
	case 400:
		*raw = 4;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * "bias-pull-down"'s default argument is unused: PIN_CONFIG_BIAS_PULL_DOWN
 * always enables regardless of arg (see below), matching the kernel.
 * "bias-pull-up"'s default of 750 (its weakest valid ohm value) is only
 * used for a bare "bias-pull-up;" property with no explicit value; an
 * explicit value must still be one of the valid ohms below.
 */
static const struct pinconf_param ast27x5_soc1_pinconf_params[] = {
	{ "bias-disable", PIN_CONFIG_BIAS_DISABLE, 0 },
	{ "bias-pull-down", PIN_CONFIG_BIAS_PULL_DOWN, 1 },
	{ "bias-pull-up", PIN_CONFIG_BIAS_PULL_UP, 750 },
	{ "drive-strength", PIN_CONFIG_DRIVE_STRENGTH, 0 },
};

static int ast27x5_soc1_pinctrl_pinconf_set(struct udevice *dev,
					    unsigned int selector,
					    unsigned int param,
					    unsigned int arg)
{
	struct ast27x5_soc1_pinctrl_priv *priv = dev_get_priv(dev);
	struct ast27x5_soc1_field field;
	unsigned int val, idx;
	int ret;

	if (selector > K26 || (selector >= HOLE0 && selector <= HOLE3))
		return -EINVAL;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (!ast27x5_soc1_is_adc_alt(selector) &&
		    (selector >= ARRAY_SIZE(ast27x5_soc1_drv_map) ||
		     !ast27x5_soc1_drv_map[selector]))
			return -ENOTSUPP;
		field = ast27x5_soc1_bias_field_from_pin(selector);
		if (field.reg == AST27X5_SOC1_BIAS_BASE) {
			/* GPIOY/Z use disable-type pull-down bits; ignore arg. */
			val = param == PIN_CONFIG_BIAS_DISABLE;
		} else {
			/*
			 * Enable-type pull-down; the mask also clears pull-up.
			 * PIN_CONFIG_BIAS_PULL_DOWN always enables regardless
			 * of arg, matching the kernel driver.
			 */
			val = param == PIN_CONFIG_BIAS_PULL_DOWN ?
				AST27X5_SOC1_PULL_DOWN_MASK : 0;
		}
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (selector >= ARRAY_SIZE(ast27x5_soc1_drv_map) ||
		    !ast27x5_soc1_drv_map[selector])
			return -ENOTSUPP;
		ret = ast27x5_soc1_pullup_ohm_to_raw(arg, &val);
		if (ret)
			return ret;
		/* Clear pull-down whenever selecting or disabling pull-up. */
		field = ast27x5_soc1_bias_field_from_pin(selector);
		val <<= AST27X5_SOC1_PULL_UP_SHIFT;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		if (selector >= ARRAY_SIZE(ast27x5_soc1_drv_map) ||
		    !ast27x5_soc1_drv_map[selector])
			return -ENOTSUPP;
		/*
		 * Match the kernel's raw 0-3 ABI, deliberately not generic
		 * pinconf's mA units: hardware defines only 55/40/25/10 ohm
		 * output impedance, with no current rating to convert from.
		 */
		if (arg > AST27X5_SOC1_DRV_MASK)
			return -EINVAL;
		idx = ast27x5_soc1_drv_map[selector] - 1;
		field = ast27x5_soc1_drv_field_from_idx(idx);
		val = arg;
		break;
	default:
		return -ENOTSUPP;
	}

	clrsetbits_le32(ast27x5_soc1_reg(priv, field.reg),
			field.mask << field.shift, val << field.shift);

	return 0;
}

static int ast27x5_soc1_pinctrl_pinconf_group_set(struct udevice *dev,
						  unsigned int selector,
						  unsigned int param,
						  unsigned int arg)
{
	const struct ast27x5_soc1_group *group;
	unsigned int i;
	int ret;

	if (selector >= ARRAY_SIZE(ast27x5_soc1_groups))
		return -EINVAL;

	group = &ast27x5_soc1_groups[selector];
	for (i = 0; i < group->npins; i++) {
		ret = ast27x5_soc1_pinctrl_pinconf_set(dev, group->pins[i],
						       param, arg);
		if (ret)
			return ret;
	}

	return 0;
}

static int ast27x5_soc1_pinctrl_probe(struct udevice *dev)
{
	struct ast27x5_soc1_pinctrl_priv *priv = dev_get_priv(dev);

	priv->scu = dev_remap_addr(dev->parent);
	if (!priv->scu)
		return -ENOMEM;

	return 0;
}

static const struct pinctrl_ops ast27x5_soc1_pinctrl_ops = {
	.set_state = pinctrl_generic_set_state,
	.get_pins_count = ast27x5_soc1_pinctrl_get_pins_count,
	.get_pin_name = ast27x5_soc1_pinctrl_get_pin_name,
	.get_pin_muxing = ast27x5_soc1_pinctrl_get_pin_muxing,
	.get_groups_count = ast27x5_soc1_pinctrl_get_groups_count,
	.get_group_name = ast27x5_soc1_pinctrl_get_group_name,
	.get_functions_count = ast27x5_soc1_pinctrl_get_functions_count,
	.get_function_name = ast27x5_soc1_pinctrl_get_function_name,
	.pinmux_group_set = ast27x5_soc1_pinctrl_group_set,
	.gpio_request_enable = ast27x5_soc1_pinctrl_gpio_request_enable,
	.pinconf_num_params = ARRAY_SIZE(ast27x5_soc1_pinconf_params),
	.pinconf_params = ast27x5_soc1_pinconf_params,
	.pinconf_set = ast27x5_soc1_pinctrl_pinconf_set,
	.pinconf_group_set = ast27x5_soc1_pinctrl_pinconf_group_set,
};

static const struct udevice_id ast27x5_soc1_pinctrl_ids[] = {
	{ .compatible = "aspeed,ast2705-soc1-pinctrl" },
	{ }
};

U_BOOT_DRIVER(pinctrl_ast27x5_soc1) = {
	.name = "aspeed_ast27x5_soc1_pinctrl",
	.id = UCLASS_PINCTRL,
	.of_match = ast27x5_soc1_pinctrl_ids,
	.priv_auto = sizeof(struct ast27x5_soc1_pinctrl_priv),
	.ops = &ast27x5_soc1_pinctrl_ops,
	.probe = ast27x5_soc1_pinctrl_probe,
};
