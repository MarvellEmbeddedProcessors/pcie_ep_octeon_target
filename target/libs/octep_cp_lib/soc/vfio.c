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
#include "cp_compat.h"

#define MAX_DPI_ENGINES 6

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

	/* Test and setup the device */
	ret = ioctl(device, VFIO_DEVICE_GET_INFO, &device_info);
	if (ret == -1) {
		CP_LIB_LOG(ERR, CNXK, "Failed to get DPI VFIO device info; ret=%d\n", ret);
		goto close_group;
	}

	CP_LIB_LOG(INFO, CNXK, "number of DPI device regions = %d\n", device_info.num_regions);

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
			goto fail;
		}
		CP_LIB_LOG(DEBUG, CNXK, "mapped DPI device region-%d; size=0x%llx.\n",
			   reg.index, reg.size);
		vfio_info->dpi_region_base[0] = mem;
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

