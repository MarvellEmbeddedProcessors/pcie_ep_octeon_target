/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#include <stdint.h>

#include "compat.h"
#include "l2fwd_main.h"
#include "l2fwd_data.h"
#include "l2fwd_config.h"
#include "data.h"
#include "poll_mode.h"

#define RTE_LOGTYPE_L2FWD_DATA	RTE_LOGTYPE_USER1

#define MAX_NUM_SEG_PER_PKT	16
#define MEMPOOL_CACHE_SIZE	256
#define RX_DESC_DEFAULT		1024
#define TX_DESC_DEFAULT		1024

/* port statistics */
struct data_port_statistics port_stats[RTE_MAX_ETHPORTS][RTE_MAX_LCORE];

/* tx buffers */
struct rte_eth_dev_tx_buffer *tx_buffer[RTE_MAX_ETHPORTS][RTE_MAX_LCORE];

/* port forwarding table */
struct data_port_fwd_info data_fwd_table[RTE_MAX_ETHPORTS + 1];

/* number of tx queues per port */
uint16_t num_tx_queues;

/* number of rx queues per port */
uint16_t num_rx_queues;

/* Number of rx descriptors */
static uint16_t nb_rxd = RX_DESC_DEFAULT;

/* Number of tx descriptors */
static uint16_t nb_txd = TX_DESC_DEFAULT;

/* Common mempool */
static struct rte_mempool *pktmbuf_pool;

/* poll mode ops */
static struct poll_mode_ops *pm_ops;

/* data fn ops */
static struct data_fn_ops *fn_ops[L2FWD_FN_TYPE_MAX] = { 0 };

static uint8_t default_key[] = {
	0xFE, 0xED, 0x0B, 0xAD, 0xFE, 0xED, 0x0B, 0xAD,
	0xAD, 0x0B, 0xED, 0xFE, 0xAD, 0x0B, 0xED, 0xFE,
	0x13, 0x57, 0x9B, 0xEF, 0x24, 0x68, 0xAC, 0x0E,
	0x91, 0x72, 0x53, 0x11, 0x82, 0x64, 0x20, 0x44,
	0x12, 0xEF, 0x34, 0xCD, 0x56, 0xBC, 0x78, 0x9A,
	0x9A, 0x78, 0xBC, 0x56, 0xCD, 0x34, 0xEF, 0x12
};

static struct rte_eth_conf default_port_conf = {
	.rxmode = {
		.mq_mode = L2FWD_PCIE_EP_ETH_MQ_RX_RSS,
#if RTE_VERSION_NUM(22, 11, 0, 0) > L2FWD_PCIE_EP_RTE_VERSION
		.split_hdr_size = 0,
#endif
		.offloads = (L2FWD_PCIE_EP_ETH_RX_OFFLOAD_TCP_CKSUM |
			     L2FWD_PCIE_EP_ETH_RX_OFFLOAD_IPV4_CKSUM |
			     L2FWD_PCIE_EP_ETH_RX_OFFLOAD_UDP_CKSUM),
			     /* |
			      * L2FWD_PCIE_EP_ETH_RX_OFFLOAD_JUMBO_FRAME),
			      */
	},
	.rx_adv_conf = {
		.rss_conf = {
			.rss_key = default_key,
			.rss_hf = (L2FWD_PCIE_EP_ETH_RSS_IP |
				   L2FWD_PCIE_EP_ETH_RSS_UDP |
				   L2FWD_PCIE_EP_ETH_RSS_TCP),
		}
	},
	.txmode = {
		.mq_mode = L2FWD_PCIE_EP_ETH_MQ_TX_NONE,
		.offloads = (L2FWD_PCIE_EP_ETH_TX_OFFLOAD_TCP_CKSUM |
			     L2FWD_PCIE_EP_ETH_TX_OFFLOAD_IPV4_CKSUM |
			     L2FWD_PCIE_EP_ETH_TX_OFFLOAD_UDP_CKSUM),
	}
};

/* template for callback function */
typedef int (*configured_port_callback_t)(unsigned int port, void *ctx);

