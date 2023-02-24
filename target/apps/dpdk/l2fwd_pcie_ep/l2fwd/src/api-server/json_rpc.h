/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __JSON_RPC_H__
#define __JSON_RPC_H__

/* json-rpc 2.0 error codes */
#define JSON_RPC_ERROR_CODE_PARSE_ERROR		-32700
#define JSON_RPC_ERROR_CODE_INVALID_REQUEST	-32600
#define JSON_RPC_ERROR_CODE_METHOD_NOT_FOUND	-32601
#define JSON_RPC_ERROR_CODE_INVALID_PARAMS	-32603
#define JSON_RPC_ERROR_CODE_INTERNAL_ERROR	-32693

#define JSON_RPC_ERROR_MSG_PARSE_ERROR		"Parse error"
#define JSON_RPC_ERROR_MSG_INVALID_REQ		"Invalid request"
#define JSON_RPC_ERROR_MSG_METHOD_NOT_FOUND	"Method not found"
#define JSON_RPC_ERROR_MSG_INVALID_PARAMS	"Invalid param"
#define JSON_RPC_ERROR_MSG_INTERNAL_ERROR	"Internal error"

/* new tcp connection handler */
typedef int (*on_connection_cb)(int fd);

int json_rpc_init(on_connection_cb *cb);

int json_rpc_send_result(int fd, int code, cJSON *id);

int json_rpc_send_error(int fd, int code, const char *message, cJSON *id);

int json_rpc_get_int_param(cJSON *params, const char *name, int index, int *val);

int json_rpc_get_str_param(cJSON *params, const char *name, int index, char **val);

int json_rpc_uninit(void);

#endif /* __JSON_RPC_H__ */
