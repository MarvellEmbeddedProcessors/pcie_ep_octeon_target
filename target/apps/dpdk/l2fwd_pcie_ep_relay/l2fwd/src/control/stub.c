/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#include "compat.h"
#include "l2fwd_main.h"
#include "l2fwd_config.h"
#include "octep_cp_lib.h"
#include "octep_ctrl_net.h"
#include "control.h"

#define RTE_LOGTYPE_L2FWD_CTRL_STUB	RTE_LOGTYPE_USER1

struct stub_fn_data {
	/* device mac address */
	char mac[ETH_ALEN];
	/* mtu */
	int mtu;
	/* link state */
	unsigned short link_state;
	/* rx state */
	unsigned short rx_state;
	/* auto negotiation support */
	int autoneg;
	/* pause mode support */
	int pause_mode;
	/* link speed */
	unsigned int speed;
	/* supported link modes */
	unsigned long long smodes;
	/* advertised link modes */
	unsigned long long amodes;
	/* rx offload flags */
	unsigned short rx_offloads;
	/* tx offload flags */
	unsigned short tx_offloads;
	/* ext offload flags */
	unsigned long long ext_offloads;
};

struct stub_pf {
	/* pf config */
	struct stub_fn_data d;
	/* vf's */
	struct stub_fn_data *vfs[L2FWD_MAX_VF];
};

struct stub_pem {
	/* pf indices */
	struct stub_pf *pfs[L2FWD_MAX_PF];
};

/* runtime data */
static struct stub_pem *stub_data[L2FWD_MAX_PEM];

static inline struct stub_fn_data *get_fn_data(int pem, int pf, int vf)
{
	return (vf >= 0) ?
		stub_data[pem]->pfs[pf]->vfs[vf] :
		&stub_data[pem]->pfs[pf]->d;
}

static int free_pems(void)
{
	struct stub_pem *pem;
	struct stub_pf *pf;
	int i, j, k;

	for (i = 0; i < L2FWD_MAX_PEM; i++) {
		if (!stub_data[i])
			continue;

		pem = stub_data[i];
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
		stub_data[i] = NULL;
	}

	return 0;
}

static int init_fn(struct stub_fn_data *fn, struct l2fwd_config_fn *fn_cfg)
{
	if (!fn_cfg)
		return 0;

	fn->mtu = fn_cfg->mtu;
	fn->autoneg = fn_cfg->autoneg;
	fn->pause_mode = fn_cfg->pause_mode;
	fn->speed = fn_cfg->speed;
	fn->smodes = fn_cfg->smodes;
	fn->amodes = fn_cfg->amodes;

	return 0;
}

