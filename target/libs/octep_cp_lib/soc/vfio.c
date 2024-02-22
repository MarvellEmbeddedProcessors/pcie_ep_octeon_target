/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/vfio.h>

#include "octep_cp_lib.h"
#include "cp_log.h"
#include "cp_lib.h"
#include "cnxk_hw.h"
#include "cp_compat.h"

#define MAX_DPI_ENGINES 6

#define PEM_BAR0_START(pem_idx) (0x8E0000000000ULL | ((uint64_t)pem_idx << 36))
#define PEM_BAR4_START(pem_idx) (0x8E0F00000000ULL | ((uint64_t)pem_idx << 36))
#define DPI_BAR0_START(dpi_idx) (0x86e000000000ULL | ((uint64_t)dpi_idx << 36))

#define DPI_DMA_CONTROL_DMA_ENB(x)      (((x) & 0x3fULL) << 48)

#define DPI_DMA_CONTROL_O_MODE                  (0x1ULL << 14)
#define DPI_DMA_CONTROL_O_NS                    (0x1ULL << 17)
#define DPI_DMA_CONTROL_O_RO                    (0x1ULL << 18)
#define DPI_DMA_CONTROL_O_ADD1                  (0x1ULL << 19)
#define DPI_DMA_CONTROL_LDWB                    (0x1ULL << 32)
#define DPI_DMA_CONTROL_NCB_TAG_DIS             (0x1ULL << 34)
#define DPI_DMA_CONTROL_WQECSMODE1              (0x1ULL << 37)
#define DPI_DMA_CONTROL_ZBWCSEN                 (0x1ULL << 39)
#define DPI_DMA_CONTROL_WQECSOFF(offset)        (((uint64_t)offset) << 40)
#define DPI_DMA_CONTROL_WQECSDIS                (0x1ULL << 47)
#define DPI_DMA_CONTROL_UIO_DIS                 (0x1ULL << 55)
#define DPI_DMA_CONTROL_PKT_EN                  (0x1ULL << 56)
#define DPI_DMA_CONTROL_FFP_DIS                 (0x1ULL << 59)

#define DPI_EBUS_PORTX_CFG_MRRS(x)              (((x) & 0x7) << 0)
#define DPI_EBUS_PORTX_CFG_MPS(x)               (((x) & 0x7) << 4)

#define DPI_EBUS_PORTS          2
#define DPI_MRRS                128
#define DPI_MPS                 128
#define DPI_DMA_FIFO_SIZE_8KB   0x8
#define DPI_DMA_FIFO_SIZE_16KB  0x10
#define DPI_DMA_ENGINE_MASK_ALL 0x3F
#define DPI_WCTL_THR            0x30

#define DPI_CTL                  0x10010
#define DPI_DMA_CONTROL          0x10018
#define DPI_ENG_BUF_START        0x100C0
#define DPI_EBUS_PORT_CFG_START  0x10100
#define DPI_WCTL_FIF_THR         0x17008

#define DPI_ENG_BUF(eng)         (DPI_ENG_BUF_START | (eng << 3))
#define DPI_EBUS_PORT_CFG(port)  (DPI_EBUS_PORT_CFG_START | (port << 3))

#define DPI_CTL_EN               BIT_ULL(0)

#define BAD_PHYS_ADDR            (-1ULL)

#define PFN_MASK 0x7fffffffffffffULL
#define PEM_BAR4_IDX_IOVA_SHIFT 22
#define PEMx_BAR4_INDEX_OFFSET(idx) (0x700 + (idx << 3))

union cnxk_pem_bar4_idx {
	uint64_t val;
	struct {
		uint64_t addr_v:1; /* bit 0 */
		uint64_t rsvd1:2; /* bits 2:1 */
		uint64_t ca:1; /* bit 3 */
		uint64_t addr_idx:31; /* bits 34:4 */
		uint64_t rsvd2:29; /* bits 65:35 */
	} s;
};

/* Close the VFIO container used to access DPI and PEM devices */
void cnxk_destroy_vfio_container(struct octep_vfio_info *vfio_info)
{
	if (vfio_info->container) {
		close(vfio_info->container);
		vfio_info->container = 0;
	}
}

