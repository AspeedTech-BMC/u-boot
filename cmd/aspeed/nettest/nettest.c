// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) ASPEED Technology Inc.
 */
#include "platform.h"
#include "internal.h"
#include "phy.h"
#include "ncsi.h"

void prepare_tx_packet(u8 *pkt, int length)
{
	int j;
	u8 *ptr;

	ptr = pkt;
	/* DA: broadcast */
	for (j = 0; j < ETH_SIZE_DA; j++)
		*ptr++ = 0xff;

	/* SA: skip */
	ptr += ETH_SIZE_SA;

	/* length */
	*ptr++ = (length >> 8) & 0xff;
	*ptr++ = length & 0xff;

	/* payload */
	for (j = 0; j < length - ETH_SIZE_HEADER; j++)
		*ptr++ = j;

#ifdef DUMP_TX_PACKET
	ptr = pkt;
	for (j = 0; j < length; j++) {
		printf("%02x ", *ptr++);
		if ((j & 0x7) == 0x7)
			printf("\n");
	}
#endif
}

void print_xticks(int start, int end, int center)
{
	int i;

	printf("\nMargin test:");

	printf("\nT   ");
	for (i = start; i <= end; i++)
		printf("%1x", (i >> 4) & 0xf);

	printf(" - RX\nX   ");
	for (i = start; i <= end; i++)
		printf("%1x", i & 0xf);

	printf("\n|   ");
	for (i = start; i <= end; i++)
		printf("%c", (i == center) ? '|' : ' ');
	printf("\n");
}

int mactest(struct test_s *test_obj)
{
	struct mac_s *mac_obj = test_obj->mac_obj;
	struct parameter_s *parm = &test_obj->parm;
	u8 **rx_pkt_buf = test_obj->rx_pkt_buf;
	u8 **tx_pkt_buf = test_obj->tx_pkt_buf;
	int i, j, k;
	u32 rxlen;
	int status;
	int ret = 0;
	int tx_size = parm->mtu_size + ETH_SIZE_HEADER;
	u8 *ptr;

	k = 0;
loop_start:
	aspeed_mac_init(mac_obj);
	aspeed_mac_set_loopback(mac_obj, parm->control == NETDIAG_CTRL_LOOPBACK_MAC);
	aspeed_mac_set_sgmii_loopback(mac_obj, parm->control == NETDIAG_CTRL_LOOPBACK_MII);

	/* prepare the first packet */
	prepare_tx_packet(&tx_pkt_buf[0][0], tx_size);
	aspeed_mac_txpkt_add(mac_obj, tx_pkt_buf[0], tx_size, test_obj);

	for (i = 1; i < test_obj->pkt_per_test; i++) {
		memcpy(&tx_pkt_buf[i][0], &tx_pkt_buf[0][0], tx_size);

		/* every tx packet has its own SA for identification */
		ptr = &tx_pkt_buf[i][ETH_OFFSET_SA];
		memset(ptr, i, ETH_SIZE_SA);
		aspeed_mac_txpkt_add(mac_obj, tx_pkt_buf[i], tx_size, test_obj);
	}

	DSB;
	aspeed_mac_xmit(mac_obj);
	ret = 0;
	for (i = 0; i < test_obj->pkt_per_test; i++) {
		status = net_get_packet(mac_obj, (void **)&rx_pkt_buf[i], &rxlen, 10);
		if (status == 0) {
			debug("RX packet %d: length=%d addr=%p\n", i, rxlen, rx_pkt_buf[i]);

			/* examine if rx packets are in sequence by checking SA */
			j = memcmp(&rx_pkt_buf[i][ETH_OFFSET_SA], &tx_pkt_buf[i][ETH_OFFSET_SA],
				   ETH_SIZE_SA);
			if (j) {
				ret = FAIL_DATA_COMPARE;
				break;
			}
		} else {
			debug("pkt#%02x: error: %d\n", i, status);
			ret = status;
			break;
		}
	}

	if (!(test_obj->chip == AST2705 && test_obj->chip_revision == 0))
		aspeed_reset_assert(mac_obj->device);

	if ((++k < parm->loop) && ret == 0)
		goto loop_start;

	return ret;
}

