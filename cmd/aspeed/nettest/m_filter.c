// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) ASPEED Technology Inc.
 */
#include <u-boot/crc.h>
#include <rand.h>
#include "platform.h"
#include "internal.h"
#include "m_filter.h"

#define MCAST_HASH_BUCKETS	64
#define MCAST_PKT_SIZE		64
#define MCAST_RECV_TRIES	10

static u8 ftgmac100_mcast_hash(const u8 *addr)
{
	u32 crc = 0xffffffff;
	int i, j;

	/* Ethernet CRC32 reflected:
	 * Poly = 0xEDB88320
	 * LSB-first
	 */
	for (i = 0; i < 6; i++) {
		crc ^= addr[i];

		for (j = 0; j < 8; j++) {
			if (crc & 1)
				crc = (crc >> 1) ^ 0xEDB88320;
			else
				crc >>= 1;
		}
	}

	return (u8)(~(crc >> 2) & 0x3f);
}

/*
 * Find a multicast address that hashes to the requested bucket by walking
 * 01:aa:bb:cc:dd:XX candidates through ftgmac100_mcast_hash().
 */
static const u8 *mcast_addr_for_bucket(u8 bucket)
{
	static u8 addr[ETH_SIZE_DA] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 };
	u16 last;

	bucket &= 0x3f;
	srand(get_timer(0));
	addr[1] = rand() & 0xFF;
	addr[2] = rand() & 0xFF;
	addr[3] = rand() & 0xFF;
	addr[4] = rand() & 0xFF;
	for (last = 0; last <= 0xff; last++) {
		addr[5] = (u8)last;
		if (ftgmac100_mcast_hash(addr) == bucket)
			return addr;
	}
	return NULL;
}

/*
 * Send one 64-byte Ethernet frame with the given DA, wait for receive,
 * and check whether the hardware hash filter passed or dropped it.
 */
static int send_filter_pkt(struct test_s *test_obj, const u8 *da, bool expect_recv)
{
	struct mac_s *mac_obj = test_obj->mac_obj;
	u8 *tx_buf = test_obj->tx_pkt_buf[0];
	struct eth_hdr *eth = (struct eth_hdr *)tx_buf;
	void *rx_pkt;
	u32 rxlen;
	int ret;

	aspeed_mac_init_tx_desc(mac_obj);
	aspeed_mac_init_rx_desc(mac_obj);

	memcpy(eth->da, da, ETH_SIZE_DA);
	memcpy(eth->sa, mac_obj->mac_addr, ETH_SIZE_SA);
	eth->ethtype = htons(ETHTYPE_IPV4);
	memset(tx_buf + ETH_SIZE_HEADER, 0xa5, MCAST_PKT_SIZE - ETH_SIZE_HEADER);

	aspeed_mac_txpkt_add(mac_obj, tx_buf, MCAST_PKT_SIZE, test_obj);
	DSB;
	aspeed_mac_xmit(mac_obj);

	ret = net_get_packet(mac_obj, &rx_pkt, &rxlen, MCAST_RECV_TRIES);

	if (expect_recv) {
		if (ret) {
			printf("[FAIL] hash=%u expect receive, got timeout\n",
			       ftgmac100_mcast_hash(da));
			return FAIL_TIMEOUT;
		}
		if (memcmp(((struct eth_hdr *)rx_pkt)->da, da, ETH_SIZE_DA)) {
			printf("[FAIL] DA mismatch in received packet\n");
			return FAIL_ETH_HDR_COMPARE;
		}
	} else {
		if (!ret) {
			printf("[FAIL] hash=%u expect filtered, got packet\n",
			       ftgmac100_mcast_hash(da));
			return FAIL_GENERAL;
		}
	}

	return 0;
}

