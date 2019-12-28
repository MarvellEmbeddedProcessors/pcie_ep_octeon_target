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
//#include <linux/watchdog.h>
//#include <asm/arch_timer.h>
#include "barmap.h"

#define NPU_BASE_DRV_NAME  "npu_base"
#define NPU_BASE_DEVICE_ID "NPU base driver"

#define FIREWALL_CTRL_FDT_NAME         "firewall-ctrl-intr"
#define FIREWALL_MGMT_NETDEV_FDT_NAME  "firewall-mgmt-netdev-intr"
#define FIREWALL_NW_AGENT_FDT_NAME     "firewall-network-agent-intr"
#define FIREWALL_RPC_FDT_NAME          "firewall-rpc-intr"

#define PEMX_REG_BASE(pem)  (0x87E0C0000000ULL | (pem << 24))
#define NPU_HANDSHAKE_SIGNATURE 0xABCDABCD
void *pem_io_base;
void *gicd_base;
void *sdp_base;
void *npu_barmap_mem;

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

static int npu_barmap_get_irq_info(struct npu_bar_map *bar_map)
{
	int irq_count = -1;
	int first_irq = -1;

	if (get_device_irq_info(FIREWALL_CTRL_FDT_NAME,
				&irq_count, &first_irq))
		return -1;
	bar_map->ctrl_dbell_offset = NPU_BARMAP_SPI_OFFSET;
	bar_map->ctrl_dbell_bit = first_irq;
	bar_map->ctrl_dbell_count = irq_count;

	irq_count = first_irq = -1;
	if (get_device_irq_info(FIREWALL_MGMT_NETDEV_FDT_NAME,
				&irq_count, &first_irq))
		return -1;
	bar_map->mgmt_netdev_dbell_offset = NPU_BARMAP_SPI_OFFSET;
	bar_map->mgmt_netdev_dbell_bit = first_irq;
	bar_map->mgmt_netdev_dbell_count = irq_count;

	irq_count = first_irq = -1;
	if (get_device_irq_info(FIREWALL_NW_AGENT_FDT_NAME,
				&irq_count, &first_irq))
		return -1;
	bar_map->nw_agent_dbell_offset = NPU_BARMAP_SPI_OFFSET;
	bar_map->nw_agent_dbell_bit = first_irq;
	bar_map->nw_agent_dbell_count = irq_count;

	irq_count = first_irq = -1;
	if (get_device_irq_info(FIREWALL_RPC_FDT_NAME,
				&irq_count, &first_irq))
		return -1;
	bar_map->rpc_dbell_offset = NPU_BARMAP_SPI_OFFSET;
	bar_map->rpc_dbell_bit = first_irq;
	bar_map->rpc_dbell_count = irq_count;

	return 0;
}

/*
 * TODO:
 * 1. populate entry-8 for ring memory
 * 2. populate entry-15 for GICD CSRs, so that host can write through BARs
 */
