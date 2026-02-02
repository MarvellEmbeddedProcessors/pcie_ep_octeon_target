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
#include <stdbool.h>

#include "octep_cp_lib.h"
#include "cp_log.h"
#include "cp_lib.h"
#include "cnxk_hw.h"
#include "cp_compat.h"

#define MAX_DPI_ENGINES 6

#define PEM_BAR0_START(pem_idx) (0x8E0000000000ULL | ((uint64_t)pem_idx << 36))
#define PEM_BAR4_START(pem_idx) (0x8E0F00000000ULL | ((uint64_t)pem_idx << 36))
#define SDP_RVU_PF_BAR2_START(pf_idx) (0x86E080000000 | ((uint64_t)pf_idx << 22))
#define SDP_EPFX_RINFO(pem_idx)	(0x000209F0 | ((uint64_t)pem_idx << 25))
#define SDP_EPFX_RINFO_RPVF_SHIFT	(32)
#define SDP_EPFX_RINFO_RPVF_MASK	(0xFULL << SDP_EPFX_RINFO_RPVF_SHIFT)
#define SDP_EPFX_RINFO_NVFS_SHIFT	(48)
#define SDP_EPFX_RINFO_NVFS_MASK	(0x7FULL << SDP_EPFX_RINFO_NVFS_SHIFT)

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

/* Information of devices accessed through VFIO-PCI */
#define DEVICE_BDF_STRLEN 16

#define PFN_MASK 0x7fffffffffffffULL
#define PEM_BAR4_IDX_IOVA_SHIFT 22
#define PEMx_BAR4_INDEX_OFFSET(idx) (0x700 + (idx << 3))

struct octep_pem_dev_info {
	/* PEM PF device info */
	int group_fd;
	int device_fd;
	int iommu;
	char dev_bdf[DEVICE_BDF_STRLEN];

	/* VFIO region info */
	struct vfio_region_info region[VFIO_PCI_NUM_REGIONS];
	/* mmapped address of regions */
	void *mapped_region[VFIO_PCI_NUM_REGIONS];

	/* pointer to control plane mailbox memory */
	void *mbox_mem;
};

struct octep_sdp_rvu_pf_dev_info {
	/* SDP RVU PF device info */
	int group_fd;
	int device_fd;
	int iommu;
	char dev_bdf[DEVICE_BDF_STRLEN];

	/* VFIO region info */
	struct vfio_region_info region[VFIO_PCI_NUM_REGIONS];
	/* mmapped address of regions */
	void *mapped_region[VFIO_PCI_NUM_REGIONS];
};

struct octep_pem_dev_info pem_devs[OCTEP_CP_DOM_MAX];
/* TODO: Multi-dimensional array (per PEM) might be required if more than a single
 * PEM is expected
 */
struct octep_sdp_rvu_pf_dev_info sdp_dev[OCTEP_SDP_RVU_PF_PER_PEM_MAX];
int vfio_container;
bool iommu_model_set;

/* Close the VFIO container used to access DPI and PEM devices */
void cnxk_destroy_vfio_container(void)
{
	if (vfio_container) {
		close(vfio_container);
		vfio_container = 0;
	}
}

/* Create a VFIO container to access DPI and PEM devices */
int cnxk_create_vfio_container(void)
{
	vfio_container = open("/dev/vfio/vfio", O_RDWR);

	if (vfio_container < 0) {
		CP_LIB_LOG(ERR, CNXK, "failed to open VFIO device; err=%d\n", errno);
		return -1;
	}

	if (ioctl(vfio_container, VFIO_GET_API_VERSION) != VFIO_API_VERSION) {
		CP_LIB_LOG(ERR, CNXK, "Invalid API version; err=%d\n", errno);
		goto shutdown_container;
	}

	if (!ioctl(vfio_container, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU)) {
		/* Doesn't support the IOMMU driver required. */
		CP_LIB_LOG(ERR, CNXK,
			   "Doesn't support the IOMMU TYPE1; err=%d\n", errno);
		goto shutdown_container;
	}

	CP_LIB_LOG(INFO, CNXK, "Created VFIO container successfully; fd=%d\n", vfio_container);
	return 0;

shutdown_container:
	close(vfio_container);
	return -1;
}