/* Create a VFIO container to access DPI and PEM devices */
int cnxk_create_vfio_container(struct octep_vfio_info *vfio_info)
{
	int container;

	container = open("/dev/vfio/vfio", O_RDWR);

	if (container < 0) {
		CP_LIB_LOG(ERR, CNXK, "failed to open VFIO device; err=%d\n", errno);
		return -1;
	}

	if (ioctl(container, VFIO_GET_API_VERSION) != VFIO_API_VERSION) {
		CP_LIB_LOG(ERR, CNXK, "Invalid API version; err=%d\n", errno);
		goto shutdown_container;
	}

	if (!ioctl(container, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU)) {
		/* Doesn't support the IOMMU driver required. */
		CP_LIB_LOG(ERR, CNXK,
			   "Doesn't support the IOMMU TYPE1; err=%d\n", errno);
		goto shutdown_container;
	}

	CP_LIB_LOG(INFO, CNXK, "Created VFIO container successfully; fd=%d\n", container);
	vfio_info->container = container;
	return 0;

shutdown_container:
	close(container);
	return -1;
}

extern struct octep_cp_lib_cfg *lib_cfg;
void *cnxk_pem_map_reg(int pem_idx, unsigned long long addr)
{
	uint64_t bar_offset;
	int is_pem_reg = 0;
	int bar_idx;

	if ((addr & PEM_BAR4_START(pem_idx)) == PEM_BAR4_START(pem_idx)) {
		is_pem_reg = 1;
		bar_idx = 4;
		bar_offset = addr - PEM_BAR4_START(pem_idx);
	} else if ((addr & PEM_BAR0_START(pem_idx)) == PEM_BAR0_START(pem_idx)) {
		is_pem_reg = 1;
		bar_idx = 0;
		bar_offset = addr - PEM_BAR0_START(pem_idx);
	} else if ((addr & DPI_BAR0_START(0)) == DPI_BAR0_START(0)) {
		bar_idx = 0;
		bar_offset = addr - DPI_BAR0_START(0);
	} else {
		CP_LIB_LOG(ERR, CNXK, "pem_dpi_map_reg: Invalid addr 0x%llx\n", addr);
		return NULL;
	}

	if (is_pem_reg) {
		if (lib_cfg->vfio.pem_region_size[bar_idx] < bar_offset) {
			CP_LIB_LOG(ERR, CNXK,
				   "pem_map_reg: addr=0x%llx (offset=0x%llx) is beyond BAR-%d size of 0x%lx\n",
				   addr, bar_offset, bar_idx,
				   lib_cfg->vfio.pem_region_size[bar_idx]);
			return NULL;
		}

		return (lib_cfg->vfio.pem_region_base[bar_idx] + bar_offset);
	}

	if (lib_cfg->vfio.dpi_region_size[bar_idx] < bar_offset) {
		CP_LIB_LOG(ERR, CNXK,
			   "dpi_map_reg: addr=0x%llx (offset=0x%llx) is beyond BAR-%d size of 0x%lx\n",
			   addr, bar_offset, bar_idx, lib_cfg->vfio.dpi_region_size[bar_idx]);
		return NULL;
	}
	return (lib_cfg->vfio.dpi_region_base[bar_idx] + bar_offset);
}

#define PEM_RST_INT(x)          (0x300ULL + ((uint64_t)(x) << 36))
#define PEM_RST_INT_ENA_W1C(x)  (0x310ULL + ((uint64_t)(x) << 36))
#define PEM_RST_INT_ENA_W1S(x)  (0x318ULL + ((uint64_t)(x) << 36))
#define PEM_CFG(x)              (0x0D8ULL + ((uint64_t)(x) << 36))

#define PEM_CFG_B_AUTO_DP_CLR   0x0100

#define PEM_RST_INT_B_L2        0x0004
#define PEM_RST_INT_B_LINKDOWN  0x0002
#define PEM_RST_INT_B_PERST     0x0001

/* FIXME: rename to check_ or poll_ ? */
int cnxk_assert_perst_intr(struct octep_vfio_info *vfio)
{
	uint64_t word;

	if (pread(vfio->pem_device_fd, &word, sizeof(uint64_t),
		  vfio->pem_region_offset[0] + PEM_RST_INT(0)) < 0) {
		CP_LIB_LOG(ERR, LIB, "Failed to assert perst irq; could not read BAR region\n");
		return -EINVAL;
	}

	if (word & PEM_RST_INT_B_PERST) {
		CP_LIB_LOG(INFO, LIB, "Got PERST\n");
		return 0;
	} else {
		return -EAGAIN;
	}
}

