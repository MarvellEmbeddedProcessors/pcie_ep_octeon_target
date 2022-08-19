#include <stdlib.h>
#include <errno.h>
#include <libconfig.h>

#include "octep_cp_lib.h"
#include "cp_log.h"
#include "cp_lib.h"

/**
 * Object heirarchy
 * *(0 or more), +(1 or more)
 *
 * soc = { pem* };
 * pem = { idx, pf* };
 * pf = { idx, if, vf* };
 * vf = { idx, if };
 * if = { mtu, mac_addr, link_state, rx_state, autoneg, pause_mode, speed,
 *        supported_modes, advertisedd_modes
 * }
 */

#define CFG_TOKEN_SOC		"soc"
#define CFG_TOKEN_BASE_SOC	"base_soc"
#define CFG_TOKEN_PEMS		"pems"
#define CFG_TOKEN_PFS		"pfs"
#define CFG_TOKEN_VFS		"vfs"
#define CFG_TOKEN_IDX		"idx"
#define CFG_TOKEN_IF_MTU	"mtu"
#define CFG_TOKEN_IF_MAC_ADDR	"mac_addr"
#define CFG_TOKEN_IF_LSTATE	"link_state"
#define CFG_TOKEN_IF_RSTATE	"rx_state"
#define CFG_TOKEN_IF_AUTONEG	"autoneg"
#define CFG_TOKEN_IF_PMODE	"pause_mode"
#define CFG_TOKEN_IF_SPEED	"speed"
#define CFG_TOKEN_IF_SMODES	"supported_modes"
#define CFG_TOKEN_IF_AMODES	"advertised_modes"

static void print_config()
{
	struct cp_lib_pem *pem;
	struct cp_lib_pf *pf;

	pem = lib_cfg.pems;
	while (pem) {
		pf = pem->pfs;
		while (pf) {
			CP_LIB_LOG(INFO, CONFIG, "[%d]:[%d]\n",
				   pem->idx, pf->idx);
			pf = pf->next;
		}
		pem = pem->next;
	}
}

static struct cp_lib_pem *create_pem(int idx)
{
	struct cp_lib_pem *pem, *p;

	pem = calloc(1, sizeof(struct cp_lib_pem));
	if (!pem)
		return NULL;

	pem->idx = idx;
	if(lib_cfg.pems) {
		p = lib_cfg.pems;
		while (p->next)
			p = p->next;

		p->next = pem;
	} else {
		lib_cfg.pems = pem;
	}
	lib_cfg.npem++;

	return pem;
}

static struct cp_lib_pem *get_pem(int idx)
{
	struct cp_lib_pem *pem;

	if (!lib_cfg.pems)
		return NULL;

	pem = lib_cfg.pems;
	while (pem) {
		if (pem->idx == idx)
			return pem;
		pem = pem->next;
	}

	return NULL;
}

static struct cp_lib_pf *create_pf(struct cp_lib_pem *pemcfg, int idx)
{
	struct cp_lib_pf *pf, *p;

	pf = calloc(1, sizeof(struct cp_lib_pf));
	if (!pf)
		return NULL;

	pf->idx = idx;
	if(pemcfg->pfs) {
		p = pemcfg->pfs;
		while (p->next)
			p = p->next;

		p->next = pf;
	} else {
		pemcfg->pfs = pf;
	}
	pemcfg->npf++;

	return pf;
}

static struct cp_lib_pf *get_pf(struct cp_lib_pem *pemcfg, int idx)
{
	struct cp_lib_pf *pf;

	if (!pemcfg->pfs)
		return NULL;

	pf = pemcfg->pfs;
	while (pf) {
		if (pf->idx == idx)
			return pf;
		pf = pf->next;
	}

	return NULL;
}