static inline uint64_t get_sdp_rvu_pf_bar2_start(int pem_idx)
{
	int nvfs, rpvf;
	uint64_t val;

	switch (pem_idx) {
	case 0:
		return SDP_RVU_PF_BAR2_START(0);
	case 1:
		/* SDP generic BAR0 registers are at BAR2 of RVU SDP PF0 */
		val = cp_read64(sdp_dev[0].mapped_region[2] + SDP_EPFX_RINFO(0));
		nvfs = (val & SDP_EPFX_RINFO_NVFS_MASK) >> SDP_EPFX_RINFO_NVFS_SHIFT;
		rpvf = (val & SDP_EPFX_RINFO_RPVF_MASK) >> SDP_EPFX_RINFO_RPVF_SHIFT;

		return (SDP_RVU_PF_BAR2_START(0) + ((nvfs * rpvf) >> 1) * 0x20000);
	default:
		CP_LIB_LOG(ERR, CNXK, "%s: invalid SDP RVU PF %d\n", __func__, pem_idx);
		return UINT64_MAX;
	}
}

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
	} else if ((addr & SDP_RVU_PF_BAR2_START(0)) == SDP_RVU_PF_BAR2_START(0)) {
		bar_idx = 2;
		/* Use pf_idx = 1, since misc and other registers such as OEI are
		 * present in the second SDP RVU PF
		 */
		bar_offset = addr - get_sdp_rvu_pf_bar2_start(1);
	} else {
		CP_LIB_LOG(ERR, CNXK, "pem_sdp_rvu_map_reg: Invalid addr 0x%llx\n", addr);
		return NULL;
	}

	if (is_pem_reg) {
		if (pem_devs[0].region[bar_idx].size <= bar_offset) {
			CP_LIB_LOG(ERR, CNXK,
				   "pem_map_reg: addr=0x%llx (offset=0x%llx) is beyond BAR-%d size of 0x%lx\n",
				   addr, bar_offset, bar_idx,
				   pem_devs[0].region[bar_idx].size);
			return NULL;
		}

		return (pem_devs[0].mapped_region[bar_idx] + bar_offset);
	}

	/* SDP RVU PF[1] will hold miscellaneous SDP registers */
	if (sdp_dev[1].region[bar_idx].size <= bar_offset) {
		CP_LIB_LOG(ERR, CNXK,
				"sdp_map_reg: addr=0x%llx (offset=0x%llx) is beyond BAR-%d size of 0x%lx\n",
				addr, bar_offset, bar_idx, sdp_dev[1].region[bar_idx].size);
		return NULL;
	}
	return (sdp_dev[1].mapped_region[2] + bar_offset);
}

#define PEM_RST_INT(x)          (0x300ULL + ((uint64_t)(x) << 36))
#define PEM_RST_INT_ENA_W1C(x)  (0x310ULL + ((uint64_t)(x) << 36))
#define PEM_RST_INT_ENA_W1S(x)  (0x318ULL + ((uint64_t)(x) << 36))
#define PEM_CFG(x)              (0x0D8ULL + ((uint64_t)(x) << 36))

#define PEM_CFG_B_AUTO_DP_CLR   0x0100

#define PEM_RST_INT_B_L2        0x0004
#define PEM_RST_INT_B_LINKDOWN  0x0002
#define PEM_RST_INT_B_PERST     0x0001

