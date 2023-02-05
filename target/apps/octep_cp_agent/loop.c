/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "octep_cp_lib.h"
#include "cp_compat.h"
#include "octep_ctrl_net.h"
#include "octep_hw.h"
#include "loop.h"
#include "app_config.h"

#define LOOP_RX_BUF_CNT			6

static struct octep_cp_msg rx_msg[LOOP_RX_BUF_CNT];
static int rx_num = LOOP_RX_BUF_CNT;
static int max_msg_sz = sizeof(union octep_ctrl_net_max_data);
static struct app_cfg loop_cfg = { 0 };

extern struct octep_cp_lib_cfg cp_lib_cfg;

static const uint32_t resp_hdr_sz = sizeof(union octep_ctrl_net_resp_hdr);
static const uint32_t mtu_sz = sizeof(struct octep_ctrl_net_h2f_resp_cmd_mtu);
static const uint32_t mac_sz = sizeof(struct octep_ctrl_net_h2f_resp_cmd_mac);
static const uint32_t state_sz = sizeof(struct octep_ctrl_net_h2f_resp_cmd_state);
static const uint32_t link_info_sz = sizeof(struct octep_ctrl_net_link_info);
static const uint32_t if_stats_sz = sizeof(struct octep_ctrl_net_h2f_resp_cmd_get_stats);
static const uint32_t info_sz = sizeof(struct octep_ctrl_net_h2f_resp_cmd_get_info);

int loop_init()
{
	int i, j;
	struct octep_cp_msg *msg;

	printf("APP: Loop Init\n");
	/* for now only support single buffer messages */
	for (i=0; i<cp_lib_cfg.ndoms; i++) {
		for (j=0; j<cp_lib_cfg.doms[i].npfs; j++) {
			if (cp_lib_cfg.doms[i].pfs[j].max_msg_sz < max_msg_sz)
				return -EINVAL;
		}
	}

	for (i=0; i<rx_num; i++) {
		msg = &rx_msg[i];
		msg->info.s.sz = max_msg_sz;
		msg->sg_num = 1;
		msg->sg_list[0].sz = max_msg_sz;
		msg->sg_list[0].msg = calloc(1, max_msg_sz);
		if (!msg->sg_list[0].msg)
			goto mem_alloc_fail;
	}

	/* copy over global config into local runtime config */
	memcpy(&loop_cfg, &cfg, sizeof(struct app_cfg));

	printf("APP: using single buffer with msg sz %u.\n", max_msg_sz);

	return 0;

mem_alloc_fail:
	for (i=0 ;i<LOOP_RX_BUF_CNT; i++) {
		msg = &rx_msg[i];
		if (msg->sg_list[0].msg)
			free(msg->sg_list[0].msg);
		msg->sg_list[0].sz = 0;
		msg->sg_num = 0;
	}

	return -ENOMEM;
}

static int process_mtu(struct if_cfg *iface,
		       struct octep_ctrl_net_h2f_req *req,
		       struct octep_ctrl_net_h2f_resp *resp)
{
	int ret = 0;

	if (req->mtu.cmd == OCTEP_CTRL_NET_CMD_GET) {
		resp->mtu.val = iface->max_rx_pktlen;

		printf("APP: Cmd: get mtu : %u\n", resp->mtu.val);
		ret = mtu_sz;
	}
	else {
		iface->mtu = req->mtu.val;
		printf("APP: Cmd: set mtu : %u\n", req->mtu.val);
	}
	resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;

	return ret;
}

static int process_mac(struct if_cfg *iface,
		       struct octep_ctrl_net_h2f_req *req,
		       struct octep_ctrl_net_h2f_resp *resp)
{
	int ret = 0;;

	if (req->mac.cmd == OCTEP_CTRL_NET_CMD_GET) {
		memcpy(&resp->mac.addr, &iface->mac_addr, ETH_ALEN);
		ret = mac_sz;
		printf("APP: Cmd: get mac : %02x:%02x:%02x:%02x:%02x:%02x\n",
			   resp->mac.addr[0],
			   resp->mac.addr[1],
			   resp->mac.addr[2],
			   resp->mac.addr[3],
			   resp->mac.addr[4],
			   resp->mac.addr[5]);
	}
	else {
		memcpy(&iface->mac_addr, &req->mac.addr, ETH_ALEN);
		printf("APP: Cmd: set mac : %02x:%02x:%02x:%02x:%02x:%02x\n",
			   req->mac.addr[0],
			   req->mac.addr[1],
			   req->mac.addr[2],
			   req->mac.addr[3],
			   req->mac.addr[4],
			   req->mac.addr[5]);
	}
	resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;

