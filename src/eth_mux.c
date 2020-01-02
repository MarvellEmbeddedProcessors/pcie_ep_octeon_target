/*
 *
 * TODO: License
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
//#include <linux/ioport.h>
//#include <linux/netdevice.h>
//#include <linux/etherdevice.h>
#include <linux/delay.h>
//#include <linux/ethtool.h>
//#include <linux/mii.h>
//#include <linux/crc32.h>
//#include <linux/io.h>
#include <linux/types.h>
#include <linux/version.h>
#include "octeon_device.h"
#include "octeon_poll.h"

MODULE_AUTHOR("Marvell Semiconductors Inc");
MODULE_DESCRIPTION("OcteonTX Host PCI Driver");
MODULE_LICENSE("GPL");

#define MAX_OTX_DEVICES 2
octeon_device_t *oceton_device[MAX_OTX_DEVICES];
uint32_t otx_device_count = 0;
#define otx_assign_dev_name(otx)         \
    sprintf( ((otx)->device_name), "OcteonTX%d", ((otx)->octeon_id))

#define PCI_DMA_64BIT 0xffffffffffffffffULL

#define otx_print_msg printk

#if  LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)
extern int __devinit
octeon_probe(struct pci_dev *pdev, const struct pci_device_id *ent UNUSED);
#else
extern int octeon_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
#endif
extern void octeon_remove(struct pci_dev *pdev);
#ifdef PCIE_AER
pci_ers_result_t otx_pcie_error_detected(struct pci_dev *pdev,
					    pci_channel_state_t state)
{
	printk("%s: invoked; state = %u\n", __func__, state);
	return PCI_ERS_RESULT_DISCONNECT;
}

pci_ers_result_t otx_pcie_mmio_enabled(struct pci_dev *pdev)
{
	printk("%s : \n", __FUNCTION__);
	return PCI_ERS_RESULT_RECOVERED;
}

pci_ers_result_t otx_pcie_slot_reset(struct pci_dev *pdev)
{
	printk("%s : \n", __FUNCTION__);
	return PCI_ERS_RESULT_RECOVERED;
}

void otx_pcie_resume(struct pci_dev *pdev)
{
	printk("%s : \n", __FUNCTION__);
}

/* For PCI-E Advanced Error Recovery (AER) Interface */
static struct pci_error_handlers otx_err_handler = {
	.error_detected = otx_pcie_error_detected,
	.mmio_enabled = otx_pcie_mmio_enabled,
	.slot_reset = otx_pcie_slot_reset,
	.resume = otx_pcie_resume,
};
#endif

#ifndef  DEFINE_PCI_DEVICE_TABLE
#define  DEFINE_PCI_DEVICE_TABLE(otx_pci_table) struct pci_device_id otx_pci_tbl[] __devinitdata
#endif

#define OCTEONTX_VENDOR_ID 0x177D
static DEFINE_PCI_DEVICE_TABLE(otx_pci_tbl) = {
	{
	OCTEONTX_VENDOR_ID, 0xA300, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},	//83xx PF
	{
	0, 0, 0, 0, 0, 0, 0}
};

static octeon_device_t *otx_allocate_device(int pci_id)
{
	int otx_id = 0;
	octeon_device_t *otx = NULL;

	for (otx_id = 0; otx_id < MAX_OTX_DEVICES; otx_id++) {
		if (oceton_device[otx_id] == NULL)
			break;
	}

	if (otx_id == MAX_OTX_DEVICES) {
		printk("OCTEONTX: Could not find empty slot for device pointer. otx_device_count: %d MAX_OTX_DEVICES: %d\n",
			     otx_device_count, MAX_OTX_DEVICES);
		return NULL;
	}

//	otx = otx_allocate_device_mem(pci_id);
	otx = vmalloc(sizeof(octeon_device_t));
	if (otx == NULL) {
		printk("Failed to allocate memory for OcteonTX device\n");
		return NULL;
	}

	otx_device_count++;
	oceton_device[otx_id] = otx;

	otx->octeon_id = otx_id;
	otx_assign_dev_name(otx);

	return otx;
}

void otx_remove(struct pci_dev *pdev)
{
	octeon_device_t *otx_dev = pci_get_drvdata(pdev);
	int otx_id;

	otx_id = otx_dev->octeon_id;
	printk("OCTEONTX: Stopping device %d\n", otx_id);

	pci_release_regions(pdev);
	pci_set_drvdata(pdev, NULL);
	vfree(otx_dev);
	pci_disable_device(pdev);
	printk("OcteonTx: EXIT\n");
}

extern OCTEON_DRIVER_STATUS octeon_state;
int otx_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	octeon_device_t *otx_dev = NULL;

	printk("OCTEONTX: Found device %x:%x\n",
	       (uint32_t) pdev->vendor, (uint32_t) pdev->device);

	otx_dev = otx_allocate_device(pdev->device);
	if (otx_dev == NULL)
		return -ENOMEM;

	printk("OCTEONTX: Setting up OcteonTX device %d \n", otx_dev->octeon_id);

	/* Assign oceton_device for this device to the private data area. */
	pci_set_drvdata(pdev, otx_dev);

	/* set linux specific device pointer */
	otx_dev->pci_dev = (void *)pdev;

	if (octeon_device_init(otx_dev)) {
		otx_remove(pdev);
		return -ENOMEM;
	}
	octeon_state = OCT_DRV_ACTIVE;

	printk("OCTEONTX: OcteonTX device %d is ready\n", otx_dev->octeon_id);
	return 0;
}

static struct pci_driver otx_pci_driver = {
	.name = "OcteonTX",
	.id_table = otx_pci_tbl,
	.probe = octeon_probe,
	.remove = octeon_remove,
#ifdef PCIE_AER
	/* For AER */
	.err_handler = &otx_err_handler,
#endif
};

static void otx_init_device_list(void)
{
	memset(oceton_device, 0, (sizeof(void *) * MAX_OTX_DEVICES));
}

#define OTX_VERSION "DEVEL-0.1"
int otx_base_init_module(void)
{
	int ret;

	otx_print_msg("OCTEONTX: Loading PCI driver (base module); Version: %s\n",
		      OTX_VERSION);

	octeon_state = OCT_DRV_DEVICE_INIT_START;
	otx_init_device_list();

	ret = pci_register_driver(&otx_pci_driver);
	if (ret < 0) {
		printk("OCTEONTX: pci_module_init() returned %d\n", ret);
		printk("OCTEONTX: Your kernel may not be configured for hotplug\n");
		printk("          and no OcteonTX devices were detected\n");
		return ret;
	}
	octeon_state = OCT_DRV_DEVICE_INIT_DONE;

	if (octeon_init_poll_thread()) {
		cavium_error("OCTEON: Poll thread creation failed\n");
		return -ENODEV;
	}
	octeon_state = OCT_DRV_POLL_INIT_DONE;

	otx_print_msg("OCTEONTX: PCI driver (base module) is ready!\n");

	return ret;
}

void otx_base_exit_module(void)
{
	otx_print_msg("OCTEONTX: Preparing to unload OcteonTX PCI driver (base module)\n");

	octeon_delete_poll_thread();
	pci_unregister_driver(&otx_pci_driver);

	otx_print_msg("OCTEONTX: Stopped OcteonTX PCI driver (base module)\n");
}

module_init(otx_base_init_module);
module_exit(otx_base_exit_module);
