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
#include "cp_log.h"
#include "cp_lib.h"
#include "octep_ctrl_mbox.h"
#include "octep_ctrl_net.h"
#include "cnxk.h"
#include "cnxk_loop.h"
#include "cnxk_hw.h"

#define CNXK_LOOP_INVALID_HOST_IF_ID		0xffff

struct if_data {
	struct octep_iface_rx_stats rx_stats;
	struct octep_iface_tx_stats tx_stats;
};

/* loop mode polling state */
enum loop_poll_state {
	LOOP_POLL_STATE_INVALID,
	LOOP_POLL_STATE_BEGIN,
	LOOP_POLL_STATE_END,
};

static enum loop_poll_state lp_state = LOOP_POLL_STATE_INVALID;
static volatile int dummy_perst = 0;

static void copy_if_stats(struct if_data *iface, uint32_t offset)
{
	uint64_t *addr = (uint64_t *)(mbox.barmem + offset);

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

static inline void process_mtu(struct cp_lib_if *iface,
			       struct octep_ctrl_net_h2f_req *req,
			       struct octep_ctrl_mbox_msg *msg)
{
	struct octep_ctrl_net_h2f_resp *resp;

	resp = (struct octep_ctrl_net_h2f_resp *)msg->msg;
	if (req->mtu.cmd == OCTEP_CTRL_NET_CMD_GET) {
		resp->mtu.val = iface->mtu;
		msg->hdr.sizew = OCTEP_CTRL_NET_H2F_GET_MTU_RESP_SZW;
		CP_LIB_LOG(INFO, LOOP, "Cmd: get mtu : %u\n", resp->mtu.val);
	}
	else {
		CP_LIB_LOG(INFO, LOOP, "Cmd: set mtu : %u\n", req->mtu.val);
		iface->mtu = req->mtu.val;
		msg->hdr.sizew = OCTEP_CTRL_NET_H2F_SET_MTU_RESP_SZW;
	}
	msg->hdr.flags = OCTEP_CTRL_MBOX_MSG_HDR_FLAG_RESP;
	resp->hdr.reply = OCTEP_CTRL_NET_REPLY_OK;
}

static inline void process_mac(struct cp_lib_if *iface,
			       struct octep_ctrl_net_h2f_req *req,
			       struct octep_ctrl_mbox_msg *msg)
{
	struct octep_ctrl_net_h2f_resp *resp;

	resp = (struct octep_ctrl_net_h2f_resp *)msg->msg;
	if (req->mac.cmd == OCTEP_CTRL_NET_CMD_GET) {
		memcpy(&resp->mac.addr, &iface->mac_addr, ETH_ALEN);
		msg->hdr.sizew = OCTEP_CTRL_NET_H2F_GET_MAC_RESP_SZW;
		CP_LIB_LOG(INFO, LOOP,
			   "Cmd: get mac : %02x:%02x:%02x:%02x:%02x:%02x\n",
			   resp->mac.addr[0],
			   resp->mac.addr[1],
			   resp->mac.addr[2],
			   resp->mac.addr[3],
			   resp->mac.addr[4],
			   resp->mac.addr[5]);
	}
	else {
		CP_LIB_LOG(INFO, LOOP,
			   "Cmd: set mac : %02x:%02x:%02x:%02x:%02x:%02x\n",
			   req->mac.addr[0],
			   req->mac.addr[1],
			   req->mac.addr[2],
			   req->mac.addr[3],
			   req->mac.addr[4],
			   req->mac.addr[5]);
		memcpy(&iface->mac_addr, &req->mac.addr, ETH_ALEN);
		msg->hdr.sizew = OCTEP_CTRL_NET_H2F_SET_MAC_RESP_SZW;
	}
	msg->hdr.flags = OCTEP_CTRL_MBOX_MSG_HDR_FLAG_RESP;
	resp->hdr.reply = OCTEP_CTRL_NET_REPLY_OK;
}

static inline void process_get_if_stats(struct if_data *iface,
					struct octep_ctrl_net_h2f_req *req,
					struct octep_ctrl_mbox_msg *msg)
{
	struct octep_ctrl_net_h2f_resp *resp;

	resp = (struct octep_ctrl_net_h2f_resp *)msg->msg;
	if (req->get_stats.offset) {
		CP_LIB_LOG(INFO, LOOP, "Cmd: get if stats : %x\n",
			   req->get_stats.offset);
		copy_if_stats(iface, req->get_stats.offset);
		resp->hdr.reply = OCTEP_CTRL_NET_REPLY_OK;
	} else {
		CP_LIB_LOG(INFO, LOOP, "Cmd: Invalid get if stats : %x\n",
			   req->get_stats.offset);
		resp->hdr.reply = OCTEP_CTRL_NET_REPLY_INVALID_PARAM;
	}
	msg->hdr.flags = OCTEP_CTRL_MBOX_MSG_HDR_FLAG_RESP;
}

static inline void process_link_status(struct cp_lib_if *iface,
				       struct octep_ctrl_net_h2f_req *req,
				       struct octep_ctrl_mbox_msg *msg)
{
	struct octep_ctrl_net_h2f_resp *resp;

	resp = (struct octep_ctrl_net_h2f_resp *)msg->msg;
	if (req->link.cmd == OCTEP_CTRL_NET_CMD_GET) {
		resp->link.state = iface->link_state;
		msg->hdr.sizew = OCTEP_CTRL_NET_H2F_GET_STATE_RESP_SZW;
		CP_LIB_LOG(INFO, LOOP, "Cmd: get link state : %u\n",
			   resp->link.state);
	}
	else {
		CP_LIB_LOG(INFO, LOOP, "Cmd: set link state : %u\n",
			   req->link.state);
		iface->link_state = req->link.state;
		msg->hdr.sizew = OCTEP_CTRL_NET_H2F_SET_STATE_RESP_SZW;
	}
	msg->hdr.flags = OCTEP_CTRL_MBOX_MSG_HDR_FLAG_RESP;
	resp->hdr.reply = OCTEP_CTRL_NET_REPLY_OK;
}

static inline void process_rx_state(struct cp_lib_if *iface,
				    struct octep_ctrl_net_h2f_req *req,
				    struct octep_ctrl_mbox_msg *msg)
{
	struct octep_ctrl_net_h2f_resp *resp;

	resp = (struct octep_ctrl_net_h2f_resp *)msg->msg;
	if (req->rx.cmd == OCTEP_CTRL_NET_CMD_GET) {
		resp->rx.state = iface->rx_state;
		msg->hdr.sizew = OCTEP_CTRL_NET_H2F_GET_STATE_RESP_SZW;
		CP_LIB_LOG(INFO, LOOP, "Cmd: get rx state : %u\n",
			   resp->rx.state);
	}
	else {
		CP_LIB_LOG(INFO, LOOP, "Cmd: set rx state : %u\n",
			   req->rx.state);
		iface->rx_state = req->rx.state;
		msg->hdr.sizew = OCTEP_CTRL_NET_H2F_SET_STATE_RESP_SZW;
	}
	msg->hdr.flags = OCTEP_CTRL_MBOX_MSG_HDR_FLAG_RESP;
	resp->hdr.reply = OCTEP_CTRL_NET_REPLY_OK;
}

static inline void process_link_info(struct cp_lib_if *iface,
				     struct octep_ctrl_net_h2f_req *req,
			             struct octep_ctrl_mbox_msg *msg)
{
	struct octep_ctrl_net_h2f_resp *resp;

	resp = (struct octep_ctrl_net_h2f_resp *)msg->msg;
	if (req->link_info.cmd == OCTEP_CTRL_NET_CMD_GET) {
		resp->link_info.supported_modes = iface->supported_modes;
		resp->link_info.advertised_modes = iface->advertised_modes;
		resp->link_info.autoneg = iface->autoneg;
		resp->link_info.pause = iface->pause_mode;
		resp->link_info.speed = iface->speed;
		msg->hdr.sizew = OCTEP_CTRL_NET_H2F_GET_LINK_INFO_RESP_SZW;
		CP_LIB_LOG(INFO, LOOP, "Cmd: get link info\n");
	}
	else {
		CP_LIB_LOG(INFO, LOOP,
			   "Cmd: set link info: am:%lx a:%x p:%x s:%x\n",
			   req->link_info.info.advertised_modes,
			   req->link_info.info.autoneg,
			   req->link_info.info.pause,
			   req->link_info.info.speed);
		iface->advertised_modes = req->link_info.info.advertised_modes;
		iface->autoneg = req->link_info.info.autoneg;
		iface->pause_mode = req->link_info.info.pause;
		iface->speed = req->link_info.info.speed;
		msg->hdr.sizew = OCTEP_CTRL_NET_H2F_SET_LINK_INFO_RESP_SZW;
	}
	msg->hdr.flags = OCTEP_CTRL_MBOX_MSG_HDR_FLAG_RESP;
	resp->hdr.reply = OCTEP_CTRL_NET_REPLY_OK;
}

static inline int get_iface(struct cp_lib_pf *pf, int idx,
			    struct cp_lib_if **iface, struct if_data **if_data)
{
	struct cp_lib_vf *vf;

	if (idx == 0) {
		*iface = &pf->iface;
		*if_data = pf->psoc;
		return 0;
	} else {
		vf = pf->vfs;
		while (vf) {
			if (vf->idx == (idx - 1)) {
				*iface = &vf->iface;
				*if_data = vf->psoc;
				return 0;
			}
			vf = vf->next;
		}
	}

	return -ENAVAIL;
}

static int cnxk_loop_process_mbox_req(void *user_ctx,
				      struct octep_ctrl_mbox_msg *msg)
{
	struct octep_ctrl_net_h2f_req *req;
	struct cp_lib_if *iface;
	struct if_data *ifdata;
	int err;

	if (lp_state != LOOP_POLL_STATE_BEGIN)
		return -EAGAIN;

	if (user_cfg.msg_handler) {
		err = user_cfg.msg_handler(OCTEP_CP_EVENT_MBOX, user_ctx, msg);
		if (!err)
			return 0;
	}

	req = (struct octep_ctrl_net_h2f_req *)msg->msg;
	err = get_iface((struct cp_lib_pf *)user_ctx,
			req->hdr.receiver,
			&iface,
			&ifdata);
	if (err) {
		CP_LIB_LOG(INFO, LOOP, "Invalid receiver: %u\n",
			   req->hdr.receiver);
		return err;
	}

	iface->host_if_id = req->hdr.sender;
	switch (req->hdr.cmd) {
		case OCTEP_CTRL_NET_H2F_CMD_MTU:
			process_mtu(iface, req, msg);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_MAC:
			process_mac(iface, req, msg);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_GET_IF_STATS:
			process_get_if_stats(ifdata, req, msg);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_LINK_STATUS:
			process_link_status(iface, req, msg);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_RX_STATE:
			process_rx_state(iface, req, msg);
			break;
		case OCTEP_CTRL_NET_H2F_CMD_LINK_INFO:
			process_link_info(iface, req, msg);
			break;
		default:
			CP_LIB_LOG(INFO, LOOP, "Unhandled Cmd : %u\n",
				   req->hdr.cmd);
			break;
	}

	return 0;
}

static int free_if_data()
{
	struct cp_lib_pem *pem;
	struct cp_lib_pf *pf;
	struct cp_lib_vf *vf;

	pem = cfg.pems;
	while (pem) {
		pf = pem->pfs;
		while (pf) {
			if (pf->psoc)
				free(pf->psoc);

			vf = pf->vfs;
			while (vf) {
				if (vf->psoc)
					free(vf->psoc);

				vf = vf->next;
			}
			pf = pf->next;
		}
		pem = pem->next;
	}

	return 0;
}

static int alloc_if_data()
{
	struct cp_lib_pem *pem;
	struct cp_lib_pf *pf;
	struct cp_lib_vf *vf;

	pem = cfg.pems;
	while (pem) {
		pf = pem->pfs;
		while (pf) {
			pf->psoc = calloc(sizeof(struct if_data), 1);
			if (!pf->psoc) {
				CP_LIB_LOG(ERR, LOOP, "Oom for pem[%d]pf[%d]\n",
					   pem->idx, pf->idx);
				free_if_data();
				return -ENOMEM;
			}
			vf = pf->vfs;
			while (vf) {
				vf->psoc = calloc(sizeof(struct if_data), 1);
				if (!vf->psoc) {
					CP_LIB_LOG(ERR, LOOP,
						   "Oom for pem[%d]pf[%d]"
						   "vf[%d]\n",
						   pem->idx, pf->idx, vf->idx);
					free_if_data();
					return -ENOMEM;
				}
				vf = vf->next;
			}
			pf = pf->next;
		}
		pem = pem->next;
	}

	return 0;
}

int cnxk_loop_init(struct octep_cp_lib_cfg *p_cfg)
{
	int err;

	CP_LIB_LOG(INFO, LOOP, "init\n");

	if (!p_cfg)
		return -EINVAL;

	err = alloc_if_data();
	if (err)
		return err;

	err = cnxk_init(cnxk_loop_process_mbox_req, &cfg.pems[0].pfs[0]);
	if (err)
		return err;

	lp_state = LOOP_POLL_STATE_INVALID;
	dummy_perst = 0;

	return 0;
}

int cnxk_loop_poll(int max_events)
{
	struct octep_ctrl_mbox_msg msg;
	union octep_ctrl_net_h2f_data_sz data;
	int num_events = 0, err;

	//CP_LIB_LOG(INFO, LOOP, "poll\n");
	if (lp_state != LOOP_POLL_STATE_INVALID)
		return -EAGAIN;

	lp_state = LOOP_POLL_STATE_BEGIN;
	msg.msg = &data;
	while (lp_state == LOOP_POLL_STATE_BEGIN && num_events < max_events) {
		if (dummy_perst) {
			CP_LIB_LOG(INFO, LOOP, "perst.\n");
			if (user_cfg.msg_handler) {
				user_cfg.msg_handler(OCTEP_CP_EVENT_PERST,
						     NULL,
						     NULL);
			}
			num_events++;
			continue;
		}

		err = octep_ctrl_mbox_is_host_ready(&mbox);
		if (err)
			break;

		err = octep_ctrl_mbox_recv(&mbox, &msg);
		if (err)
			break;

		num_events++;
	}
	lp_state = LOOP_POLL_STATE_INVALID;

	return num_events;
}

static void toggle_random_link()
{
	struct octep_ctrl_mbox_msg msg = { 0 };
	struct octep_ctrl_net_f2h_req req = { 0 };
	struct cp_lib_if *iface = NULL;
	struct if_data *ifdata;
	int err;

	CP_LIB_LOG(INFO, LOOP, "toggling random link\n");
	err = get_iface(&cfg.pems[0].pfs[0], 0, &iface, &ifdata);
	if (!iface) {
		CP_LIB_LOG(INFO, LOOP,
			   "No active interfaces to trigger "
			   "link state toggle\n");
		return;
	}

	iface->link_state = (iface->link_state == OCTEP_CTRL_NET_STATE_DOWN) ?
				OCTEP_CTRL_NET_STATE_UP :
				OCTEP_CTRL_NET_STATE_DOWN;

	CP_LIB_LOG(INFO, LOOP, "Toggling if[%d]: link state to %u\n",
		   iface->idx, iface->link_state);

	req.hdr.sender = iface->idx;
	req.hdr.receiver = iface->host_if_id;
	req.hdr.cmd = OCTEP_CTRL_NET_F2H_CMD_LINK_STATUS;
	req.link.state = iface->link_state;

	msg.hdr.flags = OCTEP_CTRL_MBOX_MSG_HDR_FLAG_NOTIFY;
	msg.hdr.sizew = OCTEP_CTRL_NET_F2H_STATE_REQ_SZW;
	msg.msg = &req;
	err = octep_ctrl_mbox_send(&mbox, &msg);
	if (err)
		CP_LIB_LOG(INFO, LOOP,
			   "Error (%d) while sending link toggle mbox msg.\n",
			   err);
	err = cnxk_raise_oei_trig_interrupt();
	if (err)
		CP_LIB_LOG(INFO, LOOP,
			   "Error (%d) while triggering oei_trig interrupt.\n",
			   err);
}

int cnxk_loop_process_sigusr1()
{
	static int func = 0;

	CP_LIB_LOG(INFO, LOOP, "sigusr1\n");

	if (!func)
		toggle_random_link();
	else {
		CP_LIB_LOG(INFO, LOOP, "setting dummy_perst\n");
		dummy_perst = 1;
	}

	//func = !func;

	return 0;
}

int cnxk_loop_uninit()
{
	CP_LIB_LOG(INFO, LOOP, "uninit\n");

	if (lp_state != LOOP_POLL_STATE_INVALID) {
		lp_state = LOOP_POLL_STATE_END;
		CP_LIB_LOG(INFO, LOOP, "Wait for poll.\n");
		while (lp_state != LOOP_POLL_STATE_INVALID)
			usleep(100);
	}

	cnxk_uninit();
	free_if_data();

	return 0;
}