int cnxk_check_perst_intr(int pem)
{
	struct octep_pem_dev_info *pem_dev;
	uint64_t word;

	pem_dev = &pem_devs[pem];
	if (pread(pem_dev->device_fd, &word, sizeof(uint64_t),
		  pem_dev->region[0].offset + PEM_RST_INT(0)) < 0) {
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

static int cnxk_register_perst_intr(struct octep_pem_dev_info *pem_dev)
{
	struct vfio_irq_info irq_info;
	uint64_t word;

	memset(&irq_info, 0x0, sizeof(struct vfio_irq_info));
	irq_info.argsz = sizeof(struct vfio_irq_info);
	irq_info.index = VFIO_PCI_MSIX_IRQ_INDEX;

	if (ioctl(pem_dev->device_fd, VFIO_DEVICE_GET_IRQ_INFO, &irq_info)) {
		fprintf(stderr, "failed to get device irq info\n");
		goto register_failed;
	}
	CP_LIB_LOG(INFO, CNXK, "Number of PEM interrupts = %d\n", irq_info.count);

	/* STEP1: Disable interrupts */
	word = (PEM_RST_INT_B_L2 | PEM_RST_INT_B_LINKDOWN | PEM_RST_INT_B_PERST);
	if (pwrite(pem_dev->device_fd, &word, sizeof(uint64_t),
		   pem_dev->region[0].offset + PEM_RST_INT_ENA_W1C(0)) < 0) {
		CP_LIB_LOG(ERR, CNXK, "Failed to write PEM_RST_INT_ENA_W1C\n");
		goto register_failed;
	}

	/* STEP2: Clear outstanding interrupts */
	if (pread(pem_dev->device_fd, &word, sizeof(uint64_t),
		  pem_dev->region[0].offset + PEM_RST_INT(0)) < 0) {
		CP_LIB_LOG(ERR, CNXK, "Failed to read PEM_RST_INT\n");
		goto register_failed;
	}
	if (pwrite(pem_dev->device_fd, &word, sizeof(uint64_t),
		   pem_dev->region[0].offset + PEM_RST_INT(0)) < 0) {
		CP_LIB_LOG(ERR, CNXK, "Failed to write PEM_RST_INT\n");
		goto register_failed;
	}

	/* STEP3: Auto clear DISPORT after PERST */
	if (pread(pem_dev->device_fd, &word, sizeof(uint64_t),
		  pem_dev->region[0].offset + PEM_CFG(0)) < 0) {
		CP_LIB_LOG(ERR, CNXK, "Failed to read PEM_CFG\n");
		goto register_failed;
	}
	word |= PEM_CFG_B_AUTO_DP_CLR;
	if (pwrite(pem_dev->device_fd, &word, sizeof(uint64_t),
		   pem_dev->region[0].offset + PEM_CFG(0)) < 0) {
		CP_LIB_LOG(ERR, CNXK, "Failed to read PEM_CFG\n");
		goto register_failed;
	}

	/* FIXME: was link down enabled in existing lib/app ? */
	/* STEP4: Enable LinkDown and PERST */
	word = (PEM_RST_INT_B_LINKDOWN | PEM_RST_INT_B_PERST);
	if (pwrite(pem_dev->device_fd, &word, sizeof(uint64_t),
		   pem_dev->region[0].offset + PEM_RST_INT_ENA_W1S(0)) < 0) {
		CP_LIB_LOG(ERR, CNXK, "Failed to write PEM_RST_INT_ENA_W1S\n");
		goto register_failed;
	}

	CP_LIB_LOG(INFO, CNXK, "Enabled PEM link down and PERST interrupts\n");
	return 0;

register_failed:
	return -EFAULT;
}

int cnxk_enable_perst_intr(struct octep_pem_dev_info *pem_dev)
{
	uint64_t word;

	word = (PEM_RST_INT_B_LINKDOWN | PEM_RST_INT_B_PERST);
	if (pwrite(pem_dev->device_fd, &word, sizeof(uint64_t),
		   pem_dev->region[0].offset + PEM_RST_INT_ENA_W1S(0)) < 0) {
		CP_LIB_LOG(ERR, CNXK, "Failed to write PEM_RST_INT_ENA_W1S\n");
		return -EBUSY;
	}

	return 0;
}

int cnxk_clear_perst_intr(int pem)
{
	struct octep_pem_dev_info *pem_dev;
	uint64_t word;

	pem_dev = &pem_devs[pem];
	if (pread(pem_dev->device_fd, &word, sizeof(uint64_t),
		  pem_dev->region[0].offset + PEM_RST_INT(0)) < 0) {
		CP_LIB_LOG(ERR, CNXK, "Failed to read PEM_RST_INT\n");
		return -EBUSY;
	}

	if (pwrite(pem_dev->device_fd, &word, sizeof(uint64_t),
		   pem_dev->region[0].offset + PEM_RST_INT(0)) < 0) {
		CP_LIB_LOG(ERR, CNXK, "Failed to write PEM_RST_INT\n");
		return -EBUSY;
	}
	return 0;
}

int cnxk_disable_perst_intr(struct octep_pem_dev_info *pem_dev)
{
	uint64_t word;

	word = (PEM_RST_INT_B_LINKDOWN | PEM_RST_INT_B_PERST);
	if (pwrite(pem_dev->device_fd, &word, sizeof(uint64_t),
		   pem_dev->region[0].offset + PEM_RST_INT_ENA_W1C(0)) < 0) {
		CP_LIB_LOG(ERR, CNXK, "Failed to write PEM_RST_INT_ENA_W1C\n");
		return -EBUSY;
	}

	return 0;
}

/* TODO: Handle multi pem use case. How would it look like with SDP RVU PFs?
 * Would there be another SDP RVU PF?
 */
static int cnxk_pem_setup_mbox_memory(struct octep_pem_dev_info *pem_dev)
{
	/* The BAR4 of the SDP RVU PFs are now directly mapped to the EPF BAR4 */
	if (!sdp_dev[0].mapped_region[4] || !sdp_dev[0].mapped_region[4])
		return -EIO;

	if (sdp_dev[0].region[4].size > PEMX_BAR4_INDEX_ADDR)
		pem_dev->mbox_mem = sdp_dev[0].mapped_region[4] + (PEMX_BAR4_INDEX_ADDR);
	else
		pem_dev->mbox_mem = sdp_dev[1].mapped_region[4] + (PEMX_BAR4_INDEX_ADDR);

	return 0;
}

uint64_t cnxk_pem_get_mbox_memory(int pem)
{
	return (uint64_t)pem_devs[pem].mbox_mem;
}

void cnxk_pem_uninit(int pem)
{
	struct octep_pem_dev_info *pem_dev;
	int i;

	pem_dev = &pem_devs[pem];

	if (pem_dev->mbox_mem) {
		munmap(pem_dev->mbox_mem, PEMX_BAR4_INDEX_SIZE);
		pem_dev->mbox_mem = NULL;
	}

	for (i = 0; i < VFIO_PCI_NUM_REGIONS; i++) {
		if (!pem_dev->mapped_region[i])
			continue;

		munmap(pem_dev->mapped_region[i], pem_dev->region[i].size);
	}

	if (pem_dev->device_fd)
		close(pem_dev->device_fd);
	if (pem_dev->group_fd) {
		close(pem_dev->group_fd);
		pem_dev->group_fd = 0;
	}
}

int cnxk_pem_init(int pem)
{
	struct octep_pem_dev_info *pem_dev;
	struct vfio_group_status group_status = { .argsz = sizeof(group_status) };
	struct vfio_device_info device_info = { .argsz = sizeof(device_info) };
	struct vfio_region_info reg = { .argsz = sizeof(reg) };
	int group, device, ret, i;
	char filepath[FILENAME_MAX];
	void *mem;

	pem_dev = &pem_devs[pem];
	/* Open the group */
	snprintf(filepath, sizeof(filepath), "%s%d", "/dev/vfio/", pem_dev->iommu);
	group = open(filepath, O_RDWR);
	if (group < 0) {
		CP_LIB_LOG(ERR, CNXK,
				"failed to open PEM VFIO group at %s; err=%d\n", filepath, errno);
		return -1;
	}
	pem_dev->group_fd = group;

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
	ret = ioctl(group, VFIO_GROUP_SET_CONTAINER, &vfio_container);
	if (ret == -1) {
		CP_LIB_LOG(ERR, CNXK,
			   "Failed to add PEM VFIO group to the container; ret=%d\n", ret);
		goto close_group;
	}

	/* Get a file descriptor for the device */
	device = ioctl(group, VFIO_GROUP_GET_DEVICE_FD, pem_dev->dev_bdf);
	if (device == -1) {
		CP_LIB_LOG(ERR, CNXK, "Failed to get PEM device VFIO FD; ret=%d\n", device);
		goto close_group;
	}
	pem_dev->device_fd = device;

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
		pem_dev->mapped_region[i] = mem;
		pem_dev->region[i] = reg;
	}

	/* Handle this here since technically mbox is in BAR4 of EPF, which should be
	 * initialised by pf/pem
	 */
	if (cnxk_pem_setup_mbox_memory(pem_dev)) {
		CP_LIB_LOG(ERR, CNXK, "Failed to setup mailbox memory\n");
		goto uninit_pem;
	}

	if (cnxk_register_perst_intr(pem_dev)) {
		CP_LIB_LOG(ERR, CNXK, "Failed to configure PERST interrupt\n");
		goto uninit_pem;
	}
	return 0;

uninit_pem:
	printf("PEM init failed\n");
	cnxk_pem_uninit(pem);

close_group:
	close(group);
	return -1;
}

static int cnxk_sdp_rvu_pf_init(void)
{
	struct vfio_group_status group_status = { .argsz = sizeof(group_status) };
	struct vfio_device_info device_info = { .argsz = sizeof(device_info) };
	struct vfio_region_info reg = { .argsz = sizeof(reg) };
	int group, device, ret, pf = 0;
	char filepath[FILENAME_MAX];
	void *mem;

	CP_LIB_LOG(INFO, CNXK, "Initializing SDP RVU PF ...\n");

	for (pf = 0; pf < OCTEP_SDP_RVU_PF_PER_PEM_MAX; pf++) {
		/* Open the group */
		snprintf(filepath, sizeof(filepath), "%s%d", "/dev/vfio/", sdp_dev[pf].iommu);
		group = open(filepath, O_RDWR);
		if (group < 0) {
			CP_LIB_LOG(ERR, CNXK,
				   "failed to open SDP RVU PF VFIO group at %s; reason: %s\n",
				   filepath, strerror(errno));
			return -1;
		}
		sdp_dev[pf].group_fd = group;

		ret = ioctl(group, VFIO_GROUP_GET_STATUS, &group_status);
		if (ret == -1) {
			CP_LIB_LOG(ERR, CNXK,
				   "Failed to get VFIO group status for "
				   "SDP RVU PF; err=%d\n", ret);
			goto close_group;
			return ret;
		}

		if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
			CP_LIB_LOG(ERR, CNXK,
				   "VFIO Group is not viable; check if "
				   "SDP RVU PF device bound to vfio driver\n");
			goto close_group;
		}

		/* Add the group to the container */
		ret = ioctl(group, VFIO_GROUP_SET_CONTAINER, &vfio_container);
		if (ret == -1) {
			CP_LIB_LOG(ERR, CNXK,
					"Failed to add SDP RVU PF VFIO "
					"group to the container; ret=%d\n", ret);
			goto close_group;
		}

		/* To be done only once; duplicate calls will fail.
		 * Here a duplicate call might imply the ioctl being
		 * called during DPI config init
		 */
		ret = ioctl(vfio_container, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU);
		if (ret == -1 && !iommu_model_set) {
			CP_LIB_LOG(ERR, CNXK, "Failed to set IOMMU model; "
				   "ret=%d\n", ret);
			goto close_group;
		}
		iommu_model_set = true;
		/* Get a file descriptor for the device */
		device = ioctl(group, VFIO_GROUP_GET_DEVICE_FD, sdp_dev[pf].dev_bdf);
		if (device == -1) {
			CP_LIB_LOG(ERR, CNXK,
				   "Failed to get SDP RVU PF device VFIO FD; "
				   "ret=%d\n", device);
			goto close_group;
		}
		sdp_dev[pf].device_fd = device;

		/* Test and setup the device */
		ret = ioctl(device, VFIO_DEVICE_GET_INFO, &device_info);
		if (ret == -1) {
			CP_LIB_LOG(ERR, CNXK, "Failed to get SDP RVU PF VFIO "
				   "device info; ret=%d\n", ret);
			goto close_group;
		}

		for (int i = 0; i < 2; i++) {
			const int bar_idx[] = { 2, 4 };
			reg.index = bar_idx[i];
			ret = ioctl(device, VFIO_DEVICE_GET_REGION_INFO, &reg);
			if (ret == -1) {
				CP_LIB_LOG(ERR, CNXK,
					   "Failed to get SDP RVU PF device "
					   "info for region-%d; ret=%d\n",
					   reg.index, ret);
				goto fail;
			} else {
				mem = mmap(NULL, reg.size, PROT_READ | PROT_WRITE,
					   MAP_SHARED, device, reg.offset);
				if (mem == MAP_FAILED) {
					CP_LIB_LOG(ERR, CNXK,
						   "failed to mmap SDP RVU PF "
						   "region-%d, offset=%d\n",
						   reg.index, reg.offset);
					/* FIXME: replace with uninit_pem */
					goto fail;
				}
				CP_LIB_LOG(DEBUG, CNXK,
					   "mapped SDP RVU PF device "
					   "region-%d; size=0x%llx.\n",
					   reg.index, reg.size);
				sdp_dev[pf].mapped_region[reg.index] = mem;
				sdp_dev[pf].region[reg.index] = reg;
			}
		}
	}

		return 0;
fail:
	CP_LIB_LOG(ERR, CNXK, "SDP RVU PF %d init failed !!\n", pf);
	close(sdp_dev[pf].device_fd);
	sdp_dev[pf].device_fd = 0;
	if (pf > 0) {
		close(sdp_dev[pf - 1].device_fd);
		sdp_dev[pf - 1].device_fd = 0;
	}

close_group:
	close(group);
	return -1;
}