static int iterate_configured_ports(configured_port_callback_t fn, void *ctx)
{
	int port, err;

	for (port = 0; port < RTE_MAX_ETHPORTS; port++) {
		if (data_fwd_table[port].fn_ops != fn_ops[L2FWD_FN_TYPE_NIC])
			continue;

		err = fn(port, ctx);
		if (err < 0)
			return err;
	}

	return 0;
}

static void drop_pkt(uint16_t port, void *m, uint16_t queue)
{
	struct rte_mbuf *mbuf = (struct rte_mbuf *)m;
	struct rte_eth_dev_tx_buffer *txb;

	txb = tx_buffer[mbuf->port][queue];
	txb->error_callback(&mbuf, 1, txb->error_userdata);
}

static int calculate_mbuf_size(void)
{
	uint16_t nb_min_segs = MAX_NUM_SEG_PER_PKT;
	uint32_t max_pktlen = 0;
	unsigned int portid;
	int ret;

	/* Find maximum packet size and minimum packet segments from
	 * dev_info for each port.
	 * pool buffer size = max_pkt_size / min_num_segs
	 */
	RTE_ETH_FOREACH_DEV(portid) {
		struct rte_eth_dev_info dev_info;
		struct rte_eth_desc_lim *lim;

		ret = rte_eth_dev_info_get(portid, &dev_info);
		if (ret != 0)
			continue;

		lim = &dev_info.rx_desc_lim;
		if (lim->nb_mtu_seg_max)
			nb_min_segs = RTE_MIN(lim->nb_mtu_seg_max, nb_min_segs);

		lim = &dev_info.tx_desc_lim;
		if (lim->nb_mtu_seg_max)
			nb_min_segs = RTE_MIN(lim->nb_mtu_seg_max, nb_min_segs);

		max_pktlen = RTE_MAX(dev_info.max_rx_pktlen, max_pktlen);
	}
	return ((max_pktlen / nb_min_segs) +
		((max_pktlen % nb_min_segs) != 0) +
		RTE_PKTMBUF_HEADROOM);
}

static int create_mbuf_pool(void)
{
	uint16_t nb_ports = rte_eth_dev_count_avail();
	unsigned int nb_lcores = rte_lcore_count();
	unsigned int nb_mbufs;
	uint32_t mbuf_sz;

	nb_mbufs = RTE_MAX(nb_ports *
			    (nb_rxd +
			     nb_txd +
			     MAX_PKT_BURST +
			     (nb_lcores * MEMPOOL_CACHE_SIZE)),
			   8192U);
	mbuf_sz = calculate_mbuf_size();
	/* create the mbuf pool */
	pktmbuf_pool = rte_pktmbuf_pool_create("mbuf_pool",
					       nb_mbufs,
					       MEMPOOL_CACHE_SIZE,
					       0,
					       mbuf_sz,
					       rte_socket_id());
	if (pktmbuf_pool == NULL) {
		RTE_LOG(ERR, L2FWD_DATA, "Cannot init mbuf pool.\n");
		return -ENOMEM;
	}

	return 0;
}

static int populate_port_conf(unsigned int portid,
			      struct rte_eth_dev_info *dev_info,
			      struct rte_eth_conf *port_conf)
{
	*port_conf = default_port_conf;

	if (!(dev_info->rx_offload_capa & L2FWD_PCIE_EP_ETH_RX_OFFLOAD_SCATTER))
		RTE_LOG(INFO, L2FWD_DATA,
			"SCATTER not supported in driver\n");
	else
		port_conf->rxmode.offloads |= L2FWD_PCIE_EP_ETH_RX_OFFLOAD_SCATTER;

	if (!(dev_info->tx_offload_capa & L2FWD_PCIE_EP_ETH_TX_OFFLOAD_MULTI_SEGS))
		RTE_LOG(INFO, L2FWD_DATA,
			"MULTI_SEG not supported in driver\n");
	else
		port_conf->txmode.offloads |= L2FWD_PCIE_EP_ETH_TX_OFFLOAD_MULTI_SEGS;

