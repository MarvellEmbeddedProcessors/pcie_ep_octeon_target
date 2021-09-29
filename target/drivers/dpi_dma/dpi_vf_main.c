/* Copyright (c) 2020 Marvell.
 * SPDX-License-Identifier: GPL-2.0
 */

/* DPI DMA VF driver
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/iommu.h>
#include <linux/fs.h>
#include <linux/dmapool.h>

#include "dpi.h"
#include "dpi_vf.h"
#include "dpi_cmd.h"
#include "dma_api.h"
#include "soc_api.h"

#define DRV_NAME				"octeontx-dpi-vf"
#define DRV_VERSION				"2.0"
#define PCI_VENDOR_ID_CAVIUM			0x177d
#define PCI_DEVICE_ID_OCTEONTX_DPI_VF_83XX 	0xA058
#define PCI_DEVICE_ID_OCTEONTX_DPI_VF_93XX	0xA081
#define PCI_SUBSYS_DEVID_CN10K_A		0xB900

#define DPI0_PCI_BUS			5
#define DPI_VF_PCI_CFG_BAR0 		0
#define DPI_VF_GMID 			0x5
#define DPI_MAX_PFS			2
#define DPI_MAX_QUEUES 			(8 * DPI_MAX_PFS)

unsigned int pem_num = 0;
module_param(pem_num, uint, 0644);
MODULE_PARM_DESC(pem_num, "pem number to use");

static int num_vfs[DPI_MAX_PFS];
static int total_vfs;
static struct dpivf_t* vf[DPI_MAX_QUEUES] = { 0 };
static unsigned int vf_domain_id = DPI_VF_GMID;

static const struct pci_device_id dpi_id_table[] = {
 	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVICE_ID_OCTEONTX_DPI_VF_83XX) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVICE_ID_OCTEONTX_DPI_VF_93XX) },
	{ 0, }	/* end of table */
};

/* List of supported SOC's. Add entry for new soc
 * Function pointer's cannot be NULL.
 */
static struct vf_soc_ops socs[SOC_MAX] = {
	{
		.init = otx_init,
		.open = otx_open,
		.buf_alloc = otx_buf_alloc,
		.buf_free = otx_buf_free,
		.fill_header = otx_fill_header,
		.dma_to_host = otx_dma_to_host,
		.dma_sync = otxx_dma_sync,
		.dma_async_vector = otxx_dma_async_vector,
		.dma_sync_sli = otxx_dma_sync_sli,
		.host_writel = otx_host_writel,
		.host_map_writel = otxx_host_map_writel,
		.host_iounmap = otxx_host_iounmap,
		.host_ioremap = otxx_host_ioremap,
		.close = otx_close,
		.cleanup = otx_cleanup
	},
	{
		.init = otx2_init,
		.open = otx2_open,
		.buf_alloc = otx2_buf_alloc,
		.buf_free = otx2_buf_free,
		.fill_header = otx2_fill_header,
		.dma_to_host = otx2_dma_to_host,
		.dma_sync = otxx_dma_sync,
		.dma_async_vector = otxx_dma_async_vector,
		.dma_sync_sli = otxx_dma_sync_sli,
		.host_writel = otx2_host_writel,
		.host_map_writel = otxx_host_map_writel,
		.host_iounmap = otxx_host_iounmap,
		.host_ioremap = otx2_host_ioremap,
		.close = otx2_close,
		.cleanup = otx2_cleanup
	},
	{
		.init = otx2_init,
		.open = otx2_open,
		.buf_alloc = otx2_buf_alloc,
		.buf_free = otx2_buf_free,
		.fill_header = cn10k_fill_header,
		.dma_to_host = otx2_dma_to_host,
		.dma_sync = otxx_dma_sync,
		.dma_async_vector = otxx_dma_async_vector,
		.dma_sync_sli = otxx_dma_sync_sli,
		.host_writel = otx2_host_writel,
		.host_map_writel = otxx_host_map_writel,
		.host_iounmap = otxx_host_iounmap,
		.host_ioremap = otx2_host_ioremap,
		.close = otx2_close,
		.cleanup = otx2_cleanup
	},
};

