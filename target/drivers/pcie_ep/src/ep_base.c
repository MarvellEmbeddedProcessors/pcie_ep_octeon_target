/* Copyright (c) 2020 Marvell.
 * SPDX-License-Identifier: GPL-2.0
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/iommu.h>
//#include <linux/watchdog.h>
//#include <asm/arch_timer.h>
#include "barmap.h"

#define NPU_BASE_DRV_NAME  "npu_base"
#define NPU_BASE_DEVICE_ID "NPU base driver"
#define MRVL_CPU_PART_OCTEONTX2_98XX	0x0B1

#define FDT_NAME         "pcie-ep"
static unsigned int  host_sid = 0x030000;
module_param(host_sid, uint, 0644);
MODULE_PARM_DESC(host_sid, "host stream id (((0x3 + PEM_NUM) << 16) + Host_requester id");

static unsigned int  pem_num = 0;
module_param(pem_num, uint, 0644);
MODULE_PARM_DESC(pem_num, "PEM number to use");

static unsigned int  epf_num = 0;
module_param(epf_num, uint, 0644);
MODULE_PARM_DESC(epf_num, "epf number to use");

static uint64_t sdp_num = 0;

#define CN93xx_SDP_BASE(a) (0x86E080000000ULL | a << 36)
#define CN93XX_SDPX_EPFX_SCRATCH_OFFSET(epf) (0x000205E0ULL | (epf << 25))
#define CN93XX_SDPX_EPFX_OEI_TRIG_ADDR(sdp, epf) (0x86E0C0000000ULL | (sdp << 36) | (epf << 25))
#define PEMX_REG_BASE_93XX(pem) (0x8E0000000000ULL | ((uint64_t)pem << 36))

#define CN83xx_SDP_BASE 0x874080000000ULL
#define CN83XX_SDP0_SCRATCH_OFFSET(epf) (0x00020180ULL | (epf << 23))
#define CN83XX_SDP0_EPFX_OEI_TRIG_ADDR(epf) (0x874000800000ULL | (epf << 16))
#define PEMX_REG_BASE_83XX(pem)  (0x87E0C0000000ULL | (pem << 24))

#define PEM_BAR1_INDEX_OFFSET(idx) (0x100 + (idx << 3))
#define PEM_BAR4_INDEX_OFFSET(idx) (0x700 + (idx << 3))
#define PEM_DIS_PORT_OFFSET 0x50

#define NPU_HANDSHAKE_SIGNATURE 0xABCDABCD
void *oei_trig_remap_addr;
void __iomem *nwa_internal_addr;
EXPORT_SYMBOL(nwa_internal_addr);

const struct iommu_ops *smmu_ops;
//TODO: fix the names npu_barmap_mem and npu_bar_map
/* local memory mapped through BAR for access from host */
void *npu_barmap_mem;

/* NPU BAR map structure */
struct npu_bar_map bar_map;
struct npu_irq_info irq_info[MAX_INTERRUPTS];

/* platform device */
struct device	*plat_dev;

static uint64_t npu_csr_read(uint64_t csr_addr)
{
	uint64_t val;
	uint64_t *addr;

	addr = ioremap(csr_addr, 8);
	if (addr == NULL) {
		printk("Failed to ioremap SDP CSR space\n");
		return -1UL;
	}
	val = READ_ONCE(*addr);
	iounmap(addr);
	return val;
}

static void npu_csr_write(uint64_t csr_addr, uint64_t val)
{
	uint64_t *addr;

	addr = ioremap(csr_addr, 8);
	if (addr == NULL) {
		printk("Failed to ioremap SDP CSR space\n");
		return;
	}
	WRITE_ONCE(*addr, val);
	iounmap(addr);
}

static irqreturn_t npu_base_interrupt(int irq, void *dev_id)
{
	printk("Interrupt Received\n");
	printk("%s: %s\n", __func__, (char *)dev_id);

	/* TODO: delete; only for testing/debug */
	if(npu_barmap_mem) {
		printk("ctrl[0] = %x\n",
		       *(volatile uint32_t *)(npu_barmap_mem +
					      (NPU_BARMAP_CTRL_OFFSET - NPU_BARMAP_FIREWALL_OFFSET)));
		printk("netdev[0] = %x\n",
		       *(volatile uint32_t *)(npu_barmap_mem +
					      (NPU_BARMAP_MGMT_NETDEV_OFFSET - NPU_BARMAP_FIREWALL_OFFSET)));
		printk("nw_agent[0] = %x\n",
		       *(volatile uint32_t *)(npu_barmap_mem +
					      (NPU_BARMAP_NW_AGENT_OFFSET - NPU_BARMAP_FIREWALL_OFFSET)));
		nwa_internal_addr = npu_barmap_mem +
				    (NPU_BARMAP_NW_AGENT_OFFSET -
				     NPU_BARMAP_FIREWALL_OFFSET);
		printk("RPC[0] = %x\n",
		       *(volatile uint32_t *)(npu_barmap_mem +
					      (NPU_BARMAP_RPC_OFFSET - NPU_BARMAP_FIREWALL_OFFSET)));
	}

	return IRQ_HANDLED;
}