	if (dev_info->rx_offload_capa & L2FWD_PCIE_EP_ETH_RX_OFFLOAD_RSS_HASH) {
		RTE_LOG(INFO, L2FWD_DATA, "setting rx hash for port id %d\n", portid);
		port_conf->rxmode.offloads |= L2FWD_PCIE_EP_ETH_RX_OFFLOAD_RSS_HASH;
	}

	port_conf->rx_adv_conf.rss_conf.rss_hf &= dev_info->flow_type_rss_offloads;
	if (port_conf->rx_adv_conf.rss_conf.rss_hf !=
	    default_port_conf.rx_adv_conf.rss_conf.rss_hf) {
		RTE_LOG(INFO, L2FWD_DATA,
			"Port %u modified RSS hash function based on "
			"hardware support, requested:%#"PRIx64" "
			"configured:%#"PRIx64"",
			portid,
			default_port_conf.rx_adv_conf.rss_conf.rss_hf,
			port_conf->rx_adv_conf.rss_conf.rss_hf);
	}

	if (dev_info->tx_offload_capa & L2FWD_PCIE_EP_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
		port_conf->txmode.offloads |= L2FWD_PCIE_EP_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

	l2fwd_configure_pkt_len(port_conf, dev_info);

	return 0;
}

static int configure_port_queues(unsigned int port,
				 struct rte_eth_dev_info *dev_info,
				 struct rte_eth_conf *port_conf)
{
	struct rte_eth_rxconf rxq_conf;
	struct rte_eth_txconf txq_conf;
	int i, err;

	for (i = 0; i < num_rx_queues; i++) {
		rxq_conf = dev_info->default_rxconf;
		rxq_conf.offloads = port_conf->rxmode.offloads;
		err = rte_eth_rx_queue_setup(port, i, nb_rxd,
					     rte_eth_dev_socket_id(port),
					     &rxq_conf,
					     pktmbuf_pool);
		if (err < 0) {
			RTE_LOG(ERR, L2FWD_DATA,
				"Failed to set rxq(%d) (%u): %s\n",
				 i, port, strerror(-err));
			return err;
		}
	}

	for (i = 0; i < num_tx_queues; i++) {
		txq_conf = dev_info->default_txconf;
		txq_conf.offloads = port_conf->txmode.offloads;
		err = rte_eth_tx_queue_setup(port, i, nb_txd,
					     rte_eth_dev_socket_id(port),
					     &txq_conf);
		if (err < 0) {
			RTE_LOG(ERR, L2FWD_DATA,
				"Failed to set txq(%d) (%u): %s\n",
				 i, port, strerror(-err));
			return err;
		}
		/* Initialize TX buffers */
		if (tx_buffer[port][i])
			rte_free(tx_buffer[port][i]);
		tx_buffer[port][i] = rte_zmalloc_socket(NULL,
							RTE_ETH_TX_BUFFER_SIZE(MAX_PKT_BURST),
							0,
							rte_eth_dev_socket_id(port));
		if (tx_buffer[port][i] == NULL) {
			RTE_LOG(ERR, L2FWD_DATA,
				"Failed to alloc tx buffer(%d) (%u): %s\n",
				 i, port, strerror(-err));
			goto tx_buffer_alloc_fail;
		}

		err = rte_eth_tx_buffer_set_err_callback(tx_buffer[port][i],
							 rte_eth_tx_buffer_count_callback,
							 &port_stats[port][i].dropped);
		if (err < 0) {
			RTE_LOG(ERR, L2FWD_DATA,
				"Failed to set err callback for tx buffer(%d) (%u): %s\n",
				 i, port, strerror(-err));
			goto tx_buffer_alloc_fail;
		}
	}

	return 0;

tx_buffer_alloc_fail:
	for (i = 0; i < num_tx_queues; i++)
		rte_free(tx_buffer[port][i]);