static int cnxk_register_perst_intr(struct octep_vfio_info *vfio)
{
	struct vfio_irq_info irq_info;
	uint64_t word;

	memset(&irq_info, 0x0, sizeof(struct vfio_irq_info));
	irq_info.argsz = sizeof(struct vfio_irq_info);
	irq_info.index = VFIO_PCI_MSIX_IRQ_INDEX;

	if (ioctl(vfio->pem_device_fd, VFIO_DEVICE_GET_IRQ_INFO, &irq_info)) {
		fprintf(stderr, "failed to get device irq info\n");
		goto register_failed;
	}
	CP_LIB_LOG(INFO, CNXK, "Number of PEM interrupts = %d\n", irq_info.count);

	/* STEP1: Disable interrupts */
	word = (PEM_RST_INT_B_L2 | PEM_RST_INT_B_LINKDOWN | PEM_RST_INT_B_PERST);
	if (pwrite(vfio->pem_device_fd, &word, sizeof(uint64_t),
		   vfio->pem_region_offset[0] + PEM_RST_INT_ENA_W1C(0)) < 0) {
		CP_LIB_LOG(ERR, CNXK, "Failed to write PEM_RST_INT_ENA_W1C\n");
		goto register_failed;
	}

	/* STEP2: Clear outstanding interrupts */
	if (pread(vfio->pem_device_fd, &word, sizeof(uint64_t),
		  vfio->pem_region_offset[0] + PEM_RST_INT(0)) < 0) {
		CP_LIB_LOG(ERR, CNXK, "Failed to read PEM_RST_INT\n");
		goto register_failed;
	}
	if (pwrite(vfio->pem_device_fd, &word, sizeof(uint64_t),
		   vfio->pem_region_offset[0] + PEM_RST_INT(0)) < 0) {
		CP_LIB_LOG(ERR, CNXK, "Failed to write PEM_RST_INT\n");
		goto register_failed;
	}

	/* STEP3: Auto clear DISPORT after PERST */
	if (pread(vfio->pem_device_fd, &word, sizeof(uint64_t),
		  vfio->pem_region_offset[0] + PEM_CFG(0)) < 0) {
		CP_LIB_LOG(ERR, CNXK, "Failed to read PEM_CFG\n");
		goto register_failed;
	}
	word |= PEM_CFG_B_AUTO_DP_CLR;
	if (pwrite(vfio->pem_device_fd, &word, sizeof(uint64_t),
		   vfio->pem_region_offset[0] + PEM_CFG(0)) < 0) {
		CP_LIB_LOG(ERR, CNXK, "Failed to read PEM_CFG\n");
		goto register_failed;
	}

	/* FIXME: was link down enabled in existing lib/app ? */
	/* STEP4: Enable LinkDown and PERST */
	word = (PEM_RST_INT_B_LINKDOWN | PEM_RST_INT_B_PERST);
	if (pwrite(vfio->pem_device_fd, &word, sizeof(uint64_t),
		   vfio->pem_region_offset[0] + PEM_RST_INT_ENA_W1S(0)) < 0) {
		CP_LIB_LOG(ERR, CNXK, "Failed to write PEM_RST_INT_ENA_W1S\n");
		goto register_failed;
	}

	CP_LIB_LOG(INFO, CNXK, "Enabled PEM link down and PERST interrupts\n");
	return 0;

register_failed:
	return -EFAULT;
}

int cnxk_enable_perst_intr(struct octep_vfio_info *vfio)
{
	uint64_t word;

	word = (PEM_RST_INT_B_LINKDOWN | PEM_RST_INT_B_PERST);
	if (pwrite(vfio->pem_device_fd, &word, sizeof(uint64_t),
		   vfio->pem_region_offset[0] + PEM_RST_INT_ENA_W1S(0)) < 0) {
		CP_LIB_LOG(ERR, CNXK, "Failed to write PEM_RST_INT_ENA_W1S\n");
		return -EBUSY;
	}

	return 0;
}

int cnxk_clear_perst_intr(struct octep_vfio_info *vfio)
{
	uint64_t word;

	if (pread(vfio->pem_device_fd, &word, sizeof(uint64_t),
		  vfio->pem_region_offset[0] + PEM_RST_INT(0)) < 0) {
		CP_LIB_LOG(ERR, CNXK, "Failed to read PEM_RST_INT\n");
		return -EBUSY;
	}

	if (pwrite(vfio->pem_device_fd, &word, sizeof(uint64_t),
		   vfio->pem_region_offset[0] + PEM_RST_INT(0)) < 0) {
		CP_LIB_LOG(ERR, CNXK, "Failed to write PEM_RST_INT\n");
		return -EBUSY;
	}
	return 0;
}