struct vf_soc_ops *soc = NULL; /* Soc detected at startup */
/* First vf is shared by cores which do not get dedicated dpi queue */
struct dpivf_t* shared_vf = NULL;

int get_dpi_dma_dev_count(int handle)
{
	int instance, type;

	instance = (handle >> 4) & 0xf;
	type = handle & 0xf;

	/* rpc gets all available devices, everyone else gets 1 device */
	return (type == HANDLE_TYPE_RPC) ? num_vfs[instance] : (num_vfs[instance] != 0);
}
EXPORT_SYMBOL(get_dpi_dma_dev_count);

struct device *get_dpi_dma_dev(int handle, int index)
{
	int instance, actual_idx;

	instance = (handle >> 4) & 0xf;

	if (index < 0 || index >= num_vfs[instance])
		return NULL;

	actual_idx = index;

	if (instance)
		actual_idx = num_vfs[0] + index;

	return (vf[actual_idx]) ? vf[actual_idx]->dev : NULL;
}
EXPORT_SYMBOL(get_dpi_dma_dev);

void host_writel(uint32_t val,  void __iomem *host_addr)
{
	soc->host_writel(shared_vf, val, host_addr);
}
EXPORT_SYMBOL(host_writel);

void host_map_writel(host_dma_addr_t host_addr, uint32_t val)
{
	soc->host_map_writel(shared_vf, host_addr, val);
}
EXPORT_SYMBOL(host_map_writel);

/* return cpu_addr to be used to read/write to a given
 * host phys_addr
 */
void __iomem *host_ioremap(host_dma_addr_t host_addr)
{
	return soc->host_ioremap(shared_vf, host_addr);
}
EXPORT_SYMBOL(host_ioremap);

void host_iounmap(void __iomem *addr)
{
	soc->host_iounmap(shared_vf, addr);
}
EXPORT_SYMBOL(host_iounmap);

/*
 * do_dma_sync() - API to transfer data between host and target
 *
 *  @local_dma_addr: DMA address allocated by target
 *  @host_dma_addr: DMA address allocated by host
 *  @local_virt_addr: Virtual address allocated by target
 *  @len: Size of bytes to be transferred
 *  @dir: Direction of the transfer
 */
int do_dma_sync(struct device *dev, local_dma_addr_t local_dma_addr,
		host_dma_addr_t host_dma_addr, void *local_virt_addr,
		int len, host_dma_dir_t dir)
{
	return soc->dma_sync((struct dpivf_t*)dev_get_drvdata(dev),
			     local_dma_addr, host_dma_addr, NULL, len, dir);
}
EXPORT_SYMBOL(do_dma_sync);

int do_dma_sync_dpi(struct device *dev, local_dma_addr_t local_dma_addr,
		    host_dma_addr_t host_addr, void *local_ptr,
		    int len, host_dma_dir_t dir)
{
	return soc->dma_sync((struct dpivf_t *)dev_get_drvdata(dev),
			     local_dma_addr, host_addr, local_ptr, len, dir);
}
EXPORT_SYMBOL(do_dma_sync_dpi);

int do_dma_sync_sli(host_dma_addr_t local_addr, host_dma_addr_t host_addr,
		    void *virt_addr, int len, host_dma_dir_t dir)
{
	return soc->dma_sync_sli(shared_vf, local_addr, host_addr,
				 virt_addr, len, dir);

}
EXPORT_SYMBOL(do_dma_sync_sli);

/* caller needs to provide iova addrs, and completion_word,
 * caller needs to poll completion word we use only the common fields in hdr
 * for 8xxx and 9xxx hence dont differentiate
 */
int do_dma_async_dpi_vector(struct device* dev, local_dma_addr_t *local_addr,
			    host_dma_addr_t *host_addr, int *len, int num_ptrs,
			    host_dma_dir_t dir, local_dma_addr_t comp_iova)
{
	return soc->dma_async_vector((struct dpivf_t *)dev_get_drvdata(dev),
				     local_addr, host_addr, len, num_ptrs,
				     dir, comp_iova);
}
EXPORT_SYMBOL(do_dma_async_dpi_vector);

