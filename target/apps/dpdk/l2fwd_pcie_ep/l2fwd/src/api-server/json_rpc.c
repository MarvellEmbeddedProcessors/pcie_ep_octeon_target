/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#include <unistd.h>
#include <cJSON.h>

#include "compat.h"
#include "server.h"
#include "json_rpc.h"

#define RTE_LOGTYPE_L2FWD_API_SERVER_JSON_RPC	RTE_LOGTYPE_USER1

#define JSON_RPC_FIELD_JSONRPC			"jsonrpc"
#define JSON_RPC_FIELD_ID			"id"
#define JSON_RPC_FIELD_ERROR			"error"
#define JSON_RPC_FIELD_RESULT			"result"
#define JSON_RPC_FIELD_METHOD			"method"
#define JSON_RPC_FIELD_PARAMS			"params"

#define JSON_RPC_ERROR_FIELD_CODE		"code"
#define JSON_RPC_ERROR_FIELD_MSG		"message"

#define JSON_RPC_VALUE_JSONRPC			"2.0"

#define JSON_BUF_LEN				512

int json_rpc_send_error(int fd, int code, const char *message, cJSON *id)
{
	char buf[JSON_BUF_LEN];
	int n;

	n = snprintf(buf, JSON_BUF_LEN,
		     "{\"jsonrpc\": \"2.0\","
		     "\"error\": {\"code\": %d, \"message\": \"%s\"}",
		     code, message);
	if (!id)
		n += snprintf(buf + n, (JSON_BUF_LEN - n), ",\"id\": null");
	else if (cJSON_IsString(id))
		n += snprintf(buf + n, (JSON_BUF_LEN - n), ",\"id\": \"%s\"",
			      cJSON_GetStringValue(id));
	else if (cJSON_IsNumber(id))
		n += snprintf(buf + n, (JSON_BUF_LEN - n), ",\"id\": \"%d\"",
			      (int)id->valuedouble);

	n += snprintf(buf + n, (JSON_BUF_LEN - n), "}\n");
	write(fd, buf, n);

	return 0;
}

int json_rpc_send_result(int fd, int code, cJSON *id)
{
	char buf[JSON_BUF_LEN];
	int n;

	n = snprintf(buf, JSON_BUF_LEN,
		     "{\"jsonrpc\": \"2.0\",\"result\": %d}",
		     code);
	if (!id)
		n += snprintf(buf + n, (JSON_BUF_LEN - n), ",\"id\": null");
	else if (cJSON_IsString(id))
		n += snprintf(buf + n, (JSON_BUF_LEN - n), ",\"id\": \"%s\"",
			      cJSON_GetStringValue(id));
	else if (cJSON_IsNumber(id))
		n += snprintf(buf + n, (JSON_BUF_LEN - n), ",\"id\": \"%d\"",
			      (int)id->valuedouble);

	n += snprintf(buf + n, (JSON_BUF_LEN - n), "}\n");
	write(fd, buf, n);

	return 0;
}

int json_rpc_get_int_param(cJSON *params, const char *name, int index, int *val)
{
	cJSON *param;

	param = (cJSON_IsArray(params) == cJSON_True) ?
		 cJSON_GetArrayItem(params, index) :
		 cJSON_GetObjectItem(params, name);
	if (!param)
		return -EINVAL;

	*val = (int)param->valuedouble;

	return 0;
}

int json_rpc_get_str_param(cJSON *params, const char *name, int index, char **val)
{
	cJSON *param;

	param = (cJSON_IsArray(params) == cJSON_True) ?
		 cJSON_GetArrayItem(params, index) :
		 cJSON_GetObjectItem(params, name);
	if (!param)
		return -EINVAL;

	*val = cJSON_GetStringValue(param);

	return 0;
}

static int process_request(int fd, cJSON *request)
{
	cJSON *jsonrpc, *id, *method;

	id = cJSON_GetObjectItem(request, JSON_RPC_FIELD_ID);
	if (!id) {
		json_rpc_send_error(fd,
				    JSON_RPC_ERROR_CODE_INVALID_REQUEST,
				    JSON_RPC_ERROR_MSG_INVALID_REQ,
				    id);
		return -EINVAL;
	}

	jsonrpc = cJSON_GetObjectItem(request, JSON_RPC_FIELD_JSONRPC);
	if (!jsonrpc) {
		json_rpc_send_error(fd,
				    JSON_RPC_ERROR_CODE_INVALID_REQUEST,
				    JSON_RPC_ERROR_MSG_INVALID_REQ,
				    id);
		return -EINVAL;
	}

	method = cJSON_GetObjectItem(request, JSON_RPC_FIELD_METHOD);
	if (!method) {
		json_rpc_send_error(fd,
				    JSON_RPC_ERROR_CODE_INVALID_REQUEST,
				    JSON_RPC_ERROR_MSG_INVALID_REQ,
				    id);
		return -EINVAL;
	}

	return api_process_method(fd,
				  method,
				  cJSON_GetObjectItem(request,
						      JSON_RPC_FIELD_PARAMS),
				  id);
}

static int json_rpc_connection_cb(int fd)
{
	const char *end_ptr = NULL;
	cJSON *root;
	int n_read, err;
	char buffer[JSON_BUF_LEN];

	n_read = read(fd, buffer, JSON_BUF_LEN);
	if (n_read <= 0)
		return -EIO;

	buffer[n_read] = 0;
	root = cJSON_ParseWithOpts(buffer, &end_ptr, cJSON_False);
	if (!root) {
		json_rpc_send_error(fd,
				    JSON_RPC_ERROR_CODE_PARSE_ERROR,
				    JSON_RPC_ERROR_MSG_PARSE_ERROR,
				    NULL);
		return 0;
	}

	err = process_request(fd, root);
	cJSON_Delete(root);

	return err;
}

int json_rpc_init(on_connection_cb *cb)
{
	*cb = json_rpc_connection_cb;

	return 0;
}

int json_rpc_uninit(void)
{
	return 0;
}
