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
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched/signal.h>
#include <linux/workqueue.h>
#include "barmap.h"
#include "ep_base.h"

#define NPU_BASE_DRV_NAME  "npu_base"
#define NPU_BASE_DEVICE_ID "NPU base driver"

static unsigned int  pem_num[2] = {0,2};
static int pem_num_arr_cnt = 0;
module_param_array(pem_num, uint, &pem_num_arr_cnt, 0644);
MODULE_PARM_DESC(pem_num, "PEM number to use");

static unsigned int  epf_num[2] = {0,0};
static int epf_num_arr_cnt = 0;
module_param_array(epf_num, uint, &epf_num_arr_cnt, 0644);
MODULE_PARM_DESC(epf_num, "epf number to use");

static uint64_t sdp_num[2] = {0,1};

static void npu_perst_handler(struct work_struct *w);
static struct workqueue_struct *npu_perst_wq = NULL;

enum supported_plat {
	OTX_CN83XX,
	OTX2_CN9XXX,
	OTX3_CN10K,
};

union sdp_epf_oei_trig {
	uint64_t u64;
	struct {
		uint64_t bit_num:6;
		uint64_t rsvd2:12;
		uint64_t clr:1;
		uint64_t set:1;
		uint64_t rsvd:44;
	} s;
};

#define PEMX_BASE(a, b)		((a) | ((uint64_t)b << 36))
#define PEM_CFG_WR		0x18ull
#define PEM_CFG_RD		0x20ull
#define PEM_DIS_PORT_OFFSET	0x50ull
#define PEM_ON_OFFSET		0xE0ull
#define PEM_RST_INT		0x300ull
#define PEM_RST_INT_ENA_W1C	0x310ull
#define PEM_RST_INT_ENA_W1S	0x318ull

#define FW_STATUS_READY		0x1ul

#define PCIEEP_VSECST_CTL	0x4d0

#define SDP_SCRATCH_OFFSET(x) ({			\
	u64 offset = 0;					\
							\
	if (pcie_ep_dev->plat_model == OTX_CN83XX)	\
		offset = 0x20180ull | (x << 23);	\
	else if (pcie_ep_dev->plat_model == OTX2_CN9XXX)\
		offset = 0x205E0ull | (x <<25);		\
	else if (pcie_ep_dev->plat_model == OTX3_CN10K) \
		offset = 0x209E0ull | (x <<25);		\
	offset; })					\

#define PEM_BAR_INDEX_OFFSET(x) ({				\
	u64 offset = 0;					\
	if (pcie_ep_dev->plat_model == OTX_CN83XX)	\
		offset = 0x100ull | (x << 3);	\
	else if (pcie_ep_dev->plat_model == OTX2_CN9XXX)\
		offset = 0x700ull | (x <<3);		\
	else if (pcie_ep_dev->plat_model == OTX3_CN10K) \
		offset = 0x700ull | (x <<3);		\
	offset; })					\

#define NPU_HANDSHAKE_SIGNATURE 0xABCDABCD
#define KEEPALIVE_OEI_TRIG_BIT	5
#define KEEPALIVE_INTERVAL_MS	500

void __iomem *nwa_internal_addr;
EXPORT_SYMBOL(nwa_internal_addr);

#define PCIE_EP_MATCH_DATA(name, imp)	\
static unsigned int name = imp

PCIE_EP_MATCH_DATA(otx_cn83xx, OTX_CN83XX);
PCIE_EP_MATCH_DATA(otx2_cn9xxx, OTX2_CN9XXX);
PCIE_EP_MATCH_DATA(otx3_cn10k, OTX3_CN10K);

//TODO: fix the names npu_barmap_mem and npu_bar_map
/* NPU BAR map structure */
#define MAX_SDP_PF 2
struct otx_pcie_ep *g_pcie_ep_dev[MAX_SDP_PF];
//struct npu_bar_map bar_map[MAX_SDP_PF];
//struct npu_irq_info irq_info[MAX_INTERRUPTS];
/* local memory mapped through BAR for access from host */
//void *npu_barmap_mem[MAX_SDP_PF];

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

void send_oei_trigger(struct otx_pcie_ep *pcie_ep_dev, int type)
{
	union sdp_epf_oei_trig trig;
	uint64_t *addr;
	int i;

	/* Don't send interrupts until OEI enabled by host */
	for (i = 0; i < pcie_ep_dev->num_pfs; i++) {
		addr = (uint64_t*)pcie_ep_dev->oei_rint_ena_remap_addr[i];
		if (!READ_ONCE(*addr))
			return;

		trig.u64 = 0;
		trig.s.set = 1;
		trig.s.bit_num = type;
		addr = (uint64_t*)pcie_ep_dev->oei_trig_remap_addr[i];
		WRITE_ONCE(*addr, trig.u64);

		/* if not keepalive then skip for rest of the PFs */
		if (type != KEEPALIVE_OEI_TRIG_BIT)
			break;
	}
}

