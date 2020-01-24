// SPDX-License-Identifier: (GPL-2.0)
/* DPI DMA VF driver
 *
 * Copyright (C) 2015-2019 Marvell, Inc.
 */

#define DPI_XTYPE_OUTBOUND 0
#define DPI_XTYPE_INBOUND 1
#define DPI_XTYPE_INTERNAL_ONLY 2

typedef union dpi_dma_ptr_s {
	u64 u[2];
	struct dpi_dma_s {
#ifdef __LITTLE_ENDIAN_BITFIELD
		u64 length:16;
		u64 reserved:44;
		u64 bed:1; /* Big-Endian */
		u64 alloc_l2:1;
		u64 full_write:1;
		u64 invert:1;
#else
		u64 invert:1;
		u64 full_write:1;
		u64 alloc_l2:1;
		u64 bed:1; /* Big-Endian */
		u64 reserved:44;
		u64 length:16;
#endif
		u64 ptr;
	} s;
} dpi_dma_ptr_t;

typedef struct dpi_dma_req_compl_s {
	u64 cdata;
	void (*compl_cb)(void *dev, void *arg);
	void *cb_data;
} dpi_dma_req_compl_t;

#define DPI_MAX_POINTER         15
typedef struct dpi_dma_buf_ptr_s {
	dpi_dma_ptr_t *rptr[DPI_MAX_POINTER]; /* Read From pointer list */
	dpi_dma_ptr_t *wptr[DPI_MAX_POINTER]; /* Write to pointer list */
	u8 rptr_cnt;
	u8 wptr_cnt;
	volatile dpi_dma_req_compl_t *comp_ptr;
} dpi_dma_buf_ptr_t;

typedef struct dpi_cring_data_s {
	dpi_dma_req_compl_t **compl_data;
	u16 max_cnt;
	u16 head;
	u16 tail;
} dpi_cring_data_t;

typedef struct dpi_dma_queue_ctx_s {
	u16 xtype:2;
	/* Completion pointer type */
	u16 pt:2;
	/* Completion updated using WQE */
	u16 tt:2;
	u16 grp:10;

	u32 tag;

	/* Valid only for Outbound only mode */
	u16 aura:12;
	u16 csel:1;
	u16 ca:1;
	u16 fi:1;
	u16 ii:1;

	u16 fl:1;
	u16 pvfe:1;
	u16 dealloce:1;
	u16 req_type:2;
	u16 use_lock:1;
	u16 reserved:10;

	u16 deallocv;

	dpi_cring_data_t *c_ring;
} dpi_dma_queue_ctx_t;

typedef enum {
	DPI_DMA_QUEUE_SUCCESS = 0,
	DPI_DMA_QUEUE_NO_MEMORY = -1,
	DPI_DMA_QUEUE_INVALID_PARAM = -2,
} dpi_dma_queue_result_t;

/*
 * Structure dpi_dma_instr_hdr_s
 *
 * DPI DMA Instruction Header Structure
 * DPI DMA Instruction Header Format
 */
typedef union dpi_dma_instr_hdr_s {
	u64 u[4];
	struct dpi_dma_instr_hdr_s_s {
#ifdef __LITTLE_ENDIAN_BITFIELD
		u64 tag:32;
		u64 tt:2;
		u64 grp:10;
		u64 reserved_44_47:4;
		u64 nfst:4;
		u64 reserved_52_53:2;
		u64 nlst:4;
		u64 reserved_58_63:6;
#else
		u64 reserved_58_63:6;
		u64 nlst:4;
		u64 reserved_52_53:2;
		u64 nfst:4;
		u64 reserved_44_47:4;
		u64 grp:10;
		u64 tt:2;
		u64 tag:32;
#endif
		/* Word 0 - End */
#ifdef __LITTLE_ENDIAN_BITFIELD
		u64 aura:12;
		u64 reserved_76_79:4;
		u64 deallocv:16;
		u64 dealloce:1;
		u64 pvfe:1;
		u64 reserved_98_99:2;
		u64 pt:2;
		u64 reserved_102_103:2;
		u64 fl:1;
		u64 ii:1;
		u64 fi:1;
		u64 ca:1;
		u64 csel:1;
		u64 reserved_109_111:3;
		u64 xtype:2;
		u64 reserved_114_119:6;
		u64 fport:2;
		u64 reserved_122_123:2;
		u64 lport:2;
		u64 reserved_126_127:2;
#else
		u64 reserved_126_127:2;
		u64 lport:2;
		u64 reserved_122_123:2;
		u64 fport:2;
		u64 reserved_114_119:6;
		u64 xtype:2;
		u64 reserved_109_111:3;
		u64 csel:1;
		u64 ca:1;
		u64 fi:1;
		u64 ii:1;
		u64 fl:1;
		u64 reserved_102_103:2;
		u64 pt:2;
		u64 reserved_98_99:2;
		u64 pvfe:1;
		u64 dealloce:1;
		u64 deallocv:16;
		u64 reserved_76_79:4;
		u64 aura:12;
#endif

		/* Word 1 - End */
		u64 ptr:64;
		/* Word 2 - End */
		u64 reserved_192_255:64;
		/* Word 3 - End */
	} s;
} dpi_dma_instr_hdr_s;
