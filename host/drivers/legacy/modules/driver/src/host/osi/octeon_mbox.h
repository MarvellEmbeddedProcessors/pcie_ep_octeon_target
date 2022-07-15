#ifndef __OCTEON_MBOX_H__
#define __OCTEON_MBOX_H__

#include "octeon_main.h"

#define OTX_PF_MBOX_VERSION 0

typedef enum {
	OTX_VF_MBOX_CMD_SET_MTU,
	OTX_VF_MBOX_CMD_SET_MAC_ADDR,
	OTX_VF_MBOX_CMD_START_QUEUE,
	OTX_VF_MBOX_CMD_STOP_QUEUE,
	OTX_VF_MBOX_CMD_GET_LINK,
	OTX_VF_MBOX_CMD_BULK_SEND,
	OTX_VF_MBOX_CMD_BULK_GET,
	OTX_VF_MBOX_CMD_LAST,
} otx_vf_mbox_opcode_t;

typedef enum {
	OTX_VF_MBOX_TYPE_CMD,
	OTX_VF_MBOX_TYPE_RSP_ACK,
	OTX_VF_MBOX_TYPE_RSP_NACK,
} otx_vf_mbox_word_type_t;

struct otx_vf_mbox_word_hdr {
	uint64_t version:3;
	uint64_t rsvd1:2;
	uint64_t opcode:5;
	uint64_t rsvd2:3;
	uint64_t id:1;
	uint64_t type:2;
} __packed;

typedef enum {
	OTX_VF_LINK_STATUS_DOWN,
	OTX_VF_LINK_STATUS_UP,
} otx_vf_link_status_t;

typedef enum {
	OTX_VF_LINK_SPEED_NONE,
	OTX_VF_LINK_SPEED_10,
	OTX_VF_LINK_SPEED_100,
	OTX_VF_LINK_SPEED_1000,
	OTX_VF_LINK_SPEED_2500,
	OTX_VF_LINK_SPEED_5000,
	OTX_VF_LINK_SPEED_10000,
	OTX_VF_LINK_SPEED_20000,
	OTX_VF_LINK_SPEED_25000,
	OTX_VF_LINK_SPEED_40000,
	OTX_VF_LINK_SPEED_50000,
	OTX_VF_LINK_SPEED_100000,
	OTX_VF_LINK_SPEED_LAST,
} otx_vf_link_speed_t;

typedef enum {
	OTX_VF_LINK_HALF_DUPLEX,
	OTX_VF_LINK_FULL_DUPLEX,
} otx_vf_link_duplex_t;

typedef enum {
	OTX_VF_LINK_AUTONEG,
	OTX_VF_LINK_FIXED,
} otx_vf_link_autoneg_t;

struct otx_vf_mbox_link {
	uint64_t link_status:1;
	uint64_t link_speed:8;
	uint64_t duplex:1;
	uint64_t autoneg:1;
	uint64_t rsvd:37;
} __packed;

#define OTX_VF_MBOX_TIMEOUT_MS 10
#define OTX_VF_MBOX_MAX_RETRIES 2
#define OTX_VF_MBOX_VERSION     1
#define MBOX_MAX_DATA_SIZE      6
#define MBOX_MORE_FRAG_FLAG     1

union otx_vf_mbox_word {
	uint64_t u64;
	struct {
		uint64_t version:3;
		uint64_t rsvd1:2;
		uint64_t opcode:5;
		uint64_t rsvd2:3;
		uint64_t id:1;
		uint64_t type:2;
		uint64_t data:48;
	} s;
	struct {
		uint64_t version:3;
		uint64_t rsvd1:2;
		uint64_t opcode:5;
		uint64_t rsvd2:2;
		uint64_t frag:1;
		uint64_t id:1;
		uint64_t type:2;
		uint8_t data[6];
	} s_data;
	struct {
		uint64_t version:3;
		uint64_t rsvd1:2;
		uint64_t opcode:5;
		uint64_t rsvd2:3;
		uint64_t id:1;
		uint64_t type:2;
		uint8_t mac_addr[6];
	} s_set_mac;
	struct {
		uint64_t version:3;
		uint64_t rsvd1:2;
		uint64_t opcode:5;
		uint64_t rsvd2:3;
		uint64_t id:1;
		uint64_t type:2;
		uint64_t mtu:48;
	} s_set_mtu;
	struct {
		uint64_t version:3;
		uint64_t rsvd1:2;
		uint64_t opcode:5;
		uint64_t rsvd2:3;
		uint64_t id:1;
		uint64_t type:2;
		uint64_t link_status:1;
		uint64_t link_speed:8;
		uint64_t duplex:1;
		uint64_t autoneg:1;
		uint64_t rsvd:37;
	} s_get_link;
} __packed;

void handle_mbox_work(struct work_struct *work);
#endif