	return ret;
}

static int process_get_if_stats(struct if_stats *ifstats,
				struct octep_ctrl_net_h2f_req *req,
				struct octep_ctrl_net_h2f_resp *resp)
{
	/* struct if_stats = struct octep_ctrl_net_h2f_resp_cmd_get_stats */
	memcpy(&resp->if_stats, ifstats, if_stats_sz);
	resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;
	printf("APP: Cmd: get if stats\n");

	return if_stats_sz;
}

static int process_link_status(struct if_cfg *iface,
			       struct octep_ctrl_net_h2f_req *req,
			       struct octep_ctrl_net_h2f_resp *resp)
{
	int ret = 0;

	if (req->link.cmd == OCTEP_CTRL_NET_CMD_GET) {
		resp->link.state = iface->link_state;
		ret = state_sz;
		printf("APP: Cmd: get link state : %u\n", resp->link.state);
	}
	else {
		iface->link_state = req->link.state;
		printf("APP: Cmd: set link state : %u\n", req->link.state);
	}
	resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;

	return ret;
}

static int process_rx_state(struct if_cfg *iface,
			    struct octep_ctrl_net_h2f_req *req,
			    struct octep_ctrl_net_h2f_resp *resp)
{
	int ret = 0;

	if (req->rx.cmd == OCTEP_CTRL_NET_CMD_GET) {
		resp->rx.state = iface->rx_state;
		ret = state_sz;
		printf("APP: Cmd: get rx state : %u\n", resp->rx.state);
	}
	else {
		iface->rx_state = req->rx.state;
		printf("APP: Cmd: set rx state : %u\n", req->rx.state);
	}
	resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;

	return ret;
}

static int process_link_info(struct if_cfg *iface,
			     struct octep_ctrl_net_h2f_req *req,
			     struct octep_ctrl_net_h2f_resp *resp)
{
	int ret;

	if (req->link_info.cmd == OCTEP_CTRL_NET_CMD_GET) {
		resp->link_info.supported_modes = iface->supported_modes;
		resp->link_info.advertised_modes = iface->advertised_modes;
		resp->link_info.autoneg = iface->autoneg;
		resp->link_info.pause = iface->pause_mode;
		resp->link_info.speed = iface->speed;
		ret = link_info_sz;
		printf("APP: Cmd: get link info\n");
	}
	else {
		iface->advertised_modes = req->link_info.info.advertised_modes;
		iface->autoneg = req->link_info.info.autoneg;
		iface->pause_mode = req->link_info.info.pause;
		iface->speed = req->link_info.info.speed;
		printf("APP: Cmd: set link info: am:%lx a:%x p:%x s:%x\n",
			req->link_info.info.advertised_modes,
			req->link_info.info.autoneg,
			req->link_info.info.pause,
			req->link_info.info.speed);
	}
	resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;

	return ret;
}

static int process_get_info(struct octep_fw_info *info,
			    struct octep_ctrl_net_h2f_req *req,
			    struct octep_ctrl_net_h2f_resp *resp)
{
	memcpy(&resp->info.fw_info, info, sizeof(struct octep_fw_info));
	printf("APP: Cmd: get info\n");
	resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;

	return info_sz;
}

static int process_dev_remove(union octep_cp_msg_info *fn_ctx,
			      struct fn_cfg *fn,
			      struct octep_ctrl_net_h2f_resp *resp)
{
	union octep_cp_msg_info vf_ctx;
	struct fn_cfg *orig_fn;
	struct pf_cfg *pf;
	int i;

	printf("\nCmd: device remove\n");

	orig_fn = app_config_get_fn(&cfg, fn_ctx);
	memcpy(fn, orig_fn, sizeof(struct fn_cfg));
	if (fn_ctx->s.is_vf)
		goto ret;

