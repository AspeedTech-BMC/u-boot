// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) ASPEED Technology Inc.
 */
#include "platform.h"
#include "internal.h"
#include "phy.h"
#include "nettest.h"
#include "ncsi.h"
#include "checksum.h"
#include "vlan.h"
#include "pktgen.h"
#include "pattern.h"

// #define DUMP_TX_PACKET

#define IS_INTERFACE_ARG_RMII(x) (strncmp((x), "rmii", strlen("rmii")) == 0)
#define IS_INTERFACE_ARG_RGMII(x) (strncmp((x), "rgmii", strlen("rgmii")) == 0)
#define IS_INTERFACE_ARG_RGMII_RXID(x) \
	(strncmp((x), "rgmii-rxid", strlen("rgmii-rxid")) == 0)
#define IS_INTERFACE_ARG_RGMII_TXID(x) \
	(strncmp((x), "rgmii-txid", strlen("rgmii-txid")) == 0)
#define IS_INTERFACE_ARG_RGMII_ID(x) \
	(strncmp((x), "rgmii-id", strlen("rgmii-id")) == 0)
#define IS_INTERFACE_ARG_SGMII(x) \
	(strncmp((x), "sgmii", strlen("sgmii")) == 0)

static int is_mac_obj_registered;

struct mac_txdes_s txdes[PKT_PER_TEST] DMA_ALIGNED;
struct mac_rxdes_s rxdes[PKT_PER_TEST] DMA_ALIGNED;

struct mac_adaptor_s {
	u32 nobjs;
	struct mac_s *objs[NUM_OF_MAC_DEVICES];
};

static struct mac_adaptor_s mac_adaptor = { 0 };
struct test_s test_obj = { 0 };
u8 mac_addr[8] = { 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05 }; /*[0], [1]: pendding*/

int net_connect_mdio(int mac_idx, int mdio_idx)
{
	if (mac_idx >= NUM_OF_MAC_DEVICES ||
	    mdio_idx >= NUM_OF_MDIO_DEVICES)
		return FAIL_PARAMETER_INVALID;

	mac_data[mac_idx].phy->mdio = &mdio_data[mdio_idx];

	return 0;
}

void net_enable_pin(struct aspeed_group_config_s *group)
{
	struct aspeed_sig_desc_s *desc;
	u32 ndescs = group->ndescs;
	int i;

	for (i = 0; i < ndescs; i++) {
		desc = &group->descs[i];
#if defined(ASPEED_AST2700)
		if (desc->clr)
			CLRBITS((void __iomem *)AST_IO_SCU_BASE + desc->offset,
				desc->reg_set);
		else
			SETBITS((void __iomem *)AST_IO_SCU_BASE + desc->offset,
				desc->reg_set);
#elif defined(ASPEED_AST2600)
		if (desc->clr)
			CLRBITS((void __iomem *)AST_SCU_BASE + desc->offset,
				desc->reg_set);
		else
			SETBITS((void __iomem *)AST_SCU_BASE + desc->offset,
				desc->reg_set);
#endif
	}
}

int net_enable_mdio_pin(int mdio_idx)
{
	net_enable_pin(&mdio_pinctrl[mdio_idx]);

	return 0;
}

int net_enable_sgmii_pin(void)
{
#if defined(ASPEED_AST2700)
	/* PCIE/SGMII MUX */
	writel(0x00000001, AST_IO_SCU_BASE + 0x47c);
#endif
	return 0;
}

int net_enable_rgmii_pin(int rgmii_idx)
{
	net_enable_pin(&rgmii_pinctrl[rgmii_idx]);

	return 0;
}

int net_enable_rmii_pin(int rmii_idx)
{
	net_enable_pin(&rmii_pinctrl[rmii_idx]);

	return 0;
}

void mac_cmd_register_obj(struct mac_s *obj)
{
	mac_adaptor.objs[mac_adaptor.nobjs++] = obj;
	if (mac_adaptor.nobjs > NUM_OF_MAC_DEVICES)
		debug("exceed MAX MAC objects: %d\n", mac_adaptor.nobjs);
}

