/* Copyright (c) 2020 Marvell.
 * SPDX-License-Identifier: GPL-2.0
 */

/* Mgmt ethernet driver
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/highmem.h>

/* #include <mmio_api_arm.h> */
#include "mmio_api.h"
#include "dma_api.h"

MODULE_DESCRIPTION("A dma driver");
MODULE_LICENSE("GPL");

#define SLI0_S2M_REGX_ACC(i)    (0x874001000000UL |  (i << 4))
#define SLI0_M2S_MAC0_CTL	 0x874001002100UL

union sli_s2m_op_s {
	uint64_t u64;
	struct {
		uint64_t addr       :32;
		uint64_t region     :8;
		uint64_t did_hi     :4;
		uint64_t node       :2;
		uint64_t rsvd2      :1;
		uint64_t io         :1;
		uint64_t rsvd1      :16;
	} s;
} __packed;

#define CTYPE_PCI_MEMORY 0
#define CTYPE_PCI_CONF   1
#define CTYPE_PCI_IO     2

#define RWTYPE_RELAXED_ORDER 0
#define RWTYPE_NO_SNOOP      1

extern unsigned long part_num;

union sli0_s2m_regx_acc {
	uint64_t u64;
	struct {
		uint64_t ba         :32;
		uint64_t rsvd3      :8;
		uint64_t rtype      :2;
		uint64_t wtype      :2;
		uint64_t rsvd2      :4;
		uint64_t nmerge     :1;
		uint64_t epf        :3;
		uint64_t zero       :1;
		uint64_t ctype      :2;
		uint64_t rsvd1      :9;
	} s;
} __packed;

static void setup_s2m_regx_acc(void)
{
	union sli0_s2m_regx_acc  reg0_acc;
	uint64_t __iomem *reg;
	int i;

	for (i = 0; i < 256; i++) {
		reg0_acc.u64 = 0;
		reg0_acc.s.nmerge = 1;
		reg0_acc.s.ba = i;
		reg = ioremap(SLI0_S2M_REGX_ACC(i), 8);
		if (!reg) {
			printk(KERN_DEBUG "ioremap failed for addr 0x%lx\n",
			       SLI0_S2M_REGX_ACC(i));
			return;
		}
		writeq(reg0_acc.u64, reg);
		iounmap(reg);
	}
}

/* return cpu_addr to be used to read/write to a given
 * host phys_addr
 */
void __iomem *host_ioremap(host_dma_addr_t host_addr)
{
	void  __iomem *raddrp = NULL;
	void  __iomem *raddr = NULL;
	union sli_s2m_op_s  s2m_op;
	int index;

	/* printk(KERN_DEBUG "host_writel  host_addr 0x%llx val  %u\n",
		  host_addr, val); */
	if (part_num == CAVIUM_CPU_PART_T83) {
		index = host_addr >> 32;
		if (index > 255) {
			printk(KERN_ERR "phys addr too big 0x%llx\n", host_addr);
			return NULL;
		}
		s2m_op.u64 = 0;
		s2m_op.s.region = index;
		s2m_op.s.io = 1;
		s2m_op.s.did_hi = 8;
		s2m_op.s.addr = host_addr & ((1UL << 32) - 1);
		/* printk(KERN_DEBUG "s2m_op.u64 0x%016llx\n", s2m_op.u64); */
		raddrp = ioremap((s2m_op.u64 & (~(PAGE_SIZE - 1))), PAGE_SIZE);
		if (raddrp == NULL) {
			printk(KERN_ERR "ioremap failed\n");
			return NULL;
		}
		raddr = raddrp + (s2m_op.u64 & (PAGE_SIZE - 1));
	} else { /* 93XX */
		raddr = (void  __iomem *)host_addr;
	}
	return raddr;
}
EXPORT_SYMBOL(host_ioremap);

void host_iounmap(void __iomem *addr)
{
	iounmap(addr);
}
EXPORT_SYMBOL(host_iounmap);

void host_map_writel(host_dma_addr_t host_addr, uint32_t val)
{
	uint32_t *addr;

	addr = (uint32_t *)host_ioremap(host_addr);
	if (addr == NULL) {
		printk(KERN_ERR "ioremap failed\n");
		return;
	}
	writel(val, addr);
	host_iounmap(addr);
}
EXPORT_SYMBOL(host_map_writel);

void host_writel(uint32_t val,  void __iomem *host_addr)
{
	if (part_num == CAVIUM_CPU_PART_T83) {
		writel(val, host_addr);
	} else {
		do_dma_to_host(val, (host_dma_addr_t)host_addr);
	}
}
EXPORT_SYMBOL(host_writel);

int do_dma_sync_sli(host_dma_addr_t local_addr, host_dma_addr_t host_addr,
		    void *virt_addr, int len, host_dma_dir_t dir)
{
	void  __iomem *raddrp = NULL;
	void  __iomem *raddr = NULL;
	union sli_s2m_op_s  s2m_op;
	void  *laddr;
	int index;

	/* printk(KERN_DEBUG "dma_sync virt_addr %p host_addr 0x%llx\n len %d dir %d\n",
		  virt_addr, host_addr, len, dir); */
	index = host_addr >> 32;
	if (index > 255) {
		printk(KERN_DEBUG "phys addr too big 0x%llx\n", host_addr);
		return -1;
	}
	if (len > PAGE_SIZE) {
		printk(KERN_DEBUG "len too big %d\n", len);
		return -1;
	}
	s2m_op.u64 = 0;
	s2m_op.s.region = index;
	s2m_op.s.io = 1;
	s2m_op.s.did_hi = 8;
	s2m_op.s.addr = host_addr & ((1UL << 32) - 1);
	laddr = virt_addr;
	raddrp = ioremap((s2m_op.u64 & (~(PAGE_SIZE - 1))), PAGE_SIZE);
	if (raddrp == NULL) {
		printk(KERN_DEBUG "ioremap failed\n");
		return -1;
	}
	raddr = (uint8_t *)raddrp + (s2m_op.u64 & (PAGE_SIZE - 1));
	if (dir == DMA_TO_HOST)
		mmio_memwrite(raddr, laddr, len);
	if (dir == DMA_FROM_HOST)
		mmio_memread(laddr, raddr, len);
	iounmap(raddrp);

	return 0;
}
EXPORT_SYMBOL(do_dma_sync_sli);

static int __init dma_api_init_module(void)
{
	uint64_t __iomem *reg;

	reg = ioremap(SLI0_M2S_MAC0_CTL, 8);
	if (!reg) {
		printk(KERN_DEBUG "ioremap failed for addr 0x%lx\n",
		       SLI0_M2S_MAC0_CTL);
		return -ENOMEM;;
	}
	if (readq(reg) & (1UL << 17))
		writeq(readq(reg), reg);
	iounmap(reg);

	setup_s2m_regx_acc();
	dpi_vf_init();
	return 0;
}

static void dma_api_exit_module(void)
{
	dpi_vf_cleanup();
	return;
}

module_init(dma_api_init_module);
module_exit(dma_api_exit_module);
