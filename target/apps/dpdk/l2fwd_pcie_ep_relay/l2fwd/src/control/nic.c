/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#include "compat.h"
#include "l2fwd_main.h"
#include "l2fwd_config.h"
#include "octep_cp_lib.h"
#include "octep_ctrl_net.h"
#include "control.h"

#define RTE_LOGTYPE_L2FWD_CTRL_NIC	RTE_LOGTYPE_USER1

struct nic_fn_data {
	unsigned int portid;
	/* rx offload flags */
	unsigned short rx_offloads;
	/* tx offload flags */
	unsigned short tx_offloads;
	/* ext offload flags */
	unsigned long long ext_offloads;
};

struct nic_pf {
	/* pf config */
	struct nic_fn_data d;
	/* vf's */
	struct nic_fn_data *vfs[L2FWD_MAX_VF];
};

struct nic_pem {
	/* pf indices */
	struct nic_pf *pfs[L2FWD_MAX_PF];
};

/* runtime data */
static struct nic_pem *nic_data[L2FWD_MAX_PEM] = { 0 };

static inline struct nic_fn_data *get_fn_data(int pem, int pf, int vf)
{
	return (vf >= 0) ?
		nic_data[pem]->pfs[pf]->vfs[vf] :
		&nic_data[pem]->pfs[pf]->d;
}

static int free_pems(void)
{
	struct nic_pem *pem;
	struct nic_pf *pf;
	int i, j, k;

	for (i = 0; i < L2FWD_MAX_PEM; i++) {
		if (!nic_data[i])
			continue;

		pem = nic_data[i];
		for (j = 0; j < L2FWD_MAX_PF; j++) {
			if (!pem->pfs[j])
				continue;

			pf = pem->pfs[j];
			for (k = 0; k < L2FWD_MAX_VF; k++) {
				if (!pf->vfs[k])
					continue;

				free(pf->vfs[k]);
				pf->vfs[k] = NULL;
			}
			free(pf);
			pem->pfs[j] = NULL;
		}

		free(pem);
		nic_data[i] = NULL;
	}

	return 0;
}

static int init_fn(struct nic_fn_data *fn, struct l2fwd_config_fn *fn_cfg)
{
	fn->portid = l2fwd_pcie_ep_find_port(&fn_cfg->to_wire_dbdf);

	return 0;
}

static int init_valid_cfg(int pem_idx, int pf_idx, int vf_idx, void *data)
{
	struct l2fwd_config_fn *fn_cfg;
	struct nic_pem *pem;
	struct nic_pf *pf;

	fn_cfg = (vf_idx < 0) ?
		  &l2fwd_cfg.pems[pem_idx].pfs[pf_idx].d :
		  &l2fwd_cfg.pems[pem_idx].pfs[pf_idx].vfs[vf_idx];

	if (!nic_data[pem_idx]) {
		nic_data[pem_idx] = calloc(1, sizeof(struct nic_pem));
		if (!nic_data[pem_idx]) {
			RTE_LOG(ERR, L2FWD_CTRL_NIC,
				"[%d]: pem data alloc failed\n",
				pem_idx);
			return -ENOMEM;
		}
	}
	pem = nic_data[pem_idx];
	if (!pem->pfs[pf_idx]) {
		pem->pfs[pf_idx] = calloc(1, sizeof(struct nic_pf));
		if (!pem->pfs[pf_idx]) {
			RTE_LOG(ERR, L2FWD_CTRL_NIC,
				"[%d][%d]: pf data alloc failed\n",
				pem_idx, pf_idx);
			return -ENOMEM;
		}
	}
	pf = pem->pfs[pf_idx];
	if (vf_idx < 0)
		return init_fn(&pf->d, fn_cfg);

	if (!pf->vfs[vf_idx]) {
		pf->vfs[vf_idx] = calloc(1, sizeof(struct nic_fn_data));
		if (!pf->vfs[vf_idx]) {
			RTE_LOG(ERR, L2FWD_CTRL_NIC,
				"[%d][%d][%d]: vf data alloc failed\n",
				pem_idx, pf_idx, vf_idx);
			return -ENOMEM;
		}
	}

	return init_fn(pf->vfs[vf_idx], fn_cfg);
}

