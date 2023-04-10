/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#include "compat.h"
#include "l2fwd.h"
#include "l2fwd_main.h"
#include "l2fwd_control.h"
#include "l2fwd_config.h"
#include "octep_cp_lib.h"
#include "octep_ctrl_net.h"
#include "control.h"

#define RTE_LOGTYPE_L2FWD_CTRL	RTE_LOGTYPE_USER1

/* Control plane version */
#define CP_VERSION_MAJOR		1
#define CP_VERSION_MINOR		0
#define CP_VERSION_VARIANT		0

#define CP_VERSION_CURRENT		(OCTEP_CP_VERSION(CP_VERSION_MAJOR, \
							  CP_VERSION_MINOR, \
							  CP_VERSION_VARIANT))

/* ctrl-net response sizes */
#define CTRL_NET_RESP_MTU_SZ		sizeof(struct octep_ctrl_net_h2f_resp_cmd_mtu)
#define CTRL_NET_RESP_MAC_SZ		sizeof(struct octep_ctrl_net_h2f_resp_cmd_mac)
#define CTRL_NET_RESP_STATE_SZ		sizeof(struct octep_ctrl_net_h2f_resp_cmd_state)
#define CTRL_NET_RESP_LINK_INFO_SZ	sizeof(struct octep_ctrl_net_link_info)
#define CTRL_NET_RESP_IF_STATS_SZ	sizeof(struct octep_ctrl_net_h2f_resp_cmd_get_stats)
#define CTRL_NET_RESP_INFO_SZ		sizeof(struct octep_ctrl_net_h2f_resp_cmd_get_info)
#define CTRL_NET_RESP_HDR_SZ		sizeof(union octep_ctrl_net_resp_hdr)

#define ALARM_INTERVAL_MSECS			100
#define ALARM_INTERVAL_USECS			(ALARM_INTERVAL_MSECS * 1000)
#define RX_BUF_CNT				6

enum control_state {
	CONTROL_STATE_UNKNOWN,
	CONTROL_STATE_INIT,
	CONTROL_STATE_READY,
	CONTROL_STATE_IN_PEM_RESET,
	CONTROL_STATE_IN_PF_RESET,
	CONTROL_STATE_IN_VF_RESET,
	CONTROL_STATE_MAX
};

/* Runtime vf data */
struct control_vf {
	/* to_host_dbdf */
	struct rte_pci_addr dbdf;
	/* fn ops */
	struct control_fn_ops *ops;
};

/* pcie mac domain pf configuration */
struct control_pf {
	/* to_host_dbdf */
	struct rte_pci_addr dbdf;
	/* mseconds until next heartbeat */
	int msecs_to_next_hbeat;
	/* preset heartbeat info */
	struct octep_cp_event_info hbeat;
	/* preset context for receiving messages */
	union octep_cp_msg_info ctx;
	/* fn ops */
	struct control_fn_ops *ops;
	/* vf's */
	struct control_vf *vfs[L2FWD_MAX_VF];
};

struct control_pem {
	/* pf indices */
	struct control_pf *pfs[L2FWD_MAX_PF];
};

/* port map */
struct port_map {
	/* to_host_dbdf */
	struct rte_pci_addr port1;
	/* to_wire_dbdf */
	struct rte_pci_addr port2;
};

/* runtime data for all pf's and vf's */
static struct control_pem *run_data[L2FWD_MAX_PEM] = { 0 };

/* octep_cp_lib configuration */
static struct octep_cp_lib_cfg cp_lib_cfg = { 0 };

/* Array of buffer's to receive messages, common for all pf's */
static struct octep_cp_msg rx_msg[RX_BUF_CNT];

static int max_msg_sz = sizeof(union octep_ctrl_net_max_data);

/* control fn ops */
static struct control_fn_ops *fn_ops[L2FWD_FN_TYPE_MAX] = { 0 };

/* current operating state */
static enum control_state c_state = CONTROL_STATE_UNKNOWN;

/* l2fwd control ops */
static struct l2fwd_control_ops *l2fwd_ops;

/* template for callback function */
typedef void (*valid_pf_callback_t)(int pem, int pf, void *ctx);

/* template for callback function */
typedef int (*valid_fn_callback_t)(int pem, int pf, int vf, void *ctx);

static void iterate_valid_pfs(valid_pf_callback_t fn, void *ctx)
{
	int i, j;

	for (i = 0; i < L2FWD_MAX_PEM; i++) {
		if (!run_data[i])
			continue;

		for (j = 0; j < L2FWD_MAX_PF; j++) {
			if (run_data[i]->pfs[j])
				fn(i, j, ctx);
		}
	}
}