static void cnxk_sdp_rvu_pf_uninit(void)
{
	int i, j;

	for (i = 0; i < OCTEP_SDP_RVU_PF_PER_PEM_MAX; i++) {
		if (sdp_dev[i].device_fd)
			close(sdp_dev[i].device_fd);
		if (sdp_dev[i].group_fd) {
			close(sdp_dev[i].group_fd);
			sdp_dev[i].group_fd = 0;
		}

		for (j = 0; j < VFIO_PCI_NUM_REGIONS; j++) {
			if (!sdp_dev[i].mapped_region[j])
				continue;
			munmap(sdp_dev[i].mapped_region[j], sdp_dev[i].region[j].size);
		}
	}
}

int cnxk_vfio_global_init(void)
{
	int ret;

	/* create VFIO container */
	if (cnxk_create_vfio_container())
		return -ENODEV;

	if (cnxk_sdp_rvu_pf_init()) {
		ret = -ENODEV;
		goto destroy_container;
	}
	return 0;

destroy_container:
	cnxk_destroy_vfio_container();
	return ret;
}

void cnxk_vfio_global_uninit(void)
{
	cnxk_sdp_rvu_pf_uninit();
	cnxk_destroy_vfio_container();
}

static int get_pci_iommu_group(char *path)
{
	char buf[FILENAME_MAX];
	int group;

	memset(buf, 0, sizeof(buf));
	if (readlink(path, buf, FILENAME_MAX) < 0) {
		CP_LIB_LOG(ERR, CNXK,
			   "failed to read link from path %s\n", path);
		return -1;
	}

	group = atoi(strrchr(buf, '/') + 1);
	return group;
}

