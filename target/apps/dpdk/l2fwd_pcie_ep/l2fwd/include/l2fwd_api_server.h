/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __L2FWD_API_SERVER_H__
#define __L2FWD_API_SERVER_H__

/* Initialize api server.
 *
 * Initialize local data for handling api messages.
 * Global configuration can be used for initialization.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_api_server_init(void);

/* Start api server.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_api_server_start(void);

/* Stop api server.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_api_server_stop(void);

/* Uninitialize api server.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_api_server_uninit(void);

#endif /* __L2FWD_API_SERVER_H__ */
