// SPDX-License-Identifier: (GPL-2.0)
/* Mgmt ethernet driver
 *
 * Copyright (C) 2015-2019 Marvell, Inc.
 */

u64 npa_alloc_buf(void);
u16 npa_pf_func(void);
int npa_aura_pool_init(int pool_size, int buf_size, struct device *dev);