int cnxk_disable_perst_intr(struct octep_vfio_info *vfio)
{
	uint64_t word;

	word = (PEM_RST_INT_B_LINKDOWN | PEM_RST_INT_B_PERST);
	if (pwrite(vfio->pem_device_fd, &word, sizeof(uint64_t),
		   vfio->pem_region_offset[0] + PEM_RST_INT_ENA_W1C(0)) < 0) {
		CP_LIB_LOG(ERR, CNXK, "Failed to write PEM_RST_INT_ENA_W1C\n");
		return -EBUSY;
	}

	return 0;
}

static unsigned long virt_to_phys(void *virt)
{
	int page_size = getpagesize();
	unsigned long virtual = (unsigned long)virt;
	unsigned long aligned = (virtual & ~(page_size - 1));
	uint64_t page;
	off_t offset;
	int fdmem;

	/* allocate page in physical memory and prevent from swapping */
	mlock((void *)aligned, page_size);

	fdmem = open("/proc/self/pagemap", O_RDONLY);
	if (fdmem < 0) {
		CP_LIB_LOG(ERR, CNXK,
			   "failed to convert virt to phys addr; cannot open pagemap\n");
		return BAD_PHYS_ADDR;
	}
	offset = (off_t) (virtual / page_size) * sizeof(uint64_t);
	if (lseek(fdmem, offset, SEEK_SET) == (off_t) -1) {
		CP_LIB_LOG(ERR, CNXK, "cannot lseek() in pagemap\n");
		close(fdmem);
		return BAD_PHYS_ADDR;
	}
	if (read(fdmem, &page, sizeof(uint64_t)) <= 0) {
		CP_LIB_LOG(ERR, CNXK, "cannot read pagemap\n");
		close(fdmem);
		return BAD_PHYS_ADDR;
	}
	close(fdmem);

	/* pfn (page frame number) are bits 0-54 (see pagemap.txt in Linux doc) */
	return ((page & PFN_MASK) * page_size) + (virtual % page_size);
}

