/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2021 Marvell.
 */

#ifndef __L2FWD_PCIE_EP_COMPAT_H__
#define __L2FWD_PCIE_EP_COMPAT_H__

#include <rte_version.h>
#include <rte_log.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_pci.h>
#include <rte_bus_pci.h>
#include <rte_malloc.h>
#include <rte_lcore.h>

#define L2FWD_PCIE_EP_RTE_VERSION  RTE_VERSION_NUM(RTE_VER_YEAR, RTE_VER_MONTH, \
						   RTE_VER_MINOR, RTE_VER_RELEASE)

#if RTE_VERSION_NUM(21, 11, 0, 0) > L2FWD_PCIE_EP_RTE_VERSION
#define L2FWD_PCIE_EP_ETH_MQ_RX_NONE              ETH_MQ_RX_NONE

#define L2FWD_PCIE_EP_ETH_RX_OFFLOAD_TCP_CKSUM    DEV_RX_OFFLOAD_TCP_CKSUM
#define L2FWD_PCIE_EP_ETH_RX_OFFLOAD_VLAN_FILTER  DEV_RX_OFFLOAD_VLAN_FILTER
#define L2FWD_PCIE_EP_ETH_RX_OFFLOAD_IPV4_CKSUM	  DEV_RX_OFFLOAD_IPV4_CKSUM
#define L2FWD_PCIE_EP_ETH_RX_OFFLOAD_UDP_CKSUM	  DEV_RX_OFFLOAD_UDP_CKSUM
#define L2FWD_PCIE_EP_ETH_RX_OFFLOAD_SCATTER	  DEV_RX_OFFLOAD_SCATTER
#define L2FWD_PCIE_EP_ETH_RX_OFFLOAD_VLAN_STRIP	  DEV_RX_OFFLOAD_VLAN_STRIP
#define L2FWD_PCIE_EP_ETH_RX_OFFLOAD_JUMBO_FRAME  DEV_RX_OFFLOAD_JUMBO_FRAME
#define L2FWD_PCIE_EP_ETH_MQ_RX_RSS		  ETH_MQ_RX_RSS
#define L2FWD_PCIE_EP_ETH_RX_OFFLOAD_RSS_HASH	  DEV_RX_OFFLOAD_RSS_HASH

#define L2FWD_PCIE_EP_ETH_RSS_IP  ETH_RSS_IP
#define L2FWD_PCIE_EP_ETH_RSS_TCP ETH_RSS_TCP
#define L2FWD_PCIE_EP_ETH_RSS_UDP ETH_RSS_UDP

#define L2FWD_PCIE_EP_TX_IP_CKSUM		  PKT_TX_IP_CKSUM
#define L2FWD_PCIE_EP_TX_TCP_CKSUM		  PKT_TX_TCP_CKSUM
#define L2FWD_PCIE_EP_TX_UDP_CKSUM		  PKT_TX_UDP_CKSUM
#define L2FWD_PCIE_EP_TX_IPV4			  PKT_TX_IPV4
#define L2FWD_PCIE_EP_TX_IPV6			  PKT_TX_IPV6
#define L2FWD_PCIE_EP_RX_IP_CKSUM_GOOD		  PKT_RX_IP_CKSUM_GOOD
#define L2FWD_PCIE_EP_RX_L4_CKSUM_GOOD		  PKT_RX_L4_CKSUM_GOOD

#define L2FWD_PCIE_EP_ETH_MQ_TX_NONE ETH_MQ_TX_NONE

#define L2FWD_PCIE_EP_ETH_TX_OFFLOAD_MULTI_SEGS	    DEV_TX_OFFLOAD_MULTI_SEGS
#define L2FWD_PCIE_EP_ETH_TX_OFFLOAD_IPV4_CKSUM	    DEV_TX_OFFLOAD_IPV4_CKSUM
#define L2FWD_PCIE_EP_ETH_TX_OFFLOAD_UDP_CKSUM	    DEV_TX_OFFLOAD_UDP_CKSUM
#define L2FWD_PCIE_EP_ETH_TX_OFFLOAD_TCP_CKSUM	    DEV_TX_OFFLOAD_TCP_CKSUM
#define L2FWD_PCIE_EP_ETH_TX_OFFLOAD_MBUF_FAST_FREE DEV_TX_OFFLOAD_MBUF_FAST_FREE