static int mfilter_scan_test(struct test_s *test_obj)
{
	struct mac_s *mac_obj = test_obj->mac_obj;
	bool loopback = test_obj->parm.control == NETDIAG_CTRL_LOOPBACK_MAC;
	int b, i, ret = 0;

	printf("= Scan: testing %d hash bit positions\n", MCAST_HASH_BUCKETS);

	for (b = 0; b < MCAST_HASH_BUCKETS; b++) {
		u32 ht0 = 0, ht1 = 0;

#if defined(U_BOOT)
		if (ctrlc()) {
			clear_ctrlc();
			return FAIL_CTRL_C;
		}
#endif

		if (b < 32)
			ht0 = BIT(b);
		else
			ht1 = BIT(b - 32);

		printf("Bit %2d: ", b);

		for (i = 0; i < MCAST_HASH_BUCKETS; i++) {
			bool expect_hit = (i == b);

			aspeed_mac_init(mac_obj);
			aspeed_mac_set_loopback(mac_obj, loopback);
			aspeed_mac_set_mcast_hash(mac_obj, ht0, ht1);
			aspeed_mac_enable_hash_mcast(mac_obj, true);

			ret = send_filter_pkt(test_obj, mcast_addr_for_bucket(i), expect_hit);
			if (ret) {
				printf("%s addr[%d] FAIL\n", expect_hit ? "hit" : "miss", i);
				aspeed_mac_reg_dump(mac_obj);
				goto out_reset;
			}

			aspeed_reset_assert(mac_obj->device);
		}
		printf("PASS\n");
	}

	return 0;

out_reset:
	aspeed_reset_assert(mac_obj->device);
	return ret;
}

static int mfilter_random_test(struct test_s *test_obj)
{
	struct mac_s *mac_obj = test_obj->mac_obj;
	bool loopback = test_obj->parm.control == NETDIAG_CTRL_LOOPBACK_MAC;
	u32 ht0, ht1;
	int b, ret = 0;

	srand(get_timer(0));
	ht0 = rand();
	ht1 = rand();

	printf("= Random Test: MAHT0=0x%08x MAHT1=0x%08x\n", ht0, ht1);

	for (b = 0; b < MCAST_HASH_BUCKETS; b++) {
		bool expect_hit = (b < 32) ? !!(ht0 & BIT(b)) : !!(ht1 & BIT(b - 32));

#if defined(U_BOOT)
		if (ctrlc()) {
			clear_ctrlc();
			ret = FAIL_CTRL_C;
			goto out_reset;
		}
#endif

		aspeed_mac_init(mac_obj);
		aspeed_mac_set_loopback(mac_obj, loopback);
		aspeed_mac_set_mcast_hash(mac_obj, ht0, ht1);
		aspeed_mac_enable_hash_mcast(mac_obj, true);

		printf("Bit %2d (%s): ", b, expect_hit ? "hit " : "miss");
		ret = send_filter_pkt(test_obj, mcast_addr_for_bucket(b), expect_hit);
		printf("%s\n", ret ? "FAIL" : "PASS");
		if (ret) {
			aspeed_mac_reg_dump(mac_obj);
			goto out_reset;
		}

		aspeed_reset_assert(mac_obj->device);
	}

	return 0;

out_reset:
	aspeed_reset_assert(mac_obj->device);
	return ret;
}

int multicast_filter_test(struct test_s *test_obj)
{
	struct mac_s *mac_obj = test_obj->mac_obj;
	int ret;

	printf("== Multicast Hash Table Filter Test\n");

	switch (test_obj->m_filter.mode) {
	case NETDIAG_M_FILTER_SCAN:
		ret = mfilter_scan_test(test_obj);
		break;
	case NETDIAG_M_FILTER_RANDOM:
		ret = mfilter_random_test(test_obj);
		break;
	default:
		return FAIL_PARAMETER_INVALID;
	}

	aspeed_mac_set_loopback(mac_obj, false);
	aspeed_mac_enable_hash_mcast(mac_obj, false);

	printf("\n%s\n", ret ? "netdiag FAIL" : "netdiag PASS");
	return ret;
}