static int init_pems(void)
{
	int err = 0;

	err = for_each_valid_config_fn(&init_valid_cfg, NULL);
	if (err < 0) {
		free_pems();
		return err;
	}

	return 0;
}

static int nic_get_mtu(int pem, int pf, int vf, uint16_t *mtu)
{
	struct nic_fn_data *fn;

	fn = get_fn_data(pem, pf, vf);

	return rte_eth_dev_get_mtu(fn->portid, mtu);
}

static int nic_set_mtu(int pem, int pf, int vf, uint16_t mtu)
{
	struct nic_fn_data *fn;

	fn = get_fn_data(pem, pf, vf);

	return rte_eth_dev_set_mtu(fn->portid, mtu);
}

static int nic_get_mac(int pem, int pf, int vf, uint8_t *addr)
{
	struct nic_fn_data *fn;
	struct rte_ether_addr mac;
	int err;

	fn = get_fn_data(pem, pf, vf);
	err = rte_eth_macaddr_get(fn->portid, &mac);
	if (!err)
		memcpy(addr, &mac.addr_bytes, ETH_ALEN);

	return err;
}

static int nic_set_mac(int pem, int pf, int vf, uint8_t *addr)
{
	struct rte_ether_addr mac;
	struct nic_fn_data *fn;

	fn = get_fn_data(pem, pf, vf);
	memcpy(&mac.addr_bytes, addr, ETH_ALEN);

	return rte_eth_dev_default_mac_addr_set(fn->portid, &mac);
}

static int nic_get_link_state(int pem, int pf, int vf, uint16_t *state)
{
	struct rte_eth_link link;
	struct nic_fn_data *fn;
	int err;

	fn = get_fn_data(pem, pf, vf);
	err = rte_eth_link_get_nowait(fn->portid, &link);
	if (!err)
		*state = link.link_status;

	return err;
}

static int nic_set_link_state(int pem, int pf, int vf, uint16_t state)
{
	struct nic_fn_data *fn;

	fn = get_fn_data(pem, pf, vf);

	return (state) ?
	       rte_eth_dev_set_link_up(fn->portid) :
	       rte_eth_dev_set_link_down(fn->portid);
}

static int nic_get_link_info(int pem, int pf, int vf,
			     struct octep_ctrl_net_link_info *info)
{
	struct rte_eth_conf conf = { 0 };
	struct rte_eth_link link = { 0 };
	struct nic_fn_data *fn;
	int err;

	fn = get_fn_data(pem, pf, vf);

/*
 *	err = rte_eth_dev_conf_get(fn->portid, &conf);
 *	if (err < 0)
 *		return err;
 */
	err = rte_eth_link_get_nowait(fn->portid, &link);
	if (err < 0)
		return err;

	info->supported_modes = conf.link_speeds;
	info->advertised_modes = conf.link_speeds;
	info->autoneg = link.link_autoneg;
	/* ??? resp->link_info.pause */
	info->speed = link.link_speed;

	return 0;
}

static int nic_set_link_info(int pem, int pf, int vf,
			     struct octep_ctrl_net_link_info *info)
{
	/** TODO potentially stop and reconfigure port */

	return 0;
}

static inline int reset_vf(int pem_idx, int pf_idx, int vf_idx)
{
	struct l2fwd_config_fn *fn_cfg;
	struct nic_fn_data *fn;

	fn = get_fn_data(pem_idx, pf_idx, vf_idx);
	fn_cfg = &l2fwd_cfg.pems[pem_idx].pfs[pf_idx].vfs[vf_idx];

	return init_fn(fn, fn_cfg);
}

