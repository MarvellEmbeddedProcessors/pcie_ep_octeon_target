/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __OTX2_NIC_H__
#define __OTX2_NIC_H__

/* Initialize nic mode implementation.
 *
 * return value: 0 on success, -errno on failure.
 */
int otx2_nic_init(struct octep_cp_lib_cfg *p_cfg);

/* Poll for interrupts and host messages.
 *
 * @param max_events: Maximum number of events to process.
 *
 * return value: 0 on success, -errno on failure.
 */
int otx2_nic_poll(int max_events);

/* Process user interrupt signal.
 *
 * return value: 0 on success, -errno on failure.
 */
int otx2_nic_process_sigusr1();

/* Stop processing interrupts and host requests.
 *
 * return value: 0 on success, -errno on failure.
 */
int otx2_nic_stop();

/* Uninitialize nic mode implementation.
 *
 * return value: 0 on success, -errno on failure.
 */
int otx2_nic_uninit();

#endif /* __OTX2_NIC_H__ */