int cnxk_vfio_parse_pem_dev(const char *dev)
{
	struct octep_pem_dev_info *pem_dev;
	char filepath[FILENAME_MAX];
	struct stat sb;

	/* FIXME: extend the parsing for multiple PEMs and non-zero PEM if required */
	pem_dev = &pem_devs[0];
	snprintf(filepath, sizeof(filepath), "%s%s", "/sys/bus/pci/devices/", dev);
	if (stat(filepath, &sb) || !S_ISDIR(sb.st_mode)) {
		CP_LIB_LOG(ERR, LIB, "Invalid PEM device BDF %s\n", dev);
		return -1;
	}
	strncpy(pem_dev->dev_bdf, dev, sizeof(pem_dev->dev_bdf) - 1);

	/* get IOMMU group of the PEM device */
	snprintf(filepath, sizeof(filepath), "%s%s/%s",
		 "/sys/bus/pci/devices/", dev, "iommu_group");
	pem_dev->iommu = get_pci_iommu_group(filepath);
	if (pem_dev->iommu < 0) {
		CP_LIB_LOG(ERR, LIB, "Failed to find IOMMU group of PEM device at %s\n", dev);
		return -1;
	}

	CP_LIB_LOG(INFO, CNXK, "PEM: device = %s; IOMMU group = %d\n", dev, pem_dev->iommu);
	return 0;
}