int of_irq_count(struct device_node *dev)
{
	struct of_phandle_args irq;
	int nr = 0;

	while (of_irq_parse_one(dev, nr, &irq) == 0)
		nr++;

	return nr;
}

static int get_device_irq_info(const char *name, int *irq_count, int *first_irq)
{
	struct of_phandle_args irq_data;
	struct device_node *dev;
	int ret = 0, i;

	printk("####### %s: called for %s ########\n", __func__, name);
	dev = of_find_node_by_name(NULL, name);
	if (dev == NULL) {
		printk("can't find FDT dev %s\n", name);
		return -EEXIST;
	}
	printk("found FDT dev %s\n", name);

	*irq_count = of_irq_count(dev);
	if (*irq_count == 0) {
		printk("Error: No interrupts found for device %s\n", name);
		ret = -1;
		goto err;
	}

	ret = of_irq_parse_one(dev, 0, &irq_data);
	if (ret) {
		printk("Error: Failed to parse irq at index-0 of device %s; ret=%d\n",
		       name, ret);
		goto err;
	}

	for (i = 0; i < irq_data.args_count; i++)
		printk("%s: irq-arg[%d]=%u\n", __func__, i, irq_data.args[i]);
	*first_irq = irq_data.args[1];
	printk("%s: irq_count = %d, first_irq=%d\n", name, *irq_count, *first_irq);

err:
	of_node_put(dev);
	return ret;
}

/*
 * TODO:
 * 1. populate entry-8 for ring memory
 * 2. populate entry-15 for GICD CSRs, so that host can write through BARs
 */
static int npu_base_setup(struct npu_bar_map *bar_map)
{
	phys_addr_t barmap_mem_phys;
	phys_addr_t phys_addr_i;
	unsigned long part_num;
	uint64_t bar_idx_val;
	uint64_t bar_idx_addr;
	uint64_t disport_addr;
	int i;

	part_num = read_cpuid_part_number();
	if ((part_num != CAVIUM_CPU_PART_T83) &&
	    (part_num != MRVL_CPU_PART_OCTEONTX2_96XX) &&
	    (part_num != MRVL_CPU_PART_OCTEONTX2_98XX)) {
		printk("Unsupported CPU type\n");
		return -1;
	}
	npu_barmap_mem = kmalloc(NPU_BARMAP_FIREWALL_SIZE, GFP_KERNEL);
	if (npu_barmap_mem == NULL) {
		printk("%s: Failed to allocate memory\n", __func__);
		return -ENOMEM;
	}
	barmap_mem_phys = virt_to_phys(npu_barmap_mem);
	printk("%s: allocated memory for barmap; size=%u; virt=%p, phys=%llx\n",
	       __func__, NPU_BARMAP_FIREWALL_SIZE, npu_barmap_mem, barmap_mem_phys);

	/* write the control structure to mapped space */
	printk("Copying NPU bar map info to base of mapped memory ...\n");
	memcpy(npu_barmap_mem, (void *)bar_map, sizeof(struct npu_bar_map));

//TODO: remove BAR1 references; 96xx has BAR4
	//TODO: unmap() in unload
	/* Write memory mapping to PEM BAR1_INDEX */
	printk("Mapping NPU physical memory to BAR1 entries ...\n");
	if (NPU_BARMAP_FIREWALL_SIZE > NPU_BARMAP_MAX_SIZE) {
		printk("Error: map area is larger than max BAR1 size available; map area size= %u, available = %u\n",
		        NPU_BARMAP_FIREWALL_SIZE, NPU_BARMAP_MAX_SIZE);
		return -1;
	}

	for (i = NPU_BARMAP_FIREWALL_FIRST_ENTRY;
	     i < CN83XX_PEM_BAR1_INDEX_MAX_ENTRIES-1; i++) {
		int ii = i - NPU_BARMAP_FIREWALL_FIRST_ENTRY;
		phys_addr_i = barmap_mem_phys + (ii * NPU_BARMAP_ENTRY_SIZE);
		if (part_num == CAVIUM_CPU_PART_T83)
			bar_idx_addr = PEMX_REG_BASE_83XX(pem_num) + PEM_BAR1_INDEX_OFFSET(i);
		else if (part_num == MRVL_CPU_PART_OCTEONTX2_96XX ||
			 part_num == MRVL_CPU_PART_OCTEONTX2_98XX)
			bar_idx_addr = PEMX_REG_BASE_93XX(pem_num) + PEM_BAR4_INDEX_OFFSET(i);
		else {
			printk("Error: Invalid part_num = %lu\n", part_num);
			return -1;
		}
		bar_idx_val = ((phys_addr_i >> 22) << 4) | 1;
		printk("Writing BAR entry-%d; addr=%llx, val=%llx\n",
		       i, bar_idx_addr, bar_idx_val);
		npu_csr_write(bar_idx_addr, bar_idx_val);
	}

	/* Map GICD CSR space to BAR1_INDEX(15) so that Host can just do a
	 * local memory write to interrupt NPU for any work on any virtual ring
	 */
	if (part_num == CAVIUM_CPU_PART_T83)
		bar_idx_addr = PEMX_REG_BASE_83XX(pem_num) + PEM_BAR1_INDEX_OFFSET(15);
	else if (part_num == MRVL_CPU_PART_OCTEONTX2_96XX ||
		 part_num == MRVL_CPU_PART_OCTEONTX2_98XX)
		bar_idx_addr = PEMX_REG_BASE_93XX(pem_num)  + PEM_BAR4_INDEX_OFFSET(15);

	bar_idx_val = ((NPU_GICD_BASE >> 22) << 4) | 1;
	printk("Writing BAR entry-15 to map GICD; addr=%llx, val=%llx\n",
	       bar_idx_addr, bar_idx_val);
	npu_csr_write(bar_idx_addr, bar_idx_val);

	if (part_num == CAVIUM_CPU_PART_T83)
		oei_trig_remap_addr = ioremap(CN83XX_SDP0_EPFX_OEI_TRIG_ADDR(epf_num), 8);
	else if (part_num == MRVL_CPU_PART_OCTEONTX2_96XX ||
		 part_num == MRVL_CPU_PART_OCTEONTX2_98XX)
		oei_trig_remap_addr = ioremap(CN93XX_SDPX_EPFX_OEI_TRIG_ADDR(sdp_num, epf_num), 8);

	if (oei_trig_remap_addr == NULL) {
		printk("Failed to ioremap oei_trig space\n");
		return -1;
	}
	if (part_num == MRVL_CPU_PART_OCTEONTX2_96XX ||
	    part_num == MRVL_CPU_PART_OCTEONTX2_98XX) {
		disport_addr = PEMX_REG_BASE_93XX(pem_num) + PEM_DIS_PORT_OFFSET;
		npu_csr_write(disport_addr, 1);
	}

	return 0;
}

