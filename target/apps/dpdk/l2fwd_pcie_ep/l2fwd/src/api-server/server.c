/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#include <unistd.h>
#include <errno.h>
#include <cJSON.h>
#include <pthread.h>
#include <arpa/inet.h>
#include "compat.h"
#include "l2fwd_api_server.h"
#include "server.h"
#include "json_rpc.h"

#define RTE_LOGTYPE_L2FWD_API_SERVER	RTE_LOGTYPE_USER1

extern uint16_t api_srv_port;

/* thread for servicing connections */
static pthread_t conn_thread;

/* connection callback for json-rpc handler */
static on_connection_cb conn_cb;

/* socket accepting tcp clients */
static int sfd;

/* socket servicing connected client */
static int cfd;

struct l2fwd_api_server_ops *l2fwd_ops;

int l2fwd_api_server_init(struct l2fwd_api_server_ops *ops)
{
	sfd = 0;
	cfd = 0;
	conn_thread = 0;
	json_rpc_init(&conn_cb);
	l2fwd_ops = ops;

	return 0;
}

static inline void *get_in_addr(struct sockaddr *sa)
{
	return (sa->sa_family == AF_INET) ?
			 (void *)(&(((struct sockaddr_in *) sa)->sin_addr)) :
			 (void *)(&(((struct sockaddr_in6 *) sa)->sin6_addr));
}

static void *connection_thread_fn(void *arg)
{
	struct sockaddr_in peer_addr;
	socklen_t peer_sz = sizeof(peer_addr);
	char s[INET6_ADDRSTRLEN];
	void *in_addr;
	int err;

	while (true) {
		cfd = accept(sfd, (struct sockaddr *) &peer_addr, &peer_sz);
		if (cfd < 0)
			break;

		in_addr = get_in_addr((struct sockaddr *)&peer_addr);
		inet_ntop(peer_addr.sin_family, in_addr, s, sizeof(s));
		RTE_LOG(INFO, L2FWD_API_SERVER, "Connection from: %s\n", s);

		do {
			err = conn_cb(cfd);
			if (err < 0) {
				RTE_LOG(ERR, L2FWD_API_SERVER,
					"Closing connection to: %s\n", s);
				close(cfd);
				cfd = 0;
				break;
			}
		} while (true);
	}

	return NULL;
}

int l2fwd_api_server_start(void)
{
	struct sockaddr_in server_addr = {
		.sin_family = AF_INET,
		.sin_port = api_srv_port ? htons(api_srv_port) : htons(L2FWD_API_SERVER_PORT),
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK)
	};
	struct sockaddr_in sockaddr;
	int err, yes = 1;
	socklen_t len;

	if (sfd || cfd || conn_thread)
		return 0;

	sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd <= 0) {
		RTE_LOG(ERR, L2FWD_API_SERVER,
			"Error in socket: %s\n", strerror(errno));
		return -errno;
	}

	err = setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
	if (err < 0) {
		RTE_LOG(ERR, L2FWD_API_SERVER,
			"Error in setsockopt: %s\n", strerror(errno));
		close(sfd);
		return -errno;
	}

	len = sizeof(struct sockaddr_in);
	err = bind(sfd, (struct sockaddr *)&server_addr, len);
	if (err < 0) {
		RTE_LOG(ERR, L2FWD_API_SERVER,
			"Error in bind: %s\n", strerror(errno));
		close(sfd);
		return -errno;
	}

	err = getsockname(sfd, (struct sockaddr *)&sockaddr, &len);
	if (err < 0) {
		RTE_LOG(ERR, L2FWD_API_SERVER,
			"Error in getsockname: %s\n", strerror(errno));
		close(sfd);
		return -errno;
	}

	err = listen(sfd, 1);
	if (err < 0) {
		RTE_LOG(ERR, L2FWD_API_SERVER,
			"Error in listen: %s\n", strerror(errno));
		close(sfd);
		return -errno;
	}

	err = pthread_create(&conn_thread, NULL, connection_thread_fn, NULL);
	if (err) {
		RTE_LOG(ERR, L2FWD_API_SERVER,
			"Error while starting serrver thread: %s\n",
			strerror(errno));
	}

	RTE_LOG(INFO, L2FWD_API_SERVER, "Listening on %s:%d\n",
		inet_ntoa(sockaddr.sin_addr), ntohs(sockaddr.sin_port));

	return 0;
}

int l2fwd_api_server_stop(void)
{
	shutdown(sfd, SHUT_RD);
	close(sfd);
	sfd = 0;
	close(cfd);
	cfd = 0;
	pthread_join(conn_thread, NULL);
	conn_thread = 0;
	RTE_LOG(INFO, L2FWD_API_SERVER, "Stopped\n");

	return 0;
}

int l2fwd_api_server_uninit(void)
{
	json_rpc_uninit();

	return 0;
}