int dpi_vf_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	int err;
	struct dpivf_t* dpi_vf;
	char kobj_name[16];

	dpi_vf = devm_kzalloc(dev, sizeof(struct dpivf_t), GFP_KERNEL);
	if (!dpi_vf) {
		dev_err(dev, "Unable to allocate DPI VF device\n");
		return -ENOMEM;
	}

	dev_set_drvdata(dev, dpi_vf);
	dpi_vf->dev = dev;
	dpi_vf->pdev = pdev;
	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device\n");
		goto pcim_dev_enable_fail;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "PCI request regions failed 0x%x\n", err);
		goto pci_req_region_fail;
	}

	/* MAP PF's configuration registers */
	dpi_vf->reg_base = pcim_iomap(pdev, DPI_VF_PCI_CFG_BAR0, 0);
	if (!dpi_vf->reg_base) {
		dev_err(dev, "Cannot map config register space, aborting\n");
		goto pcim_iomap_fail;
	}

	sprintf(kobj_name, "dpi_kobj_%d", vf_domain_id);
	dpi_vf->kobj = kobject_create_and_add(kobj_name, NULL);
	if (!dpi_vf->kobj) {
		dev_err(dev, "Failed to create dpi vf Kobject\n");
		goto kobj_create_fail;
	}
	dpi_vf->domain = vf_domain_id++;
	dpi_vf->vf_id = pdev->devfn - 1;

	if (!soc) {
		switch (pdev->device) {
		case PCI_DEVICE_ID_OCTEONTX_DPI_VF_83XX:
			soc = &socs[SOC_OCTEONTX];
			break;
		case PCI_DEVICE_ID_OCTEONTX_DPI_VF_93XX:
			soc = &socs[SOC_OCTEONTX2];
			/* DPI in CN10K shares the same devid as in OTX2.
			 * So use subsystem devid for differentiating.
			 */
			if (pdev->subsystem_device >= PCI_SUBSYS_DEVID_CN10K_A)
				soc = &socs[SOC_CN10K];
			break;
		}

		err = soc->init();
		if (err != 0)
			goto soc_init_fail;
	}

	err = soc->open(dpi_vf);
	if (err != 0) {
		dev_err(dev, "Failed to initialize dpi vf\n");
		goto vf_open_fail;
	}

	if (pdev->bus->number == DPI0_PCI_BUS)	/* DPI-0 */
		num_vfs[0] = num_vfs[0] + 1;
	else				/* DPI-1 */
		num_vfs[1] = num_vfs[1] + 1;

	vf[total_vfs++] = dpi_vf;
	if(num_vfs[0] == 1)
		shared_vf = dpi_vf;

	return 0;

vf_open_fail:
soc_init_fail:
	kobject_del(dpi_vf->kobj);
kobj_create_fail:
	pcim_iounmap(pdev, dpi_vf->reg_base);
pcim_iomap_fail:
	pci_release_regions(pdev);
pci_req_region_fail:
	pci_disable_device(pdev);
pcim_dev_enable_fail:
	pci_set_drvdata(pdev, NULL);
	devm_kfree(dev, dpi_vf);
	return err;
}

static void dpi_remove(struct pci_dev *pdev)
{
	struct dpivf_t *dpi_vf = dev_get_drvdata(&pdev->dev);
	soc->close(dpi_vf);
	kobject_del(dpi_vf->kobj);
	devm_kfree(dpi_vf->dev, dpi_vf);

	if (pdev->bus->number == DPI0_PCI_BUS)	/* DPI-0 */
		num_vfs[0] = num_vfs[0] - 1;
	else				/* DPI-1 */
		num_vfs[1] = num_vfs[1] - 1;
	total_vfs--;
}

static struct pci_driver dpi_vf_driver = {
	.name = DRV_NAME,
	.id_table = dpi_id_table,
	.probe = dpi_vf_probe,
	.remove = dpi_remove,
};


static int __init dpi_vf_init_module(void)
{
	return pci_register_driver(&dpi_vf_driver);
}

static void dpi_vf_exit_module(void)
{
	pci_unregister_driver(&dpi_vf_driver);
	if (soc)
		soc->cleanup();
}

module_init(dpi_vf_init_module);
module_exit(dpi_vf_exit_module);
MODULE_AUTHOR("Cavium");
MODULE_DESCRIPTION("Cavium OcteonTX DPI Virtual Function Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, dpi_id_table);
