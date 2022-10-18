#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <libconfig.h>

#include "octep_cp_lib.h"
#include "app_config.h"

struct app_cfg cfg;

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

static void print_if(struct if_cfg *iface)
{
	printf("mac_addr: %02x:%02x:%02x:%02x:%02x:%02x\n",
	       iface->mac_addr[0], iface->mac_addr[1],
	       iface->mac_addr[2], iface->mac_addr[3],
	       iface->mac_addr[4], iface->mac_addr[5]);
	printf("mtu: %d, link: %d, rx: %d, autoneg: 0x%x\n",
	       iface->mtu, iface->link_state, iface->rx_state,
	       iface->autoneg);
	printf("pause_mode: 0x%x, speed: %d\n",
	       iface->pause_mode, iface->speed);
	printf("supported_modes: 0x%lx, advertised_modes: 0x%lx\n",
	       iface->supported_modes, iface->advertised_modes);
}

static void print_config()
{
	struct pem_cfg *pem;
	struct pf_cfg *pf;
	struct vf_cfg *vf;

	pem = cfg.pems;
	while (pem) {
		pf = pem->pfs;
		while (pf) {
			printf("[%d]:[%d]\n", pem->idx, pf->idx);
			print_if(&pf->iface);
			vf = pf->vfs;
			while (vf) {
				printf("[%d]:[%d]:[%d]\n",
				       pem->idx, pf->idx, vf->idx);
				print_if(&vf->iface);
				vf = vf->next;
			}
			pf = pf->next;
		}
		pem = pem->next;
	}
}

static struct pem_cfg *create_pem(int idx)
{
	struct pem_cfg *pem, *p;

	pem = calloc(sizeof(struct pem_cfg), 1);
	if (!pem)
		return NULL;

	pem->idx = idx;
	if(cfg.pems) {
		p = cfg.pems;
		while (p->next)
			p = p->next;

		p->next = pem;
	} else {
		cfg.pems = pem;
	}
	cfg.npem++;

	return pem;
}

static struct pem_cfg *get_pem(int idx)
{
	struct pem_cfg *pem;

	if (!cfg.pems)
		return NULL;

	pem = cfg.pems;
	while (pem) {
		if (pem->idx == idx)
			return pem;
		pem = pem->next;
	}

	return NULL;
}

static struct pf_cfg *create_pf(struct pem_cfg *pemcfg, int idx)
{
	struct pf_cfg *pf, *p;

	pf = calloc(sizeof(struct pf_cfg), 1);
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

static struct pf_cfg *get_pf(struct pem_cfg *pemcfg, int idx)
{
	struct pf_cfg *pf;

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

static struct vf_cfg *create_vf(struct pf_cfg *pfcfg, int idx)
{
	struct vf_cfg *vf, *p;

	vf = calloc(sizeof(struct vf_cfg), 1);
	if (!vf)
		return NULL;

	vf->idx = idx;
	if(pfcfg->vfs) {
		p = pfcfg->vfs;
		while (p->next)
			p = p->next;

		p->next = vf;
	} else {
		pfcfg->vfs = vf;
	}
	pfcfg->nvf++;

	return vf;
}

static struct vf_cfg *get_vf(struct pf_cfg *pfcfg, int idx)
{
	struct vf_cfg *vf;

	if (!pfcfg->vfs)
		return NULL;

	vf = pfcfg->vfs;
	while (vf) {
		if (vf->idx == idx)
			return vf;
		vf = vf->next;
	}