static void config_clock(void)
{
#if !defined(U_BOOT)
#if defined(ASPEED_AST2700)
	/* HPLL : 1000MHz */
	writel(0x10000027, AST_IO_SCU_BASE + 0x300);
	/* MAC clock divider : 5 */
	CLRSETBITS(AST_IO_SCU_BASE + 0x280, GENMASK(31, 29), 4 << 29);
	/* RGMII clock divider : 8 */
	CLRSETBITS(AST_IO_SCU_BASE + 0x280, GENMASK(27, 25), 3 << 25);
	/* RGMII clock select : HPLL (set to 0) */
	CLRBITS(AST_IO_SCU_BASE + 0x284, BIT(18));
	/* SGMII clock select : HPLL (set to 0) */
	CLRBITS(AST_IO_SCU_BASE + 0x284, BIT(19));
	/* RMII clock divider : 20 */
	CLRSETBITS(AST_IO_SCU_BASE + 0x280, GENMASK(23, 21), 4 << 21);

	writel(0x00CF4D75, AST_IO_SCU_BASE + 0x390);
	writel(0x00410410, AST_IO_SCU_BASE + 0x394);
	writel(0x00410410, AST_IO_SCU_BASE + 0x398);
#elif defined(ASPEED_AST2600) || defined(PLATFORM_AST2605)
	writel(0x10004077, AST_SCU_BASE + 0x240);
	writel(0x8000003b, AST_SCU_BASE + 0x244);
	CLRSETBITS(AST_SCU_BASE + 0x304, GENMASK(23, 16), 0x74 << 16);
	CLRSETBITS(AST_SCU_BASE + 0x310, GENMASK(26, 24), 0 << 24);
	CLRSETBITS(AST_SCU_BASE + 0x340, GENMASK(31, 28), 0x9 << 28);

	/* force delay value for MII loopback */
	// writel(0x90410410, AST_SCU_BASE + 0x340);
	// writel(0x082082, AST_SCU_BASE + 0x350);
#elif defined(PLATFORM_AST1300)
	CLRSETBITS(AST_SCU_BASE + 0x310, GENMASK(26, 24), 1 << 24);
	CLRSETBITS(AST_SCU_BASE + 0x310, GENMASK(19, 16), 9 << 16);
	/* bit[31]: 1=select HPLL as clock source
	 * bit[23:20]: 7=HPLL clock / 8 = 1000/8 = 125M
	 */
	CLRSETBITS(AST_SCU_BASE + 0x310, BIT(31) | GENMASK(23, 20), 7 << 20);
	/* bit[31]: 1=select internal clock source */
	SETBITS(AST_SCU_BASE + 0x350, BIT(31));
#endif
#endif
}

void demo_net_init(void)
{
	int i;

	for (i = 0; i < NUM_OF_MDIO_DEVICES; i++)
		aspeed_mdio_init(&mdio_data[i]);

	for (i = 0; i < NUM_OF_MAC_DEVICES; i++)
		mac_cmd_register_obj(&mac_data[i]);
}

int net_get_packet(struct mac_s *obj, void **packet, u32 *rxlen, int max_try)
{
	int status;
	int try = 0;

	do {
		status = aspeed_mac_recv(obj, packet, rxlen);
		if (status != FAIL_TIMEOUT)
			break;

		mdelay(1);
	} while (++try < max_try);

	return status;
}

void netdiag_init_parameter(struct parameter_s *parm)
{
	parm->mac_index = 0;
	parm->mdio_index = 0;
	parm->speed = 1000;
	parm->interface = PHY_INTERFACE_MODE_RGMII_ID;
	parm->control = NETDIAG_CTRL_LOOPBACK_PHY;

	parm->checksum = NETDIAG_CKS_NONE;
	parm->vlan = NETDIAG_VLAN_NONE;

	parm->mode = MODE_MARGIN;
	parm->margin = 1;

	parm->loop = 100;
	parm->packets = 4;

	parm->mtu_size = 1500;

	parm->ncsi_mode = NCSI_MODE_NONE;
	parm->ncsi_package = 0;
	parm->ncsi_channel = 0;

	parm->tx_delay = -1;
	parm->rx_delay = -1;
}

