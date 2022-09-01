/* Copyright (c) 2022 Marvell.
 * SPDX-License-Identifier: GPL-2.0
 */

#include "octeon_hw.h"
#include "octeon_mbox.h"
#include "octeon_config.h"
#include "octeon_macros.h"

static void
handle_vf_get_link_status(octeon_device_t *oct, int vf_id,
			 union otx_vf_mbox_word cmd,
			 union otx_vf_mbox_word *rsp)
{
	rsp->s_get_link.id = cmd.s_get_link.id;
	rsp->s_get_link.type = OTX_VF_MBOX_TYPE_RSP_ACK;
	rsp->s_get_link.link_status =  OTX_VF_LINK_STATUS_UP;
	rsp->s_get_link.link_speed = OTX_VF_LINK_SPEED_10000;
	rsp->s_get_link.duplex = OTX_VF_LINK_FULL_DUPLEX;
	rsp->s_get_link.autoneg = OTX_VF_LINK_AUTONEG;
	cavium_print_msg("mbox get link status cmd vf %d cmd_id %d\n",
			 vf_id, cmd.s_get_link.id);
}

static void
handle_vf_set_mtu(octeon_device_t *oct, int vf_id, union otx_vf_mbox_word cmd,
		 union otx_vf_mbox_word *rsp)
{
	rsp->s_set_mtu.id = cmd.s_set_mtu.id;
	rsp->s_set_mtu.type = OTX_VF_MBOX_TYPE_RSP_ACK;
	cavium_print_msg("mbox handle mtu cmd vf %d id %d mtu is %d\n",
			 vf_id, cmd.s_set_mtu.id, (int)cmd.s_set_mtu.mtu);
}

static void
handle_vf_set_mac_addr(octeon_device_t *oct,  int vf_id, union otx_vf_mbox_word cmd,
		      union otx_vf_mbox_word *rsp)
{
	int i;

	for (i = 0; i < MBOX_MAX_DATA_SIZE; i++)
		oct->vf_info[vf_id].mac_addr[i] = cmd.s_set_mac.mac_addr[i];

	rsp->s_set_mac.id = cmd.s_set_mac.id;
	rsp->s_set_mac.type = OTX_VF_MBOX_TYPE_RSP_ACK;
	cavium_print_msg("%s %pM\n",  __func__, oct->vf_info[vf_id].mac_addr);
}

static void
handle_vf_get_mac_addr(octeon_device_t *oct,  int vf_id, union otx_vf_mbox_word cmd,
		      union otx_vf_mbox_word *rsp)
{
	int i;

	rsp->s_set_mac.id = cmd.s_set_mac.id;
	rsp->s_set_mac.type = OTX_VF_MBOX_TYPE_RSP_ACK;
	for (i = 0; i < MBOX_MAX_DATA_SIZE; i++)
		rsp->s_set_mac.mac_addr[i] = oct->vf_info[vf_id].mac_addr[i];
	cavium_print_msg("%s vf_info: %pM\n",  __func__, oct->vf_info[vf_id].mac_addr);
}

static void
handle_vf_pf_get_data(octeon_device_t *oct,  octeon_mbox_t *mbox, int vf_id,
		        union otx_vf_mbox_word cmd, union otx_vf_mbox_word *rsp)
{
	int i=0;
	int length=0;
	rsp->s_data.id = cmd.s_data.id;
	rsp->s_data.type = OTX_VF_MBOX_TYPE_RSP_ACK;
	cavium_print_msg("handle_vf_pf_get_data received\n");
	if (cmd.s_data.frag != MBOX_MORE_FRAG_FLAG) {
		mbox->config_data_index = 0;
		memset(mbox->config_data,0,MAX_MBOX_DATA_SIZE);
		/* Call OPCODE specific GET API
		 * to fetch requested data from PF driver.
		 * mbox->message_len = get_stats(mbox->config_data);
		 */
		*((int32_t *)rsp->s_data.data) = mbox->message_len;
		cavium_print_msg("handle_vf_pf_get_data msg len %d:",mbox->message_len);
		return;
	}
	if (mbox->message_len > MBOX_MAX_DATA_SIZE) {
		length = MBOX_MAX_DATA_SIZE;
		cavium_print_msg("handle_vf_pf_get_data more to send data to VF \n");
	}
	else {
		length = mbox->message_len;
		cavium_print_msg("handle_vf_pf_get_data last to send data to VF \n");
	}
	mbox->message_len -= length;
	for (i=0;i< length;i++) {
		rsp->s_data.data[i] = mbox->config_data[mbox->config_data_index];
		mbox->config_data_index++;
	}
	return;
}