static int init_valid_cfg(int pem_idx, int pf_idx, int vf_idx, void *data)
{
	struct l2fwd_config_fn *fn_cfg;
	struct stub_pem *pem;
	struct stub_pf *pf;

	fn_cfg = (vf_idx < 0) ?
		  &l2fwd_cfg.pems[pem_idx].pfs[pf_idx].d :
		  &l2fwd_cfg.pems[pem_idx].pfs[pf_idx].vfs[vf_idx];

	if (!stub_data[pem_idx]) {
		stub_data[pem_idx] = calloc(1, sizeof(struct stub_pem));
		if (!stub_data[pem_idx]) {
			RTE_LOG(ERR, L2FWD_CTRL_STUB,
				"pem data alloc failed [%d].\n",
				pem_idx);
			return -ENOMEM;
		}
	}
	pem = stub_data[pem_idx];
	if (!pem->pfs[pf_idx]) {
		pem->pfs[pf_idx] = calloc(1, sizeof(struct stub_pf));
		if (!pem->pfs[pf_idx]) {
			RTE_LOG(ERR, L2FWD_CTRL_STUB,
				"pf data alloc failed [%d][%d].\n",
				pem_idx, pf_idx);
			return -ENOMEM;
		}
	}
	pf = pem->pfs[pf_idx];
	if (vf_idx < 0)
		return init_fn(&pf->d, fn_cfg);

	if (!pf->vfs[vf_idx]) {
		pf->vfs[vf_idx] = calloc(1, sizeof(struct stub_fn_data));
		if (!pf->vfs[vf_idx]) {
			RTE_LOG(ERR, L2FWD_CTRL_STUB,
				"vf data alloc failed [%d][%d][%d].\n",
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

static int stub_get_mtu(int pem, int pf, int vf, uint16_t *mtu)
{
	struct stub_fn_data *fn;

	fn = get_fn_data(pem, pf, vf);
	*mtu = fn->mtu;

	return 0;
}

static int stub_set_mtu(int pem, int pf, int vf, uint16_t mtu)
{
	struct stub_fn_data *fn;

	fn = get_fn_data(pem, pf, vf);
	fn->mtu = mtu;

	return 0;
}

static int stub_get_mac(int pem, int pf, int vf, uint8_t *addr)
{
	struct stub_fn_data *fn;

	fn = get_fn_data(pem, pf, vf);
	memcpy(addr, &fn->mac, ETH_ALEN);

	return 0;
}

static int stub_set_mac(int pem, int pf, int vf, uint8_t *addr)
{
	struct stub_fn_data *fn;

	fn = get_fn_data(pem, pf, vf);
	memcpy(&fn->mac, addr, ETH_ALEN);

	return 0;
}

static int stub_get_link_state(int pem, int pf, int vf, uint16_t *state)
{
	struct stub_fn_data *fn;

	fn = get_fn_data(pem, pf, vf);
	*state = fn->link_state;

	return 0;
}

static int stub_set_link_state(int pem, int pf, int vf, uint16_t state)
{
	struct stub_fn_data *fn;

	fn = get_fn_data(pem, pf, vf);
	fn->link_state = state;

	return 0;
}

static int stub_get_link_info(int pem, int pf, int vf,
			     struct octep_ctrl_net_link_info *info)
{
	struct stub_fn_data *fn;

	fn = get_fn_data(pem, pf, vf);
	info->supported_modes = fn->smodes;
	info->advertised_modes = fn->amodes;
	info->autoneg = fn->autoneg;
	info->pause = fn->pause_mode;
	info->speed = fn->speed;

	return 0;
}

static int stub_set_link_info(int pem, int pf, int vf,
			     struct octep_ctrl_net_link_info *info)
{
	struct stub_fn_data *fn;

	fn = get_fn_data(pem, pf, vf);
	fn->smodes = info->supported_modes;
	fn->amodes = info->advertised_modes;
	fn->autoneg = info->autoneg;
	fn->pause_mode = info->pause;
	fn->speed = info->speed;

	return 0;
}

static inline int reset_vf(int pem_idx, int pf_idx, int vf_idx)
{
	return init_fn(stub_data[pem_idx]->pfs[pf_idx]->vfs[vf_idx],
		       &l2fwd_cfg.pems[pem_idx].pfs[pf_idx].vfs[vf_idx]);
}

static int reset_pf(int pem_idx, int pf_idx)
{
	struct stub_pf *pf;
	int vf_idx, err;

	pf = stub_data[pem_idx]->pfs[pf_idx];
	err = init_fn(&pf->d, &l2fwd_cfg.pems[pem_idx].pfs[pf_idx].d);
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
	struct stub_pem *pem;
	int pf_idx, err;

	if (!stub_data[pem_idx])
		return -EINVAL;

	pem = stub_data[pem_idx];
	for (pf_idx = 0; pf_idx < L2FWD_MAX_PF; pf_idx++) {
		if (!pem->pfs[pf_idx])
			continue;

		err = reset_pf(pem_idx, pf_idx);
		if (err < 0)
			return err;
	}

	return 0;
}

static int stub_reset(int pem, int pf, int vf)
{
	if (pf < 0 && vf < 0)
		return reset_pem(pem);

	if (vf < 0)
		return reset_pf(pem, pf);

	return reset_vf(pem, pf, vf);
}

static int stub_set_port(int pem, int pf, int vf, const struct rte_pci_addr *port)
{
	if (!stub_data[pem])
		return -EINVAL;

	if (!stub_data[pem]->pfs[pf])
		return -EINVAL;

	if (vf >= 0) {
		if (!stub_data[pem]->pfs[pf]->vfs[vf])
			return -EINVAL;

		return init_fn(stub_data[pem]->pfs[pf]->vfs[vf], NULL);
	}

	return init_fn(&stub_data[pem]->pfs[pf]->d, NULL);
}

static int stub_get_offloads(int pem, int pf, int vf,
			     struct octep_ctrl_net_offloads *offloads)
{
	struct stub_fn_data *fn;

	fn = get_fn_data(pem, pf, vf);
	offloads->rx_offloads = fn->rx_offloads;
	offloads->tx_offloads = fn->tx_offloads;
	offloads->ext_offloads = fn->ext_offloads;

	return 0;
}

static int stub_set_offloads(int pem, int pf, int vf,
			     struct octep_ctrl_net_offloads *offloads)
{
	struct stub_fn_data *fn;

	fn = get_fn_data(pem, pf, vf);
	fn->rx_offloads = offloads->rx_offloads;
	fn->tx_offloads = offloads->tx_offloads;
	fn->ext_offloads = offloads->ext_offloads;

	return 0;
}

static struct control_fn_ops stub_ctrl_ops = {
	.get_mtu = stub_get_mtu,
	.set_mtu = stub_set_mtu,
	.get_mac = stub_get_mac,
	.set_mac = stub_set_mac,
	.get_link_state = stub_get_link_state,
	.set_link_state = stub_set_link_state,
	.get_rx_state = stub_get_link_state,
	.set_rx_state = stub_set_link_state,
	.get_link_info = stub_get_link_info,
	.set_link_info = stub_set_link_info,
	.reset = stub_reset,
	.set_port = stub_set_port,
	.get_offloads = stub_get_offloads,
	.set_offloads = stub_set_offloads,
};

int ctrl_stub_init(struct control_fn_ops **ops)
{
	int err;

	err = init_pems();
	if (err < 0)
		return err;

	*ops = &stub_ctrl_ops;

	return 0;
}

int ctrl_stub_uninit(void)
{
	free_pems();

	return 0;
}
