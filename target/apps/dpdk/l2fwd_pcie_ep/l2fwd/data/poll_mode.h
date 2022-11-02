/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __POLL_MODE_H__
#define __POLL_MODE_H__

struct poll_mode_ops {
	int (*start)(void);
	int (*pause)(unsigned int port);
	int (*resume)(unsigned int port);
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
int poll_mode_0_init(struct data_ops *ops, struct poll_mode_ops **pm_ops);

/* Uninitialize poll mode 0.
 *
 * return value: 0 on success, -errno on failure.
 */
int poll_mode_0_uninit(void);


/* Initialize poll mode 1.
 *
 * @param ops: [IN] non-null pointer to struct data_ops
 * @param pm_ops: [IN/OUT] non-null pointer to struct poll_mode_ops
 *
 * return value: 0 on success, -errno on failure.
 */
int poll_mode_1_init(struct data_ops *ops, struct poll_mode_ops **pm_ops);

/* Uninitialize poll mode 0.
 *
 * return value: 0 on success, -errno on failure.
 */
int poll_mode_1_uninit(void);

#endif /* __POLL_MODE_H__ */
