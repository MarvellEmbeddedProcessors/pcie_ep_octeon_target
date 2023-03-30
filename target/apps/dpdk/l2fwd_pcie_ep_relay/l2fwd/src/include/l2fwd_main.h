/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __L2FWD_MAIN_H__
#define __L2FWD_MAIN_H__

/* Maximum number of supported pem's */
#define L2FWD_MAX_PEM		8
/* Maximum number of supported pf's per pem */
#define L2FWD_MAX_PF		128
/* Maximum number of supported vf's per pf */
#define L2FWD_MAX_VF		128

/* function implementation types */
enum {
	L2FWD_FN_TYPE_UNKNOWN = -1,
	L2FWD_FN_TYPE_STUB = 0,
	L2FWD_FN_TYPE_NIC,
	L2FWD_FN_TYPE_MAX
};

extern struct l2fwd_user_config l2fwd_user_cfg;

static const struct rte_pci_addr zero_dbdf = { 0, 0, 0, 0 };

/* Check if dbdf is empty or 00:0000:0.0
 *
 * return value: 1 on success, 0 on failure.
 */
static inline int is_zero_dbdf(const struct rte_pci_addr *dbdf)
{
	return !rte_pci_addr_cmp(dbdf, &zero_dbdf);
}

#endif /* __L2FWD_MAIN_H__ */
