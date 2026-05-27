/* SPDX-License-Identifier: GPL-2.0+ */
#include "platform.h"

#ifndef _INTERNAL_H_
#define _INTERNAL_H_

#define PKT_PER_TEST	32
#define MTU_SIZE		9578
#define MAC_TX_SIZE		(ETH_SIZE_HEADER + MTU_SIZE)
#define MAX_PACKET_SIZE		(ETH_SIZE_HEADER + ETH_SIZE_VLAN + MTU_SIZE + ETH_SIZE_FCS)

#define CEIL_16(x)		((((x) + 15) >> 4) << 4)

#if (CONFIG_SYS_CACHELINE_SIZE < 16)
#error "MAC engine requires that the DMA address be aligned to at least 16 bytes."
#endif

/* topologic */
#define NETDIAG_CTRL_LOOPBACK_EXT		0	/* PHY MDI loopback */
#define NETDIAG_CTRL_LOOPBACK_PHY		1	/* PHY PCS loopback */
#define NETDIAG_CTRL_LOOPBACK_MAC		2	/* MAC internal loopback */
#define NETDIAG_CTRL_LOOPBACK_MII		3	/* RMII/RGMII/SGMII loopback */
#define NETDIAG_CTRL_LOOPBACK_OFF		4	/* loopback off, pure TX */
#define NETDIAG_CTRL_NCSI			5	/* loopback off, NC-SI test */

#define MODE_SCAN				0	/* timing scan */
#define MODE_MARGIN				1	/* margin check */

/* Ethernet layer II frame */
#define ETH_SIZE_DA		6
#define ETH_SIZE_SA		6
#define ETH_SIZE_TYPE_LENG	2
#define ETH_SIZE_FCS		4
#define ETH_SIZE_VLAN		4
#define ETH_SIZE_HEADER		(ETH_SIZE_DA + ETH_SIZE_SA + ETH_SIZE_TYPE_LENG)
#define ETH_VLAN_SIZE_HEADER	(ETH_SIZE_DA + ETH_SIZE_SA + ETH_SIZE_VLAN + ETH_SIZE_TYPE_LENG)

#define ETH_OFFSET_DA		0
#define ETH_OFFSET_SA		(ETH_OFFSET_DA + ETH_SIZE_DA)

#define ETHTYPE_IPV4		0x0800
#define ETHTYPE_IPV6		0x86DD
#define ETHTYPE_VLAN		0x8100
#define ETHTYPE_QINQ		0x88a8
#define ETHTYPE_NCSI		0x88f8

#define IP_IPV4_VERSION		4
#define IP_HDR_OFFSET		ETH_SIZE_HEADER

#define IP_PROTOCOL_TCP		0x06
#define IP_PROTOCOL_UDP		0x11

#define MAX_DELAY_TAPS_RGMII_TX		63
#define MAX_DELAY_TAPS_RGMII_RX		63
#define MAX_DELAY_TAPS_RMII_TX		1
#define MAX_DELAY_TAPS_RMII_RX		63

#define TXDESC1_INS_VLAN	BIT(16)
#define RXDESC1_IPCS_FAIL	BIT(27)
#define RXDESC1_UDPCS_FAIL	BIT(26)
#define RXDESC1_TCPCS_FAIL	BIT(25)
#define RXDESC1_IPV6		BIT(19)
#define RXDESC1_PROTL_TYPE	GENMASK(21, 20)
#define PROTOCOL_IP		0x01
#define PROTOCOL_TCP		0x02
#define PROTOCOL_UDP		0x03

#define MAX_DELAY_TAPS_RGMII_TX		63
#define MAX_DELAY_TAPS_RGMII_RX		63
#define MAX_DELAY_TAPS_RMII_TX		1
#define MAX_DELAY_TAPS_RMII_RX		63

struct aspeed_sig_desc_s {
	u32 offset;
	u32 reg_set;
	int clr;
};

struct aspeed_group_config_s {
	char *group_name;
	int ndescs;
	struct aspeed_sig_desc_s *descs;
};

#define iowrite32(v, x) writel(v, x)

