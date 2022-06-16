/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __OTX2_LOOP_H__
#define __OTX2_LOOP_H__

/* Initialize loop mode implementation.
 *
 * return value: 0 on success, -errno on failure.
 */
int otx2_loop_init(struct octep_cp_lib_cfg *p_cfg);

/* Poll for interrupts and host messages.
 *
 * @param max_events: Maximum number of events to process.
 *
 * return value: 0 on success, -errno on failure.
 */
int otx2_loop_poll(int max_events);

/* Process user interrupt signal.
 *
 * return value: 0 on success, -errno on failure.
 */
int otx2_loop_process_sigusr1();

/* Uninitialize loop mode implementation.
 *
 * return value: 0 on success, -errno on failure.
 */
int otx2_loop_uninit();

#endif /* __OTX2_LOOP_H__ */
