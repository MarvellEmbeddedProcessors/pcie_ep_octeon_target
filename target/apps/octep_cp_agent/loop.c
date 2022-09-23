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

static uint64_t rx_ctx[LOOP_RX_BUF_CNT];
static struct octep_cp_msg rx_msg[LOOP_RX_BUF_CNT];
static int rx_num = LOOP_RX_BUF_CNT;
static int max_msg_sz = sizeof(union octep_ctrl_net_max_data);

extern struct octep_cp_lib_cfg cp_lib_cfg;

static const uint32_t resp_hdr_sz = sizeof(union octep_ctrl_net_resp_hdr);
static const uint32_t mtu_sz = sizeof(struct octep_ctrl_net_h2f_resp_cmd_mtu);
static const uint32_t mac_sz = sizeof(struct octep_ctrl_net_h2f_resp_cmd_mac);
static const uint32_t state_sz = sizeof(struct octep_ctrl_net_h2f_resp_cmd_state);
static const uint32_t link_info_sz = sizeof(struct octep_ctrl_net_link_info);

int loop_init()
{
	int i, j;
	struct octep_cp_msg *msg;

	printf("Loop: Init\n");
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

	printf("Loop: using single buffer with msg sz %u.\n", max_msg_sz);

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

static void copy_if_stats(struct if_stats *iface, uint32_t offset)
{
	struct if_stats dummy;
	uint64_t *addr = (uint64_t *)&dummy; //(mbox.barmem + offset);

	cp_write64(iface->rx_stats.pkts, addr++);
	cp_write64(iface->rx_stats.octets, addr++);
	cp_write64(iface->rx_stats.pause_pkts, addr++);
	cp_write64(iface->rx_stats.pause_octets, addr++);
	cp_write64(iface->rx_stats.dmac0_pkts, addr++);
	cp_write64(iface->rx_stats.dmac0_octets, addr++);
	cp_write64(iface->rx_stats.dropped_pkts_fifo_full, addr++);
	cp_write64(iface->rx_stats.dropped_octets_fifo_full, addr++);
	cp_write64(iface->rx_stats.err_pkts, addr++);
	cp_write64(iface->rx_stats.dmac1_pkts, addr++);
	cp_write64(iface->rx_stats.dmac1_octets, addr++);
	cp_write64(iface->rx_stats.ncsi_dropped_pkts, addr++);
	cp_write64(iface->rx_stats.ncsi_dropped_octets, addr++);

	cp_write64(iface->tx_stats.xscol, addr++);
	cp_write64(iface->tx_stats.xsdef, addr++);
	cp_write64(iface->tx_stats.mcol, addr++);
	cp_write64(iface->tx_stats.scol, addr++);
	cp_write64(iface->tx_stats.octs, addr++);
	cp_write64(iface->tx_stats.pkts, addr++);
	cp_write64(iface->tx_stats.hist_lt64, addr++);
	cp_write64(iface->tx_stats.hist_eq64, addr++);
	cp_write64(iface->tx_stats.hist_65to127, addr++);
	cp_write64(iface->tx_stats.hist_128to255, addr++);
	cp_write64(iface->tx_stats.hist_256to511, addr++);
	cp_write64(iface->tx_stats.hist_512to1023, addr++);
	cp_write64(iface->tx_stats.hist_1024to1518, addr++);
	cp_write64(iface->tx_stats.hist_gt1518, addr++);
	cp_write64(iface->tx_stats.bcst, addr++);
	cp_write64(iface->tx_stats.mcst, addr++);
	cp_write64(iface->tx_stats.undflw, addr++);
	cp_write64(iface->tx_stats.ctl, addr);
}

static int process_mtu(struct if_cfg *iface,
		       struct octep_ctrl_net_h2f_req *req,
		       struct octep_ctrl_net_h2f_resp *resp)
{
	int ret = 0;

	if (req->mtu.cmd == OCTEP_CTRL_NET_CMD_GET) {
		resp->mtu.val = iface->mtu;
		printf("Cmd: get mtu : %u\n", resp->mtu.val);
		ret = mtu_sz;
	}
	else {
		iface->mtu = req->mtu.val;
		printf("Cmd: set mtu : %u\n", req->mtu.val);
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
		printf("Cmd: get mac : %02x:%02x:%02x:%02x:%02x:%02x\n",
			   resp->mac.addr[0],
			   resp->mac.addr[1],
			   resp->mac.addr[2],
			   resp->mac.addr[3],
			   resp->mac.addr[4],
			   resp->mac.addr[5]);
	}
	else {
		memcpy(&iface->mac_addr, &req->mac.addr, ETH_ALEN);
		printf("Cmd: set mac : %02x:%02x:%02x:%02x:%02x:%02x\n",
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

static int process_get_if_stats(struct if_stats *iface,
				struct octep_ctrl_net_h2f_req *req,
				struct octep_ctrl_net_h2f_resp *resp)
{
	if (req->get_stats.offset) {
		copy_if_stats(iface, req->get_stats.offset);
		resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;
		printf("Cmd: get if stats : %x\n", req->get_stats.offset);
	} else {
		resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_INVALID_PARAM;
		printf("Cmd: Invalid get if stats : %x\n",
		       req->get_stats.offset);
	}

	return 0;
}

static int process_link_status(struct if_cfg *iface,
			       struct octep_ctrl_net_h2f_req *req,
			       struct octep_ctrl_net_h2f_resp *resp)
{
	int ret = 0;

	if (req->link.cmd == OCTEP_CTRL_NET_CMD_GET) {
		resp->link.state = iface->link_state;
		ret = state_sz;
		printf("Cmd: get link state : %u\n", resp->link.state);
	}
	else {
		iface->link_state = req->link.state;
		printf("Cmd: set link state : %u\n", req->link.state);
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
		printf("Cmd: get rx state : %u\n", resp->rx.state);
	}
	else {
		iface->rx_state = req->rx.state;
		printf("Cmd: set rx state : %u\n", req->rx.state);
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
		printf("Cmd: get link info\n");
	}
	else {
		iface->advertised_modes = req->link_info.info.advertised_modes;
		iface->autoneg = req->link_info.info.autoneg;
		iface->pause_mode = req->link_info.info.pause;
		iface->speed = req->link_info.info.speed;
		printf("Cmd: set link info: am:%lx a:%x p:%x s:%x\n",
			req->link_info.info.advertised_modes,
			req->link_info.info.autoneg,
			req->link_info.info.pause,
			req->link_info.info.speed);
	}
	resp->hdr.s.reply = OCTEP_CTRL_NET_REPLY_OK;

	return ret;
}

static int process_msg(uint64_t ctx, struct octep_cp_msg* msg)
{
	struct octep_ctrl_net_h2f_req *req;
	struct octep_ctrl_net_h2f_resp resp = { 0 };
	struct octep_cp_msg resp_msg;
	struct if_cfg *iface;
	struct if_stats *ifdata;
	int err, resp_sz;

	err = app_config_get_if_from_msg_info(&msg->info,
					      &iface,
					      &ifdata);
	if (err) {
		printf("Invalid msg[%lx]\n", msg->info.words[0]);
		return err;
	}
	req = (struct octep_ctrl_net_h2f_req *)msg->sg_list[0].msg;
	resp.hdr.words[0] = req->hdr.words[0];
	iface->host_if_id = req->hdr.s.sender;
	resp_sz = resp_hdr_sz;
	switch (req->hdr.s.cmd) {
		case OCTEP_CTRL_NET_H2F_CMD_MTU:
			resp_sz += process_mtu(iface, req, &resp);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_MAC:
			resp_sz += process_mac(iface, req, &resp);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_GET_IF_STATS:
			resp_sz += process_get_if_stats(ifdata, req, &resp);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_LINK_STATUS:
			resp_sz += process_link_status(iface, req, &resp);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_RX_STATE:
			resp_sz += process_rx_state(iface, req, &resp);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_LINK_INFO:
			resp_sz += process_link_info(iface, req, &resp);
			break;
		default:
			printf("Unhandled Cmd : %u\n", req->hdr.s.cmd);
			resp_sz = 0;
			break;
	}

	if (resp_sz >= resp_hdr_sz) {
		resp_msg.info = msg->info;
		resp_msg.info.s.sz = resp_sz;
		resp_msg.sg_num = 1;
		resp_msg.sg_list[0].sz = resp_sz;
		resp_msg.sg_list[0].msg = &resp;
		octep_cp_lib_send_msg_resp((uint64_t *)&ctx,
					   (struct octep_cp_msg *)&resp_msg,
					   1);
	}

	return 0;
}

int loop_process_msgs()
{
	struct octep_cp_msg* msg;
	int ret, i;

	ret = octep_cp_lib_recv_msg(rx_ctx,
				    rx_msg,
				    rx_num);
	for (i=0; i<ret; i++) {
		msg = &rx_msg[i];
		process_msg(rx_ctx[i], msg);
		/* library will overwrite msg size in header so reset it */
		msg->info.s.sz = max_msg_sz;
	}

	return 0;
}

int loop_uninit()
{
	int i;

	printf("%s\n", __func__);

	for (i=0 ;i<rx_num; i++) {
		if (rx_msg[i].sg_list[0].msg)
			free(rx_msg[i].sg_list[0].msg);
		rx_msg[i].sg_list[0].sz = 0;
	}

	return 0;
}