static const char help_string[] = "\nnetdiag: network diagnostic tool.\n"
"    -o <mac>,<mdio> | mac (mandatory):  index of the mac object\n"
"                    | mdio (optional, default mdio = mac): index of the mdio object\n"
"    -s <speed>      | speed (optional, default 1000)\n"
"                    |   1000\n"
"                    |   100\n"
"                    |   10\n"
"    -l <loopback>   | loopback (optional, default phy)\n"
"                    |   ext        = PHY MDI (PHY external) loopback\n"
"                    |   phy        = PHY PCS (PHY internal) loopback\n"
"                    |   mii        = RMII/RGMII loopback\n"
"                    |   mac        = MAC loopback\n"
"                    |   tx         = TX only\n"
"                    |   tx-nway    = TX only & enable Nway\n"
"    -i <interface>  | interface (optional, default rgmii-id)\n"
"                    |   rmii       = set interface as RMII\n"
"                    |   rgmii      = set interface as RGMII w/o PHY TX & RX delay\n"
"                    |   rgmii-id   = set interface as RGMII w/  PHY TX & RX delay\n"
"                    |   rgmii-txid = set interface as RGMII w/  PHY TX delay\n"
"                    |   rgmii-rxid = set interface as RGMII w/  PHY RX delay\n"
"                    |   sgmii      = set interface as SGMII\n"
"    -m <mode>,<taps>| mode (optional, default margin)\n"
"                    |   margin     = check margin of the current delay setting\n"
"                    |   scan       = scan full delay taps\n"
"                    | taps: (optional, default 1): number of the delay taps to be\n"
"                    |       checked in margin check mode\n"
"    -k <count, pkt> | count: loop test count (optional, default 100)\n"
"                    |   pkt: test packets per loop (optional, default 4)\n"
"    -u <mtu size>   | mtu size in byte (optional, default 1500)\n"
"    -n <m>,<p>,<c>  | m: NC-SI test mode (optional, default single)\n"
"                    |   multi   = specify package number and channel number\n"
"                    |   single  = specify package id and channel id\n"
"                    | p: the package number/id of NC-SI\n"
"                    |   1 - 8   = package number (mode=multi)\n"
"                    |   0 - 7   = package id     (mode=single)\n"
"                    | c: the channel number/id of NC-SI\n"
"                    |   1 - 248 = channel number (mode=multi)\n"
"                    |   0 - 30  = channel id     (mode=single)\n"
"    -c <type>       | type: checksum test type (optional)\n"
"                    |   tx-ip   = TX IP\n"
"                    |   tx-tcp4 = TX TCP IPv4\n"
"                    |   tx-tcp6 = TX TCP IPv6\n"
"                    |   tx-udp4 = TX UDP IPv4\n"
"                    |   tx-udp6 = TX UDP IPv6\n"
"                    |   tx-all  = TX ALL\n"
"                    |   rx-ip   = RX IP\n"
"                    |   rx-tcp4 = RX TCP IPv4\n"
"                    |   rx-tcp6 = RX TCP IPv6\n"
"                    |   rx-udp4 = RX UDP IPv4\n"
"                    |   rx-udp6 = RX UDP IPv6\n"
"                    |   rx-all  = RX ALL\n"
"                    |   all     = TX/RX ALL\n"
"    -v <type>       | type: VLAN test type (optional)\n"
"                    |   tx      = TX VLAN\n"
"                    |   rx      = RX VLAN\n"
"                    |   rx-qinq = RX VLAN QinQ (0x8100 in 0x8100)\n"
"                    |   rx-all  = RX ALL\n"
"                    |   all     = TX/RX VLAN\n"
"    -d <tx>,<rx>    | tx: RGMII/RMII TX Delay\n"
"                    |   0 - 63  = (RGMII) tx step delay\n"
"                    |   0 - 1   = (RMII)  tx clock edge\n"
"                    | rx: RGMII/RMII RX Delay\n"
"                    |   0 - 63  = (RGMII) rx step delay\n"
"                    |   0 - 63  = (RMII)  rx step delay\n"
"    -z              | show the MAC information\n";