static void
handle_vf_pf_config_data(octeon_device_t *oct,  octeon_mbox_t *mbox, int vf_id,
		        union otx_vf_mbox_word cmd, union otx_vf_mbox_word *rsp)
{
	int i=0;
	int length;

	rsp->s_data.id = cmd.s_data.id;
	rsp->s_data.type = OTX_VF_MBOX_TYPE_RSP_ACK;
	cavium_print_msg("handle_vf_pf_config_data received\n");
	if ((cmd.s_data.frag == MBOX_MORE_FRAG_FLAG) && (mbox->message_len == 0)) {
		length = *((int32_t *)cmd.s_data.data);
		cavium_print_msg("handle_vf_pf_config_data length %d: \n",length);
		mbox->message_len = length;
		mbox->config_data_index = 0;
		memset(mbox->config_data,0,MAX_MBOX_DATA_SIZE);
		return;
	}
	if (cmd.s_data.frag == MBOX_MORE_FRAG_FLAG) {
		for (i=0;i< MBOX_MAX_DATA_SIZE;i++) {
			mbox->config_data[mbox->config_data_index] = cmd.s_data.data[i];
			mbox->config_data_index++;
		}
		mbox->message_len -= MBOX_MAX_DATA_SIZE;
	}
	else {
		for (i=0;i< mbox->message_len;i++) {
			mbox->config_data[mbox->config_data_index] = cmd.s_data.data[i];
			mbox->config_data_index++;
		}
		/* Calls OPCODE specific configuration handler by passing
		 * received config data as input argument.
		 */
		mbox->config_data_index = 0;
		mbox->message_len = 0;
		memset(mbox->config_data,0,MAX_MBOX_DATA_SIZE);
	}
}

void handle_mbox_work(struct work_struct *work)
{
	struct cavium_wk *wk = container_of(work, struct cavium_wk, work);
	octeon_mbox_t *mbox = NULL;
	octeon_device_t *oct = NULL;
	union otx_vf_mbox_word cmd = { 0 };
	union otx_vf_mbox_word rsp = { 0 };
	int vf_id;

	mbox = (octeon_mbox_t *)wk->ctxptr;
	oct = (octeon_device_t *)mbox->oct;
	vf_id = mbox->vf_id;

	cavium_mutex_lock(&mbox->lock);
	cmd.u64 = OCTEON_READ64(mbox->vf_pf_data_reg);
	rsp.u64 = 0;

	cavium_print_msg("handle_mbox_work is called vf_id %d\n",vf_id);
	if (cmd.s.version != OTX_PF_MBOX_VERSION) {
		cavium_print_msg("mbox version mis match vf version %d pf version %d\n",
				cmd.s.version, OTX_PF_MBOX_VERSION);
		rsp.s.type = OTX_VF_MBOX_TYPE_RSP_NACK;
		goto done;
	}
	switch(cmd.s.opcode) {
	case OTX_VF_MBOX_CMD_GET_LINK:
		handle_vf_get_link_status(oct, vf_id, cmd, &rsp);
		break;
	case OTX_VF_MBOX_CMD_SET_MTU:
		handle_vf_set_mtu(oct, vf_id, cmd, &rsp);
		break;
	case OTX_VF_MBOX_CMD_SET_MAC_ADDR:
		handle_vf_set_mac_addr(oct, vf_id, cmd, &rsp);
		break;
	case OTX_VF_MBOX_CMD_GET_MAC_ADDR:
		handle_vf_get_mac_addr(oct, vf_id, cmd, &rsp);
		break;
	case OTX_VF_MBOX_CMD_BULK_SEND:
		handle_vf_pf_config_data(oct, mbox, vf_id, cmd, &rsp);
		break;
	case OTX_VF_MBOX_CMD_BULK_GET:
		handle_vf_pf_get_data(oct, mbox, vf_id, cmd, &rsp);
		break;
	default:
		cavium_print_msg("handle_mbox_work is called OTX_VF_MBOX_TYPE_RSP_NACK\n");
		rsp.s.type = OTX_VF_MBOX_TYPE_RSP_NACK;
		break;
	}
done:
	OCTEON_WRITE64(mbox->vf_pf_data_reg, rsp.u64);
	cavium_mutex_unlock(&mbox->lock);
}