static int cnxk_pem_setup_mbox_memory(struct octep_vfio_info *vfio)
{
	union cnxk_pem_bar4_idx bar4_idx = {0};
	void *pem_bar0 = vfio->pem_region_base[0];
	int length = PEMX_BAR4_INDEX_SIZE;
	unsigned long paddr;
	void *addr;

	addr = mmap(0, length, PROT_READ|PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
	if (addr == MAP_FAILED) {
		CP_LIB_LOG(ERR, CNXK, "Failed to map mailbox memory\n");
		return -1;
	}

	paddr = virt_to_phys(addr);
	CP_LIB_LOG(DEBUG, CNXK, "CP mailbox: virt_addr = %p; phys_addr = 0x%lx\n", addr, paddr);

	bar4_idx.s.addr_v = 1;
	bar4_idx.s.addr_idx = paddr >> PEM_BAR4_IDX_IOVA_SHIFT;
	cp_write64(bar4_idx.val, pem_bar0 + PEMx_BAR4_INDEX_OFFSET(PEMX_BAR4_INDEX_MBOX));
	vfio->mbox_mem = addr;
	return 0;
}

static void cnxk_pem_uninit(struct octep_vfio_info *vfio_info)
{
	int idx;
	for (idx = 0; idx < VFIO_PCI_NUM_REGIONS; idx++) {
		if (vfio_info->pem_region_base[idx])
			munmap(vfio_info->pem_region_base[idx], vfio_info->pem_region_size[idx]);
	}
}

int cnxk_pem_init(struct octep_vfio_info *vfio_info)
{
	struct vfio_group_status group_status = { .argsz = sizeof(group_status) };
	struct vfio_device_info device_info = { .argsz = sizeof(device_info) };
	struct vfio_region_info reg = { .argsz = sizeof(reg) };
	int container, group, device, ret, i;
	char filepath[FILENAME_MAX];
	void *mem;

	container = vfio_info->container;

	/* Open the group */
	snprintf(filepath, sizeof(filepath), "%s%d", "/dev/vfio/", vfio_info->pem_iommu);
	group = open(filepath, O_RDWR);
	if (group < 0) {
		CP_LIB_LOG(ERR, CNXK,
				"failed to open PEM VFIO group at %s; err=%d\n", filepath, errno);
		return -1;
	}

	/* Test the group is viable and available */
	ret = ioctl(group, VFIO_GROUP_GET_STATUS, &group_status);
	if (ret == -1) {
		CP_LIB_LOG(ERR, CNXK,
			   "Failed to get VFIO group status for PEM; err=%d\n", ret);
		goto close_group;
	}

	if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
		/* Group is not viable (ie, not all devices bound for vfio) */
		CP_LIB_LOG(ERR, CNXK,
			   "VFIO Group is not viable; check if PEM device bound to vfio driver\n");
		goto close_group;
	}

	/* Add the group to the container */
	ret = ioctl(group, VFIO_GROUP_SET_CONTAINER, &container);
	if (ret == -1) {
		CP_LIB_LOG(ERR, CNXK,
			   "Failed to add PEM VFIO group to the container; ret=%d\n", ret);
		goto close_group;
	}

	/* Get a file descriptor for the device */
	device = ioctl(group, VFIO_GROUP_GET_DEVICE_FD, vfio_info->pem_dev);
	if (device == -1) {
		CP_LIB_LOG(ERR, CNXK, "Failed to get PEM device VFIO FD; ret=%d\n", device);
		goto close_group;
	}
	vfio_info->pem_device_fd = device;

	/* Test and setup the device */
	ret = ioctl(device, VFIO_DEVICE_GET_INFO, &device_info);
	if (ret == -1) {
		CP_LIB_LOG(ERR, CNXK, "Failed to get PEM VFIO device info; ret=%d\n", ret);
		goto close_group;
	}

	/* map active BAR regions */
	for (i = 0; i <= VFIO_PCI_BAR5_REGION_INDEX; i++) {
		reg.index = i;
		ret = ioctl(device, VFIO_DEVICE_GET_REGION_INFO, &reg);
		if (ret == -1) {
			CP_LIB_LOG(ERR, CNXK,
				   "Failed to get PEM device info for region-%d; ret=%d\n",
				   reg.index, ret);
			goto uninit_pem;
		}

		if (!(reg.flags & VFIO_REGION_INFO_FLAG_MMAP))
			continue;
		if (!reg.size)
			continue;

		mem = mmap(NULL, reg.size, PROT_READ | PROT_WRITE, MAP_SHARED, device, reg.offset);
		if (mem == MAP_FAILED) {
			CP_LIB_LOG(ERR, CNXK, "failed to mmap PEM region-%d\n", reg.index);
			goto uninit_pem;
		}
		CP_LIB_LOG(DEBUG, CNXK, "mapped PEM device region-%d; size=0x%llx.\n",
				reg.index, reg.size);
		vfio_info->pem_region_base[i] = mem;
		vfio_info->pem_region_offset[i] = reg.offset;
		vfio_info->pem_region_size[i] = reg.size;
	}

	if (cnxk_pem_setup_mbox_memory(vfio_info)) {
		CP_LIB_LOG(ERR, CNXK, "Failed to setup mailbox memory\n");
		goto uninit_pem;
	}

	if (cnxk_register_perst_intr(vfio_info)) {
		CP_LIB_LOG(ERR, CNXK, "Failed to configure PERST interrupt\n");
		goto uninit_pem;
	}
	return 0;

uninit_pem:
	printf("PEM init failed\n");
	cnxk_pem_uninit(vfio_info);

close_group:
	close(group);
	return -1;
}