	return err;
}

static void set_ptypes(uint16_t portid)
{
	uint32_t ptype_mask = (RTE_PTYPE_L2_MASK | RTE_PTYPE_L3_MASK |
			       RTE_PTYPE_L4_MASK);
	int ret;

	ret = rte_eth_dev_get_supported_ptypes(portid, RTE_PTYPE_ALL_MASK,
					       NULL, 0);
	if (ret <= 0)  {
		RTE_LOG(INFO, L2FWD_DATA, "no ptypes supported\n");
		return;
	}
	ret++;

	uint32_t ptypes[ret];

	ret = rte_eth_dev_set_ptypes(portid, ptype_mask, ptypes, ret);
	if (ret < 0) {
		RTE_LOG(INFO, L2FWD_DATA,
			"Unable to set requested ptypes for Port %d\n", portid);
		return;
	}
}

static int configure_port(unsigned int port)
{
	struct rte_eth_dev_info dev_info;
	struct rte_eth_conf port_conf;
	int err;

	err = rte_eth_dev_info_get(port, &dev_info);
	if (err < 0) {
		RTE_LOG(ERR, L2FWD_DATA,
			"Failed to get device info (%u): %s\n",
			port, strerror(-err));
		return err;
	}

	err = populate_port_conf(port, &dev_info, &port_conf);
	if (err < 0)
		return err;

	err = rte_eth_dev_configure(port, num_rx_queues, num_tx_queues, &port_conf);
	if (err < 0) {
		RTE_LOG(ERR, L2FWD_DATA,
			"Failed to configure device (%u): %s\n",
			port, strerror(-err));
		return err;
	}

	err = rte_eth_dev_set_mtu(port, dev_info.max_mtu);
	if (err < 0) {
		RTE_LOG(ERR, L2FWD_DATA,
			"Failed to set mtu (%u) to %d: %s\n",
			 port, dev_info.max_mtu, strerror(-err));
		return err;
	}

	err = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
	if (err < 0) {
		RTE_LOG(ERR, L2FWD_DATA,
			"Failed to adjust descriptor count (%u) to %d %d: %s\n",
			port, nb_rxd, nb_txd, strerror(-err));
		return err;
	}

	/* Update Hash with new RSS Key */
	port_conf.rx_adv_conf.rss_conf.rss_key_len = dev_info.hash_key_size;
	rte_eth_dev_rss_hash_update(port, &port_conf.rx_adv_conf.rss_conf);

	err = configure_port_queues(port, &dev_info, &port_conf);
	if (err < 0)
		return err;

	set_ptypes(port);

	return 0;
}

static void clear_fwd_entry(struct data_port_fwd_info *fwd)
{
	memset(fwd, 0, sizeof(struct data_port_fwd_info));
	fwd->dst = RTE_MAX_ETHPORTS;
	fwd->fn_ops = fn_ops[L2FWD_FN_TYPE_STUB];
}

static int connect_ports(unsigned int port, unsigned int dst_port)
{
	struct data_port_fwd_info *fwd = NULL;
	int err;

	if (port < RTE_MAX_ETHPORTS) {
		err = configure_port(port);
		if (err < 0)
			return err;

		fwd = &data_fwd_table[port];
		fwd->offload = false;
		fwd->fn_ops = fn_ops[L2FWD_FN_TYPE_NIC];
		fwd->dst = dst_port;
	}
	if (dst_port < RTE_MAX_ETHPORTS) {
		err = configure_port(dst_port);
		if (err < 0) {
			if (fwd)
				clear_fwd_entry(fwd);

			return err;
		}

		fwd = &data_fwd_table[dst_port];
		fwd->offload = false;
		fwd->fn_ops = fn_ops[L2FWD_FN_TYPE_NIC];
		fwd->dst = port;
	}

	return 0;
}

static inline int validate_port(struct data_port_fwd_info *fwd,
				unsigned int port, struct rte_pci_addr *dbdf)
{
	if (port == RTE_MAX_ETHPORTS) {
		/* dbdf is non-zero but could not find matching port */
		RTE_LOG(ERR, L2FWD_DATA,
			"Device not found [%d:%d:%d.%d]\n",
			dbdf->domain, dbdf->bus, dbdf->devid, dbdf->function);
		return -EINVAL;
	}
	if (fwd->fn_ops == fn_ops[L2FWD_FN_TYPE_NIC]) {
		/* entry is already configured for nic mode */
		RTE_LOG(ERR, L2FWD_DATA,
			"[%d:%d:%d.%d] already configured.\n",
			dbdf->domain, dbdf->bus, dbdf->devid, dbdf->function);
		return -EINVAL;
	}

	return 0;
}

static int init_valid_cfg(int pem_idx, int pf_idx, int vf_idx, void *ctx)
{
	struct data_port_fwd_info *src, *dst;
	unsigned int src_port, dst_port;
	struct l2fwd_config_fn *fn;
	int err;

	fn = (vf_idx < 0) ?
	      &l2fwd_cfg.pems[pem_idx].pfs[pf_idx].d :
	      &l2fwd_cfg.pems[pem_idx].pfs[pf_idx].vfs[vf_idx];

	if (is_zero_dbdf(&fn->to_host_dbdf))
		return 0;

	src_port = l2fwd_pcie_ep_find_port(&fn->to_host_dbdf);
	if (src_port == RTE_MAX_ETHPORTS) {
		RTE_LOG(ERR, L2FWD_DATA,
			"[%d][%d][%d] Device not found ["PCI_PRI_FMT"]\n",
			pem_idx, pf_idx, vf_idx,
			fn->to_host_dbdf.domain,
			fn->to_host_dbdf.bus,
			fn->to_host_dbdf.devid,
			fn->to_host_dbdf.function);
		return -EINVAL;
	}
	src = &data_fwd_table[src_port];
	err = validate_port(src, src_port, &fn->to_host_dbdf);
	if (err < 0)
		return err;

	dst_port = l2fwd_pcie_ep_find_port(&fn->to_wire_dbdf);
	dst = &data_fwd_table[dst_port];
	if (!is_zero_dbdf(&fn->to_wire_dbdf)) {
		err = validate_port(dst, dst_port, &fn->to_wire_dbdf);
		if (err < 0)
			return err;
	}

	err = connect_ports(src_port, dst_port);
	if (err < 0)
		return err;

	RTE_LOG(INFO, L2FWD_DATA,
		"[%d][%d][%d]: ["PCI_PRI_FMT"][%u] <---> ["PCI_PRI_FMT"][%u]\n",
		pem_idx, pf_idx, vf_idx, fn->to_host_dbdf.domain,
		fn->to_host_dbdf.bus, fn->to_host_dbdf.devid,
		fn->to_host_dbdf.function, src_port,
		fn->to_wire_dbdf.domain, fn->to_wire_dbdf.bus,
		fn->to_wire_dbdf.devid, fn->to_wire_dbdf.function, dst_port);

	return 0;
}

int free_configured_port(unsigned int port, void *ctx)
{
	int q;

	for (q = 0; q < num_tx_queues; q++)
		rte_free(tx_buffer[port][q]);

	return 0;
}

static int free_fwd_table(void)
{
	iterate_configured_ports(&free_configured_port, NULL);

	return 0;
}

static int init_fwd_table(void)
{
	int i, err;

	/* by default all port ops are stub */
	for (i = 0; i <= RTE_MAX_ETHPORTS; i++)
		clear_fwd_entry(&data_fwd_table[i]);

	num_tx_queues = num_rx_queues = rte_lcore_count() - 1;

	/* selectively set nic ops for valid dbdf's */
	err = for_each_valid_config_fn(&init_valid_cfg, NULL);
	if (err < 0)
		free_fwd_table();

	return err;
}

static struct data_ops ops = {
	.drop_pkt = drop_pkt
};

int l2fwd_data_init(void)
{
	int err;

	err = poll_mode_init(&ops, &pm_ops);
	if (err < 0)
		return err;

	err = data_stub_init(&ops, &fn_ops[L2FWD_FN_TYPE_STUB]);
	if (err < 0)
		goto data_stub_init_fail;

	err = data_nic_init(&ops, &fn_ops[L2FWD_FN_TYPE_NIC]);
	if (err < 0)
		goto data_nic_init_fail;

	err = create_mbuf_pool();
	if (err < 0)
		goto create_mbuf_pool_fail;

	err = init_fwd_table();
	if (err < 0)
		goto fwd_table_init_fail;

	return 0;

fwd_table_init_fail:
	rte_mempool_free(pktmbuf_pool);
create_mbuf_pool_fail:
	data_nic_uninit();
data_nic_init_fail:
	data_stub_uninit();
data_stub_init_fail:
	poll_mode_uninit();

	return err;
}

static int start_configured_port(unsigned int port, void *ctx)
{
	int err;

	err = rte_eth_dev_start(port);
	if (err < 0) {
		RTE_LOG(ERR, L2FWD_DATA, "Could not start (%d): %s\n",
			port, strerror(-err));
		return err;
	}

	/* This is required for interfaces to work with bridge on the host.
	 * Correct way to deal with it is for the host to set
	 * promiscuous mode through host ndo_ops.
	 * Remove this when we support ndo_ops to set IFF_PROMISC
	 */
	RTE_LOG(INFO, L2FWD_DATA, "Setting promiscuous mode (%u)\n", port);
	err = rte_eth_promiscuous_enable(port);
	if (err < 0)
		RTE_LOG(ERR, L2FWD_DATA,
			"Failed to set promiscuous mode (%u): %s\n",
			 port, strerror(-err));

	data_fwd_table[port].running = true;

	return 0;
}

static int stop_configured_port(unsigned int port, void *ctx)
{
	rte_eth_dev_stop(port);
	data_fwd_table[port].running = false;

	return 0;
}

int l2fwd_data_start(void)
{
	int err;

	err = iterate_configured_ports(&start_configured_port, NULL);
	if (err < 0)
		goto start_error;

	pm_ops->configure();
	pm_ops->start();

	return 0;

start_error:
	iterate_configured_ports(&stop_configured_port, NULL);

	return err;
}

int l2fwd_data_stop(void)
{
	pm_ops->stop();
	iterate_configured_ports(&stop_configured_port, NULL);

	return 0;
}

int l2fwd_data_start_port(struct rte_pci_addr *dbdf)
{
	int port, err;

	port = l2fwd_pcie_ep_find_port(dbdf);
	if (port == RTE_MAX_ETHPORTS)
		return -ENOENT;

	if (!data_port_is_configured(port))
		return -EINVAL;

	err = start_configured_port(port, NULL);
	if (err < 0)
		return err;

	return 0;
}

int l2fwd_data_stop_port(struct rte_pci_addr *dbdf)
{
	int port;

	port = l2fwd_pcie_ep_find_port(dbdf);
	if (port == RTE_MAX_ETHPORTS)
		return -ENOENT;

	if (!data_port_is_configured(port))
		return -EINVAL;

	stop_configured_port(port, NULL);

	return 0;
}

int l2fwd_data_clear_fwd_table(void)
{
	int i;

	for (i = 0; i <= RTE_MAX_ETHPORTS; i++) {
		if (data_fwd_table[i].running)
			return -EPERM;
	}

	free_fwd_table();
	for (i = 0; i <= RTE_MAX_ETHPORTS; i++)
		clear_fwd_entry(&data_fwd_table[i]);

	pm_ops->configure();

	return 0;
}

int l2fwd_data_add_fwd_table_entry(struct rte_pci_addr *port1,
				   struct rte_pci_addr *port2)
{
	int host_port, wire_port, dst_port, err;