#define L2FWD_PCIE_EP_ETH_LINK_DOWN	   ETH_LINK_DOWN
#define L2FWD_PCIE_EP_ETH_LINK_FULL_DUPLEX ETH_LINK_FULL_DUPLEX

#define L2FWD_PCIE_EP_ETHER_ADDR_PRT_FMT     "%02X:%02X:%02X:%02X:%02X:%02X"


static inline
int l2fwd_pcie_ep_get_pci_dev_addr(uint16_t port, struct rte_pci_addr *addr)
{
	struct rte_eth_dev_info dev_info;
	struct rte_pci_device *pdev;
	int err;

	err = rte_eth_dev_info_get(port, &dev_info);
	if (err < 0)
		return err;

	pdev = RTE_DEV_TO_PCI(dev_info.device);
	*addr = pdev->addr;

	return 0;
}

static void l2fwd_configure_pkt_len(struct rte_eth_conf *port_conf,
				    struct rte_eth_dev_info *dev_info)
{
	port_conf->rxmode.max_rx_pkt_len = dev_info->max_rx_pktlen;
	if (dev_info->max_rx_pktlen > RTE_ETHER_MAX_LEN)
		port_conf->rxmode.offloads |= DEV_RX_OFFLOAD_JUMBO_FRAME;
}

static inline
unsigned int l2fwd_pcie_ep_find_port(const struct rte_pci_addr *dbdf)
{
	struct rte_pci_addr addr;
	unsigned int portid;
	int err;

	RTE_ETH_FOREACH_DEV(portid) {
		err = l2fwd_pcie_ep_get_pci_dev_addr(portid, &addr);
		if (err < 0)
			continue;

		if (!rte_pci_addr_cmp(dbdf, &addr))
			return portid;
	}

	return RTE_MAX_ETHPORTS;
}

#else /* L2FWD_PCIE_EP_RTE_VERSION */

#define L2FWD_PCIE_EP_ETH_MQ_RX_NONE              RTE_ETH_MQ_RX_NONE

#define L2FWD_PCIE_EP_ETH_RX_OFFLOAD_TCP_CKSUM    RTE_ETH_RX_OFFLOAD_TCP_CKSUM
#define L2FWD_PCIE_EP_ETH_RX_OFFLOAD_VLAN_FILTER  RTE_ETH_RX_OFFLOAD_VLAN_FILTER
#define L2FWD_PCIE_EP_ETH_RX_OFFLOAD_IPV4_CKSUM	  RTE_ETH_RX_OFFLOAD_IPV4_CKSUM
#define L2FWD_PCIE_EP_ETH_RX_OFFLOAD_UDP_CKSUM	  RTE_ETH_RX_OFFLOAD_UDP_CKSUM
#define L2FWD_PCIE_EP_ETH_RX_OFFLOAD_SCATTER	  RTE_ETH_RX_OFFLOAD_SCATTER
#define L2FWD_PCIE_EP_ETH_RX_OFFLOAD_VLAN_STRIP	  RTE_ETH_RX_OFFLOAD_VLAN_STRIP
#define L2FWD_PCIE_EP_ETH_RX_OFFLOAD_JUMBO_FRAME  0
#define L2FWD_PCIE_EP_ETH_MQ_RX_RSS		  RTE_ETH_MQ_RX_RSS
#define L2FWD_PCIE_EP_ETH_RX_OFFLOAD_RSS_HASH	  RTE_ETH_RX_OFFLOAD_RSS_HASH

#define L2FWD_PCIE_EP_ETH_RSS_IP  RTE_ETH_RSS_IP
#define L2FWD_PCIE_EP_ETH_RSS_TCP RTE_ETH_RSS_TCP
#define L2FWD_PCIE_EP_ETH_RSS_UDP RTE_ETH_RSS_UDP

