/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __L2FWD_H__
#define __L2FWD_H__

#ifndef BIT_ULL
#define BIT_ULL(nr) (1ULL << (nr))
#endif

/* l2fwd features */
#define L2FWD_FEATURE_DATA_PLANE	BIT_ULL(0)
#define L2FWD_FEATURE_CTRL_PLANE	BIT_ULL(1)
#define L2FWD_FEATURE_API_SERVER	BIT_ULL(2)
#define L2FWD_FEATURE_PKT_CAPTURE	BIT_ULL(3)

#define L2FWD_FEATURE(features, f)	((features) & (f))

struct l2fwd_user_config {
	/* path to config file */
	char cfg_file_path[256];
	/* features to be enabled L2FWD_FEATURE_* */
	uint64_t features;
};

/* Initialize l2fwd.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_init(struct l2fwd_user_config *user_cfg);

/* Start l2fwd.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_start(void);

/* Poll l2fwd.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_poll(void);

/* Print l2fwd stats.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_print_stats(void);

/* Handle SIGALRM in l2fwd.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_sigalrm(void);

/* Stop l2fwd.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_stop(void);

/* UnInitialize control plane.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_uninit(void);

#endif /* __L2FWD_H__ */