static int ep_keepalive_thread(void *arg)
{
	struct otx_pcie_ep *pcie_ep_dev;

	pcie_ep_dev = (struct otx_pcie_ep *)arg;

	msleep_interruptible(KEEPALIVE_INTERVAL_MS);

	while (!kthread_should_stop()) {
		send_oei_trigger(pcie_ep_dev, KEEPALIVE_OEI_TRIG_BIT);
		msleep_interruptible(KEEPALIVE_INTERVAL_MS);
	}

	return 0;
}

static void npu_perst_handler(struct work_struct *w)
{
	struct otx_pcie_ep *pcie_ep;
	uint64_t addr, val;

	pcie_ep = container_of(w, struct otx_pcie_ep, perst_wrk);

	addr = PEMX_BASE(pcie_ep->pem_base, 0) + PEM_RST_INT;
	npu_csr_write(addr, 0x1);

	/* Sleep for few seconds until host is ready */
	msleep(10*1000);

	addr = PEMX_BASE(pcie_ep->pem_base, 0) + PEM_ON_OFFSET;
	val = npu_csr_read(addr);

	/* Wait until PEMOOR is set */
	do {
		msleep(1000);
		val = npu_csr_read(addr);
	} while (!(val & 0x2));

	addr = PEMX_BASE(pcie_ep->pem_base, 0) + PEM_CFG_WR;
	val = ((FW_STATUS_READY << 32) |
	       (1 << 18) |
	       (1 << 15) |
	       (PCIEEP_VSECST_CTL));
	npu_csr_write(addr, val);

	msleep(2000);
	addr = PEMX_BASE(pcie_ep->pem_base, 0) + PEM_DIS_PORT_OFFSET;
	npu_csr_write(addr, 1);
}

static irqreturn_t npu_perst_interrupt(int irq, void *arg)
{
	struct otx_pcie_ep *pcie_ep = (struct otx_pcie_ep *)arg;

	printk("Received %d PERST interrupt\n", irq);

	if (npu_perst_wq)
		queue_work(npu_perst_wq, &pcie_ep->perst_wrk);

	return IRQ_HANDLED;
}


