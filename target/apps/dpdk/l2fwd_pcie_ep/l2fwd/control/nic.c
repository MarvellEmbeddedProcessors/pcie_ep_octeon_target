#include "compat.h"
#include "l2fwd_config.h"
#include "octep_cp_lib.h"
#include "octep_ctrl_net.h"
#include "control.h"

#define RTE_LOGTYPE_L2FWD_CTRL_NIC	RTE_LOGTYPE_USER1

struct nic_fn_data {
	unsigned int portid;
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

static int init_fn(int pem, int pf, int vf, struct nic_fn_data *fn,
		   struct fn_config *fn_cfg)
{

	fn->portid = find_rte_port(&fn_cfg->to_wire_dbdf);
	if (fn->portid == RTE_MAX_ETHPORTS) {
		RTE_LOG(ERR, L2FWD_CTRL_NIC,
			"[%d][%d][%d]: Device not found ["PCI_PRI_FMT"]\n",
			pem, pf, vf,
			fn_cfg->to_wire_dbdf.domain,
			fn_cfg->to_wire_dbdf.bus,
			fn_cfg->to_wire_dbdf.devid,
			fn_cfg->to_wire_dbdf.function);
		return -EIO;
	}

	rte_eth_dev_default_mac_addr_set(fn->portid, &fn_cfg->mac);

	return 0;
}

static int init_valid_cfg(int pem_idx, int pf_idx, int vf_idx, void *data)
{
	struct fn_config *fn_cfg;
	struct nic_pem *pem;
	struct nic_pf *pf;

	fn_cfg = (vf_idx < 0) ?
		  &pem_cfg[pem_idx].pfs[pf_idx].d :
		  &pem_cfg[pem_idx].pfs[pf_idx].vfs[vf_idx];

	if (is_zero_dbdf(&fn_cfg->to_wire_dbdf))
		return 0;

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
		return init_fn(pem_idx, pf_idx, vf_idx, &pf->d, fn_cfg);

	if (!pf->vfs[vf_idx]) {
		pf->vfs[vf_idx] = calloc(1, sizeof(struct nic_fn_data));
		if (!pf->vfs[vf_idx]) {
			RTE_LOG(ERR, L2FWD_CTRL_NIC,
				"[%d][%d][%d]: vf data alloc failed\n",
				pem_idx, pf_idx, vf_idx);
			return -ENOMEM;
		}
	}

	return init_fn(pem_idx, pf_idx, vf_idx, pf->vfs[vf_idx], fn_cfg);
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

	fn = (vf >= 0) ?
	      nic_data[pem]->pfs[pf]->vfs[vf] :
	      &nic_data[pem]->pfs[pf]->d;

	return rte_eth_dev_get_mtu(fn->portid, mtu);
}

static int nic_set_mtu(int pem, int pf, int vf, uint16_t mtu)
{
	struct nic_fn_data *fn;

	fn = (vf >= 0) ?
	      nic_data[pem]->pfs[pf]->vfs[vf] :
	      &nic_data[pem]->pfs[pf]->d;

	return rte_eth_dev_set_mtu(fn->portid, mtu);
}

static int nic_get_mac(int pem, int pf, int vf, uint8_t *addr)
{
	struct nic_fn_data *fn;
	struct rte_ether_addr mac;
	int err;

	fn = (vf >= 0) ?
	      nic_data[pem]->pfs[pf]->vfs[vf] :
	      &nic_data[pem]->pfs[pf]->d;

	err = rte_eth_macaddr_get(fn->portid, &mac);
	if (!err)
		memcpy(addr, &mac.addr_bytes, ETH_ALEN);

	return err;
}

static int nic_set_mac(int pem, int pf, int vf, uint8_t *addr)
{
	struct rte_ether_addr mac;
	struct nic_fn_data *fn;

	fn = (vf >= 0) ?
	      nic_data[pem]->pfs[pf]->vfs[vf] :
	      &nic_data[pem]->pfs[pf]->d;

	memcpy(&mac.addr_bytes, addr, ETH_ALEN);

	return rte_eth_dev_default_mac_addr_set(fn->portid, &mac);
}

static int nic_get_link_state(int pem, int pf, int vf, uint16_t *state)
{
	struct rte_eth_link link;
	struct nic_fn_data *fn;
	int err;

	fn = (vf >= 0) ?
	      nic_data[pem]->pfs[pf]->vfs[vf] :
	      &nic_data[pem]->pfs[pf]->d;

	err = rte_eth_link_get_nowait(fn->portid, &link);
	if (!err)
		*state = link.link_status;

	return err;
}

static int nic_set_link_state(int pem, int pf, int vf, uint16_t state)
{
	struct nic_fn_data *fn;

	fn = (vf >= 0) ?
	      nic_data[pem]->pfs[pf]->vfs[vf] :
	      &nic_data[pem]->pfs[pf]->d;

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

	fn = (vf >= 0) ?
	      nic_data[pem]->pfs[pf]->vfs[vf] :
	      &nic_data[pem]->pfs[pf]->d;

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
	return init_fn(pem_idx, pf_idx, vf_idx,
		       nic_data[pem_idx]->pfs[pf_idx]->vfs[vf_idx],
		       &pem_cfg[pem_idx].pfs[pf_idx].vfs[vf_idx]);
}

static int reset_pf(int pem_idx, int pf_idx)
{
	struct nic_pf *pf;
	int vf_idx, err;

	pf = nic_data[pem_idx]->pfs[pf_idx];
	err = init_fn(pem_idx, pf_idx, -1, &pf->d,
		      &pem_cfg[pem_idx].pfs[pf_idx].d);
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
	.reset = nic_reset
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