	host_port = l2fwd_pcie_ep_find_port(port1);
	if (host_port == RTE_MAX_ETHPORTS)
		return -ENOENT;

	if (data_fwd_table[host_port].running)
		return -EPERM;

	dst_port = data_fwd_table[host_port].dst;
	if (data_fwd_table[dst_port].running)
		return -EPERM;

	wire_port = l2fwd_pcie_ep_find_port(port2);
	if (wire_port == RTE_MAX_ETHPORTS)
		return -ENOENT;

	if (data_fwd_table[wire_port].running)
		return -EPERM;

	dst_port = data_fwd_table[wire_port].dst;
	if (data_fwd_table[dst_port].running)
		return -EPERM;

	err = connect_ports(host_port, wire_port);
	if (err < 0)
		return err;

	start_configured_port(host_port, NULL);
	start_configured_port(wire_port, NULL);
	pm_ops->configure();

	return 0;
}

int l2fwd_data_del_fwd_table_entry(struct rte_pci_addr *port1,
				   struct rte_pci_addr *port2)
{
	int host_port, wire_port;

	host_port = l2fwd_pcie_ep_find_port(port1);
	if (host_port == RTE_MAX_ETHPORTS)
		return -ENOENT;

	wire_port = l2fwd_pcie_ep_find_port(port2);
	if (wire_port == RTE_MAX_ETHPORTS)
		return -ENOENT;