static irqreturn_t npu_base_interrupt(int irq, void *arg)
{
	struct otx_pcie_ep *pcie_ep = (struct otx_pcie_ep *)arg;
	printk("Interrupt %d Received\n", irq);

	/* TODO: delete; only for testing/debug */
	if(pcie_ep->npu_barmap_mem) {
		printk("ctrl[0] = %x\n",
		       *(volatile uint32_t *)(pcie_ep->npu_barmap_mem +
					      (NPU_BARMAP_CTRL_OFFSET - NPU_BARMAP_FIREWALL_OFFSET)));
		printk("netdev[0] = %x\n",
		       *(volatile uint32_t *)(pcie_ep->npu_barmap_mem +
					      (NPU_BARMAP_MGMT_NETDEV_OFFSET - NPU_BARMAP_FIREWALL_OFFSET)));
		printk("nw_agent[0] = %x\n",
		       *(volatile uint32_t *)(pcie_ep->npu_barmap_mem +
					      (NPU_BARMAP_NW_AGENT_OFFSET - NPU_BARMAP_FIREWALL_OFFSET)));
		nwa_internal_addr = pcie_ep->npu_barmap_mem +
				    (NPU_BARMAP_NW_AGENT_OFFSET -
				     NPU_BARMAP_FIREWALL_OFFSET);
		printk("RPC[0] = %x\n",
		       *(volatile uint32_t *)(pcie_ep->npu_barmap_mem +
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

static int get_device_irq_info(struct device *dev, int *irq_count, int *first_irq)
{
	struct of_phandle_args irq_data;
	struct device_node *of_node = dev->of_node;
	int ret = 0, i;

	printk("####### %s: called ########\n", __func__);
	if (of_node == NULL) {
		printk("FDT node is NULL\n");
		return -EEXIST;
	}
	printk("FDT dev %s\n", of_node->name);

	*irq_count = of_irq_count(of_node);
	if (*irq_count == 0) {
		printk("Error: No interrupts found for device %s\n", of_node->name);
		ret = -1;
		goto err;
	}

	ret = of_irq_parse_one(of_node, 0, &irq_data);
	if (ret) {
		printk("Error: Failed to parse irq at index 0, ret=%d\n", ret);
		goto err;
	}

	for (i = 0; i < irq_data.args_count; i++)
		printk("%s: irq-arg[%d]=%u\n", __func__, i, irq_data.args[i]);
	*first_irq = irq_data.args[1];
	printk("%s: irq_count = %d, first_irq=%d\n", of_node->name, *irq_count, *first_irq);

err:
	return ret;
}

/*
 * TODO:
 * 1. populate entry-8 for ring memory
 * 2. populate entry-15 for GICD CSRs, so that host can write through BARs
 */
static int npu_base_setup(struct otx_pcie_ep *pcie_ep_dev)
{
	phys_addr_t barmap_mem_phys;
	phys_addr_t phys_addr_i;
	uint64_t bar_idx_val;
	uint64_t bar_idx_addr;
	uint64_t disport_addr;
	int instance = pcie_ep_dev->instance;
	int i;

	pcie_ep_dev->npu_barmap_mem = kmalloc(NPU_BARMAP_FIREWALL_SIZE, GFP_KERNEL);
	if (pcie_ep_dev->npu_barmap_mem == NULL) {
		printk("%s: Failed to allocate memory\n", __func__);
		return -ENOMEM;
	}
	barmap_mem_phys = virt_to_phys(pcie_ep_dev->npu_barmap_mem);
	printk("%s: allocated memory for barmap; size=%u; virt=%p, phys=%llx\n",
	       __func__, NPU_BARMAP_FIREWALL_SIZE, pcie_ep_dev->npu_barmap_mem, barmap_mem_phys);

	/* write the control structure to mapped space */
	printk("Copying NPU bar map info to base of mapped memory ...\n");
	memcpy(pcie_ep_dev->npu_barmap_mem, (void *)&pcie_ep_dev->bar_map, sizeof(struct npu_bar_map));

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
	     i < NPU_PEM_BAR_INDEX_MAX_ENTRIES-1; i++) {
		int ii = i - NPU_BARMAP_FIREWALL_FIRST_ENTRY;
		phys_addr_i = barmap_mem_phys + (ii * NPU_BARMAP_ENTRY_SIZE);
		bar_idx_addr = PEMX_BASE(pcie_ep_dev->pem_base, pem_num[instance]) + PEM_BAR_INDEX_OFFSET(i);
		bar_idx_val = ((phys_addr_i >> 22) << 4) | 1;
		printk("Writing BAR entry-%d; addr=%llx, val=%llx\n",
		       i, bar_idx_addr, bar_idx_val);
		npu_csr_write(bar_idx_addr, bar_idx_val);
	}

	/* Map GICD CSR space to BAR1_INDEX(15) so that Host can just do a
	 * local memory write to interrupt NPU for any work on any virtual ring
	 */
	bar_idx_addr = PEMX_BASE(pcie_ep_dev->pem_base, pem_num[instance]) + PEM_BAR_INDEX_OFFSET(15);
	bar_idx_val = ((NPU_GICD_BASE >> 22) << 4) | 1;
	printk("Writing BAR entry-15 to map GICD; addr=%llx, val=%llx\n",
	       bar_idx_addr, bar_idx_val);
	npu_csr_write(bar_idx_addr, bar_idx_val);

	bar_idx_addr = PEMX_BASE(pcie_ep_dev->pem_base, pem_num[instance]) + PEM_BAR_INDEX_OFFSET(14);
	bar_idx_val = ((NPU_PTP_BASE >> 22) << 4) | 1;
	printk("Writing BAR entry-14 to map PTP; addr=%llx, val=%llx\n",
	       bar_idx_addr, bar_idx_val);
	npu_csr_write(bar_idx_addr, bar_idx_val);

	{
		struct device_node *dev;
		const void *ptr;
		int len;

		dev = of_find_node_by_name(NULL, "rvu-sdp");
		if (dev == NULL) {
			printk("can't find FDT dev %s\n", "rvu-sdp");
			return -EINVAL;
		}

		ptr = of_get_property(dev, "num-sdp-pfs", &len);
		if (ptr == NULL) {
			printk("SDP DTS: Failed to get num-sdp-pfs\n");
			pcie_ep_dev->num_pfs = 1;
		} else {
			pcie_ep_dev->num_pfs = be32_to_cpup((u32 *)ptr);
		}
	}

	for (i = 0; i < pcie_ep_dev->num_pfs; i++) {
		pcie_ep_dev->oei_trig_remap_addr[i] =
			(uint64_t)ioremap(pcie_ep_dev->oei_trig_addr | i << 25, 8);
		if (pcie_ep_dev->oei_trig_remap_addr[i] == (uint64_t)NULL) {
			printk("Failed to ioremap oei_trig space\n");
			return -1;
		}

#define SDPX_EPFX_OEI_RINT_ENA_W1S(A,B)	(0x86E080020390 | \
					 (((A)&1)<<36) | (((B)&0xF)<<25))
#define SDPX_EPFX_OEI_RINT_ENA_W1SX(A,B,C)	(0x86e080020700 | \
						 (((A)&1)<<36) | (((B)&0x3)<<25)\
						 | (((C)&0xf)<<4))
		if (pcie_ep_dev->plat_model == OTX3_CN10K) {
			pcie_ep_dev->oei_rint_ena_remap_addr[i] =
				(uint64_t)ioremap(SDPX_EPFX_OEI_RINT_ENA_W1SX(sdp_num[instance], i, 0), 8);
		} else {
			pcie_ep_dev->oei_rint_ena_remap_addr[i] =
				(uint64_t)ioremap(SDPX_EPFX_OEI_RINT_ENA_W1S(sdp_num[instance], i), 8);
		}
		if (pcie_ep_dev->oei_rint_ena_remap_addr[i] == (uint64_t)NULL) {
			printk("Failed to ioremap oei_rint_ena space\n");
			return -1;
		}
	}
	if (pcie_ep_dev->plat_model == OTX2_CN9XXX ||
	    pcie_ep_dev->plat_model == OTX3_CN10K) {
		disport_addr = PEMX_BASE(pcie_ep_dev->pem_base, pem_num[instance]) + PEM_DIS_PORT_OFFSET;
		npu_csr_write(disport_addr, 1);
	}

	return 0;
}

static void npu_handshake_ready(struct otx_pcie_ep *pcie_ep_dev)
{
	uint64_t scratch_val;
	uint64_t scratch_addr;
	int instance = pcie_ep_dev->instance;

	printk("Writing to scratch register\n");
	scratch_addr = pcie_ep_dev->sdp_base + SDP_SCRATCH_OFFSET(epf_num[instance]);
	scratch_val = ((uint64_t)(NPU_BARMAP_FIREWALL_FIRST_ENTRY *
		       NPU_BARMAP_ENTRY_SIZE) << 32) |
		       NPU_HANDSHAKE_SIGNATURE;
	npu_csr_write(scratch_addr, scratch_val);
	printk("Wrote to scratch: %llx=%llx\n",
	       scratch_addr, npu_csr_read(scratch_addr));
}

extern void mv_facility_conf_init(struct otx_pcie_ep *ep);
extern void npu_device_access_init(int instance);

static int pcie_ep_dt_probe(struct platform_device *pdev,
			    struct otx_pcie_ep *pcie_ep_dev)
{
	struct device *dev = &pdev->dev;
	const unsigned int *data;
	int ret = 0;

	data = of_device_get_match_data(dev);
	pcie_ep_dev->plat_model = *data;

	ret = device_property_read_u64(dev, "pem_base",
				       &pcie_ep_dev->pem_base);
	if (ret < 0) {
		dev_err(dev, "cannot read PEM base from DT ret=%d\n", ret);
		return ret;
	}

	ret = device_property_read_u64(dev, "sdp_base",
				       &pcie_ep_dev->sdp_base);
	if (ret < 0) {
		dev_err(dev, "cannot read SDP base from DT ret=%d\n", ret);
		return ret;
	}

	ret = device_property_read_u64(dev, "oei_trig_addr",
				       &pcie_ep_dev->oei_trig_addr);
	if (ret < 0) {
		dev_err(dev, "cannot read OEI_TRIG addr from DT ret=%d\n", ret);
		return ret;
	}

	ret = device_property_read_u32(dev, "instance", &pcie_ep_dev->instance);
	if (ret < 0)
		pcie_ep_dev->instance = 0;

	return 0;
}

static int npu_base_probe(struct platform_device *pdev)
{
	struct otx_pcie_ep *pcie_ep_dev;
	struct device *dev = &pdev->dev;
	int i, irq, first_irq, irq_count, ret = 0;
	int instance = 0;
	unsigned int plat;
	uint64_t addr, val;

	pcie_ep_dev = devm_kzalloc(dev, sizeof(*pcie_ep_dev), GFP_KERNEL);
	if (!pcie_ep_dev)
		return -ENOMEM;

	if (dev->of_node) {
		ret = pcie_ep_dt_probe(pdev, pcie_ep_dev);
		if (ret < 0)
			goto exit;
	}

	platform_set_drvdata(pdev, pcie_ep_dev);

	instance = pcie_ep_dev->instance;
	plat = pcie_ep_dev->plat_model;


	printk("SDP block = %lld\n", sdp_num[instance]);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_warn(dev, "unable to get NPU interrupt.\n");
	} else {
		printk("%s: IRQ no = %d\n", __func__, irq);
		if (devm_request_irq(dev, irq, npu_base_interrupt, 0,
				     pdev->name, pcie_ep_dev)) {
			dev_warn(dev, "unable to request NPU IRQ %d.\n",
				 irq);
		}
	}

	if (get_device_irq_info(dev, &irq_count, &first_irq)) {
		printk("get irq info failed\n");
		return -1;
	}

	if (!instance && (irq_count == 9))
		irq_count --;

	for (i = 0; i < irq_count; i++) {
		pcie_ep_dev->irq_info[i].irq = platform_get_irq(pdev, i);
		pcie_ep_dev->irq_info[i].cpumask = NULL;
	}

	/* register for PERST */
	irq = platform_get_irq(pdev, i);
	if (irq < 0) {
		dev_warn(dev, "No PERST interrupt available\n");
	} else {
		/* clear PEM_RST_INT */
		addr = PEMX_BASE(pcie_ep_dev->pem_base, 0) + PEM_RST_INT_ENA_W1C;
		npu_csr_write(addr, 0x7);

		addr = PEMX_BASE(pcie_ep_dev->pem_base, 0) + PEM_RST_INT;
		val = npu_csr_read(addr);
		npu_csr_write(addr, val);

		/* enable PERST bit for interrupt */
		addr = PEMX_BASE(pcie_ep_dev->pem_base, 0) + PEM_RST_INT_ENA_W1S;
		npu_csr_write(addr, 0x1);

		printk("PERST IRQ no = %d\n", irq);
		if (devm_request_irq(dev, irq, npu_perst_interrupt, 0,
				     pdev->name, pcie_ep_dev)) {
			dev_warn(dev, "unable to request NPU PERST IRQ %d.\n",
				 irq);
		}
	}


	if (npu_bar_map_init(&pcie_ep_dev->bar_map, pem_num[instance], first_irq, irq_count)) {
		printk("bar map int failed\n");
		return -1;
	}

	if (npu_base_setup(pcie_ep_dev)) {
		printk("Base setup failed\n");
		return -1;
	}

	pcie_ep_dev->plat_dev = dev;
	g_pcie_ep_dev[instance] = pcie_ep_dev;
	npu_handshake_ready(pcie_ep_dev);
	mv_facility_conf_init(pcie_ep_dev);
	npu_device_access_init(instance);


	pcie_ep_dev->ka_thread = kthread_create(ep_keepalive_thread, pcie_ep_dev, "ep_keepalive");
	if (IS_ERR(pcie_ep_dev->ka_thread))
		return PTR_ERR(pcie_ep_dev->ka_thread);

	kthread_bind(pcie_ep_dev->ka_thread, cpumask_first(cpu_online_mask));
	wake_up_process(pcie_ep_dev->ka_thread);

	if (!instance) {
		npu_perst_wq = alloc_workqueue("npu_perst_workq", WQ_UNBOUND | WQ_HIGHPRI | WQ_MEM_RECLAIM, 1);
		if (!npu_perst_wq) {
			dev_err(dev, "Alloc workqueue for PERST handler failed\n");
			goto exit;
		}
		INIT_WORK(&pcie_ep_dev->perst_wrk, npu_perst_handler);
	}

	return 0;

exit:
	devm_kfree(dev, pcie_ep_dev);
	return ret ? -ENODEV : 0;
}

static void npu_base_shutdown(struct platform_device *pdev)
{
	printk("%s: called\n", __func__);

	return;
}

static int npu_base_remove(struct platform_device *pdev)
{
	struct otx_pcie_ep *pcie_ep_dev = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	printk("%s: called\n", __func__);

	if ((!pcie_ep_dev->instance) && npu_perst_wq)
		destroy_workqueue(npu_perst_wq);

	if (pcie_ep_dev->ka_thread)
		kthread_stop(pcie_ep_dev->ka_thread);

	devm_kfree(dev, pcie_ep_dev);

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
	{ .compatible = "marvell,octeontx-ep", .data = &otx_cn83xx},
	{ .compatible = "marvell,octeontx2-ep", .data = &otx2_cn9xxx},
	{ .compatible = "marvell,octeontx3-ep", .data = &otx3_cn10k},
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
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" NPU_BASE_DRV_NAME);