enum aspeed_dev_id {
	ASPEED_DEV_MAC0,
	ASPEED_DEV_MAC1,
	ASPEED_DEV_MAC2,
	ASPEED_DEV_MAC3,
	ASPEED_DEV_MDIO0,
	ASPEED_DEV_MDIO1,
	ASPEED_DEV_MDIO2,
	ASPEED_DEV_MDIO3,
};

enum mac_error_code {
	SUCCESS = 0,
	SUCCESS_HELP = 1,
	FAIL_CTRL_C,
	FAIL_ETH_HDR_COMPARE,
	FAIL_DATA_COMPARE,
	FAIL_FLAG_IPV6,
	FAIL_CHECKSUM,
	FAIL_PROTOCOL,
	FAIL_TX_NOT_APPEND_CHK,
	FAIL_VLAN_PACKET_SIZE,
	FAIL_VLAN_TYPE,
	FAIL_VLAN_TAGC,
	FAIL_GENERAL,
	FAIL_BUSY,
	FAIL_TIMEOUT,
	FAIL_MAC_RX_ERROR,
	FAIL_NCSI,
	FAIL_NCSI_MULTI_SCAN,
	FAIL_PARAMETER_INVALID,
	FAIL_ERR_VLAN_PROTOCOL,
	FAIL_ERR_VLAN_TCI,
};

enum test_mode {
	NONE_MODE = 0,
	MAC_MODE,
	NCSI_MODE,
	CHECKSUM_MODE,
	VLAN_MODE,
};

#define DECLARE_DEV_CLK(_name, _reg_en, _reg_dis, _bits)                       \
	struct aspeed_clk_s _name##_clk = {                                    \
		.reg_enable = (void *)_reg_en,                                 \
		.reg_disable = (void *)_reg_dis,                               \
		.bits = _bits,                                                 \
	}

#define DECLARE_DEV_RESET(_name, _reg_assert, _reg_deassert, _bits)            \
	struct aspeed_reset_s _name##_reset = {                                \
		.reg_assert = (void *)_reg_assert,                             \
		.reg_deassert = (void *)_reg_deassert,                         \
		.bits = _bits,                                                 \
	}

#define DECLARE_DEV(_name, _id, _base, _priv_data)                             \
	struct aspeed_device_s _name = {                                       \
		.dev_id = _id,                                                 \
		.base = (void *)_base,                                         \
		.reset = &_name##_reset,                               \
		.clk = &_name##_clk,                                   \
		.init = 0,                                                     \
		.private = _priv_data,                                         \
	}

#if defined(ASPEED_AST2700)
#define NUM_OF_MDIO_DEVICES		3
#define NUM_OF_MAC_DEVICES		3
#elif defined(ASPEED_AST2600)
#define NUM_OF_MDIO_DEVICES		4
#define NUM_OF_MAC_DEVICES		4
#endif

struct aspeed_reset_s {
	void __iomem *reg_assert;
	void __iomem *reg_deassert;
	u32 bits;
};

struct aspeed_clk_s {
	void __iomem *reg_enable;
	void __iomem *reg_disable;
	u32 bits;
};

struct aspeed_device_s {
	u32 dev_id;
	void __iomem *base;
	struct aspeed_reset_s *reset;
	struct aspeed_clk_s *clk;
	int init;
	void *private;
};

struct mdio_s {
	struct aspeed_device_s *device;
	u32 phy_addr;
};

struct phy_s {
	struct mdio_s *mdio;	/* mdio device to control PHY */
	u32 speed;
	u32 duplex;
	u32 autoneg;
	u32 link;
	u32 phy_mode;		/* RGMII, RGMII_RXID or RGMII_TXID */
	u16 id[2];
	u8 loopback;

	/* Keep register value */
	u16 phy_reg[32];

	u16 phy_id[32][2];
	struct phy_desc *dev_phy[32];
};

struct mac_txdes_s {
	u32 txdes0;
	u32 txdes1;
	u32 txdes2;
	u32 txdes3;
} DMA_ALIGNED;

struct mac_rxdes_s {
	u32 rxdes0;
	u32 rxdes1;
	u32 rxdes2;
	u32 rxdes3;
} DMA_ALIGNED;

