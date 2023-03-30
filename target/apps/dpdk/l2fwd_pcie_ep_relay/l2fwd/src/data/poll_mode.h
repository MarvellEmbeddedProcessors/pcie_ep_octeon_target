/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __POLL_MODE_H__
#define __POLL_MODE_H__

struct poll_mode_ops {
	int (*start)(void);
	int (*configure)(void);
	int (*stop)(void);
};

/** Poll mode operations */

/* Initialize poll mode 0.
 *
 * @param ops: [IN] non-null pointer to struct data_ops
 * @param ops: [IN/OUT] non-null pointer to struct poll_mode_ops
 *
 * return value: 0 on success, -errno on failure.
 */
int poll_mode_init(struct data_ops *ops, struct poll_mode_ops **pm_ops);

/* Uninitialize poll mode 0.
 *
 * return value: 0 on success, -errno on failure.
 */
int poll_mode_uninit(void);

#endif /* __POLL_MODE_H__ */