/* Only send packets */
void txtest(struct test_s *test_obj)
{
	struct mac_s *mac_obj = test_obj->mac_obj;
	struct parameter_s *parm = &test_obj->parm;
	u8 **tx_pkt_buf = test_obj->tx_pkt_buf;
	int tx_size = parm->mtu_size + ETH_SIZE_HEADER;
	int i, k;
	u8 *ptr;

	k = 0;
	/* prepare the first packet */
	prepare_tx_packet(&tx_pkt_buf[0][0], tx_size);
	for (i = 1; i < test_obj->pkt_per_test; i++) {
		memcpy(&tx_pkt_buf[i][0], &tx_pkt_buf[0][0], tx_size);
		/* every tx packet has its own SA for identification */
		ptr = &tx_pkt_buf[i][ETH_OFFSET_SA];
		memset(ptr, i, ETH_SIZE_SA);
	}
loop_start:
	aspeed_mac_init(mac_obj);
	aspeed_mac_set_loopback(mac_obj, parm->control == NETDIAG_CTRL_LOOPBACK_MAC);
	aspeed_mac_set_sgmii_loopback(mac_obj, parm->control == NETDIAG_CTRL_LOOPBACK_MII);
	for (i = 0; i < test_obj->pkt_per_test; i++)
		aspeed_mac_txpkt_add(mac_obj, tx_pkt_buf[i], tx_size, test_obj);

	DSB;
	aspeed_mac_xmit(mac_obj);

	if ((++k < parm->loop))
		goto loop_start;
}

void nettest_info(struct test_s *test_obj)
{
	printf("\nMargin Test Information:\n");
	printf("Scan delay setting\n");
	printf("    o : OK\n");
	printf("    x : CRC error\n");
	printf("    . : packet not found\n");
	printf("System default setting\n");
	printf("    O : OK\n");
	printf("    X : CRC error\n");
	printf("    * : packet not found\n\n");

	if (test_obj->mode == MAC_MODE) {
		printf("Dedicated PHY Test:\n");
		printf("MAC#%d MDIO#%d\n", test_obj->parm.mac_index, test_obj->parm.mdio_index);
	} else if (test_obj->mode == NCSI_MODE) {
		printf("NC-SI Test:\n");
		if (test_obj->ncsi.mode == NCSI_MODE_MULTI) {
			printf("Multiple Scan Packages: %d, Channels: %d\n",
			       test_obj->ncsi.package_number, test_obj->ncsi.channel_number);
			if (test_obj->ncsi.package_number == 0)
				printf("Try to find any package and channel.\n");
		} else {
			printf("Single Scan Package: %d, Channel: %d\n",
			       test_obj->ncsi.current_package, test_obj->ncsi.current_channel);
		}
	}
}