static int npu_handshake(struct npu_bar_map *bar_map)
{
	phys_addr_t barmap_mem_phys;
	phys_addr_t phys_addr_i;
	uint64_t bar1_idx_val;
	void *bar1_idx_addr;
	uint64_t scratch_val;
	void *intr_ena_addr;
	void *scratch_addr;
	int i;

	npu_barmap_mem = kmalloc(NPU_BARMAP_FIREWALL_SIZE, GFP_KERNEL);
	if (npu_barmap_mem == NULL) {
		printk("%s: Failed to allocate memory\n", __func__);
		return -ENOMEM;
	}
	barmap_mem_phys = virt_to_phys(npu_barmap_mem);
	printk("%s: allocated memory for barmap; size=%u; virt=%p, phys=%llx\n",
	       __func__, NPU_BARMAP_FIREWALL_SIZE, npu_barmap_mem, barmap_mem_phys);

	/* write the control structure to mapped space */
	printk("Copying NPU bar map info to base of mapped memory\n");
	memcpy(npu_barmap_mem, (void *)bar_map, sizeof(struct npu_bar_map));

#define PEM_BAR1_INDEX_OFFSET(idx) (0x100 + (idx << 3))
	//TODO: unmap() in unload
	pem_io_base = ioremap(PEMX_REG_BASE(0), 1024*1024);
	if (pem_io_base == NULL) {
		printk("Failed to ioremap PEM CSR space\n");
		return -1;
	}
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
		bar1_idx_addr = pem_io_base + PEM_BAR1_INDEX_OFFSET(i);
		bar1_idx_val = ((phys_addr_i >> 22) << 4) | 1;
		printk("Writing BAR entry-%d; addr=%p, val=%llx\n",
		       i, bar1_idx_addr, bar1_idx_val);
		*(volatile uint64_t *)bar1_idx_addr = bar1_idx_val;
	}

	/* Map GICD CSR space to BAR1_INDEX(15) so that Host can just do a
	 * local memory write to interrupt NPU for any work on any virtual ring
	 */
	bar1_idx_addr = pem_io_base + PEM_BAR1_INDEX_OFFSET(15);
	bar1_idx_val = ((NPU_GICD_BASE >> 22) << 4) | 1;
	printk("Writing BAR entry-15 to map GICD; addr=%p, val=%llx\n",
	       bar1_idx_addr, bar1_idx_val);
	*(volatile uint64_t *)bar1_idx_addr = bar1_idx_val;
	

	/* enable all interrupt bits */
#define CN83XX_GICD_BASE (0x801000000000ULL)
#define CN83XX_GICD_ISENABLER_OFFSET(x) (0x100 + (x * 4))
#define CN83xx_GICD_ISPENDR(x) (0x200 + (x*4))
	//TODO: fix based on bap_map
	//all firewall related interrupts use among first 32 SPIs
	gicd_base = ioremap(CN83XX_GICD_BASE, (1024*1024));
	if (gicd_base == NULL) {
		printk("Failed to ioremap GICD CSR space\n");
		return -1;
	}

	intr_ena_addr = gicd_base + CN83XX_GICD_ISENABLER_OFFSET(1);
	*(volatile uint32_t *)intr_ena_addr =
			BIT(15) | BIT(16) | BIT(17) | BIT(18) |
			BIT(19) | BIT(20) | BIT(21) | BIT(22);
	printk("%s: Interrupts are enabled: phys=%p, val=%x\n",
		__func__, intr_ena_addr, *(volatile uint32_t *)intr_ena_addr);

	/* write the mapped memory addr to scratch */
#define CN83xx_SDP_BASE 0x874080000000
#define CN83XX_SDP0_SCRATCH_OFFSET(x) (0x00020180 | (x << 23))
	sdp_base = ioremap(CN83xx_SDP_BASE, 1024*1024);
	if (sdp_base == NULL) {
		printk("Failed to ioremap SDP CSR space\n");
		return -1;
	}

	printk("Writing to scratch register\n");
	scratch_addr = sdp_base + CN83XX_SDP0_SCRATCH_OFFSET(0);
	scratch_val = ((uint64_t)(NPU_BARMAP_FIREWALL_FIRST_ENTRY *
		       NPU_BARMAP_ENTRY_SIZE) << 32) |
		       NPU_HANDSHAKE_SIGNATURE;
	*(volatile uint64_t *)scratch_addr = scratch_val;
	printk("Wrote to scratch: %p=%llx\n",
	       scratch_addr, *(volatile uint64_t *)scratch_addr);
	kfree(npu_barmap_mem);

	return 0;
}

static int npu_base_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int irq;
	char *dev_id = NPU_BASE_DEVICE_ID;
	struct npu_bar_map bar_map;

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

	npu_barmap_get_info(&bar_map);
	if (npu_barmap_get_irq_info(&bar_map))
		return -ENODEV;
	//TODO: npu_enable_barmap_intr();
	npu_handshake(&bar_map);
	return 0;
}

static void npu_base_shutdown(struct platform_device *pdev)
{
	printk("%s: called\n", __func__);
	return;
}

static int npu_base_remove(struct platform_device *pdev)
{
	printk("%s: called\n", __func__);
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
	{ .compatible = "cavium,firewall-ctrl", },
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
