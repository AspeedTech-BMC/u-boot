/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef _NETDIAG_M_FILTER_H
#define _NETDIAG_M_FILTER_H

#define NETDIAG_M_FILTER_NONE		0	/* no multicast address test */
#define NETDIAG_M_FILTER_SCAN		1	/* Scan all test */
#define NETDIAG_M_FILTER_RANDOM		2	/* Random target test */

int multicast_filter_test(struct test_s *test_obj);

#endif	/* _NETDIAG_M_FILTER_H */
