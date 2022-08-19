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

#define CNXK_LOOP_INVALID_HOST_IF_ID		0xffff

int loop_init(char *cfg_file_path)
{
	int err;

	printf("%s\n", __func__);

	if (!cfg_file_path)
		return -EINVAL;

	err = app_config_init(cfg_file_path);
	if (err)
		return err;

	return 0;
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
		       void *msg)
{
	struct octep_ctrl_net_h2f_resp *resp;
	int resp_szw;

	resp = (struct octep_ctrl_net_h2f_resp *)msg;
	if (req->mtu.cmd == OCTEP_CTRL_NET_CMD_GET) {
		resp->mtu.val = iface->mtu;
		resp_szw = OCTEP_CTRL_NET_H2F_GET_MTU_RESP_SZW;
		printf("Cmd: get mtu : %u\n", resp->mtu.val);
	}
	else {
		printf("Cmd: set mtu : %u\n", req->mtu.val);
		iface->mtu = req->mtu.val;
		resp_szw = OCTEP_CTRL_NET_H2F_SET_MTU_RESP_SZW;
	}
	resp->hdr.reply = OCTEP_CTRL_NET_REPLY_OK;

	return resp_szw;
}

static int process_mac(struct if_cfg *iface,
		       struct octep_ctrl_net_h2f_req *req,
		       void *msg)
{
	struct octep_ctrl_net_h2f_resp *resp;
	int resp_szw;

	resp = (struct octep_ctrl_net_h2f_resp *)msg;
	if (req->mac.cmd == OCTEP_CTRL_NET_CMD_GET) {
		memcpy(&resp->mac.addr, &iface->mac_addr, ETH_ALEN);
		resp_szw = OCTEP_CTRL_NET_H2F_GET_MAC_RESP_SZW;
		printf("Cmd: get mac : %02x:%02x:%02x:%02x:%02x:%02x\n",
			   resp->mac.addr[0],
			   resp->mac.addr[1],
			   resp->mac.addr[2],
			   resp->mac.addr[3],
			   resp->mac.addr[4],
			   resp->mac.addr[5]);
	}
	else {
		printf("Cmd: set mac : %02x:%02x:%02x:%02x:%02x:%02x\n",
			   req->mac.addr[0],
			   req->mac.addr[1],
			   req->mac.addr[2],
			   req->mac.addr[3],
			   req->mac.addr[4],
			   req->mac.addr[5]);
		memcpy(&iface->mac_addr, &req->mac.addr, ETH_ALEN);
		resp_szw = OCTEP_CTRL_NET_H2F_SET_MAC_RESP_SZW;
	}
	resp->hdr.reply = OCTEP_CTRL_NET_REPLY_OK;

	return resp_szw;
}

static int process_get_if_stats(struct if_stats *iface,
				struct octep_ctrl_net_h2f_req *req,
				void *msg)
{
	struct octep_ctrl_net_h2f_resp *resp;

	resp = (struct octep_ctrl_net_h2f_resp *)msg;
	if (req->get_stats.offset) {
		printf("Cmd: get if stats : %x\n", req->get_stats.offset);
		copy_if_stats(iface, req->get_stats.offset);
		resp->hdr.reply = OCTEP_CTRL_NET_REPLY_OK;
	} else {
		printf("Cmd: Invalid get if stats : %x\n", req->get_stats.offset);
		resp->hdr.reply = OCTEP_CTRL_NET_REPLY_INVALID_PARAM;
	}

	return 0;
}

static int process_link_status(struct if_cfg *iface,
			       struct octep_ctrl_net_h2f_req *req,
			       void *msg)
{
	struct octep_ctrl_net_h2f_resp *resp;
	int resp_szw;

	resp = (struct octep_ctrl_net_h2f_resp *)msg;
	if (req->link.cmd == OCTEP_CTRL_NET_CMD_GET) {
		resp->link.state = iface->link_state;
		resp_szw = OCTEP_CTRL_NET_H2F_GET_STATE_RESP_SZW;
		printf("Cmd: get link state : %u\n", resp->link.state);
	}
	else {
		printf("Cmd: set link state : %u\n", req->link.state);
		iface->link_state = req->link.state;
		resp_szw = OCTEP_CTRL_NET_H2F_SET_STATE_RESP_SZW;
	}
	resp->hdr.reply = OCTEP_CTRL_NET_REPLY_OK;

	return resp_szw;
}

