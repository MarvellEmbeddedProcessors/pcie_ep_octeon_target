/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __CP_LIB_H__
#define __CP_LIB_H__

#ifndef ETH_ALEN
#define ETH_ALEN	6
#endif

/* Supported soc's */
enum cp_lib_soc {
        CP_LIB_SOC_OTX2,
        CP_LIB_SOC_CNXK,
        CP_LIB_SOC_MAX
};

/* library state */
enum cp_lib_state {
	CP_LIB_STATE_INVALID,
	CP_LIB_STATE_INIT,
	CP_LIB_STATE_READY,
	CP_LIB_STATE_UNINIT,
};

/* Network interface data */
struct cp_lib_if {
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
struct cp_lib_vf {
	/* vf index */
	int idx;
	/* network interface data */
	struct cp_lib_if iface;
	/* private data for soc */
	void *psoc;
	struct cp_lib_vf *next;
};

/* Physical function configuration */
struct cp_lib_pf {
	/* pf index */
	int idx;
	/* network interface data */
	struct cp_lib_if iface;
	/* number of vf's */
	int nvf;
	/* configuration for vf's */
	struct cp_lib_vf *vfs;
	/* private data for soc */
	void *psoc;
	struct cp_lib_pf *next;
};

/* PEM configuration */
struct cp_lib_pem {
	/* pem index */
	int idx;
	/* number of pf's */
	int npf;
	/* configuration for pf's */
	struct cp_lib_pf *pfs;
	/* private data for soc */
	void *psoc;
	struct cp_lib_pem *next;
};

/* library configuration */
struct cp_lib_cfg {
	/* soc implementation to be autodetected */
	enum cp_lib_soc soc;
	/* number of pem's */
	int npem;
	/* configuration for pem's */
	struct cp_lib_pem *pems;
	/* private data for soc */
	void *psoc;
};

/* soc operations */
struct cp_lib_soc_ops {
	/* initialize */
	int (*init)(struct octep_cp_lib_cfg *p_cfg);
	/* poll for mbox messages and events */
	int (*poll)(int max_events);
	/* process SIGUSR1 */
	int (*process_sigusr1)();
	/* uninitialize */
	int (*uninit)();
};

extern volatile enum cp_lib_state state;
extern struct octep_cp_lib_cfg user_cfg;
extern struct cp_lib_cfg cfg;

/* Get soc ops.
 *
 * @param mode: mode to be used.
 * @param ops: non-null pointer to struct soc_ops* to be filled by soc impl.
 *
 * return value: 0 on success, -errno on failure.
 */
int soc_get_ops(enum octep_cp_mode mode, struct cp_lib_soc_ops **ops);

/* Parse file and populate configuration.
 *
 * @param cfg_file_path: Path to configuration file.
 *
 * return value: 0 on success, -errno on failure.
 */
int config_parse_file(const char *cfg_file_path);

#endif /* __CP_LIB_H__ */