#define L2FWD_PCIE_EP_TX_IP_CKSUM		  RTE_MBUF_F_TX_IP_CKSUM
#define L2FWD_PCIE_EP_TX_TCP_CKSUM		  RTE_MBUF_F_TX_TCP_CKSUM
#define L2FWD_PCIE_EP_TX_UDP_CKSUM		  RTE_MBUF_F_TX_UDP_CKSUM
#define L2FWD_PCIE_EP_TX_IPV4			  RTE_MBUF_F_TX_IPV4
#define L2FWD_PCIE_EP_TX_IPV6			  RTE_MBUF_F_TX_IPV6
#define L2FWD_PCIE_EP_RX_IP_CKSUM_GOOD		  RTE_MBUF_F_RX_IP_CKSUM_GOOD
#define L2FWD_PCIE_EP_RX_L4_CKSUM_GOOD		  RTE_MBUF_F_RX_L4_CKSUM_GOOD

#define L2FWD_PCIE_EP_ETH_MQ_TX_NONE RTE_ETH_MQ_TX_NONE

#define L2FWD_PCIE_EP_ETH_TX_OFFLOAD_MULTI_SEGS	    RTE_ETH_TX_OFFLOAD_MULTI_SEGS
#define L2FWD_PCIE_EP_ETH_TX_OFFLOAD_IPV4_CKSUM	    RTE_ETH_TX_OFFLOAD_IPV4_CKSUM
#define L2FWD_PCIE_EP_ETH_TX_OFFLOAD_UDP_CKSUM	    RTE_ETH_TX_OFFLOAD_UDP_CKSUM
#define L2FWD_PCIE_EP_ETH_TX_OFFLOAD_TCP_CKSUM	    RTE_ETH_TX_OFFLOAD_TCP_CKSUM
#define L2FWD_PCIE_EP_ETH_TX_OFFLOAD_MBUF_FAST_FREE RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE

#define L2FWD_PCIE_EP_ETH_LINK_DOWN	   RTE_ETH_LINK_DOWN
#define L2FWD_PCIE_EP_ETH_LINK_FULL_DUPLEX RTE_ETH_LINK_FULL_DUPLEX

#define L2FWD_PCIE_EP_ETHER_ADDR_PRT_FMT    RTE_ETHER_ADDR_PRT_FMT

#if RTE_VERSION_NUM(22, 11, 0, 0) > L2FWD_PCIE_EP_RTE_VERSION
static inline
int l2fwd_pcie_ep_get_pci_dev_addr(uint16_t port, struct rte_pci_addr *addr)
{
	struct rte_eth_dev_info dev_info;
	struct rte_pci_device *pdev;
	int err;

	err = rte_eth_dev_info_get(port, &dev_info);
	if (err < 0)
		return err;

	pdev = RTE_DEV_TO_PCI(dev_info.device);
	*addr = pdev->addr;

	return 0;
}
#else
static inline
int l2fwd_pcie_ep_get_pci_dev_addr(uint16_t port, struct rte_pci_addr *addr)
{
	struct rte_eth_dev_info dev_info;
	int err;

	err = rte_eth_dev_info_get(port, &dev_info);
	if (err < 0)
		return err;

	/* TODO rte_bus_(rte_dev_bus(dev_info.device)); */
	return -ENOTSUP;
}
#endif


static void
l2fwd_configure_pkt_len(struct rte_eth_conf *port_conf, struct rte_eth_dev_info *dev_info)
{
	port_conf->rxmode.mtu = dev_info->max_mtu;
}

static inline
unsigned int l2fwd_pcie_ep_find_port(const struct rte_pci_addr *dbdf)
{
	struct rte_pci_addr addr;
	unsigned int portid;
	int err;

	RTE_ETH_FOREACH_DEV(portid) {
		err = l2fwd_pcie_ep_get_pci_dev_addr(portid, &addr);
		if (err < 0)
			continue;

		if (!rte_pci_addr_cmp(dbdf, &addr))
			return portid;
	}

	return RTE_MAX_ETHPORTS;
}

#endif /* L2FWD_PCIE_EP_RTE_VERSION */

#endif /* __L2FWD_PCIE_EP_COMPAT_H__ */