static int reset_pf(int pem_idx, int pf_idx)
{
	struct l2fwd_config_fn *fn_cfg;
	struct nic_pf *pf;
	int vf_idx, err;

	pf = nic_data[pem_idx]->pfs[pf_idx];
	fn_cfg = &l2fwd_cfg.pems[pem_idx].pfs[pf_idx].d;
	err = init_fn(&pf->d, fn_cfg);
	if (err < 0)
		return err;

	for (vf_idx = 0; vf_idx < L2FWD_MAX_VF; vf_idx++) {
		if (!pf->vfs[vf_idx])
			continue;

		err = reset_vf(pem_idx, pf_idx, vf_idx);
		if (err < 0)
			return err;
	}

	return 0;
}

static int reset_pem(int pem_idx)
{
	struct nic_pem *pem;
	int pf_idx, err;

	if (!nic_data[pem_idx])
		return -EINVAL;

	pem = nic_data[pem_idx];
	for (pf_idx = 0; pf_idx < L2FWD_MAX_PF; pf_idx++) {
		if (!pem->pfs[pf_idx])
			continue;

		err = reset_pf(pem_idx, pf_idx);
		if (err < 0)
			return err;
	}

	return 0;
}

static int nic_reset(int pem, int pf, int vf)
{
	if (pf < 0 && vf < 0)
		return reset_pem(pem);

	if (vf < 0)
		return reset_pf(pem, pf);

	return reset_vf(pem, pf, vf);
}

static int nic_set_port(int pem, int pf, int vf, const struct rte_pci_addr *port)
{
	struct l2fwd_config_fn *fn_cfg;
	struct nic_fn_data *fn;

	if (!nic_data[pem])
		return -EINVAL;

	if (!nic_data[pem]->pfs[pf])
		return -EINVAL;

	if (vf >= 0 && !nic_data[pem]->pfs[pf]->vfs[vf])
		return -EINVAL;

	fn = get_fn_data(pem, pf, vf);
	fn_cfg = (vf < 0) ?
		  &l2fwd_cfg.pems[pem].pfs[pf].d :
		  &l2fwd_cfg.pems[pem].pfs[pf].vfs[vf];

	return init_fn(fn, fn_cfg);
}

static int nic_get_offloads(int pem, int pf, int vf,
			    struct octep_ctrl_net_offloads *offloads)
{
	struct nic_fn_data *fn;

	fn = get_fn_data(pem, pf, vf);
	offloads->rx_offloads = fn->rx_offloads;
	offloads->tx_offloads = fn->tx_offloads;
	offloads->ext_offloads = fn->ext_offloads;

	return 0;
}

static int nic_set_offloads(int pem, int pf, int vf,
			    struct octep_ctrl_net_offloads *offloads)
{
	struct nic_fn_data *fn;

	fn = get_fn_data(pem, pf, vf);
	fn->rx_offloads = offloads->rx_offloads;
	fn->tx_offloads = offloads->tx_offloads;
	fn->ext_offloads = offloads->ext_offloads;

	return 0;
}

static struct control_fn_ops nic_ctrl_ops = {
	.get_mtu = nic_get_mtu,
	.set_mtu = nic_set_mtu,
	.get_mac = nic_get_mac,
	.set_mac = nic_set_mac,
	.get_link_state = nic_get_link_state,
	.set_link_state = nic_set_link_state,
	.get_rx_state = nic_get_link_state,
	.set_rx_state = nic_set_link_state,
	.get_link_info = nic_get_link_info,
	.set_link_info = nic_set_link_info,
	.reset = nic_reset,
	.set_port = nic_set_port,
	.get_offloads = nic_get_offloads,
	.set_offloads = nic_set_offloads,
};

int ctrl_nic_init(struct control_fn_ops **ops)
{
	int err;

	err = init_pems();
	if (err < 0)
		return err;

	*ops = &nic_ctrl_ops;

	return 0;
}

int ctrl_nic_uninit(void)
{
	free_pems();

	return 0;
}