	return vf;
}

static int parse_if(config_setting_t *lcfg, struct if_cfg *iface)
{
	config_setting_t *mac;
	int ival, i, n;

	if (config_setting_lookup_int(lcfg, CFG_TOKEN_IF_MTU, &ival))
		iface->mtu = ival;

	mac = config_setting_get_member(lcfg, CFG_TOKEN_IF_MAC_ADDR);
	if (mac) {
		n = config_setting_length(mac);
		if (n > ETH_ALEN)
			n = ETH_ALEN;
		for (i=0; i<n; i++)
			iface->mac_addr[i] = config_setting_get_int_elem(mac,
									 i);
	}
	if (config_setting_lookup_int(lcfg, CFG_TOKEN_IF_LSTATE, &ival))
		iface->link_state = ival;
	if (config_setting_lookup_int(lcfg, CFG_TOKEN_IF_RSTATE, &ival))
		iface->rx_state = ival;
	if (config_setting_lookup_int(lcfg, CFG_TOKEN_IF_AUTONEG, &ival))
		iface->autoneg = ival;
	if (config_setting_lookup_int(lcfg, CFG_TOKEN_IF_PMODE, &ival))
		iface->pause_mode = ival;
	if (config_setting_lookup_int(lcfg, CFG_TOKEN_IF_SPEED, &ival))
		iface->speed = ival;
	if (config_setting_lookup_int(lcfg, CFG_TOKEN_IF_SMODES, &ival))
		iface->supported_modes = ival;
	if (config_setting_lookup_int(lcfg, CFG_TOKEN_IF_AMODES, &ival))
		iface->advertised_modes = ival;

	return 0;
}

static int parse_pf(config_setting_t *pf, struct pf_cfg *pfcfg)
{
	config_setting_t *vfs, *vf;
	int nvfs, i, idx, err;
	struct vf_cfg *vfcfg;

	err = parse_if(pf, &pfcfg->iface);
	if (err)
		return err;

	vfs = config_setting_get_member(pf, CFG_TOKEN_VFS);
	if (!vfs)
		return 0;

	nvfs = config_setting_length(vfs);
	for (i=0; i<nvfs; i++) {
		vf = config_setting_get_elem(vfs, i);
		if (!vf)
			continue;
		if (config_setting_lookup_int(vf, CFG_TOKEN_IDX, &idx) ==
		    CONFIG_FALSE)
			continue;
		vfcfg = get_vf(pfcfg, idx);
		if (!vfcfg) {
			vfcfg = create_vf(pfcfg, idx);
			if (!vfcfg) {
				printf( "Oom for pf[%d]vf[%d]\n",
					   pfcfg->idx, idx);
				continue;
			}
		}
		err = parse_if(vf, &vfcfg->iface);
		if (err)
			return err;
	}

	return 0;
}

static int parse_pem(config_setting_t *pem, struct pem_cfg *pemcfg)
{
	config_setting_t *pfs, *pf;
	int npfs, i, idx, err;
	struct pf_cfg *pfcfg;

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
				printf( "Oom for pem[%d]pf[%d]\n",
			   		   pemcfg->idx, idx);
				continue;
			}
		}
		err = parse_pf(pf, pfcfg);
		if (err)
			return err;
	}

	return 0;
}

static int parse_pems(config_setting_t *pems)
{
	config_setting_t *pem;
	int npems, i, idx, err;
	struct pem_cfg *pemcfg;

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
				printf( "Oom for pem[%d]\n", idx);
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

	printf("base config file : %s\n", cfg_file_path);
	config_init(&fcfg);
	if (!config_read_file(&fcfg, cfg_file_path)) {
		printf( "%s:%d - %s\n",
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

int app_config_init(const char *cfg_file_path)
{
	config_setting_t *lcfg, *pems;
	const char *str;
	config_t fcfg;
	int err;

	printf("config file : %s\n", cfg_file_path);
	config_init(&fcfg);
	if (!config_read_file(&fcfg, cfg_file_path)) {
		printf( "%s:%d - %s\n",
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

int app_config_get_if_from_msg_info(union octep_cp_msg_info *ctx_info,
				    union octep_cp_msg_info *msg_info,
				    struct if_cfg **iface,
				    struct if_stats **ifstats)
{
	struct pem_cfg *pem = cfg.pems;
	struct pf_cfg *pf;
	struct vf_cfg *vf;

	while (pem) {
		if (pem->idx == ctx_info->s.pem_idx) {
			pf = pem->pfs;
			while (pf) {
				if (pf->idx == ctx_info->s.pf_idx) {
					if (!msg_info->s.is_vf) {
						printf("pem[%u] pf[%u]\n",
						       pem->idx, pf->idx);
						*iface = &pf->iface;
						*ifstats = &pf->ifstats;
						return 0;
					}
					vf = pf->vfs;
					while (vf) {
						if (vf->idx == msg_info->s.vf_idx) {
							printf("pem[%u] pf[%u] vf[%u]\n",
							       pem->idx, pf->idx, vf->idx);
							*iface = &vf->iface;
							*ifstats = &vf->ifstats;
							return 0;
						}
						vf = vf->next;
					}
				}
				pf = pf->next;
			}
		}
		pem = pem->next;
	}

	return -ENOENT;
}

int app_config_uninit()
{
	struct pem_cfg *pem, *pp;
	struct pf_cfg *pf, *pfp;
	struct vf_cfg *vf, *vfp;

	printf("config uninit\n");
	pem = cfg.pems;
	while (pem) {
		pf = pem->pfs;
		while (pf) {
			vf = pf->vfs;
			while (vf) {
				vfp = vf->next;
				free(vf);
				vf = vfp;
			}
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