int nettest_test(struct test_s *test_obj)
{
	struct mac_s *mac_obj = test_obj->mac_obj;
	struct parameter_s *parm = &test_obj->parm;
	int tx_c, tx_s, tx_e, rx_c, rx_s, rx_e, tx_max, rx_max;
	int i, j, has_error = 0;
	int (*run_test)(struct test_s *test_obj);
	void (*clk_get_delay)(struct mac_s *mac_obj, int speed, u32 *tx, u32 *rx);
	void (*clk_set_delay)(struct mac_s *mac_obj, int speed, u32 tx, u32 rx);

	nettest_info(test_obj);

	if (test_obj->mode == MAC_MODE)
		run_test = mactest;
	else if (test_obj->mode == NCSI_MODE)
		run_test = ncsitest;
	else
		return FAIL_PARAMETER_INVALID;

	if (mac_obj->is_sgmii) {
		has_error = run_test(test_obj);
		if (has_error == 0) {
			printf("PASS\n");
			return 0;
		}
		printf("FAIL %d\n", has_error);
		goto out;
	}

	if (test_obj->chip == AST2705) {
		if (mac_obj->phy->phy_mode == PHY_INTERFACE_MODE_RMII) {
			clk_get_delay = ast2705_clk_get_rmii_delay;
			clk_set_delay = ast2705_clk_set_rmii_delay;
			tx_max = MAX_DELAY_TAPS_RMII_TX;
			rx_max = MAX_DELAY_TAPS_RMII_RX;
		} else {
			clk_get_delay = ast2705_clk_get_rgmii_delay;
			clk_set_delay = ast2705_clk_set_rgmii_delay;
			tx_max = MAX_DELAY_TAPS_RGMII_TX;
			rx_max = MAX_DELAY_TAPS_RGMII_RX;
		}
	} else {
		if (mac_obj->phy->phy_mode == PHY_INTERFACE_MODE_RMII) {
			clk_get_delay = aspeed_clk_get_rmii_delay;
			clk_set_delay = aspeed_clk_set_rmii_delay;
			tx_max = MAX_DELAY_TAPS_RMII_TX;
			rx_max = MAX_DELAY_TAPS_RMII_RX;
		} else {
			clk_get_delay = aspeed_clk_get_rgmii_delay;
			clk_set_delay = aspeed_clk_set_rgmii_delay;
			tx_max = MAX_DELAY_TAPS_RGMII_TX;
			rx_max = MAX_DELAY_TAPS_RGMII_RX;
		}
	}

	clk_get_delay(mac_obj, mac_obj->phy->speed, (u32 *)&tx_c,
		      (u32 *)&rx_c);

	/* if need to specify tx/rx delay */
	if (parm->tx_delay != -1 || parm->rx_delay != -1) {
		if (parm->tx_delay == -1)
			parm->tx_delay = tx_c;
		else
			tx_c = parm->tx_delay;
		if (parm->rx_delay == -1)
			parm->rx_delay = rx_c;
		else
			rx_c = parm->rx_delay;
		clk_set_delay(mac_obj, mac_obj->phy->speed, parm->tx_delay,
			      parm->rx_delay);
		/* single step */
		parm->margin = 0;
	}

	/* Calculate the margin range */
	if (parm->mode == MODE_MARGIN) {
		tx_s = max(tx_c - parm->margin, 0);
		tx_e = min(tx_c + parm->margin, tx_max);
		rx_s = max(rx_c - parm->margin, 0);
		rx_e = min(rx_c + parm->margin, rx_max);
	} else {
		tx_s = 0;
		tx_e = tx_max;
		rx_s = 0;
		rx_e = rx_max;
	}

	if (parm->control == NETDIAG_CTRL_LOOPBACK_OFF) {
		txtest(test_obj);
		return 0;
	}

	if (test_obj->mode == NCSI_MODE) {
		tx_s = tx_c;
		tx_e = tx_c;
	}

	print_xticks(rx_s, rx_e, rx_c);
	for (i = tx_s; i <= tx_e; i++) {
		printf("%02x:%c", i, (i == tx_c) ? '-' : ' ');
		for (j = rx_s; j <= rx_e; j++) {
			int status;

			clk_set_delay(mac_obj, mac_obj->phy->speed, i, j);
			status = run_test(test_obj);
			if (status == 0) {
				if (i == tx_c && j == rx_c)
					printf("O");
				else
					printf("o");
			} else if (status == FAIL_MAC_RX_ERROR) {
				if (i == tx_c && j == rx_c)
					printf("X");
				else
					printf("x");
				has_error = status;
			} else {
				if (i == tx_c && j == rx_c)
					printf("*");
				else
					printf(".");
				has_error = status;
			}
#if defined(U_BOOT)
			if (ctrlc()) {
				clear_ctrlc();
				clk_set_delay(mac_obj, mac_obj->phy->speed, tx_c, rx_c);
				aspeed_reset_deassert(mac_obj->device);
				aspeed_mac_set_loopback(mac_obj, false);
				aspeed_mac_set_sgmii_loopback(mac_obj, false);
				return FAIL_CTRL_C;
			}
#endif
		}
		printf("\n");
	}

	/* restore the delay setting */
	printf("\nSystem default setting: TX: 0x%x, RX: 0x%x\n", tx_c, rx_c);
	clk_set_delay(mac_obj, mac_obj->phy->speed, tx_c, rx_c);
out:
	aspeed_reset_deassert(mac_obj->device);
	aspeed_mac_set_loopback(mac_obj, false);
	aspeed_mac_set_sgmii_loopback(mac_obj, false);

	switch (test_obj->mode) {
	case MAC_MODE:
		if (mac_obj->is_sgmii)
			break;
		if (parm->mode == MODE_SCAN) {
			has_error = SUCCESS;
		}
		break;
	case NCSI_MODE:
		if (test_obj->ncsi.mode == NCSI_MODE_MULTI && test_obj->ncsi.package_number == 0)
			has_error = SUCCESS;
		break;
	case NONE_MODE:
	case CHECKSUM_MODE:
	default:
		break;
	}

	return has_error;
}
