/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __L2FWD_CONFIG_H__
#define __L2FWD_CONFIG_H__

#define MIN_HB_INTERVAL_MSECS		1000
#define MAX_HB_INTERVAL_MSECS		15000
#define DEFAULT_HB_INTERVAL_MSECS	MIN_HB_INTERVAL_MSECS

#define DEFAULT_HB_MISS_COUNT		20

/* pf/vf config */
struct l2fwd_config_fn {
	/* config is valid */
	bool is_valid;
	/* Plugin control override flag */
	bool plugin_controlled;
	/* dbdf mapped to host */
	struct rte_pci_addr to_host_dbdf;
	/* dbdf mapped to local interface */
	struct rte_pci_addr to_wire_dbdf;
	/* interface pkind */
	unsigned short pkind;

	/* Below config is applicable if (to_wire_dbdf == 0000:00:00.00) */
	/* device mac address */
	struct rte_ether_addr mac;
	/* mtu */
	int mtu;
	/* auto negotiation support */
	int autoneg;
	/* pause mode support */
	int pause_mode;
	/* link speed */
	unsigned int speed;
	/* supported link modes */
	unsigned long long smodes;
	/* advertised link modes */
	unsigned long long amodes;
};

/* pcie mac domain pf configuration */
struct l2fwd_config_pf {
	/* pf heartbeat interval in milliseconds */
	unsigned short hb_interval;
	/* pf heartbeat miss count */
	unsigned short hb_miss_count;
	/* pf config */
	struct l2fwd_config_fn d;
	/* vf's */
	struct l2fwd_config_fn vfs[L2FWD_MAX_VF];
};

/* pcie mac domain configuration */
struct l2fwd_config_pem {
	/* config is valid */
	bool is_valid;
	/* pf indices */
	struct l2fwd_config_pf pfs[L2FWD_MAX_PF];
};

/* l2fwd configuration */
struct l2fwd_config {
	/* pem indices */
	struct l2fwd_config_pem pems[L2FWD_MAX_PEM];
};

extern struct l2fwd_config l2fwd_cfg;

/* Initialize config data.
 *
 * Parse configuration file and populate global configuration.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_config_init(const char *file_path);

/* template for callback function */
typedef int (*valid_cfg_fn_callback_t)(int pem, int pf, int vf, void *data);

/* Iterate through valid pf/vf configuration.
 *
 * Call the callback function for each valid pf and vf interface.
 *
 * return value: 0 on success, -errno on failure.
 */
static inline int for_each_valid_config_fn(valid_cfg_fn_callback_t fn,
					   void *data) {
	int i, j, k, err;

	for (i = 0; i < L2FWD_MAX_PEM; i++) {
		if (!l2fwd_cfg.pems[i].is_valid)
			continue;

		for (j = 0; j < L2FWD_MAX_PF; j++) {
			if (!l2fwd_cfg.pems[i].pfs[j].d.is_valid)
				continue;

			err = fn(i, j, -1, data);
			if (err < 0)
				return err;

			for (k = 0; k < L2FWD_MAX_VF; k++) {
				if (!l2fwd_cfg.pems[i].pfs[j].vfs[k].is_valid)
					continue;

				err = fn(i, j, k, data);
				if (err < 0)
					return err;
			}
		}
	}

	return 0;
}

/* UnInitialize config data.
 *
 * UnInitialize local data for processing configuration file.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_config_uninit(void);

#endif /* __L2FWD_CONFIG_H__ */
