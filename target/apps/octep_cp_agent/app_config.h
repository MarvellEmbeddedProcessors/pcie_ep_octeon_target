/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __APP_CONFIG_H__
#define __APP_CONFIG_H__

#include <stdint.h>

#include <octep_hw.h>

#ifndef ETH_ALEN
#define ETH_ALEN	6
#endif

#define MIN_HB_INTERVAL_MSECS		1000
#define MAX_HB_INTERVAL_MSECS		15000
#define DEFAULT_HB_INTERVAL_MSECS	MIN_HB_INTERVAL_MSECS

#define DEFAULT_HB_MISS_COUNT		20

/* Network interface stats */
struct if_stats {
	struct octep_iface_rx_stats rx_stats;
	struct octep_iface_tx_stats tx_stats;
};

/* Network interface data */
struct if_cfg {
	uint16_t idx;
	uint16_t host_if_id;
	/* current MTU of the interface */
	uint16_t mtu;
	/* Max Receive packet length of the interface */
	uint16_t max_rx_pktlen;
	uint8_t mac_addr[ETH_ALEN];
	/* enum octep_ctrl_net_state */
	uint16_t link_state;
	/* enum octep_ctrl_net_state */
	uint16_t rx_state;
	/* OCTEP_LINK_MODE_XXX */
	uint8_t autoneg;
	/* OCTEP_LINK_MODE_XXX */
	uint8_t pause_mode;
	/* SPEED_XXX */
	uint32_t speed;
	/* OCTEP_LINK_MODE_XXX */
	uint64_t supported_modes;
	/* OCTEP_LINK_MODE_XXX */
	uint64_t advertised_modes;
};

/* Virtual function configuration */
struct vf_cfg {
	/* vf index */
	int idx;
	/* network interface data */
	struct if_cfg iface;
	struct if_stats ifstats;
	struct octep_fw_info info;
	struct vf_cfg *next;
};

/* Physical function configuration */
struct pf_cfg {
	/* pf index */
	int idx;
	/* network interface data */
	struct if_cfg iface;
	struct if_stats ifstats;
	struct octep_fw_info info;
	/* number of vf's */
	int nvf;
	/* configuration for vf's */
	struct vf_cfg *vfs;
	struct pf_cfg *next;
};

/* PEM configuration */
struct pem_cfg {
	/* pem index */
	int idx;
	/* number of pf's */
	int npf;
	/* configuration for pf's */
	struct pf_cfg *pfs;
	struct pem_cfg *next;
};

/* app configuration */
struct app_cfg {
	/* number of pem's */
	int npem;
	/* configuration for pem's */
	struct pem_cfg *pems;
};

extern struct app_cfg cfg;

/* Parse file and populate configuration.
 *
 * @param cfg_file_path: Path to configuration file.
 *
 * return value: 0 on success, -errno on failure.
 */
int app_config_init(const char *cfg_file_path);

/* Get interface based on information in essage header.
 *
 * @param ctx_info: non-null pointer to message context info. This is the
 *                  pem->pf context used to poll for messages.
 * @param msg_info: non-null pointer to message info. This is the info from
 *                  received message.
 * @param iface: non-null pointer to struct if_cfg *.
 * @param ifstats: non-null pointer to struct if_stats *.
 * @param info: non-null pointer to struct octep_fw_info *.
 *
 * return value: 0 on success, -errno on failure.
 */
int app_config_get_if_from_msg_info(union octep_cp_msg_info *ctx_info,
				    union octep_cp_msg_info *msg_info,
				    struct if_cfg **iface,
				    struct if_stats **ifstats,
				    struct octep_fw_info **info);

/* Free allocated configuration artifacts.
 *
 *
 * return value: 0 on success, -errno on failure.
 */
int app_config_uninit();

#endif /* __APP_CONFIG_H__ */