static void netdiag_cli_usage(void)
{
#if defined(U_BOOT)
	puts(help_string);
#else
	printf("%s", help_string);
#endif
	printf("\nRun the specific pattern: $netdiag <index>\n");
	printf("List the patterns:\n");
	printf("Index: Command\n");
	for (int i = 0; i < ARRAY_SIZE(patterns); i++)
		printf("   %2d: %s\n", i, patterns[i]);
}

void netdiag_print_mac_infomation(void)
{
#if defined(ASPEED_AST2700)
	void __iomem *scu = (void __iomem *)AST_IO_SCU_BASE;

	printf("=== MAC0 register:\n");

	printf("Multi-function:\n");
	printf("    0x14c0_2444       : 0x%08X\n", readl(scu + 0x444));
	printf("    0x14c0_2448[14:0] : 0x%08X(0x%lx)\n", readl(scu + 0x448),
	       readl(scu + 0x448) & GENMASK(14, 0));

	printf("MDC/MDIO:\n");
	printf("    0x14c0_2448[22:16]: 0x%08X(0x%lx)\n", readl(scu + 0x448),
	       (readl(scu + 0x448) & GENMASK(22, 16)) >> 16);
	printf("    0x14c0_2200[2]    : 0x%08X(0x%lx)\n", readl(scu + 0x200),
	       readl(scu + 0x200) & BIT(2));

	printf("Clock:\n");
	printf("    0x14c0_2280[27:25]: 0x%08X(0x%lx)\n", readl(scu + 0x280),
	       (readl(scu + 0x280) & GENMASK(27, 25)) >> 25);
	printf("    0x14c0_2284[18]   : 0x%08X(0x%lx)\n", readl(scu + 0x280),
	       readl(scu + 0x280) & BIT(18));
	printf("    0x14c0_2390       : 0x%08X\n", readl(scu + 0x390));
	printf("    0x14c0_2394       : 0x%08X\n", readl(scu + 0x394));
	printf("    0x14c0_2398       : 0x%08X\n", readl(scu + 0x398));

	printf("Reset/Clock gating:\n");
	printf("    0x14c0_2200[5]    : 0x%08X(0x%lx)\n", readl(scu + 0x200),
	       readl(scu + 0x200) & BIT(5));
	printf("    0x14c0_2398[8]    : 0x%08X(0x%lx)\n", readl(scu + 0x240),
	       readl(scu + 0x240) & BIT(8));
#endif
}

