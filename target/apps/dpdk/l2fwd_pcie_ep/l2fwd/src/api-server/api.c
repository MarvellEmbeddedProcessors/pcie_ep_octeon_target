/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#include <unistd.h>
#include <cJSON.h>

#include "compat.h"
#include "l2fwd_main.h"
#include "l2fwd_api_server.h"
#include "server.h"
#include "json_rpc.h"

#define RTE_LOGTYPE_L2FWD_API_SERVER_API	RTE_LOGTYPE_USER1

#define L2FWD_METHOD_SET_FWD_STATE              "mrvl_l2fwd_set_fwd_state"
#define L2FWD_METHOD_CLEAR_FWD_TABLE            "mrvl_l2fwd_clear_fwd_table"
#define L2FWD_METHOD_ADD_FWD_PAIR               "mrvl_l2fwd_add_fwd_pair"
#define L2FWD_METHOD_DEL_FWD_PAIR               "mrvl_l2fwd_del_fwd_pair"

#define L2FWD_SET_FWD_STATE_PARAM_STATE		"state"

#define L2FWD_ADD_FWD_PAIR_PARAM_PORT1		"port1"
#define L2FWD_ADD_FWD_PAIR_PARAM_PORT2		"port2"

static int process_set_fwd_state(int fd, cJSON *params, cJSON *id)
{
	int err, state;

	err = json_rpc_get_int_param(params,
				     L2FWD_SET_FWD_STATE_PARAM_STATE,
				     0,
				     &state);
	if (err < 0) {
		json_rpc_send_error(fd,
				    JSON_RPC_ERROR_CODE_INVALID_PARAMS,
				    JSON_RPC_ERROR_MSG_INVALID_PARAMS,
				    id);
		return -EINVAL;
	}

	err = l2fwd_ops->set_fwd_state(state);
	if (err < 0) {
		json_rpc_send_error(fd,
				    JSON_RPC_ERROR_CODE_INTERNAL_ERROR,
				    JSON_RPC_ERROR_MSG_INTERNAL_ERROR,
				    id);
		return err;
	}

	return json_rpc_send_result(fd, 0, id);
}

static int process_clear_fwd_table(int fd, cJSON *params, cJSON *id)
{
	int err;

	err = l2fwd_ops->clear_fwd_table();
	if (err < 0) {
		json_rpc_send_error(fd,
				    JSON_RPC_ERROR_CODE_INTERNAL_ERROR,
				    JSON_RPC_ERROR_MSG_INTERNAL_ERROR,
				    id);
		return err;
	}

	return json_rpc_send_result(fd, err, id);
}

static int get_pci_addr_param(cJSON *params, const char *param, int index,
			      struct rte_pci_addr *addr)
{
	char *str_addr;
	int err;

	err = json_rpc_get_str_param(params, param, index, &str_addr);
	if (err < 0)
		return err;

	err = rte_pci_addr_parse(str_addr, addr);
	if (err < 0)
		return err;

	return 0;
}

static int process_add_fwd_pair(int fd, cJSON *params, cJSON *id)
{
	struct rte_pci_addr port1, port2;
	int err;

	err = get_pci_addr_param(params, L2FWD_ADD_FWD_PAIR_PARAM_PORT1, 0, &port1);
	if (err < 0) {
		json_rpc_send_error(fd,
				    JSON_RPC_ERROR_CODE_INVALID_PARAMS,
				    JSON_RPC_ERROR_MSG_INVALID_PARAMS,
				    id);
		return err;
	}

	err = get_pci_addr_param(params, L2FWD_ADD_FWD_PAIR_PARAM_PORT2, 1, &port2);
	if (err < 0) {
		json_rpc_send_error(fd,
				    JSON_RPC_ERROR_CODE_INVALID_PARAMS,
				    JSON_RPC_ERROR_MSG_INVALID_PARAMS,
				    id);
		return err;
	}

	err = l2fwd_ops->add_fwd_table_entry(&port1, &port2);
	if (err < 0) {
		json_rpc_send_error(fd,
				    JSON_RPC_ERROR_CODE_INTERNAL_ERROR,
				    JSON_RPC_ERROR_MSG_INTERNAL_ERROR,
				    id);
		return err;
	}

	return json_rpc_send_result(fd, err, id);
}

static int process_del_fwd_pair(int fd, cJSON *params, cJSON *id)
{
	struct rte_pci_addr port1, port2;
	int err;

	err = get_pci_addr_param(params, L2FWD_ADD_FWD_PAIR_PARAM_PORT1, 0, &port1);
	if (err < 0) {
		json_rpc_send_error(fd,
				    JSON_RPC_ERROR_CODE_INVALID_PARAMS,
				    JSON_RPC_ERROR_MSG_INVALID_PARAMS,
				    id);
		return err;
	}

	err = get_pci_addr_param(params, L2FWD_ADD_FWD_PAIR_PARAM_PORT2, 1, &port2);
	if (err < 0) {
		json_rpc_send_error(fd,
				    JSON_RPC_ERROR_CODE_INVALID_PARAMS,
				    JSON_RPC_ERROR_MSG_INVALID_PARAMS,
				    id);
		return err;
	}

	err = l2fwd_ops->del_fwd_table_entry(&port1, &port2);
	if (err < 0) {
		json_rpc_send_error(fd,
				    JSON_RPC_ERROR_CODE_INTERNAL_ERROR,
				    JSON_RPC_ERROR_MSG_INTERNAL_ERROR,
				    id);
		return err;
	}

	return json_rpc_send_result(fd, err, id);
}

int api_process_method(int fd, cJSON *method, cJSON *params, cJSON *id)
{
	char *name;
	int err = -EINVAL;

	name = cJSON_GetStringValue(method);
	if (!name) {
		json_rpc_send_error(fd,
				    JSON_RPC_ERROR_CODE_METHOD_NOT_FOUND,
				    JSON_RPC_ERROR_MSG_METHOD_NOT_FOUND,
				    id);
		return -EINVAL;
	}
	if (!strncasecmp(name,
			 L2FWD_METHOD_SET_FWD_STATE,
			 strlen(L2FWD_METHOD_SET_FWD_STATE)))
		err = process_set_fwd_state(fd, params, id);
	else if (!strncasecmp(name,
			      L2FWD_METHOD_CLEAR_FWD_TABLE,
			      strlen(L2FWD_METHOD_CLEAR_FWD_TABLE)))
		err = process_clear_fwd_table(fd, params, id);
	else if (!strncasecmp(name,
			      L2FWD_METHOD_ADD_FWD_PAIR,
			      strlen(L2FWD_METHOD_ADD_FWD_PAIR)))
		err = process_add_fwd_pair(fd, params, id);
	else if (!strncasecmp(name,
			      L2FWD_METHOD_DEL_FWD_PAIR,
			      strlen(L2FWD_METHOD_DEL_FWD_PAIR)))
		err = process_del_fwd_pair(fd, params, id);
	else
		json_rpc_send_error(fd,
				    JSON_RPC_ERROR_CODE_METHOD_NOT_FOUND,
				    JSON_RPC_ERROR_MSG_METHOD_NOT_FOUND,
				    id);

	return err;
}