	/* Dont remove entries if ports are not interlinked */
	if (wire_port != data_fwd_table[host_port].dst ||
	    host_port != data_fwd_table[wire_port].dst)
		return -EINVAL;

	stop_configured_port(host_port, NULL);
	stop_configured_port(wire_port, NULL);

	clear_fwd_entry(&data_fwd_table[host_port]);
	clear_fwd_entry(&data_fwd_table[wire_port]);
	pm_ops->configure();

	return 0;
}

static int print_configured_port_stats(unsigned int port, void *ctx)
{
	static struct data_port_statistics prev_q_stats[RTE_MAX_ETHPORTS][RTE_MAX_LCORE];
	static struct data_port_statistics prev_port_stats[RTE_MAX_ETHPORTS];
	static const char *border = "########################";
	struct data_port_statistics cur_q_stats[RTE_MAX_LCORE] = { 0 };
	struct data_port_statistics cur_port_stats = { 0 };
	int i;

#define PS(x) ((x) / 10)
	RTE_LOG(INFO, L2FWD_DATA, "\n  %s NIC statistics for port %-2d %s\n",
		border, port, border);

	for (i = 0; i < num_rx_queues; i++) {
		cur_port_stats.tx += port_stats[port][i].tx;
		cur_port_stats.rx += port_stats[port][i].rx;
		cur_port_stats.dropped += port_stats[port][i].dropped;

		cur_q_stats[i].tx += port_stats[port][i].tx;
		cur_q_stats[i].rx += port_stats[port][i].rx;
		cur_q_stats[i].dropped += port_stats[port][i].dropped;

		RTE_LOG(INFO, L2FWD_DATA,
			"  [%d] RX-pkt: %-10"PRIu64" TX-pkt: %-10"PRIu64
			" Dropped:  %-"PRIu64"\n",
			i,
			PS(cur_q_stats[i].tx - prev_q_stats[port][i].tx),
			PS(cur_q_stats[i].rx - prev_q_stats[port][i].rx),
			PS(cur_q_stats[i].dropped - prev_q_stats[port][i].dropped));

	}
	RTE_LOG(INFO, L2FWD_DATA,
		"  RX-packets: %-10"PRIu64" TX-packets: %-10"PRIu64" Dropped:  "
		"%-"PRIu64"\n",
		PS(cur_port_stats.tx - prev_port_stats[port].tx),
		PS(cur_port_stats.rx - prev_port_stats[port].rx),
		PS(cur_port_stats.dropped - prev_port_stats[port].dropped));

	memcpy(&prev_q_stats[port], &cur_q_stats, sizeof(cur_q_stats));
	memcpy(&prev_port_stats[port], &cur_port_stats, sizeof(cur_port_stats));

	return 0;
}

int l2fwd_data_print_stats(void)
{
	const char top_left[] = { 27, '[', '1', ';', '1', 'H', '\0' };
	const char clr[] = { 27, '[', '2', 'J', '\0' };

	/* Clear screen and move to top left */
	RTE_LOG(INFO, L2FWD_DATA, "%s%s", clr, top_left);
	RTE_LOG(INFO, L2FWD_DATA,
		"\nPort statistics ====================================");
	iterate_configured_ports(&print_configured_port_stats, NULL);

	return 0;
}

int l2fwd_data_uninit(void)
{
	poll_mode_uninit();
	data_nic_uninit();
	data_stub_uninit();

	free_fwd_table();

	return 0;
}

int data_port_is_configured(unsigned int port)
{
	return (data_fwd_table[port].fn_ops == fn_ops[L2FWD_FN_TYPE_NIC]);
}
