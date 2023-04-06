/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <libconfig.h>

#include "compat.h"
#include "l2fwd_main.h"
#include "l2fwd_config.h"

#define RTE_LOGTYPE_L2FWD_CONFIG	RTE_LOGTYPE_USER1

/**
 * Objects and variables
 * Optional objects and variables are shown in square brackets
 *
 * Variables
 * ----------
 * int idx, end_idx, mtu, hb_interval, hb_miss_count = 0...n
 * char[] to_host_dbdf, to_wire_dbdf = "xxxx:xx:x.x"
 * char[] mac = xx:xx:xx:xx:xx:xx
 * bool autoneg, pause_mode = 0/1
 * long supported_modes, advertised_modes = 0...n
 * int pkind = 0/57/59
 *
 * if vf[mac] is not specified, it will be derived from pf[mac]
 * vf[mac] will start from (pf[mac] + 1)
 * it will be auto incremented for each vf[idx:end_idx]
 *
 * Objects
 * -------
 * id = { idx, [end_idx] };
 * dbdfs = { [to_host_dbdf], [to_wire_dbdf] };
 * if = { [mac], [mtu], [autoneg], [pause_mode], [speed], [supported_modes], [advertised_modes] };
 * fn_base = { id, [pkind], [dbdfs], [if] };
 * soc = { pem };
 * pem = { id, pf };
 * pf = { fn_base, [hb_interval], [hb_miss_count], [vf] };
 * vf = { fn_base };
 */

#define CFG_TOKEN_SOC				"soc"
#define CFG_TOKEN_PEM				"pem"
#define CFG_TOKEN_PF				"pf"
#define CFG_TOKEN_VF				"vf"

/* id */
#define CFG_TOKEN_ID_IDX			"idx"
#define CFG_TOKEN_ID_END_IDX			"end_idx"

/* dbdf */
#define CFG_TOKEN_DBDF_TO_HOST_DBDF		"to_host_dbdf"
#define CFG_TOKEN_DBDF_TO_WIRE_DBDF		"to_wire_dbdf"

/* if */
#define CFG_TOKEN_IF_MAC			"mac"
#define CFG_TOKEN_IF_AUTONEG			"autoneg"
#define CFG_TOKEN_IF_PMODE			"pause_mode"
#define CFG_TOKEN_IF_SPEED			"speed"
#define CFG_TOKEN_IF_SMODES			"supported_modes"
#define CFG_TOKEN_IF_AMODES			"advertised_modes"

/* fn_base */
#define CFG_TOKEN_FN_BASE_PKIND			"pkind"

/* info */
#define CFG_TOKEN_PF_HB_INTERVAL		"hb_interval"
#define CFG_TOKEN_PF_HB_MISS_COUNT		"hb_miss_count"

/* Plugin client */
#define CFG_TOKEN_PLUGIN_CONTROLLED		"plugin_controlled"

struct l2fwd_config l2fwd_cfg;

static void print_config(void)
{
	struct l2fwd_config_fn *vf, *fn;
	struct l2fwd_config_pem *pem;
	struct l2fwd_config_pf *pf;
	int i, j, k;

	for (i = 0; i < L2FWD_MAX_PEM; i++) {
		pem = &l2fwd_cfg.pems[i];
		for (j = 0; j < L2FWD_MAX_PF; j++) {
			pf = &pem->pfs[j];
			if (!pf->d.is_valid)
				continue;

			fn = &pf->d;
			RTE_LOG(DEBUG, L2FWD_CONFIG,
				"[%d]:[%d]\n"
				" hbi: [%u msec] hbmiss: [%u]\n"
				" pkind: [%d] to_host_dbdf: ["PCI_PRI_FMT"]"
				" to_wire_dbdf: ["PCI_PRI_FMT"]\n"
				" mac: ["L2FWD_PCIE_EP_ETHER_ADDR_PRT_FMT"]"
				" autoneg: [%u] pause: [%u]\n"
				" speed: [%u] smodes: [%llx] amodes: [%llx]\n",
				i, j,
				pf->hb_interval, pf->hb_miss_count,
				fn->pkind, fn->to_host_dbdf.domain,
				fn->to_host_dbdf.bus, fn->to_host_dbdf.devid,
				fn->to_host_dbdf.function, fn->to_wire_dbdf.domain,
				fn->to_wire_dbdf.bus, fn->to_wire_dbdf.devid,
				fn->to_wire_dbdf.function,
				fn->mac.addr_bytes[0], fn->mac.addr_bytes[1],
				fn->mac.addr_bytes[2], fn->mac.addr_bytes[3],
				fn->mac.addr_bytes[4], fn->mac.addr_bytes[5],
				fn->autoneg, fn->pause_mode, fn->speed,
				fn->smodes, fn->amodes);

			for (k = 0; k < L2FWD_MAX_VF; k++) {
				vf = &pf->vfs[k];
				if (!vf->is_valid)
					continue;

				RTE_LOG(DEBUG, L2FWD_CONFIG,
					"[%d]:[%d]:[%d]\n"
					" pkind: [%d] to_host_dbdf: ["PCI_PRI_FMT"]"
					" to_wire_dbdf: ["PCI_PRI_FMT"]\n"
					" mac: ["L2FWD_PCIE_EP_ETHER_ADDR_PRT_FMT"]"
					" autoneg: [%u] pause: [%u]\n"
					" speed: [%u] smodes: [%llx]"
					" amodes: [%llx]\n",
					i, j, k,
					vf->pkind,
					vf->to_host_dbdf.domain,
					vf->to_host_dbdf.bus,
					vf->to_host_dbdf.devid,
					vf->to_host_dbdf.function,
					vf->to_wire_dbdf.domain,
					vf->to_wire_dbdf.bus,
					vf->to_wire_dbdf.devid,
					vf->to_wire_dbdf.function,
					vf->mac.addr_bytes[0], vf->mac.addr_bytes[1],
					vf->mac.addr_bytes[2], vf->mac.addr_bytes[3],
					vf->mac.addr_bytes[4], vf->mac.addr_bytes[5],
					vf->autoneg, vf->pause_mode,
					vf->speed, vf->smodes, vf->amodes);
			}
		}
	}
}

