/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __LOOP_H__
#define __LOOP_H__

/* Initialize loop mode implementation.
 *
 * return value: 0 on success, -errno on failure.
 */
int loop_init(char *cfg_file_path);

/* Poll for interrupts and host messages.
 *
 * @param user_ctx: User context.
 * @param msg: Message buffer.
 *
 * return value: size of response in words on success, -errno on failure.
 */
int loop_process_msg(void *user_ctx, void *msg);

/* Process user interrupt signal.
 *
 * return value: 0 on success, -errno on failure.
 */
int loop_process_sigusr1();

/* Uninitialize loop mode implementation.
 *
 * return value: 0 on success, -errno on failure.
 */
int loop_uninit();

#endif /* __LOOP_H__ */