	/* Initialize data for dependent vf's */
	pf = &loop_cfg.pems[fn_ctx->s.pem_idx].pfs[fn_ctx->s.pf_idx];
	memcpy(&vf_ctx, fn_ctx, sizeof(*fn_ctx));
	vf_ctx.s.is_vf = 1;
	for (i = 0; i < pf->nvf; i++) {
		vf_ctx.s.vf_idx = i;
		fn = app_config_get_fn(&loop_cfg, &vf_ctx);
		orig_fn = app_config_get_fn(&cfg, &vf_ctx);
		memcpy(fn, orig_fn, sizeof(struct fn_cfg));
	}

ret:
	resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;
	return 0;

}

static int process_msg(union octep_cp_msg_info *ctx, struct octep_cp_msg* msg)
{
	struct octep_ctrl_net_h2f_req *req;
	struct octep_ctrl_net_h2f_resp resp = { 0 };
	struct octep_cp_msg resp_msg;
	struct fn_cfg *fn;
	int err, resp_sz;

	fn = app_config_get_fn(&loop_cfg, &msg->info);
	if (!fn) {
		printf("APP: Invalid msg[%lx]\n", msg->info.words[0]);
		return err;
	}

	req = (struct octep_ctrl_net_h2f_req *)msg->sg_list[0].msg;
	resp.hdr.words[0] = req->hdr.words[0];
	fn->iface.host_if_id = req->hdr.s.sender;
	resp_sz = resp_hdr_sz;
	switch (req->hdr.s.cmd) {
		case OCTEP_CTRL_NET_H2F_CMD_MTU:
			resp_sz += process_mtu(&fn->iface, req, &resp);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_MAC:
			resp_sz += process_mac(&fn->iface, req, &resp);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_GET_IF_STATS:
			resp_sz += process_get_if_stats(&fn->ifstats, req, &resp);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_LINK_STATUS:
			resp_sz += process_link_status(&fn->iface, req, &resp);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_RX_STATE:
			resp_sz += process_rx_state(&fn->iface, req, &resp);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_LINK_INFO:
			resp_sz += process_link_info(&fn->iface, req, &resp);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_GET_INFO:
			resp_sz += process_get_info(&fn->info, req, &resp);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_DEV_REMOVE:
			resp_sz += process_dev_remove(&msg->info, fn, &resp);
			break;
		default:
			printf("APP: Unhandled Cmd : %u\n", req->hdr.s.cmd);
			resp_sz = 0;
			break;
	}

	if (resp_sz >= resp_hdr_sz) {
		resp_msg.info = msg->info;
		resp_msg.info.s.sz = resp_sz;
		resp_msg.sg_num = 1;
		resp_msg.sg_list[0].sz = resp_sz;
		resp_msg.sg_list[0].msg = &resp;
		octep_cp_lib_send_msg_resp(ctx, &resp_msg, 1);
		fn->ifstats.tx_stats.pkts++;
		fn->ifstats.tx_stats.octs += resp_sz;
	}

	fn->ifstats.rx_stats.pkts++;
	fn->ifstats.rx_stats.octets += msg->info.s.sz;

	return 0;
}

int loop_process_msgs()
{
	union octep_cp_msg_info ctx;
	struct octep_cp_msg* msg;
	int ret, i, j, m;

	for (i = 0; i < cp_lib_cfg.ndoms; i++) {
		ctx.s.pem_idx = cp_lib_cfg.doms[i].idx;
		for (j = 0; j < cp_lib_cfg.doms[i].npfs; j++) {
			ctx.s.pf_idx = cp_lib_cfg.doms[i].pfs[j].idx;
			ret = octep_cp_lib_recv_msg(&ctx, rx_msg, rx_num);
			for (m = 0; m < ret; m++) {
				msg = &rx_msg[m];
				process_msg(&ctx, msg);
				/* library will overwrite msg size in header so reset it */
				msg->info.s.sz = max_msg_sz;
			}
		}
	}

	return 0;
}

int loop_uninit()
{
	int i;

	for (i=0 ;i<rx_num; i++) {
		if (rx_msg[i].sg_list[0].msg)
			free(rx_msg[i].sg_list[0].msg);
		rx_msg[i].sg_list[0].sz = 0;
	}

	return 0;
}
