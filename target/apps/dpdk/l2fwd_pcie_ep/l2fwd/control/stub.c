#include "compat.h"
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

static int init_fn(struct stub_fn_data *fn, struct fn_config *fn_cfg)
{
	memcpy(&fn->mac, &fn_cfg->mac.addr_bytes, ETH_ALEN);
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
	struct fn_config *fn_cfg;
	struct stub_pem *pem;
	struct stub_pf *pf;

	fn_cfg = (vf_idx < 0) ?
		  &pem_cfg[pem_idx].pfs[pf_idx].d :
		  &pem_cfg[pem_idx].pfs[pf_idx].vfs[vf_idx];

	if (!is_zero_dbdf(&fn_cfg->to_wire_dbdf))
		return 0;

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
	if (vf >= 0)
		*mtu = stub_data[pem]->pfs[pf]->vfs[vf]->mtu;
	else
		*mtu = stub_data[pem]->pfs[pf]->d.mtu;

	return 0;
}

static int stub_set_mtu(int pem, int pf, int vf, uint16_t mtu)
{
	if (vf >= 0)
		stub_data[pem]->pfs[pf]->vfs[vf]->mtu = mtu;
	else
		stub_data[pem]->pfs[pf]->d.mtu = mtu;

	return 0;
}

static int stub_get_mac(int pem, int pf, int vf, uint8_t *addr)
{
	if (vf >= 0)
		memcpy(addr, &stub_data[pem]->pfs[pf]->vfs[vf]->mac, ETH_ALEN);
	else
		memcpy(addr, &stub_data[pem]->pfs[pf]->d.mac, ETH_ALEN);

	return 0;
}

static int stub_set_mac(int pem, int pf, int vf, uint8_t *addr)
{
	if (vf >= 0)
		memcpy(&stub_data[pem]->pfs[pf]->vfs[vf]->mac, addr, ETH_ALEN);
	else
		memcpy(&stub_data[pem]->pfs[pf]->d.mac, addr, ETH_ALEN);

	return 0;
}

static int stub_get_link_state(int pem, int pf, int vf, uint16_t *state)
{
	if (vf >= 0)
		*state = stub_data[pem]->pfs[pf]->vfs[vf]->link_state;
	else
		*state = stub_data[pem]->pfs[pf]->d.link_state;

	return 0;
}

static int stub_set_link_state(int pem, int pf, int vf, uint16_t state)
{
	if (vf >= 0)
		stub_data[pem]->pfs[pf]->vfs[vf]->link_state = state;
	else
		stub_data[pem]->pfs[pf]->d.link_state = state;

	return 0;
}

static int stub_get_link_info(int pem, int pf, int vf,
			     struct octep_ctrl_net_link_info *info)
{
	struct stub_fn_data *fn;

	fn = (vf >= 0) ?
	      stub_data[pem]->pfs[pf]->vfs[vf] :
	      &stub_data[pem]->pfs[pf]->d;

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

	fn = (vf >= 0) ?
	      stub_data[pem]->pfs[pf]->vfs[vf] :
	      &stub_data[pem]->pfs[pf]->d;

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
		       &pem_cfg[pem_idx].pfs[pf_idx].vfs[vf_idx]);
}

static int reset_pf(int pem_idx, int pf_idx)
{
	struct stub_pf *pf;
	int vf_idx, err;

	pf = stub_data[pem_idx]->pfs[pf_idx];
	err = init_fn(&pf->d, &pem_cfg[pem_idx].pfs[pf_idx].d);
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
	.reset = stub_reset
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