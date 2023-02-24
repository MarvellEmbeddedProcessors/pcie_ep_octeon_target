/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __API_SERVER_H__
#define __API_SERVER_H__

extern struct l2fwd_api_server_ops *l2fwd_ops;

int api_process_method(int fd, cJSON *method, cJSON *params, cJSON *id);

#endif /* __API_SERVER_H__ */
