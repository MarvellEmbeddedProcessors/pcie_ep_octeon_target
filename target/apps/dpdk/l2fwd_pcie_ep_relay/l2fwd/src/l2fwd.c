/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#include "compat.h"
#include "l2fwd.h"
#include "l2fwd_main.h"
#include "l2fwd_control.h"
#include "l2fwd_data.h"
#include "l2fwd_config.h"
#include "l2fwd_api_server.h"

#define RTE_LOGTYPE_L2FWD       RTE_LOGTYPE_USER1

/* user configuration */
struct l2fwd_user_config l2fwd_user_cfg = { 0 };

static void control_on_before_pem_reset(int pem)
{
}

static void control_on_after_pem_reset(int pem)
{
}

static void control_on_before_pf_reset(int pem, int pf)
{
}

static void control_on_after_pf_reset(int pem, int pf)
{
}

static void control_on_before_vf_reset(int pem, int pf, int vf)
{
}

static void control_on_after_vf_reset(int pem, int pf, int vf)
{
}

static struct l2fwd_control_ops control_ops = {
	.on_before_pem_reset = control_on_before_pem_reset,
	.on_after_pem_reset = control_on_after_pem_reset,
	.on_before_pf_reset = control_on_before_pf_reset,
	.on_after_pf_reset = control_on_after_pf_reset,
	.on_before_vf_reset = control_on_before_vf_reset,
	.on_after_vf_reset = control_on_after_vf_reset,
};

static int api_server_set_fwd_state(int state)
{
	if (!L2FWD_FEATURE(l2fwd_user_cfg.features, L2FWD_FEATURE_DATA_PLANE))
		return -EINVAL;

	return (state == 0) ? l2fwd_data_stop() : l2fwd_data_start();
}

static int api_server_clear_fwd_table(void)
{
	int err;

	if (L2FWD_FEATURE(l2fwd_user_cfg.features, L2FWD_FEATURE_CTRL_PLANE)) {
		err = l2fwd_control_clear_port_mapping();
		if (err < 0)
			return err;
	}

	if (L2FWD_FEATURE(l2fwd_user_cfg.features, L2FWD_FEATURE_DATA_PLANE)) {
		err = l2fwd_data_clear_fwd_table();
		if (err < 0)
			return err;
	}

	return 0;
}

static int api_server_add_fwd_table_entry(struct rte_pci_addr *port1,
					  struct rte_pci_addr *port2)
{
	int err;

	if (L2FWD_FEATURE(l2fwd_user_cfg.features, L2FWD_FEATURE_CTRL_PLANE)) {
		err = l2fwd_control_set_port_mapping(port1, port2);
		if (err < 0)
			return err;
	}

	if (L2FWD_FEATURE(l2fwd_user_cfg.features, L2FWD_FEATURE_DATA_PLANE)) {
		err = l2fwd_data_add_fwd_table_entry(port1, port2);
		if (err < 0)
			return err;
	}

	return 0;
}

static int api_server_del_fwd_table_entry(struct rte_pci_addr *port1,
					  struct rte_pci_addr *port2)
{
	int err;

	if (L2FWD_FEATURE(l2fwd_user_cfg.features, L2FWD_FEATURE_CTRL_PLANE)) {
		err = l2fwd_control_set_port_mapping(port1, &zero_dbdf);
		if (err < 0)
			return err;
	}

	if (L2FWD_FEATURE(l2fwd_user_cfg.features, L2FWD_FEATURE_DATA_PLANE)) {
		err = l2fwd_data_del_fwd_table_entry(port1, port2);
		if (err < 0)
			return err;
	}

	return 0;
}

static struct l2fwd_api_server_ops api_server_ops = {
	.set_fwd_state = api_server_set_fwd_state,
	.clear_fwd_table = api_server_clear_fwd_table,
	.add_fwd_table_entry = api_server_add_fwd_table_entry,
	.del_fwd_table_entry = api_server_del_fwd_table_entry
};

int l2fwd_init(struct l2fwd_user_config *cfg)
{
	int err;

	l2fwd_user_cfg = *cfg;
	err = l2fwd_config_init(l2fwd_user_cfg.cfg_file_path);
	if (err < 0)
		return err;

	err = l2fwd_control_init(&control_ops);
	if (err < 0)
		goto l2fwd_control_init_fail;

	if (L2FWD_FEATURE(l2fwd_user_cfg.features, L2FWD_FEATURE_DATA_PLANE)) {
		err = l2fwd_data_init();
		if (err < 0)
			goto l2fwd_data_init_fail;
	}

	if (L2FWD_FEATURE(l2fwd_user_cfg.features, L2FWD_FEATURE_API_SERVER)) {
		err = l2fwd_api_server_init(&api_server_ops);
		if (err < 0)
			goto l2fwd_api_server_init_fail;
	}

	return 0;

l2fwd_api_server_init_fail:
	if (L2FWD_FEATURE(l2fwd_user_cfg.features, L2FWD_FEATURE_DATA_PLANE))
		l2fwd_data_uninit();
l2fwd_data_init_fail:
		l2fwd_control_uninit();
l2fwd_control_init_fail:
	l2fwd_config_uninit();

	return err;
}

int l2fwd_start(void)
{
	int err;

	if (L2FWD_FEATURE(l2fwd_user_cfg.features, L2FWD_FEATURE_DATA_PLANE)) {
		err = l2fwd_data_start();
		if (err < 0)
			return err;
	}

	if (L2FWD_FEATURE(l2fwd_user_cfg.features, L2FWD_FEATURE_API_SERVER)) {
		err = l2fwd_api_server_start();
		if (err < 0)
			goto l2fwd_api_server_start_fail;
	}

	return 0;

l2fwd_api_server_start_fail:
	if (L2FWD_FEATURE(l2fwd_user_cfg.features, L2FWD_FEATURE_DATA_PLANE))
		l2fwd_data_stop();

	return err;
}

int l2fwd_poll(void)
{
	l2fwd_data_poll();
	return l2fwd_control_poll();
}

int l2fwd_print_stats(void)
{
	if (L2FWD_FEATURE(l2fwd_user_cfg.features, L2FWD_FEATURE_DATA_PLANE))
		l2fwd_data_print_stats();

	return 0;
}

int l2fwd_sigalrm(void)
{
	return l2fwd_control_handle_alarm();
}

int l2fwd_stop(void)
{
	if (L2FWD_FEATURE(l2fwd_user_cfg.features, L2FWD_FEATURE_API_SERVER))
		l2fwd_api_server_stop();

	if (L2FWD_FEATURE(l2fwd_user_cfg.features, L2FWD_FEATURE_DATA_PLANE))
		l2fwd_data_stop();

	return 0;
}

int l2fwd_uninit(void)
{
	if (L2FWD_FEATURE(l2fwd_user_cfg.features, L2FWD_FEATURE_API_SERVER))
		l2fwd_api_server_uninit();

	if (L2FWD_FEATURE(l2fwd_user_cfg.features, L2FWD_FEATURE_DATA_PLANE))
		l2fwd_data_uninit();

	l2fwd_control_uninit();

	l2fwd_config_uninit();

	return 0;
}
