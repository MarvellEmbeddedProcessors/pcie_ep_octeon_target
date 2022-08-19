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

/* Physical function configuration */
struct cp_lib_pf {
	/* pf index */
	int idx;
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
};

/* soc operations */
struct cp_lib_soc_ops {
	/* initialize */
	int (*init)(struct octep_cp_lib_cfg *p_cfg);
	/* poll for mbox messages and events */
	int (*poll)();
	/* send heartbeat to host */
	int (*send_heartbeat)();
	/* uninitialize */
	int (*uninit)();
};

extern volatile enum cp_lib_state state;
extern struct octep_cp_lib_cfg user_cfg;
extern struct cp_lib_cfg lib_cfg;

/* Get soc ops.
 *
 * @param mode: mode to be used.
 * @param ops: non-null pointer to struct soc_ops* to be filled by soc impl.
 *
 * return value: 0 on success, -errno on failure.
 */
int soc_get_ops(struct cp_lib_soc_ops **ops);

/* Parse file and populate configuration.
 *
 * @param cfg_file_path: Path to configuration file.
 *
 * return value: 0 on success, -errno on failure.
 */
int lib_config_init(const char *cfg_file_path);

/* Free allocated configuration artifacts.
 *
 *
 * return value: 0 on success, -errno on failure.
 */
int lib_config_uninit();

#endif /* __CP_LIB_H__ */