static void npu_handshake_ready(struct npu_bar_map *bar_map)
{
	uint64_t scratch_val;
	uint64_t scratch_addr;
	unsigned long part_num;

	printk("Writing to scratch register\n");
	part_num = read_cpuid_part_number();
	if (part_num == CAVIUM_CPU_PART_T83)
		scratch_addr = CN83xx_SDP_BASE + CN83XX_SDP0_SCRATCH_OFFSET(epf_num);
	else if (part_num == MRVL_CPU_PART_OCTEONTX2_96XX ||
		 part_num == MRVL_CPU_PART_OCTEONTX2_98XX)
		scratch_addr = CN93xx_SDP_BASE(sdp_num) + CN93XX_SDPX_EPFX_SCRATCH_OFFSET(epf_num);
	else
		return;
	scratch_val = ((uint64_t)(NPU_BARMAP_FIREWALL_FIRST_ENTRY *
		       NPU_BARMAP_ENTRY_SIZE) << 32) |
		       NPU_HANDSHAKE_SIGNATURE;
	npu_csr_write(scratch_addr, scratch_val);
	printk("Wrote to scratch: %llx=%llx\n",
	       scratch_addr, npu_csr_read(scratch_addr));
}

extern void mv_facility_conf_init(struct device *dev,
				  void *mapaddr,
				  struct npu_bar_map *barmap);
extern int npu_device_access_init(void);