static inline int set_default_mac(struct rte_ether_addr *mac, int pem, int pf,
				  int vf)
{
	mac->addr_bytes[0] = 0x00;
	mac->addr_bytes[1] = 0x00;
	mac->addr_bytes[2] = 0x00;
	mac->addr_bytes[3] = pem;
	mac->addr_bytes[4] = pf;
	mac->addr_bytes[5] = vf;

	return 0;
}

static int parse_fn_base(config_setting_t *lcfg, struct l2fwd_config_fn *fn)
{
	int err, ival;
	const char *c;

	err = config_setting_lookup_int(lcfg, CFG_TOKEN_FN_BASE_PKIND, &ival);
	if (err == CONFIG_TRUE)
		fn->pkind = ival;

	if (config_setting_lookup_bool(lcfg, CFG_TOKEN_PLUGIN_CONTROLLED,
				       (int *) &fn->plugin_controlled) == CONFIG_FALSE)
		fn->plugin_controlled = false;

	err = config_setting_lookup_string(lcfg,
					   CFG_TOKEN_DBDF_TO_HOST_DBDF,
					   &c);
	if (err == CONFIG_TRUE) {
		err = rte_pci_addr_parse(c, &fn->to_host_dbdf);
		if (err < 0) {
			RTE_LOG(ERR, L2FWD_CONFIG,
				"Invalid to_host_dbdf[%s].\n", c);
			return err;
		}
	}

	err = config_setting_lookup_string(lcfg,
					   CFG_TOKEN_DBDF_TO_WIRE_DBDF,
					   &c);
	if (err == CONFIG_TRUE) {
		err = rte_pci_addr_parse(c, &fn->to_wire_dbdf);
		if (err < 0) {
			RTE_LOG(ERR, L2FWD_CONFIG,
				"Invalid to_wire_dbdf[%s].\n", c);
			return err;
		}
	}

	err = config_setting_lookup_string(lcfg, CFG_TOKEN_IF_MAC, &c);
	if (err == CONFIG_TRUE)
		rte_ether_unformat_addr(c, &fn->mac);

	config_setting_lookup_int(lcfg, CFG_TOKEN_IF_AUTONEG, &fn->autoneg);
	config_setting_lookup_int(lcfg, CFG_TOKEN_IF_PMODE, &fn->pause_mode);
	config_setting_lookup_int(lcfg, CFG_TOKEN_IF_SPEED, (int *)&fn->speed);
	config_setting_lookup_int64(lcfg, CFG_TOKEN_IF_SMODES, (long long *)&fn->smodes);
	config_setting_lookup_int64(lcfg, CFG_TOKEN_IF_AMODES, (long long *)&fn->amodes);
	fn->is_valid = true;

	return 0;
}

static int parse_pf(config_setting_t *pf, struct l2fwd_config_pf *pfcfg,
		    int pem_idx, int pf_idx)
{
	config_setting_t *vfs, *vf;
	int nvfs, i, idx, err, eidx, ival, sidx;

	err = parse_fn_base(pf, &pfcfg->d);
	if (err < 0)
		return err;

	if (rte_is_zero_ether_addr(&pfcfg->d.mac))
		set_default_mac(&pfcfg->d.mac, pem_idx + 1, pf_idx + 1, 0);

	err = config_setting_lookup_int(pf, CFG_TOKEN_PF_HB_INTERVAL, &ival);
	pfcfg->hb_interval = (err == CONFIG_TRUE) ?
			      ival : DEFAULT_HB_INTERVAL_MSECS;

	err = config_setting_lookup_int(pf, CFG_TOKEN_PF_HB_MISS_COUNT, &ival);
	pfcfg->hb_miss_count = (err == CONFIG_TRUE) ?
				ival : DEFAULT_HB_MISS_COUNT;

	vfs = config_setting_get_member(pf, CFG_TOKEN_VF);
	if (!vfs)
		return 0;

	nvfs = config_setting_length(vfs);
	for (i = 0; i < nvfs; i++) {
		vf = config_setting_get_elem(vfs, i);
		if (!vf)
			continue;

		err = config_setting_lookup_int(vf, CFG_TOKEN_ID_IDX, &sidx);
		if (err == CONFIG_FALSE)
			continue;
		err = config_setting_lookup_int(vf,
						CFG_TOKEN_ID_END_IDX,
						&eidx);
		if (err == CONFIG_FALSE)
			eidx = sidx + 1;
		if (sidx >= L2FWD_MAX_VF ||
		    eidx >= L2FWD_MAX_VF)
			continue;

		for (idx = sidx; idx < eidx; idx++) {
			err = parse_fn_base(vf, &pfcfg->vfs[idx]);
			if (err < 0)
				return err;

			if (rte_is_zero_ether_addr(&pfcfg->vfs[idx].mac))
				set_default_mac(&pfcfg->vfs[idx].mac,
						pem_idx + 1,
						pf_idx + 1,
						idx + 1);
		}
	}

	return 0;
}