struct cfg_rgmii_s {
	u32 reg_set;
	u32 reg_clr;
	u32 bits;
};

struct aspeed_mac_priv_s {
	struct cfg_rgmii_s cfg_rgmii;
	int max_rx_packet_size;
};

struct parameter_s {
	u32 mac_index;
	u32 mdio_index;
	u32 speed;
	u32 mode;
	s32 margin;
	u32 interface;
	u32 control;
	u32 loop;
	u32 mtu_size;
	u32 checksum;
	u32 vlan;
	u32 packets;
	u32 ncsi_mode;
	u32 ncsi_package;
	u32 ncsi_channel;
	s32 tx_delay;
	s32 rx_delay;
};

struct mac_s {
	struct aspeed_device_s *device;	/* MAC device self */
	struct phy_s *phy;		/* mdio device to control PHY */
	struct mac_txdes_s *txdes;	/* pointer to the TX descriptors */
	struct mac_rxdes_s *rxdes;	/* pointer to the RX descriptors */
	u32 mtu;
	u32 txptr;
	u32 rxptr;
	u32 n_txdes;		/* number of TX descriptors */
	u32 n_rxdes;		/* number of RX descriptors */
	u8 **rx_pkt_buf;
	u8 *mac_addr;
	u8 is_rgmii;		/* 0: RMII, 1: RGMII */
	u8 is_sgmii;
	u32 txdes1;		/* For chk & VLAN test */
	bool rx_vlan_remove;
};

enum chip_version {
	AST2700 = 0,
	AST2705,
};

struct test_s {
	enum test_mode mode;
	struct mac_s *mac_obj;
	u8 *rx_pkt_buf[PKT_PER_TEST];
	u8 *tx_pkt_buf[PKT_PER_TEST];
	int pkt_per_test;

	struct parameter_s parm;

	enum chip_version chip;

	struct {
		u32 mode;
		enum {
			NCSI_PROBE_PACKAGE_SP,
			NCSI_PROBE_PACKAGE_DP,
			NCSI_PROBE_CHANNEL,
			NCSI_GET_VERSION_ID,
		} state;
		u32 last_request;
		u8 complete;
		u16 temp_pci_ids[4];
		u32 temp_mf_id;
		u32 package_number;
		u32 channel_number;
		u32 package_count;
		u32 channel_count;
		u32 current_package;
		u32 current_channel;
#define NCSI_LIST_MAX	32
		struct {
			u8 package;
			u8 channel;
			u16 pci_ids[4];	/* PCI identification */
			u32 mf_id;
		} info[NCSI_LIST_MAX];
	} ncsi;

	struct {
		u32 mode;
	} checksum;

	struct {
		u32 mode;
		u16 tci[PKT_PER_TEST];
		bool append_vlan;
	} vlan;
	bool fail_stop;
};

struct eth_hdr {
	__u8 da[ETH_SIZE_DA];
	__u8 sa[ETH_SIZE_SA];
	__be16 ethtype;
} __packed;

struct vlan_ethhdr {
	__u8 da[ETH_SIZE_DA];
	__u8 sa[ETH_SIZE_SA];
	__be16 vlan_proto;
	__be16 vlan_TCI;
	__be16 vlan_encapsulated_proto;
};

struct vlan_hdr {
	__u8 da[ETH_SIZE_DA];
	__u8 sa[ETH_SIZE_SA];
	__be16 ethtype;
} __packed;

struct ip_hdr {
	__u8 ihl : 4, version : 4;
	__u8 tos;
	__be16 total_len;
	__be16 id;
	__be16 fragment_offset;
	__u8 ttl;
	__u8 protocol;
	__sum16 checksum;
	__be32 sa;
	__be32 da;
} __packed;

struct ipv6_hdr {
	__u8 priority : 4, version : 4;
	__u8 flow_lbl[3];
	__be16 payload_len;
	__u8 next_hdr;
	__u8 hop_limit;
	__u8 sa[16];
	__u8 da[16];
} __packed;

struct tcp_hdr {
	__be16 sp;
	__be16 dp;
	__be32 seq;
	__be32 ack_seq;
	__u8 res1 : 4, doff : 4;
	__u8 flag;
	__be16 window;
	__sum16 checksum;
	__be16 urg_ptr;
} __packed;