static int iterate_valid_fn(valid_fn_callback_t fn, void *ctx)
{
	int i, j, k, err;

	for (i = 0; i < L2FWD_MAX_PEM; i++) {
		if (!run_data[i])
			continue;

		for (j = 0; j < L2FWD_MAX_PF; j++) {
			if (!run_data[i]->pfs[j])
				continue;

			err = fn(i, j, -1, ctx);
			if (err)
				return err;
			for (k = 0; k < L2FWD_MAX_VF; k++) {
				if (!run_data[i]->pfs[j]->vfs[k])
					continue;

				err = fn(i, j, k, ctx);
				if (err)
					return err;
			}
		}
	}

	return 0;
}

static int free_pems(void)
{
	struct control_pem *pem;
	struct control_pf *pf;
	int i, j, k;

	for (i = 0; i < L2FWD_MAX_PEM; i++) {
		if (!run_data[i])
			continue;

		pem = run_data[i];
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
		run_data[i] = NULL;
	}

	return 0;
}

static inline int init_vf(int pem_idx, int pf_idx, int vf_idx,
			  struct control_vf *vf, struct l2fwd_config_fn *vf_cfg)
{
	vf->dbdf = vf_cfg->to_host_dbdf;
	if (!L2FWD_FEATURE(l2fwd_user_cfg.features, L2FWD_FEATURE_CTRL_PLANE) ||
	    is_zero_dbdf(&vf_cfg->to_wire_dbdf))
		vf->ops = fn_ops[L2FWD_FN_TYPE_STUB];
	else
		vf->ops = fn_ops[L2FWD_FN_TYPE_NIC];

	vf->ops->set_mac(pem_idx, pf_idx, vf_idx, vf_cfg->mac.addr_bytes);

	return 0;
}

static int init_pf(int pem_idx, int pf_idx, struct control_pf *pf,
		   struct l2fwd_config_pf *pf_cfg)
{
	struct octep_cp_event_info_heartbeat *hbeat;

	pf->dbdf = pf_cfg->d.to_host_dbdf;
	if (!L2FWD_FEATURE(l2fwd_user_cfg.features, L2FWD_FEATURE_CTRL_PLANE) ||
	    is_zero_dbdf(&pf_cfg->d.to_wire_dbdf))
		pf->ops = fn_ops[L2FWD_FN_TYPE_STUB];
	else
		pf->ops = fn_ops[L2FWD_FN_TYPE_NIC];

	pf->ops->set_mac(pem_idx, pf_idx, -1, pf_cfg->d.mac.addr_bytes);

	pf->ctx.s.pem_idx = pem_idx;
	pf->ctx.s.pf_idx = pf_idx;
	pf->msecs_to_next_hbeat = pf_cfg->hb_interval;
	pf->hbeat.e = OCTEP_CP_EVENT_TYPE_HEARTBEAT;

	hbeat = &pf->hbeat.u.hbeat;
	hbeat->dom_idx = pem_idx;
	hbeat->pf_idx = pf_idx;

	return 0;
}