int cnxk_dpi_init(struct octep_vfio_info *vfio_info)
{
	struct vfio_group_status group_status = { .argsz = sizeof(group_status) };
	struct vfio_device_info device_info = { .argsz = sizeof(device_info) };
	struct vfio_region_info reg = { .argsz = sizeof(reg) };
	int container, group, device, ret;
	char filepath[FILENAME_MAX];
	int eng = 0, port = 0;
	uint64_t regval;
	int mps, mrrs;
	void *mem;

	CP_LIB_LOG(INFO, CNXK, "Initializing DPI ...\n");
	container = vfio_info->container;

	/* Open the group */
	snprintf(filepath, sizeof(filepath), "%s%d", "/dev/vfio/", vfio_info->dpi_iommu);
	group = open(filepath, O_RDWR);
	if (group < 0) {
		CP_LIB_LOG(ERR, CNXK,
				"failed to open DPI VFIO group; err=%d\n", errno);
		return -1;
	}

	ret = ioctl(group, VFIO_GROUP_GET_STATUS, &group_status);
	if (ret == -1) {
		CP_LIB_LOG(ERR, CNXK,
			   "Failed to get VFIO group status for DPI; err=%d\n", ret);
		goto close_group;
		return ret;
	}

	if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
		CP_LIB_LOG(ERR, CNXK,
			   "VFIO Group is not viable; check if DPI device bound to vfio driver\n");
		goto close_group;
	}

	/* Add the group to the container */
	ret = ioctl(group, VFIO_GROUP_SET_CONTAINER, &container);
	if (ret == -1) {
		CP_LIB_LOG(ERR, CNXK,
			   "Failed to add DPI VFIO group to the container; ret=%d\n", ret);
		goto close_group;
	}

	/* To be done only once; duplicate calls will fail */
	ret = ioctl(container, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU);
	if (ret == -1) {
		CP_LIB_LOG(ERR, CNXK, "Failed to set IOMMU model; ret=%d\n", ret);
		goto close_group;
	}

	/* Get a file descriptor for the device */
	device = ioctl(group, VFIO_GROUP_GET_DEVICE_FD, vfio_info->dpi_dev);
	if (device == -1) {
		CP_LIB_LOG(ERR, CNXK, "Failed to get DPI device VFIO FD; ret=%d\n", device);
		goto close_group;
	}
	vfio_info->dpi_device_fd = device;

	/* Test and setup the device */
	ret = ioctl(device, VFIO_DEVICE_GET_INFO, &device_info);
	if (ret == -1) {
		CP_LIB_LOG(ERR, CNXK, "Failed to get DPI VFIO device info; ret=%d\n", ret);
		goto close_group;
	}

	reg.index = 0;
	ret = ioctl(device, VFIO_DEVICE_GET_REGION_INFO, &reg);
	if (ret == -1) {
		CP_LIB_LOG(ERR, CNXK,
			   "Failed to get DPI device info for region-%d; ret=%d\n",
			   reg.index, ret);
		goto fail;
	} else {
		mem = mmap(NULL, reg.size, PROT_READ | PROT_WRITE, MAP_SHARED, device, reg.offset);
		if (mem == MAP_FAILED) {
			CP_LIB_LOG(ERR, CNXK, "failed to mmap DPI region-%d\n", reg.index);
			/* FIXME: replace with uninit_dpi or uninit_pem_dpi */
			goto fail;
		}
		CP_LIB_LOG(DEBUG, CNXK, "mapped DPI device region-%d; size=0x%llx.\n",
			   reg.index, reg.size);
		vfio_info->dpi_region_base[0] = mem;
		vfio_info->dpi_region_offset[0] = reg.offset;
		vfio_info->dpi_region_size[0] = reg.size;
	}

	for (eng = 0; eng < MAX_DPI_ENGINES; eng++) {
		if (eng < 4)
			regval = DPI_DMA_FIFO_SIZE_8KB;
		else
			regval = DPI_DMA_FIFO_SIZE_16KB;

		CP_LIB_LOG(DEBUG, CNXK, "Enabling DPI engine %d ...\n", eng);
		cp_write64(regval, mem + DPI_ENG_BUF(eng));
	}

	regval = 0LL;
	regval = (DPI_DMA_CONTROL_ZBWCSEN | DPI_DMA_CONTROL_PKT_EN |
		  DPI_DMA_CONTROL_LDWB | DPI_DMA_CONTROL_O_MODE);
	regval |= DPI_DMA_CONTROL_DMA_ENB(DPI_DMA_ENGINE_MASK_ALL);

	cp_write64(regval, mem + DPI_DMA_CONTROL);
	cp_write64(DPI_CTL_EN, mem + DPI_CTL);

	mps = __builtin_ffs(DPI_MPS) - 8;
	mrrs = __builtin_ffs(DPI_MRRS) - 8;
	for (port = 0; port < DPI_EBUS_PORTS; port++) {
		regval = cp_read64(mem + DPI_EBUS_PORT_CFG(0));
		regval &= ~(DPI_EBUS_PORTX_CFG_MRRS(0x7) |
			    DPI_EBUS_PORTX_CFG_MPS(0x7));

		regval |= (DPI_EBUS_PORTX_CFG_MRRS(mps) |
			   DPI_EBUS_PORTX_CFG_MPS(mrrs));

		cp_write64(regval, mem + DPI_EBUS_PORT_CFG(0));
	}

	/* set write control FIFO threshold as per HW recommendation */
	cp_write64(DPI_WCTL_THR, mem + DPI_WCTL_FIF_THR);

	return 0;
fail:
	CP_LIB_LOG(ERR, CNXK, "DPI init failed !!\n");

close_group:
	close(group);
	return -1;
}