struct udp_hdr {
	__be16 sp;
	__be16 dp;
	__be16 total_len;
	__sum16 checksum;
} __packed;

struct ipc4_pseudo_hdr {
	__be32 sa;
	__be32 da;
	__u8 res;
	__u8 protocol;
	__be16 total_len; /* TCP HDR + PAYLOAD */
} __packed;

struct ipc6_pseudo_hdr {
	__u8 sa[16];
	__u8 da[16];
	__u8 res;
	__u8 protocol;
	__be16 total_len; /* TCP HDR + PAYLOAD */
} __packed;

extern u8 pkt_bufs[PKT_PER_TEST * 2][ROUNDUP_DMA_SIZE(MAX_PACKET_SIZE)] DMA_ALIGNED;
extern struct aspeed_group_config_s mdio_pinctrl[NUM_OF_MDIO_DEVICES];
extern struct aspeed_group_config_s rgmii_pinctrl[NUM_OF_MAC_DEVICES];
extern struct aspeed_group_config_s rmii_pinctrl[NUM_OF_MAC_DEVICES];
extern struct mdio_s mdio_data[NUM_OF_MDIO_DEVICES];
extern struct phy_s phy_data[NUM_OF_MDIO_DEVICES];
extern struct mac_s mac_data[NUM_OF_MAC_DEVICES];

int aspeed_reset_assert(struct aspeed_device_s *device);
int aspeed_reset_deassert(struct aspeed_device_s *device);
int aspeed_clk_enable(struct aspeed_device_s *device);
int aspeed_clk_disable(struct aspeed_device_s *device);
void aspeed_clk_set_rgmii_delay(struct mac_s *obj, int speed, u32 tx, u32 rx);
void aspeed_clk_get_rgmii_delay(struct mac_s *obj, int speed, u32 *tx, u32 *rx);
void aspeed_clk_set_rmii_delay(struct mac_s *obj, int speed, u32 tx, u32 rx);
void aspeed_clk_get_rmii_delay(struct mac_s *obj, int speed, u32 *tx, u32 *rx);

void aspeed_mdio_init(struct mdio_s *mdio);
int aspeed_mdio_read(struct mdio_s *mdio, int addr, int regnum);
int aspeed_mdio_write(struct mdio_s *mdio, int addr, int regnum, u16 val);

int aspeed_mac_reset(struct mac_s *obj);
int aspeed_mac_set_loopback(struct mac_s *obj, u8 enable);
int aspeed_mac_set_sgmii_loopback(struct mac_s *obj, u8 enable);
int aspeed_mac_init(struct mac_s *obj);
int aspeed_mac_deinit(struct mac_s *obj);
int aspeed_mac_txpkt_add(struct mac_s *obj, void *packet, int length, struct test_s *test_obj);
int aspeed_mac_xmit(struct mac_s *obj);
int aspeed_mac_recv(struct mac_s *obj, void **packet, u32 *rxlen);
int aspeed_mac_init_rx_desc(struct mac_s *obj);
int aspeed_mac_init_tx_desc(struct mac_s *obj);
int aspeed_mac_set_speed(struct mac_s *obj, u32 speed);
void aspeed_mac_reg_dump(struct mac_s *obj);
void ast2705_clk_set_rmii_delay(struct mac_s *obj, int speed, u32 tx, u32 rx);
void ast2705_clk_get_rmii_delay(struct mac_s *obj, int speed, u32 *tx, u32 *rx);
void ast2705_clk_set_rgmii_delay(struct mac_s *obj, int speed, u32 tx, u32 rx);
void ast2705_clk_get_rgmii_delay(struct mac_s *obj, int speed, u32 *tx, u32 *rx);
int net_get_packet(struct mac_s *obj, void **packet, u32 *rxlen, int max_try);
int net_enable_mdio_pin(int mdio_idx);
#if defined(ASPEED_AST2700)
#define MEMCPY memcpy
#else
void _memcpy(void *dest, void *src, size_t count);
#define MEMCPY _memcpy
#endif

#endif /* _INTERNAL_H_ */