static int parse_pem(config_setting_t *pem, struct cp_lib_pem *pemcfg)
{
	config_setting_t *pfs, *pf;
	int npfs, i, idx;
	struct cp_lib_pf *pfcfg;

	pfs = config_setting_get_member(pem, CFG_TOKEN_PFS);
	if (!pfs)
		return 0;

	npfs = config_setting_length(pfs);
	for (i=0; i<npfs; i++) {
		pf = config_setting_get_elem(pfs, i);
		if (!pf)
			continue;
		if (config_setting_lookup_int(pf, CFG_TOKEN_IDX, &idx) ==
		    CONFIG_FALSE)
			continue;

		pfcfg = get_pf(pemcfg, idx);
		if (!pfcfg) {
			pfcfg = create_pf(pemcfg, idx);
			if (!pfcfg) {
				CP_LIB_LOG(ERR, CONFIG, "Oom for pem[%d]pf[%d]\n",
			   		   pemcfg->idx, idx);
				continue;
			}
		}
	}

	return 0;
}

static int parse_pems(config_setting_t *pems)
{
	config_setting_t *pem;
	int npems, i, idx, err;
	struct cp_lib_pem *pemcfg;

	npems = config_setting_length(pems);
	for (i=0; i<npems; i++) {
		pem = config_setting_get_elem(pems, i);
		if (!pem)
			continue;
		if (config_setting_lookup_int(pem, CFG_TOKEN_IDX, &idx) ==
		    CONFIG_FALSE)
			continue;
		pemcfg = get_pem(idx);
		if (!pemcfg) {
			pemcfg = create_pem(idx);
			if (!pemcfg) {
				CP_LIB_LOG(ERR, CONFIG, "Oom for pem[%d]\n", idx);
				continue;
			}
		}
		err = parse_pem(pem, pemcfg);
		if (err)
			return err;
	}

	return 0;
}

static int parse_base_config(const char* cfg_file_path)
{
	config_setting_t *lcfg, *pems;
	config_t fcfg;
	int err;

	CP_LIB_LOG(INFO, CONFIG, "base config file : %s\n", cfg_file_path);
	config_init(&fcfg);
	if (!config_read_file(&fcfg, cfg_file_path)) {
		CP_LIB_LOG(ERR, CONFIG, "%s:%d - %s\n",
			   config_error_file(&fcfg),
			   config_error_line(&fcfg),
			   config_error_text(&fcfg));
		config_destroy(&fcfg);
		return(EXIT_FAILURE);
	}

	lcfg = config_lookup(&fcfg, CFG_TOKEN_SOC);
	if (!lcfg) {
		config_destroy(&fcfg);
		return -EINVAL;
	}

	pems = config_setting_get_member(lcfg, CFG_TOKEN_PEMS);
	if (pems) {
		err = parse_pems(pems);
		if (err) {
			config_destroy(&fcfg);
			return err;
		}
	}

	config_destroy(&fcfg);

	return 0;
}

int lib_config_init(const char *cfg_file_path)
{
	config_setting_t *lcfg, *pems;
	const char *str;
	config_t fcfg;
	int err;

	CP_LIB_LOG(INFO, CONFIG, "config file : %s\n", cfg_file_path);
	config_init(&fcfg);
	if (!config_read_file(&fcfg, cfg_file_path)) {
		CP_LIB_LOG(ERR, CONFIG, "%s:%d - %s\n",
			   config_error_file(&fcfg),
			   config_error_line(&fcfg),
			   config_error_text(&fcfg));
		config_destroy(&fcfg);
		return -EINVAL;
	}

	lcfg = config_lookup(&fcfg, CFG_TOKEN_SOC);
	if (!lcfg) {
		config_destroy(&fcfg);
		return -EINVAL;
	}

	if (config_setting_lookup_string(lcfg, CFG_TOKEN_BASE_SOC, &str)) {
		err = parse_base_config(str);
		if (err) {
			config_destroy(&fcfg);
			return err;
		}
	}

	pems = config_setting_get_member(lcfg, CFG_TOKEN_PEMS);
	if (pems) {
		err = parse_pems(pems);
		if (err) {
			config_destroy(&fcfg);
			return err;
		}
	}

	config_destroy(&fcfg);

	print_config();

	return 0;
}

int lib_config_uninit()
{
	struct cp_lib_pem *pem, *pp;
	struct cp_lib_pf *pf, *pfp;

	CP_LIB_LOG(INFO, CONFIG, "uninit\n");
	pem = lib_cfg.pems;
	while (pem) {
		pf = pem->pfs;
		while (pf) {
			pfp = pf->next;
			free(pf);
			pf = pfp;
		}
		pp = pem->next;
		free(pem);
		pem = pp;
	}

	return 0;
}