static int process_rx_state(struct if_cfg *iface,
			    struct octep_ctrl_net_h2f_req *req,
			    void *msg)
{
	struct octep_ctrl_net_h2f_resp *resp;
	int resp_szw;

	resp = (struct octep_ctrl_net_h2f_resp *)msg;
	if (req->rx.cmd == OCTEP_CTRL_NET_CMD_GET) {
		resp->rx.state = iface->rx_state;
		resp_szw = OCTEP_CTRL_NET_H2F_GET_STATE_RESP_SZW;
		printf("Cmd: get rx state : %u\n", resp->rx.state);
	}
	else {
		printf("Cmd: set rx state : %u\n", req->rx.state);
		iface->rx_state = req->rx.state;
		resp_szw = OCTEP_CTRL_NET_H2F_SET_STATE_RESP_SZW;
	}
	resp->hdr.reply = OCTEP_CTRL_NET_REPLY_OK;

	return resp_szw;
}

static int process_link_info(struct if_cfg *iface,
			     struct octep_ctrl_net_h2f_req *req,
			     void *msg)
{
	struct octep_ctrl_net_h2f_resp *resp;
	int resp_szw;

	resp = (struct octep_ctrl_net_h2f_resp *)msg;
	if (req->link_info.cmd == OCTEP_CTRL_NET_CMD_GET) {
		resp->link_info.supported_modes = iface->supported_modes;
		resp->link_info.advertised_modes = iface->advertised_modes;
		resp->link_info.autoneg = iface->autoneg;
		resp->link_info.pause = iface->pause_mode;
		resp->link_info.speed = iface->speed;
		resp_szw = OCTEP_CTRL_NET_H2F_GET_LINK_INFO_RESP_SZW;
		printf("Cmd: get link info\n");
	}
	else {
		printf("Cmd: set link info: am:%lx a:%x p:%x s:%x\n",
			req->link_info.info.advertised_modes,
			req->link_info.info.autoneg,
			req->link_info.info.pause,
			req->link_info.info.speed);
		iface->advertised_modes = req->link_info.info.advertised_modes;
		iface->autoneg = req->link_info.info.autoneg;
		iface->pause_mode = req->link_info.info.pause;
		iface->speed = req->link_info.info.speed;
		resp_szw = OCTEP_CTRL_NET_H2F_SET_LINK_INFO_RESP_SZW;
	}
	resp->hdr.reply = OCTEP_CTRL_NET_REPLY_OK;

	return resp_szw;
}

static inline int get_iface(struct pf_cfg *pf, int idx,
			    struct if_cfg **iface, struct if_stats **if_data)
{
	struct vf_cfg *vf;

	if (idx == 0) {
		*iface = &pf->iface;
		*if_data = &pf->ifstats;
		return 0;
	} else {
		vf = pf->vfs;
		while (vf) {
			if (vf->idx == (idx - 1)) {
				*iface = &vf->iface;
				*if_data = &vf->ifstats;
				return 0;
			}
			vf = vf->next;
		}
	}

	return -ENAVAIL;
}

int loop_process_msg(void *user_ctx, void *msg)
{
	struct octep_ctrl_net_h2f_req *req;
	struct if_cfg *iface;
	struct if_stats *ifdata;
	int err, resp_szw = 0;

	req = (struct octep_ctrl_net_h2f_req *)msg;
	err = get_iface(&app_cfg.pems[0].pfs[0],
			req->hdr.receiver,
			&iface,
			&ifdata);
	if (err) {
		printf("Invalid receiver: %u\n", req->hdr.receiver);
		return err;
	}

	iface->host_if_id = req->hdr.sender;
	switch (req->hdr.cmd) {
		case OCTEP_CTRL_NET_H2F_CMD_MTU:
			resp_szw = process_mtu(iface, req, msg);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_MAC:
			resp_szw = process_mac(iface, req, msg);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_GET_IF_STATS:
			resp_szw = process_get_if_stats(ifdata, req, msg);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_LINK_STATUS:
			resp_szw = process_link_status(iface, req, msg);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_RX_STATE:
			resp_szw = process_rx_state(iface, req, msg);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_LINK_INFO:
			resp_szw = process_link_info(iface, req, msg);
			break;
		default:
			printf("Unhandled Cmd : %u\n", req->hdr.cmd);
			break;
	}

	return resp_szw;
}

int loop_uninit()
{
	printf("%s\n", __func__);
	app_config_uninit();

	return 0;
}
