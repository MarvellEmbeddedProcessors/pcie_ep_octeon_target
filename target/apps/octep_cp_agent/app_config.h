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

/* Network interface stats */
struct if_stats {
	struct octep_iface_rx_stats rx_stats;
	struct octep_iface_tx_stats tx_stats;
};

/* Network interface data */
struct if_cfg {
	uint16_t idx;
	uint16_t host_if_id;
	uint16_t mtu;
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
	struct vf_cfg *next;
};

/* Physical function configuration */
struct pf_cfg {
	/* pf index */
	int idx;
	/* network interface data */
	struct if_cfg iface;
	struct if_stats ifstats;
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

extern struct app_cfg app_cfg;

/* Parse file and populate configuration.
 *
 * @param cfg_file_path: Path to configuration file.
 *
 * return value: 0 on success, -errno on failure.
 */
int app_config_init(const char *cfg_file_path);

/* Free allocated configuration artifacts.
 *
 *
 * return value: 0 on success, -errno on failure.
 */
int app_config_uninit();

#endif /* __APP_CONFIG_H__ */