static int init_valid_cfg(int pem_idx, int pf_idx, int vf_idx, void *ctx)
{
	struct control_pem *pem;
	struct control_pf *pf;

	if (!run_data[pem_idx]) {
		run_data[pem_idx] = calloc(1, sizeof(struct control_pem));
		if (!run_data[pem_idx]) {
			RTE_LOG(ERR, L2FWD_CTRL,
				"pem data alloc failed [%d].\n",
				pem_idx);
			return -ENOMEM;
		}
	}
	pem = run_data[pem_idx];
	if (!pem->pfs[pf_idx]) {
		pem->pfs[pf_idx] = calloc(1, sizeof(struct control_pf));
		if (!pem->pfs[pf_idx]) {
			RTE_LOG(ERR, L2FWD_CTRL,
				"pf data alloc failed [%d][%d].\n",
				pem_idx, pf_idx);
			return -ENOMEM;
		}
	}
	pf = pem->pfs[pf_idx];
	if (vf_idx < 0)
		return init_pf(pem_idx, pf_idx, pf,
			       &l2fwd_cfg.pems[pem_idx].pfs[pf_idx]);

	if (!pf->vfs[vf_idx]) {
		pf->vfs[vf_idx] = calloc(1, sizeof(struct control_vf));
		if (!pf->vfs[vf_idx]) {
			RTE_LOG(ERR, L2FWD_CTRL,
				"vf data alloc failed [%d][%d].\n",
				pem_idx, pf_idx);
			return -ENOMEM;
		}
	}

	return init_vf(pem_idx, pf_idx, vf_idx, pf->vfs[vf_idx],
		       &l2fwd_cfg.pems[pem_idx].pfs[pf_idx].vfs[vf_idx]);
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

static int update_config_for_soc(int pem_idx, int pf_idx, int vf_idx, void *ctx)
{
	struct octep_cp_lib_info *info = (struct octep_cp_lib_info *)ctx;
	struct l2fwd_config_fn *fn;

	fn = (vf_idx < 0) ? &l2fwd_cfg.pems[pem_idx].pfs[pf_idx].d :
			    &l2fwd_cfg.pems[pem_idx].pfs[pf_idx].vfs[vf_idx];
	fn->mtu = (info->soc_model.flag &
		   (OCTEP_CP_SOC_MODEL_CN96xx_Ax |
		    OCTEP_CP_SOC_MODEL_CNF95xxN_A0 |
		    OCTEP_CP_SOC_MODEL_CNF95xxO_A0)) ?
		   (16 * 1024) : ((64 * 1024) - 1);

	return 0;
}

/* Initialize octep_cp_lib */
static int init_octep_cp_lib(void)
{
	struct octep_cp_lib_info info;
	int dom, pf, i, j, err;

	dom = 0;
	/* gather pf indices for all valid pf's */
	for (i = 0; i < OCTEP_CP_DOM_MAX; i++) {
		pf = 0;
		for (j = 0; j < OCTEP_CP_PF_PER_DOM_MAX; j++) {
			if (l2fwd_cfg.pems[i].pfs[j].d.is_valid)
				cp_lib_cfg.doms[dom].pfs[pf++].idx = j;
		}
		if (pf) {
			cp_lib_cfg.doms[dom].idx = i;
			cp_lib_cfg.doms[dom++].npfs = pf;
			cp_lib_cfg.ndoms++;
		}
	}
	cp_lib_cfg.min_version = CP_VERSION_CURRENT;
	cp_lib_cfg.max_version = CP_VERSION_CURRENT;
	err =  octep_cp_lib_init(&cp_lib_cfg);
	if (err < 0)
		return err;

	/* for now only support single buffer messages */
	for (i = 0; i < cp_lib_cfg.ndoms; i++) {
		for (j = 0; j < cp_lib_cfg.doms[i].npfs; j++) {
			if (cp_lib_cfg.doms[i].pfs[j].max_msg_sz < max_msg_sz) {
				octep_cp_lib_uninit();
				return -EINVAL;
			}
		}
	}

	err = octep_cp_lib_get_info(&info);
	if (err < 0) {
		octep_cp_lib_uninit();
		return -EINVAL;
	}

	err = for_each_valid_config_fn(&update_config_for_soc, &info);
	if (err < 0) {
		free_pems();
		return err;
	}

	return 0;
}

static int uninit_rx_buf(void)
{
	struct octep_cp_msg *msg;
	int i;

	for (i = 0; i < RX_BUF_CNT; i++) {
		msg = &rx_msg[i];
		if (msg->sg_list[0].msg)
			free(msg->sg_list[0].msg);
		msg->sg_list[0].sz = 0;
		msg->sg_num = 0;
	}
	return -ENOMEM;
}

/* Allocate rx buffers for polling */
static int init_rx_buf(void)
{
	struct octep_cp_msg *msg;
	int i;

	for (i = 0; i < RX_BUF_CNT; i++) {
		msg = &rx_msg[i];
		msg->info.s.sz = max_msg_sz;
		msg->sg_num = 1;
		msg->sg_list[0].sz = max_msg_sz;
		msg->sg_list[0].msg = calloc(1, max_msg_sz);
		if (!msg->sg_list[0].msg)
			goto mem_alloc_fail;
	}

	return 0;

mem_alloc_fail:
	uninit_rx_buf();
	return -ENOMEM;
}

static inline void valid_pf_set_fw_ready(int pem, int pf, void *ctx)
{
	struct octep_cp_event_info info = {
		.e = OCTEP_CP_EVENT_TYPE_FW_READY,
		.u.fw_ready.dom_idx = pem,
		.u.fw_ready.pf_idx = pf,
		.u.fw_ready.ready = *((int *)ctx)
	};

	octep_cp_lib_send_event(&info);
}

static int set_fw_ready(int ready)
{
	iterate_valid_pfs(&valid_pf_set_fw_ready, &ready);
	return 0;
}

int l2fwd_control_init(struct l2fwd_control_ops *ops)
{
	int err;

	if (c_state >= CONTROL_STATE_INIT)
		return -EAGAIN;

	c_state = CONTROL_STATE_INIT;
	err = init_octep_cp_lib();
	if (err < 0)
		return err;

	/* Initialize each type of op */
	err = ctrl_stub_init(&fn_ops[L2FWD_FN_TYPE_STUB]);
	if (err < 0)
		goto ctrl_stub_init_fail;

	err = ctrl_nic_init(&fn_ops[L2FWD_FN_TYPE_NIC]);
	if (err < 0)
		goto ctrl_nic_init_fail;

	err = init_pems();
	if (err < 0)
		goto pem_init_fail;

	err = init_rx_buf();
	if (err < 0)
		goto init_rx_buf_fail;

	err = set_fw_ready(1);
	if (err < 0)
		goto set_fw_ready_fail;

	ualarm(ALARM_INTERVAL_USECS, 0);

	c_state = CONTROL_STATE_READY;
	l2fwd_ops = ops;
	RTE_LOG(INFO, L2FWD_CTRL,
		"Using single buffer with msg sz %u.\n", max_msg_sz);

	return 0;

set_fw_ready_fail:
	uninit_rx_buf();
init_rx_buf_fail:
pem_init_fail:
	free_pems();
	ctrl_nic_uninit();
ctrl_nic_init_fail:
	ctrl_stub_uninit();
ctrl_stub_init_fail:
	octep_cp_lib_uninit();
	c_state = CONTROL_STATE_UNKNOWN;
	return err;
}

static inline void valid_pf_send_hbeat(int pem_idx, int pf_idx, void *ctx)
{
	struct control_pf *pf;

	if (c_state != CONTROL_STATE_READY)
		return;

	pf = run_data[pem_idx]->pfs[pf_idx];
	if (pf->msecs_to_next_hbeat <= 0) {
		octep_cp_lib_send_event(&pf->hbeat);
		pf->msecs_to_next_hbeat = l2fwd_cfg.pems[pem_idx].pfs[pf_idx].hb_interval;
	}
	pf->msecs_to_next_hbeat -= ALARM_INTERVAL_MSECS;
}

int l2fwd_control_handle_alarm(void)
{
	iterate_valid_pfs(&valid_pf_send_hbeat, NULL);
	ualarm(ALARM_INTERVAL_USECS, 0);

	return 0;
}

static inline struct control_fn_ops *get_fn_ops(union octep_cp_msg_info *info)
{
	int pem = info->s.pem_idx;
	int pf = info->s.pf_idx;
	int vf;

	if (!run_data[pem])
		return NULL;

	if (!run_data[pem]->pfs[pf])
		return NULL;

	if (info->s.is_vf) {
		vf = info->s.vf_idx;
		if (run_data[pem]->pfs[pf]->vfs[vf])
			return run_data[pem]->pfs[pf]->vfs[vf]->ops;

		return NULL;
	}

	return run_data[pem]->pfs[pf]->ops;
}

static inline int send_response(union octep_cp_msg_info *pf_ctx,
				union octep_cp_msg_info *msg_ctx,
				struct octep_ctrl_net_h2f_resp *resp,
				int resp_sz)
{
	struct octep_cp_msg resp_msg;
	int err, msg_sz;

	/* copy over sender context (pem/pf/vf) */
	msg_sz = CTRL_NET_RESP_HDR_SZ + resp_sz;
	resp_msg.sg_num = 1;
	resp_msg.info = *msg_ctx;
	resp_msg.info.s.sz = msg_sz;
	resp_msg.sg_list[0].sz = msg_sz;
	resp_msg.sg_list[0].msg = resp;

	err = octep_cp_lib_send_msg_resp(pf_ctx, &resp_msg, 1);

	return (err == 1) ? 0 : err;
}

static int process_mtu(union octep_cp_msg_info *info,
		       struct octep_ctrl_net_h2f_req *req,
		       struct octep_ctrl_net_h2f_resp *resp,
		       struct control_fn_ops *ops)
{
	uint16_t mtu;
	int err, vf_idx;

	vf_idx = (info->s.is_vf) ? info->s.vf_idx : -1;
	if (req->mtu.cmd == OCTEP_CTRL_NET_CMD_GET) {
		err = ops->get_mtu(info->s.pem_idx,
				   info->s.pf_idx,
				   vf_idx,
				   &mtu);
		if (err < 0) {
			RTE_LOG(ERR, L2FWD_CTRL,
				"[%d][%d][%d]: Get mtu failed %d\n",
				info->s.pem_idx, info->s.pf_idx, vf_idx, err);
			resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_GENERIC_FAIL;
			return 0;
		}
		resp->mtu.val = mtu;
		RTE_LOG(DEBUG, L2FWD_CTRL,
			"[%d][%d][%d]: Get mtu %u\n",
			info->s.pem_idx, info->s.pf_idx, vf_idx, resp->mtu.val);
		resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;
		return CTRL_NET_RESP_MTU_SZ;
	}

	err = ops->set_mtu(info->s.pem_idx,
			   info->s.pf_idx,
			   vf_idx,
			   req->mtu.val);
	if (err < 0) {
		RTE_LOG(ERR, L2FWD_CTRL,
			"[%d][%d][%d]: Set mtu failed %d\n",
			info->s.pem_idx, info->s.pf_idx, vf_idx, err);
		resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_GENERIC_FAIL;
		return 0;
	}
	RTE_LOG(DEBUG, L2FWD_CTRL, "[%d][%d][%d]: Set mtu %u\n",
		info->s.pem_idx, info->s.pf_idx, vf_idx, req->mtu.val);
	resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;

	return 0;
}

static int process_mac(union octep_cp_msg_info *info,
		       struct octep_ctrl_net_h2f_req *req,
		       struct octep_ctrl_net_h2f_resp *resp,
		       struct control_fn_ops *ops)
{
	int err, vf_idx;

	vf_idx = (info->s.is_vf) ? info->s.vf_idx : -1;
	if (req->mac.cmd == OCTEP_CTRL_NET_CMD_GET) {
		err = ops->get_mac(info->s.pem_idx,
				   info->s.pf_idx,
				   vf_idx,
				   (uint8_t *)&resp->mac.addr);
		if (err < 0) {
			RTE_LOG(ERR, L2FWD_CTRL,
				"[%d][%d][%d]: Get mac failed %d\n",
				info->s.pem_idx, info->s.pf_idx, vf_idx, err);
			resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_GENERIC_FAIL;
			return 0;
		}

		RTE_LOG(DEBUG, L2FWD_CTRL,
			"[%d][%d][%d]: Get mac "L2FWD_PCIE_EP_ETHER_ADDR_PRT_FMT"\n",
			info->s.pem_idx, info->s.pf_idx, vf_idx,
			resp->mac.addr[0], resp->mac.addr[1],
			resp->mac.addr[2], resp->mac.addr[3],
			resp->mac.addr[4], resp->mac.addr[5]);
		resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;
		return CTRL_NET_RESP_MAC_SZ;
	}

	err = ops->set_mac(info->s.pem_idx,
			   info->s.pf_idx,
			   (info->s.is_vf) ? info->s.vf_idx : -1,
			   (uint8_t *)&req->mac.addr);
	if (err < 0) {
		RTE_LOG(ERR, L2FWD_CTRL,
			"[%d][%d][%d]: Set mac failed %d\n",
			info->s.pem_idx, info->s.pf_idx, vf_idx, err);
		resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_GENERIC_FAIL;
		return 0;
	}
	RTE_LOG(DEBUG, L2FWD_CTRL,
		"[%d][%d][%d]: Set mac "L2FWD_PCIE_EP_ETHER_ADDR_PRT_FMT"\n",
		info->s.pem_idx, info->s.pf_idx, vf_idx,
		req->mac.addr[0], req->mac.addr[1], req->mac.addr[2],
		req->mac.addr[3], req->mac.addr[4], req->mac.addr[5]);
	resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;

	return 0;
}

static int process_get_if_stats(union octep_cp_msg_info *info,
				struct octep_ctrl_net_h2f_req *req,
				struct octep_ctrl_net_h2f_resp *resp,
				struct control_fn_ops *ops)
{
	resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;
	RTE_LOG(DEBUG, L2FWD_CTRL,
		"[%d][%d][%d]: Get if stats\n",
		info->s.pem_idx, info->s.pf_idx,
		(info->s.is_vf) ? info->s.vf_idx : -1);

	return CTRL_NET_RESP_IF_STATS_SZ;
}

static int process_link_status(union octep_cp_msg_info *info,
			       struct octep_ctrl_net_h2f_req *req,
			       struct octep_ctrl_net_h2f_resp *resp,
			       struct control_fn_ops *ops)
{
	uint16_t state;
	int err, vf_idx;

	vf_idx = (info->s.is_vf) ? info->s.vf_idx : -1;
	if (req->link.cmd == OCTEP_CTRL_NET_CMD_GET) {
		err = ops->get_link_state(info->s.pem_idx,
					  info->s.pf_idx,
					  vf_idx,
					  &state);
		if (err < 0) {
			RTE_LOG(ERR, L2FWD_CTRL,
				"[%d][%d][%d]: Get link state failed %d\n",
				info->s.pem_idx, info->s.pf_idx, vf_idx, err);
			resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_GENERIC_FAIL;
			return 0;
		}
		resp->link.state = state;
		RTE_LOG(DEBUG, L2FWD_CTRL,
			"[%d][%d][%d]: Get link state %u\n",
			info->s.pem_idx, info->s.pf_idx, vf_idx, resp->link.state);
		resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;
		return CTRL_NET_RESP_STATE_SZ;
	}

	err = ops->set_link_state(info->s.pem_idx,
				  info->s.pf_idx,
				  vf_idx,
				  req->link.state);
	if (err < 0) {
		RTE_LOG(ERR, L2FWD_CTRL,
			"[%d][%d][%d]: Set link state failed %d\n",
			info->s.pem_idx, info->s.pf_idx, vf_idx, err);
		resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_GENERIC_FAIL;
		return 0;
	}
	RTE_LOG(DEBUG, L2FWD_CTRL,
		"[%d][%d][%d]: Set link state %u\n",
		info->s.pem_idx, info->s.pf_idx, vf_idx, req->link.state);
	resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;

	return 0;
}

static int process_rx_state(union octep_cp_msg_info *info,
			    struct octep_ctrl_net_h2f_req *req,
			    struct octep_ctrl_net_h2f_resp *resp,
			    struct control_fn_ops *ops)
{
	uint16_t state;
	int err, vf_idx;

	vf_idx = (info->s.is_vf) ? info->s.vf_idx : -1;
	if (req->rx.cmd == OCTEP_CTRL_NET_CMD_GET) {
		err = ops->get_rx_state(info->s.pem_idx,
					info->s.pf_idx,
					vf_idx,
					&state);
		if (err < 0) {
			RTE_LOG(ERR, L2FWD_CTRL,
				"[%d][%d][%d]: Get rx state failed %d\n",
				info->s.pem_idx, info->s.pf_idx, vf_idx, err);
			resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_GENERIC_FAIL;
			return 0;
		}
		resp->link.state = state;
		RTE_LOG(DEBUG, L2FWD_CTRL,
			"[%d][%d][%d]: Get rx state %u\n",
			info->s.pem_idx, info->s.pf_idx, vf_idx, resp->rx.state);
		resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;
		return CTRL_NET_RESP_STATE_SZ;
	}

	err = ops->set_rx_state(info->s.pem_idx,
				info->s.pf_idx,
				vf_idx,
				req->rx.state);
	if (err < 0) {
		RTE_LOG(ERR, L2FWD_CTRL,
			"[%d][%d][%d]: Set rx state failed %d\n",
			info->s.pem_idx, info->s.pf_idx, vf_idx, err);
		resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_GENERIC_FAIL;
		return 0;
	}
	RTE_LOG(DEBUG, L2FWD_CTRL,
		"[%d][%d][%d]: Set rx state %u\n",
		info->s.pem_idx, info->s.pf_idx, vf_idx, req->rx.state);
	resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;

	return 0;
}

static int process_link_info(union octep_cp_msg_info *info,
			     struct octep_ctrl_net_h2f_req *req,
			     struct octep_ctrl_net_h2f_resp *resp,
			     struct control_fn_ops *ops)
{
	struct octep_ctrl_net_link_info li;
	int err, vf_idx;

	vf_idx = (info->s.is_vf) ? info->s.vf_idx : -1;
	if (req->link_info.cmd == OCTEP_CTRL_NET_CMD_GET) {
		err = ops->get_link_info(info->s.pem_idx,
					 info->s.pf_idx,
					 vf_idx,
					 &li);
		if (err < 0) {
			RTE_LOG(ERR, L2FWD_CTRL,
				"[%d][%d][%d]: Get link info failed %d\n",
				info->s.pem_idx, info->s.pf_idx, vf_idx, err);
			resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_GENERIC_FAIL;
			return 0;
		}
		resp->link_info = li;
		RTE_LOG(DEBUG, L2FWD_CTRL,
			"[%d][%d][%d]: Get link info "
			"am:0x%lx sm:0x%lx a:0x%x p:0x%x s:0x%x\n",
			info->s.pem_idx, info->s.pf_idx, vf_idx,
			resp->link_info.advertised_modes,
			resp->link_info.supported_modes,
			resp->link_info.autoneg,
			resp->link_info.pause,
			resp->link_info.speed);
		resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;
		return CTRL_NET_RESP_LINK_INFO_SZ;
	}

	li = req->link_info.info;
	err = ops->set_link_info(info->s.pem_idx,
				 info->s.pf_idx,
				 vf_idx,
				 &li);
	if (err < 0) {
		RTE_LOG(ERR, L2FWD_CTRL,
			"[%d][%d][%d]: Set link info failed %d\n",
			info->s.pem_idx, info->s.pf_idx, vf_idx, err);
		resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_GENERIC_FAIL;
		return 0;
	}
	RTE_LOG(DEBUG, L2FWD_CTRL,
		"[%d][%d][%d]: Set link info "
		"am:0x%lx sm:0x%lx a:0x%x p:0x%x s:0x%x\n",
		info->s.pem_idx, info->s.pf_idx, vf_idx,
		li.advertised_modes, li.supported_modes,
		li.autoneg, li.pause, li.speed);
	resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;

	return 0;
}

static int process_get_info(union octep_cp_msg_info *info,
			    struct octep_ctrl_net_h2f_req *req,
			    struct octep_ctrl_net_h2f_resp *resp,
			    struct control_fn_ops *ops)
{
	struct l2fwd_config_pf *pf_cfg;
	struct l2fwd_config_fn *fn_cfg;
	int vf_idx;

	vf_idx = (info->s.is_vf) ? info->s.vf_idx : -1;
	pf_cfg = &l2fwd_cfg.pems[info->s.pem_idx].pfs[info->s.pf_idx];
	fn_cfg = (vf_idx > 0) ? &pf_cfg->vfs[vf_idx] : &pf_cfg->d;

	resp->info.fw_info.pkind = fn_cfg->pkind;
	if (vf_idx > 0) {
		resp->info.fw_info.hb_interval = 0;
		resp->info.fw_info.hb_miss_count = 0;
	} else {
		resp->info.fw_info.hb_interval = pf_cfg->hb_interval;
		resp->info.fw_info.hb_miss_count = pf_cfg->hb_miss_count;
	}

	resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;
	RTE_LOG(DEBUG, L2FWD_CTRL,
		"[%d][%d][%d]: Get info pkind:%x hbi:%u hbmiss:%u\n",
		info->s.pem_idx, info->s.pf_idx, vf_idx,
		resp->info.fw_info.pkind,
		resp->info.fw_info.hb_interval,
		resp->info.fw_info.hb_miss_count);

	return CTRL_NET_RESP_INFO_SZ;
}

static int process_msg(union octep_cp_msg_info *ctx, struct octep_cp_msg *msg)
{
	union octep_cp_msg_info *info = &msg->info;
	struct octep_ctrl_net_h2f_resp resp;
	struct octep_ctrl_net_h2f_req *req;
	struct control_fn_ops *ops;
	int resp_sz = 0;

	/* Copy correct header in response */
	req = (struct octep_ctrl_net_h2f_req *)msg->sg_list[0].msg;
	resp.hdr.words[0] = req->hdr.words[0];
	ops = get_fn_ops(info);
	if (!ops) {
		RTE_LOG(DEBUG, L2FWD_CTRL,
			"[%d][%d][%d]: Request for invalid interface\n",
			info->s.pem_idx, info->s.pf_idx,
			(info->s.is_vf) ? info->s.vf_idx : -1);
		resp.hdr.s.reply = OCTEP_CTRL_NET_REPLY_INVALID_PARAM;
		send_response(ctx, info, &resp, 0);
		return 0;
	}
	switch (req->hdr.s.cmd) {
	case OCTEP_CTRL_NET_H2F_CMD_MTU:
		resp_sz += process_mtu(info, req, &resp, ops);
		break;
	case OCTEP_CTRL_NET_H2F_CMD_MAC:
		resp_sz += process_mac(info, req, &resp, ops);
		break;
	case OCTEP_CTRL_NET_H2F_CMD_GET_IF_STATS:
		resp_sz += process_get_if_stats(info, req, &resp, ops);
		break;
	case OCTEP_CTRL_NET_H2F_CMD_LINK_STATUS:
		resp_sz += process_link_status(info, req, &resp, ops);
		break;
	case OCTEP_CTRL_NET_H2F_CMD_RX_STATE:
		resp_sz += process_rx_state(info, req, &resp, ops);
		break;
	case OCTEP_CTRL_NET_H2F_CMD_LINK_INFO:
		resp_sz += process_link_info(info, req, &resp, ops);
		break;
	case OCTEP_CTRL_NET_H2F_CMD_GET_INFO:
		resp_sz += process_get_info(info, req, &resp, ops);
		break;
	default:
		RTE_LOG(ERR, L2FWD_CTRL,
			"[%d][%d][%d]: Unhandled Cmd : %u\n",
			info->s.pem_idx, info->s.pf_idx,
			(info->s.is_vf) ? info->s.vf_idx : -1,
			req->hdr.s.cmd);
		resp.hdr.s.reply = OCTEP_CTRL_NET_REPLY_INVALID_PARAM;
		break;
	}
	send_response(ctx, info, &resp, resp_sz);

	return 0;
}

static void valid_pf_process_msg(int pem, int pf, void *ctx)
{
	union octep_cp_msg_info *msg_ctx;
	int ret, m;

	if (c_state != CONTROL_STATE_READY)
		return;

	msg_ctx = &run_data[pem]->pfs[pf]->ctx;
	ret = octep_cp_lib_recv_msg(msg_ctx, rx_msg, RX_BUF_CNT);
	if (ret < 0)
		return;

	for (m = 0; m < ret; m++) {
		process_msg(msg_ctx, &rx_msg[m]);
		/* library will overwrite msg size in header so reset it */
		rx_msg[ret].info.s.sz = max_msg_sz;
	}
}

static void valid_pf_process_perst(int pem, int pf, void *ctx)
{
	int *perst_pem = (int *)ctx;

	if (*perst_pem != pem)
		return;

	l2fwd_ops->on_before_pf_reset(pem, pf);
	run_data[pem]->pfs[pf]->ops->reset(pem, pf, -1);
	init_pf(pem, pf, run_data[pem]->pfs[pf], &l2fwd_cfg.pems[pem].pfs[pf]);
	l2fwd_ops->on_after_pf_reset(pem, pf);
}

int l2fwd_control_poll(void)
{
	struct octep_cp_event_info info[L2FWD_MAX_PEM];
	int ret, e, pem_idx;

	/* poll each pf for up to RX_BUF_CNT followed by events */
	iterate_valid_pfs(&valid_pf_process_msg, NULL);

	if (c_state != CONTROL_STATE_READY)
		return -EAGAIN;

	ret = octep_cp_lib_recv_event(info, L2FWD_MAX_PEM);
	for (e = 0; e < ret; e++) {
		if (info[e].e == OCTEP_CP_EVENT_TYPE_PERST) {
			pem_idx = info[e].u.perst.dom_idx;

			RTE_LOG(INFO, L2FWD_CTRL,
				"[%d]: PERST event\n",
				pem_idx);

			c_state = CONTROL_STATE_IN_PEM_RESET;
			l2fwd_ops->on_before_pem_reset(pem_idx);
			iterate_valid_pfs(&valid_pf_process_perst, &pem_idx);
			l2fwd_ops->on_after_pem_reset(pem_idx);
			c_state = CONTROL_STATE_READY;

		}
	}

	return 0;
}

static int clear_valid_fn_mapping(int pem, int pf, int vf, void *ctx)
{
	if (vf < 0) {
		run_data[pem]->pfs[pf]->ops->set_port(pem, pf, vf, &zero_dbdf);
		run_data[pem]->pfs[pf]->ops = fn_ops[L2FWD_FN_TYPE_STUB];
		return 0;
	}

	run_data[pem]->pfs[pf]->vfs[vf]->ops->set_port(pem, pf, vf, &zero_dbdf);
	run_data[pem]->pfs[pf]->vfs[vf]->ops = fn_ops[L2FWD_FN_TYPE_STUB];

	return 0;
}

int l2fwd_control_clear_port_mapping(void)
{
	iterate_valid_fn(clear_valid_fn_mapping, NULL);

	return 0;
}

static int set_valid_fn_mapping(int pem, int pf, int vf, void *ctx)
{
	struct port_map *pm = (struct port_map *)ctx;
	struct control_fn_ops **ops;
	uint8_t mac[ETH_ALEN];

	if (vf < 0) {
		if (rte_pci_addr_cmp(&run_data[pem]->pfs[pf]->dbdf,
				     &pm->port1))
			return 0;

		ops = &run_data[pem]->pfs[pf]->ops;
	} else {
		if (rte_pci_addr_cmp(&run_data[pem]->pfs[pf]->vfs[vf]->dbdf,
				     &pm->port1))
			return 0;

		ops = &run_data[pem]->pfs[pf]->vfs[vf]->ops;
	}

	(*ops)->get_mac(pem, pf, vf, mac);
	clear_valid_fn_mapping(pem, pf, vf, NULL);
	if (!rte_pci_addr_cmp(&zero_dbdf, &pm->port2))
		*ops = fn_ops[L2FWD_FN_TYPE_STUB];
	else
		*ops = fn_ops[L2FWD_FN_TYPE_NIC];

	(*ops)->set_port(pem, pf, vf, &pm->port2);
	(*ops)->set_mac(pem, pf, vf, mac);

	return -EIO;
}

int l2fwd_control_set_port_mapping(const struct rte_pci_addr *port1,
				   const struct rte_pci_addr *port2)
{
	struct port_map pm = {
		.port1 = *port1,
		.port2 = *port2
	};
	int err;

	err = iterate_valid_fn(set_valid_fn_mapping, &pm);
	if (err)
		return 0;

	return -ENODEV;
}

int l2fwd_control_init_fn(int pem, int pf, int vf)
{
	if (vf < 0) {
		if (!l2fwd_cfg.pems[pem].pfs[pf].d.is_valid)
			return -EINVAL;
	} else {
		if (!l2fwd_cfg.pems[pem].pfs[pf].vfs[vf].is_valid)
			return -EINVAL;
	}

	return init_valid_cfg(pem, pf, vf, NULL);
}

int l2fwd_control_uninit(void)
{
	set_fw_ready(0);
	octep_cp_lib_uninit();
	ctrl_nic_uninit();
	ctrl_stub_uninit();
	free_pems();
	uninit_rx_buf();

	return 0;
}