int netdiag_parse_parameter_from_argv(int argc, char *const argv[], struct parameter_s *parm)
{
	char *data_ptrs[3];
	int opt;
#if defined(U_BOOT)
	struct getopt_state gs;
	char *optarg;
#else
	int option_index = 0;
#endif
	static const char optstring[] = "o:i:l:s:m:k:u:n:c:v:d:zh";
#if !defined(U_BOOT)
	static const struct option long_options[] = {
		{ "objects", 1, NULL, 'o' },  { "interface", 1, NULL, 'i' },
		{ "loopback", 1, NULL, 'l' }, { "speed", 1, NULL, 's' },
		{ "mode", 1, NULL, 'm' },     { "count", 1, NULL, 'k' },
		{ "mtu", 1, NULL, 'u' },      { "ncsi", 1, NULL, 'n' },
		{ "checksum", 1, NULL, 'c' }, { "vlan", 1, NULL, 'v' },
		{ "delay", 1, NULL, 'd' },    { "help", 0, NULL, 'h' },
	};
#endif

	netdiag_init_parameter(parm);

#if defined(U_BOOT)
	getopt_init_state(&gs);
	while ((opt = getopt(&gs, argc, argv, optstring)) != EOF) {
		optarg = gs.arg;
#else
	/* getopt_long_init() is mandatory before getopt_long() */
	getopt_long_init();
	while ((opt = getopt_long(argc, argv, optstring, long_options,
				  &option_index)) != EOF) {
#endif
		switch (opt) {
		case 'o':
			data_ptrs[0] = strtok(optarg, ",");
			data_ptrs[1] = strtok(NULL, ",");
			if (data_ptrs[0])
				parm->mac_index = simple_strtoul(data_ptrs[0], NULL, 16);
			else
				return -1;
			if (data_ptrs[1])
				parm->mdio_index = simple_strtoul(data_ptrs[1], NULL, 16);
			else
				parm->mdio_index = parm->mac_index;
			break;
		case 'i':
			if (IS_INTERFACE_ARG_RMII(optarg))
				parm->interface = PHY_INTERFACE_MODE_RMII;
			else if (IS_INTERFACE_ARG_RGMII_RXID(optarg))
				parm->interface = PHY_INTERFACE_MODE_RGMII_RXID;
			else if (IS_INTERFACE_ARG_RGMII_TXID(optarg))
				parm->interface = PHY_INTERFACE_MODE_RGMII_TXID;
			else if (IS_INTERFACE_ARG_RGMII_ID(optarg))
				parm->interface = PHY_INTERFACE_MODE_RGMII_ID;
			else if (IS_INTERFACE_ARG_RGMII(optarg))
				parm->interface = PHY_INTERFACE_MODE_RGMII;
			else if (IS_INTERFACE_ARG_SGMII(optarg))
				parm->interface = PHY_INTERFACE_MODE_SGMII;
			break;
		case 'l':
			if (strncmp(optarg, "phy", strlen("phy")) == 0)
				parm->control = NETDIAG_CTRL_LOOPBACK_PHY;
			else if (strncmp(optarg, "mac", strlen("mac")) == 0)
				parm->control = NETDIAG_CTRL_LOOPBACK_MAC;
			else if (strncmp(optarg, "mii", strlen("mii")) == 0)
				parm->control = NETDIAG_CTRL_LOOPBACK_MII;
			else if (strncmp(optarg, "tx", strlen("tx")) == 0)
				parm->control = NETDIAG_CTRL_LOOPBACK_OFF;
			break;
		case 's':
			parm->speed = simple_strtoul(optarg, NULL, 10);
			break;
		case 'm':
			data_ptrs[0] = strtok(optarg, ",");
			data_ptrs[1] = strtok(NULL, ",");
			if (data_ptrs[0] && (strncmp(data_ptrs[0], "scan",
						     strlen("scan")) == 0))
				parm->mode = MODE_SCAN;

			if (data_ptrs[1] && parm->mode == MODE_MARGIN)
				parm->margin = simple_strtoul(data_ptrs[1], NULL, 10);
			break;
		case 'k':
			data_ptrs[0] = strtok(optarg, ",");
			data_ptrs[1] = strtok(NULL, ",");
			if (data_ptrs[0])
				parm->loop = simple_strtoul(data_ptrs[0], NULL, 10);
			if (data_ptrs[1])
				parm->packets = simple_strtoul(data_ptrs[1], NULL, 10);
			break;
		case 'u':
			parm->mtu_size = simple_strtoul(optarg, NULL, 10);
			break;
		case 'n':
			data_ptrs[0] = strtok(optarg, ",");
			data_ptrs[1] = strtok(NULL, ",");
			data_ptrs[2] = strtok(NULL, ",");
			if (data_ptrs[0]) {
				if (strncmp(data_ptrs[0], "multi", strlen("multi")) == 0)
					parm->ncsi_mode = NCSI_MODE_MULTI;
				else if (strncmp(data_ptrs[0], "single", strlen("single")) == 0)
					parm->ncsi_mode = NCSI_MODE_SINGLE;
				if (data_ptrs[1])
					parm->ncsi_package = simple_strtoul(data_ptrs[1], NULL, 10);
				if (data_ptrs[2])
					parm->ncsi_channel = simple_strtoul(data_ptrs[2], NULL, 10);
			}
			break;
		case 'c':
			if (strncmp(optarg, "tx-ip", strlen("tx-ip")) == 0)
				parm->checksum = NETDIAG_CKS_TX_IP;
			else if (strncmp(optarg, "tx-tcp4", strlen("tx-tcp4")) == 0)
				parm->checksum = NETDIAG_CKS_TX_TCP4;
			else if (strncmp(optarg, "tx-tcp6", strlen("tx-tcp6")) == 0)
				parm->checksum = NETDIAG_CKS_TX_TCP6;
			else if (strncmp(optarg, "tx-udp4", strlen("tx-udp4")) == 0)
				parm->checksum = NETDIAG_CKS_TX_UDP4;
			else if (strncmp(optarg, "tx-udp6", strlen("tx-udp6")) == 0)
				parm->checksum = NETDIAG_CKS_TX_UDP6;
			else if (strncmp(optarg, "tx-all", strlen("tx-all")) == 0)
				parm->checksum = NETDIAG_CKS_TX_ALL;
			else if (strncmp(optarg, "rx-ip", strlen("rx-ip")) == 0)
				parm->checksum = NETDIAG_CKS_RX_IP;
			else if (strncmp(optarg, "rx-tcp4", strlen("rx-tcp4")) == 0)
				parm->checksum = NETDIAG_CKS_RX_TCP4;
			else if (strncmp(optarg, "rx-tcp6", strlen("rx-tcp6")) == 0)
				parm->checksum = NETDIAG_CKS_RX_TCP6;
			else if (strncmp(optarg, "rx-udp4", strlen("rx-udp4")) == 0)
				parm->checksum = NETDIAG_CKS_RX_UDP4;
			else if (strncmp(optarg, "rx-udp6", strlen("rx-udp6")) == 0)
				parm->checksum = NETDIAG_CKS_RX_UDP6;
			else if (strncmp(optarg, "rx-all", strlen("rx-all")) == 0)
				parm->checksum = NETDIAG_CKS_RX_ALL;
			else if (strncmp(optarg, "all", strlen("all")) == 0)
				parm->checksum = NETDIAG_CKS_ALL;
			break;
		case 'v':
			if (strncmp(optarg, "tx", strlen("tx")) == 0)
				parm->vlan = NETDIAG_VLAN_TX;
			else if (strncmp(optarg, "rx-qinq", strlen("rx-qinq")) == 0)
				parm->vlan = NETDIAG_VLAN_RX_QINQ;
			else if (strncmp(optarg, "rx", strlen("rx")) == 0)
				parm->vlan = NETDIAG_VLAN_RX;
			else if (strncmp(optarg, "rx-all", strlen("rx-all")) == 0)
				parm->vlan = NETDIAG_VLAN_RX_ALL;
			else if (strncmp(optarg, "all", strlen("all")) == 0)
				parm->vlan = NETDIAG_VLAN_ALL;
			break;
		case 'd':
			data_ptrs[0] = strtok(optarg, ",");
			data_ptrs[1] = strtok(NULL, ",");
			if (data_ptrs[0])
				parm->tx_delay = simple_strtoul(data_ptrs[0], NULL, 10);
			if (data_ptrs[1])
				parm->rx_delay = simple_strtoul(data_ptrs[1], NULL, 10);
			break;
		case 'z':
			netdiag_print_mac_infomation();
			return SUCCESS_HELP;
		case 'h':
		default:
			netdiag_cli_usage();
			return SUCCESS_HELP;
		}
	}

	return 0;
}

int netdiag_split_pattern(char *pattern, char *argv[])
{
	int argc = 0;

	argv[argc] = strtok(pattern, " ");
	while (argv[argc])
		argv[++argc] = strtok(NULL, " ");

	return argc;
}

int netdiag_parse_parameter_from_pattern(int pattern_index, struct parameter_s *parm)
{
	int argc;
	char *argv[32] = { NULL };
	char patterns_cmd[256] = { 0 };

	memcpy(patterns_cmd, patterns[pattern_index], strlen(patterns[pattern_index]));

	argc = netdiag_split_pattern(patterns_cmd, argv);
	if (argc > 0)
		return netdiag_parse_parameter_from_argv(argc, argv, parm);

	return FAIL_PARAMETER_INVALID;
}

int netdiag_func(int argc, char *const argv[])
{
	struct mac_s *mac_obj = NULL;
	struct phy_s *phy;
	struct parameter_s *parm = &test_obj.parm;
	int i;
	int has_error = 0;

	if (argc <= 1) {
		netdiag_cli_usage();
		return 0;
	} else if (argc == 2 && strcmp(argv[1], "-h") && strcmp(argv[1], "-z")) {
		has_error = netdiag_parse_parameter_from_pattern(simple_strtoul(argv[1], NULL, 10),
								 parm);
	} else {
		has_error = netdiag_parse_parameter_from_argv(argc, argv, parm);
	}

	if (has_error == SUCCESS_HELP)
		return 0;

	if (is_mac_obj_registered == 0) {
		demo_net_init();
		is_mac_obj_registered = 1;
	}

	if (parm->mac_index >= NUM_OF_MAC_DEVICES || parm->mdio_index >= NUM_OF_MDIO_DEVICES) {
		netdiag_cli_usage();
		return FAIL_PARAMETER_INVALID;
	}

	mac_obj = mac_adaptor.objs[parm->mac_index];
	phy = mac_obj->phy;
	net_connect_mdio(parm->mac_index, parm->mdio_index);

#if defined(ASPEED_AST2700)
	/* AST2700 MAC#2 only supports SGMII */
	if (parm->mac_index == 2) {
		mac_obj->is_sgmii = 1;
		parm->interface = PHY_INTERFACE_MODE_SGMII;
		parm->speed = 1000;
		parm->loop = 1;
	}
#endif

	/* NC-SI */
	if (parm->ncsi_mode != NCSI_MODE_NONE) {
#if defined(ASPEED_AST2700)
		if (parm->mac_index >= 2) {
			printf("AST2700 MAC#2 does not support NC-SI.\n");
			return FAIL_PARAMETER_INVALID;
		}
#endif
		test_obj.mode = NCSI_MODE;
		test_obj.ncsi.mode = parm->ncsi_mode;
		test_obj.ncsi.package_number = parm->ncsi_package;
		test_obj.ncsi.channel_number = parm->ncsi_channel;
		test_obj.ncsi.current_package = parm->ncsi_package;
		test_obj.ncsi.current_channel = parm->ncsi_channel;
		parm->control = NETDIAG_CTRL_NCSI;
		parm->interface = PHY_INTERFACE_MODE_RMII;
		parm->speed = 100;
		memset(test_obj.ncsi.info, 0xFF, sizeof(test_obj.ncsi.info));
		printf("ncsi parameters:\n mode: %d, package_num: %d, channel_num: %d\n",
		       test_obj.ncsi.mode, test_obj.ncsi.package_number,
		       test_obj.ncsi.channel_number);
	} else if (parm->checksum != NETDIAG_CKS_NONE) {
		test_obj.mode = CHECKSUM_MODE;
		test_obj.checksum.mode = parm->checksum;
	} else if (parm->vlan != NETDIAG_CKS_NONE) {
		test_obj.mode = VLAN_MODE;
		test_obj.vlan.mode = parm->vlan;
	} else {
		test_obj.mode = MAC_MODE;
	}

	printf("parameters:\nmac: %d, mdio: %d\n"
	       "speed: %d, control: %d\n"
	       "interface: %d, mode: %d, count: %d\n"
	       "mtu-size: %d, checksum: %d, margin: %d\n"
	       "tx-delay: %d, rx-delay: %d, packets: %d\n"
	       "vlan: %d\n",
	       parm->mac_index, parm->mdio_index, parm->speed, parm->control, parm->interface,
	       parm->mode, parm->loop, parm->mtu_size, parm->checksum, parm->margin, parm->tx_delay,
	       parm->rx_delay, parm->packets, parm->vlan);

	/* configure phy */
	phy->speed = parm->speed;
	phy->duplex = 1;
	phy->autoneg = 0;
	phy->phy_mode = parm->interface;
	if (parm->control == NETDIAG_CTRL_LOOPBACK_PHY) {
		phy->loopback = PHY_LOOPBACK_INT;
	} else if (parm->control == NETDIAG_CTRL_LOOPBACK_EXT) {
		phy->loopback = PHY_LOOPBACK_EXT;
	} else {
		phy->loopback = PHY_LOOPBACK_OFF;
		phy->autoneg = 1;
	}

	/* if mac loopback or mii loopback, no need to init phy */
	if (parm->control != NETDIAG_CTRL_LOOPBACK_MAC &&
	    parm->control != NETDIAG_CTRL_LOOPBACK_MII && parm->control != NETDIAG_CTRL_NCSI &&
	    !mac_obj->is_sgmii) {
		net_enable_mdio_pin(mac_obj->phy->mdio->device->dev_id - ASPEED_DEV_MDIO0);
		phy_init(phy);
	}

	/* configure mac */
	config_clock();
	mac_obj->is_rgmii =
		(mac_obj->phy->phy_mode == PHY_INTERFACE_MODE_RMII) ? 0 : 1;
	if (mac_obj->is_sgmii)
		net_enable_sgmii_pin();
	else if (mac_obj->is_rgmii)
		net_enable_rgmii_pin(mac_obj->device->dev_id - ASPEED_DEV_MAC0);
	else
		net_enable_rmii_pin(mac_obj->device->dev_id - ASPEED_DEV_MAC0);

	debug("pkt_bufs: 0x%p\n", pkt_bufs);
	for (i = 0; i < PKT_PER_TEST; i++) {
		test_obj.tx_pkt_buf[i] = &pkt_bufs[i][0];
		test_obj.rx_pkt_buf[i] = &pkt_bufs[PKT_PER_TEST + i][0];
		debug("tx_pkt_buf: 0x%p, rx_pkt_buf: 0x%p\n",
		      test_obj.tx_pkt_buf[i], test_obj.rx_pkt_buf[i]);
	}

	debug("address of txdes: %p\n", &txdes[0]);
	debug("address of rxdes: %p\n", &rxdes[0]);

	mac_obj->txdes = txdes;
	mac_obj->rxdes = rxdes;
	mac_obj->n_txdes = parm->packets;
	mac_obj->n_rxdes = parm->packets;
	mac_obj->rx_pkt_buf = &test_obj.rx_pkt_buf[0];
	mac_obj->mac_addr = &mac_addr[2];
	mac_obj->mtu = parm->mtu_size;

	aspeed_reset_assert(mac_obj->device);

	test_obj.mac_obj = mac_obj;
	test_obj.pkt_per_test = parm->packets;
	test_obj.fail_stop = true;
	switch (test_obj.mode) {
	case MAC_MODE:
		has_error = nettest_test(&test_obj);

		if (has_error && parm->mode == MODE_MARGIN)
			printf("\nnetdiag FAIL\n");
		else
			printf("\nnetdiag PASS\n");

		if (parm->control == NETDIAG_CTRL_LOOPBACK_PHY ||
		    parm->control == NETDIAG_CTRL_LOOPBACK_EXT)
			phy_free(phy);
		break;
	case NCSI_MODE:
		has_error = nettest_test(&test_obj);

		if (test_obj.mode == NCSI_MODE)
			ncsi_print_info(&test_obj);

		if (has_error == FAIL_CTRL_C)
			break;

		if (has_error && parm->mode == MODE_MARGIN)
			printf("\nnetdiag FAIL\n");
		else
			printf("\nnetdiag PASS\n");
		break;
	case CHECKSUM_MODE:
		has_error = checksum_offload_test(&test_obj);
		break;
	case VLAN_MODE:
		has_error = vlan_offload_test(&test_obj);
		break;
	case NONE_MODE:
	default:
		has_error = FAIL_PARAMETER_INVALID;
		break;
	};

	if (has_error)
		printf("Error code: %d\n", has_error);

	return has_error;
}