static int npu_base_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int i, irq, first_irq, irq_count, ret;
	char *dev_id = NPU_BASE_DEVICE_ID;
	unsigned long part_num;
	struct device *smmu_dev;
	struct iommu_domain *host_domain;

	part_num = read_cpuid_part_number();
	printk("CPU Part: 0x%lx\n", part_num);
	if (part_num == MRVL_CPU_PART_OCTEONTX2_96XX ||
	    part_num == MRVL_CPU_PART_OCTEONTX2_98XX) {
		smmu_dev = bus_find_device_by_name(&platform_bus_type,
						   NULL,
						   "830000000000.smmu");
		if (!smmu_dev) {
			dev_err(dev, "Cannot locate smmu device\n");
			goto exit;
		}

		smmu_ops = platform_bus_type.iommu_ops;
		if (!smmu_ops) {
			dev_err(dev, "Cannot locate smmu_ops\n");
			goto exit;
		}

		host_domain = smmu_ops->domain_alloc(IOMMU_DOMAIN_IDENTITY);
		if (!host_domain) {
			dev_err(dev, "Cannot allocate smmu domain for host\n");
			goto exit;
		}

		/* caller sets these; see __iommu_domain_alloc */
		host_domain->ops = smmu_ops;
		host_domain->type = IOMMU_DOMAIN_IDENTITY;
		host_domain->pgsize_bitmap = smmu_ops->pgsize_bitmap;

		dev_dbg(dev, "OLD dev->iommu_fwspec %p\n", dev->iommu_fwspec);

		ret = iommu_fwspec_init(dev, smmu_dev->fwnode, smmu_ops);
		if (ret) {
			dev_err(dev, "Error %d from iommu_fwspec_init()\n",
				ret);
			goto exit;
		}

		dev_dbg(dev, "NEW dev->iommu_fwspec %p\n", dev->iommu_fwspec);

		ret = iommu_fwspec_add_ids(dev, &host_sid, 1);
		if (ret) {
			dev_err(dev, "Error %d from iommu_fwspec_add_ids()\n",
				ret);
			goto exit;
		}

		ret = smmu_ops->add_device(dev);
		if (ret) {
			dev_err(dev, "Error %d from smmu_ops->add_device()\n",
				ret);
			goto exit;
		}

		ret = smmu_ops->attach_dev(host_domain, dev);
		if (ret) {
			/* remove device from domain */
			smmu_ops->remove_device(dev);

			dev_err(dev,
				"Error %d from smmu_ops->attach_dev()\n",
				ret);
			goto exit;
		}
	}

	/* on 98xx SDP number changes as per the PEM used */
	if (part_num == MRVL_CPU_PART_OCTEONTX2_98XX) {
		sdp_num = pem_num / 2;
	}

	printk("SDP block = %lld\n", sdp_num);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_warn(dev, "unable to get NPU interrupt.\n");
	} else {
		printk("%s: IRQ no = %d\n", __func__, irq);
		if (devm_request_irq(dev, irq, npu_base_interrupt, 0,
				     pdev->name, (void *)dev_id)) {
			dev_warn(dev, "unable to request NPU IRQ %d.\n",
				 irq);
		}
	}

	if (get_device_irq_info(FDT_NAME, &irq_count, &first_irq)) {
		printk("get irq info failed\n");
		return -1;
	}

	for (i = 0; i < irq_count; i++) {
		irq_info[i].irq = platform_get_irq(pdev, i);
		irq_info[i].cpumask = NULL;
	}

	if (npu_bar_map_init(&bar_map, pem_num, first_irq, irq_count)) {
		printk("bar map int failed\n");
		return -1;
	}

	if (npu_base_setup(&bar_map)) {
		printk("Base setup failed\n");
		return -1;
	}

	plat_dev = dev;
	npu_handshake_ready(&bar_map);
	mv_facility_conf_init(dev, npu_barmap_mem, &bar_map);
	npu_device_access_init();
	return 0;

exit:
	return ret ? -ENODEV : 0;
}

static void npu_base_shutdown(struct platform_device *pdev)
{
	printk("%s: called\n", __func__);
	return;
}

static int npu_base_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	printk("%s: called\n", __func__);

	/* remove device from domain */
	if (smmu_ops)
		smmu_ops->remove_device(dev);

	return 0;
}

/* Disable watchdog if it is active during suspend */
static int __maybe_unused npu_base_suspend(struct device *dev)
{
	printk("%s: called\n", __func__);
	return 0;
}

/* Enable watchdog if necessary */
static int __maybe_unused npu_base_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops npu_base_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(npu_base_suspend, npu_base_resume)
};

static const struct of_device_id npu_base_off_match[] = {
	{ .compatible = "marvell,octeontx-ep", },
	{ .compatible = "marvell,octeontx2-ep", },
	{},
};
MODULE_DEVICE_TABLE(of, npu_base_off_match);

static const struct platform_device_id npu_base_pdev_match[] = {
	{ .name = NPU_BASE_DRV_NAME, },
	{},
};
MODULE_DEVICE_TABLE(platform, npu_base_pdev_match);

static struct platform_driver npu_base_driver = {
	.driver = {
		.name = NPU_BASE_DRV_NAME,
		.pm = &npu_base_pm_ops,
		.of_match_table = npu_base_off_match,
	},
	.probe = npu_base_probe,
	.remove = npu_base_remove,
	.shutdown = npu_base_shutdown,
	.id_table = npu_base_pdev_match,
};

module_platform_driver(npu_base_driver);

MODULE_DESCRIPTION("NPU Interrupt Test Driver");
MODULE_AUTHOR("Veerasenareddy Burru <vburru@marvell.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" NPU_BASE_DRV_NAME);
