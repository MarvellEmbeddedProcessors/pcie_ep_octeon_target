/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __L2FWD_H__
#define __L2FWD_H__

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

/* Check if dbdf is empty or 00:0000:0.0
 *
 * return value: 1 on success, 0 on failure.
 */
static inline int is_zero_dbdf(const struct rte_pci_addr *dbdf)
{
	static const struct rte_pci_addr zero_dbdf = { 0, 0, 0, 0 };

	return !rte_pci_addr_cmp(dbdf, &zero_dbdf);
}

/* Find rte port for given rte_pci_addr.
 *
 * return value: port index on success, RTE_MAX_ETHPORTS on error
 */
static inline unsigned int find_rte_port(struct rte_pci_addr *dbdf)
{
#if L2FWD_PCIE_EP_RTE_VERSION < RTE_VERSION_NUM(22, 11, 0, 0)
	struct rte_eth_dev_info dev_info;
	struct rte_pci_device *pci_dev;
	unsigned int portid;
	int err;

	RTE_ETH_FOREACH_DEV(portid) {
		err = rte_eth_dev_info_get(portid, &dev_info);
		if (err < 0)
			continue;

		pci_dev = RTE_DEV_TO_PCI(dev_info.device);
		if (!rte_pci_addr_cmp(dbdf, &pci_dev->addr))
			return portid;
	}
	return RTE_MAX_ETHPORTS;
#else
	return RTE_MAX_ETHPORTS;
#endif
}

/* Control plane calls this before resetting pem */
void l2fwd_on_before_control_pem_reset(int pem);

/* Control plane calls this before resetting pf */
void l2fwd_on_before_control_pf_reset(int pem, int pf);

/* Control plane calls this before resetting vf */
void l2fwd_on_before_control_vf_reset(int pem, int pf, int vf);

/* Control plane calls this after resetting vf */
void l2fwd_on_after_control_vf_reset(int pem, int pf, int vf);

/* Control plane calls this after resetting pf */
void l2fwd_on_after_control_pf_reset(int pem, int pf);

/* Control plane calls this after resetting pem */
void l2fwd_on_after_control_pem_reset(int pem);

#endif /* __L2FWD_H__ */