static int parse_pem(config_setting_t *pem, struct l2fwd_config_pem *pemcfg,
		     int pem_idx)
{
	config_setting_t *pfs, *pf;
	int npfs, i, idx, eidx, err, valid_pfs, sidx;

	pfs = config_setting_get_member(pem, CFG_TOKEN_PF);
	if (!pfs) {
		RTE_LOG(ERR, L2FWD_CONFIG,
			"pf objects not found.\n");
		return -EINVAL;
	}

	npfs = config_setting_length(pfs);
	valid_pfs = 0;
	for (i = 0; i < npfs; i++) {
		pf = config_setting_get_elem(pfs, i);
		if (!pf)
			continue;

		err = config_setting_lookup_int(pf, CFG_TOKEN_ID_IDX, &sidx);
		if (err == CONFIG_FALSE)
			continue;
		err = config_setting_lookup_int(pf,
						CFG_TOKEN_ID_END_IDX,
						&eidx);
		if (err == CONFIG_FALSE)
			eidx = sidx + 1;
		if (sidx >= L2FWD_MAX_PF ||
		    eidx >= L2FWD_MAX_PF)
			continue;

		for (idx = sidx; idx < eidx; idx++) {
			err = parse_pf(pf, &pemcfg->pfs[idx], pem_idx, idx);
			if (!err)
				valid_pfs++;
		}
	}
	if (valid_pfs > 0) {
		pemcfg->is_valid = true;
		return 0;
	}

	return -EINVAL;
}

static int parse_pems(config_setting_t *pems)
{
	config_setting_t *pem;
	int npems, i, idx, err, eidx, valid_pems, sidx;

	npems = config_setting_length(pems);
	valid_pems = 0;
	for (i = 0; i < npems; i++) {
		pem = config_setting_get_elem(pems, i);
		if (!pem)
			continue;
		err = config_setting_lookup_int(pem, CFG_TOKEN_ID_IDX, &sidx);
		if (err == CONFIG_FALSE)
			continue;
		err = config_setting_lookup_int(pem,
						CFG_TOKEN_ID_END_IDX,
						&eidx);
		if (err == CONFIG_FALSE)
			eidx = sidx;
		if (sidx >= L2FWD_MAX_PEM ||
		    eidx >= L2FWD_MAX_PEM)
			continue;

		for (idx = sidx; idx <= eidx; idx++) {
			err = parse_pem(pem, &l2fwd_cfg.pems[idx], idx);
			if (!err)
				valid_pems++;
		}
	}

	return (valid_pems > 0) ? 0 : -EINVAL;
}

int l2fwd_config_init(const char *file_path)
{
	config_setting_t *lcfg, *pems;
	config_t fcfg;
	int err;

	memset(&l2fwd_cfg, 0, sizeof(struct l2fwd_config));

	RTE_LOG(DEBUG, L2FWD_CONFIG, "config file : %s\n", file_path);
	config_init(&fcfg);
	if (!config_read_file(&fcfg, file_path)) {
		RTE_LOG(ERR, L2FWD_CONFIG, "%s:%d - %s\n",
			config_error_file(&fcfg),
			config_error_line(&fcfg),
			config_error_text(&fcfg));
		config_destroy(&fcfg);
		return -EINVAL;
	}

	lcfg = config_lookup(&fcfg, CFG_TOKEN_SOC);
	if (!lcfg) {
		RTE_LOG(ERR, L2FWD_CONFIG, "soc object not found.\n");
		config_destroy(&fcfg);
		return -EINVAL;
	}

	pems = config_setting_get_member(lcfg, CFG_TOKEN_PEM);
	if (pems) {
		err = parse_pems(pems);
		if (err) {
			RTE_LOG(ERR, L2FWD_CONFIG,
				"Valid pem objects not found.\n");
			config_destroy(&fcfg);
			return err;
		}
	}

	config_destroy(&fcfg);

	print_config();

	return 0;
}

int l2fwd_config_uninit(void)
{
	memset(&l2fwd_cfg, 0, sizeof(struct l2fwd_config));

	return 0;
}
