/* Copyright (c) 2020 Marvell.
 * SPDX-License-Identifier: GPL-2.0
 */

/* dpi vf internal
 */

#ifndef _DPI_VF_H_
#define _DPI_VF_H_

#include <linux/types.h>

struct dpivf_t {
	struct device *dev;
	void __iomem *reg_base;
	void __iomem *reg_base2;
	struct msix_entry *msix_entries;
	struct kobject *kobj;
	u16 domain;
	u16 vf_id;
	void *soc_priv;
};

extern unsigned int pem_num;
extern struct dpivf_t* shared_vf;

#endif /* _DPI_VF_H_ */