int cnxk_vfio_parse_sdp_rvu_pf_dev(const char *arg)
{
	char *bdf[OCTEP_SDP_RVU_PF_PER_PEM_MAX] = {
		[0 ... OCTEP_SDP_RVU_PF_PER_PEM_MAX - 1] = NULL };
	char filepath[FILENAME_MAX];
	char *dev = NULL;
	struct stat sb;

	dev = malloc(strlen(arg) + 1);
	if (!dev) {
		CP_LIB_LOG(ERR, LIB,
			   "Unable to allocate memory for SDP RVU BDF %s\n",
			   arg);
		return -ENOMEM;
	}
	strcpy(dev, arg);
	bdf[0] = strtok(dev, ",");
	bdf[1] = strtok(NULL, ",");
	if (!bdf[0] || !bdf[1]) {
		CP_LIB_LOG(ERR, LIB,
			   "Unable to parse SDP RVU PF BDFs from %s. "
			   "Requires at least %d comma separated BDFs\n",
			   dev, OCTEP_SDP_RVU_PF_PER_PEM_MAX);
		free(dev);
		return -EINVAL;
	}

	for (int i = 0; i < OCTEP_SDP_RVU_PF_PER_PEM_MAX; i++) {
		snprintf(filepath, sizeof(filepath), "%s%s",
			 "/sys/bus/pci/devices/", bdf[i]);
		if (stat(filepath, &sb) || !S_ISDIR(sb.st_mode)) {
			CP_LIB_LOG(ERR, LIB, "Invalid SDP RVU device BDF %s\n",
				   bdf[i]);
			free(dev);
			return -1;
		}
		strncpy(sdp_dev[i].dev_bdf, bdf[i], sizeof(sdp_dev[i].dev_bdf) - 1);

		/* get IOMMU group of the SDP RVU PF device */
		snprintf(filepath, sizeof(filepath), "%s%s/%s",
				"/sys/bus/pci/devices/", bdf[i], "iommu_group");
		sdp_dev[i].iommu = get_pci_iommu_group(filepath);
		if (sdp_dev[i].iommu < 0) {
			CP_LIB_LOG(ERR, LIB,
				   "Failed to find IOMMU group of DPI device at %s\n",
				   bdf[i]);
			free(dev);
			return -1;
		}

		CP_LIB_LOG(INFO, CNXK,
			   "SDP RVU PF: device = %s; "
			   "IOMMU group = %d\n", bdf[i],
			   sdp_dev[i].iommu);
	}
	free(dev);
	return 0;
}
