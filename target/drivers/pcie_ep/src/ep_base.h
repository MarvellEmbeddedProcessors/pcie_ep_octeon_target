#ifndef __EP_BASE_H__
#define __EP_BASE_H__

struct otx_pcie_ep {
	uint64_t	pem_base;
	uint64_t	sdp_base;
	uint64_t	oei_trig_addr;
	unsigned int	instance;
	unsigned int	plat_model;
	struct device *plat_dev;
	struct npu_bar_map bar_map;
	struct npu_irq_info irq_info[MAX_INTERRUPTS];
	void *npu_barmap_mem;
	mv_facility_conf_t facility_conf[MV_FACILITY_COUNT];
	mv_facility_event_cb_t facility_handler[MV_FACILITY_COUNT];
};

#endif