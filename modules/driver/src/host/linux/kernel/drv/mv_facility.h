/**
 * @file mv_facility.h
 * @brief defines facility interface data structures and APIs
 *
 * Facility is a mechanism provided through which host and target
 * modules (implementing the functionality of facilities) communicate
 * via OcteonTX memory made accessible to Host through OcteonTX BAR1
 */
 
#ifndef _MV_FACILITY_H_
#define _MV_FACILITY_H_

/**
 * @brief facility memory address
 *
 * address of memory assigned to facility
 */
typedef union {
	uint64_t u64;

	/* Host IO remapped address of facility memory inside BAR1 */
	void __iomem *host_addr;

	/* Target virtual address of facility memory mapped to BAR1 */
	void *target_addr;
} mv_facility_map_addr_t;

typedef mv_facility_map_addr_t mv_bar_map_addr_t;

/**
 * @brief facility bar mapped memory
 *
 * facility retrieves the bar memory and size using this structure
 */
typedef struct {
	mv_bar_map_addr_t addr;
	uint32_t memsize;
} mv_bar_map_t;

typedef u64 host_dma_addr_t;

enum mv_target {
	TARGET_HOST = 0,
	TARGET_EP = 1,
};

/**
 * @brief Get Facility handle
 *
 * Returns the facility handle based on the passed facility name, this handle
 * should be used for all Facility based API below
 * @param name		the Facility name.
 * @return handle >= 0 on success, on error returns errno.
 */
int mv_get_facility_handle(char *name);

/**
 * @brief Get the Facility device
 *
 * Returns the facility device used for dma
 * @param handle	Facility handle
 * @param dev		the device used by the facility
 *
 * @return 0, on success and standard error numbers on failure.
 */
int mv_pci_get_dma_dev(
	int			handle,
	struct device		**dev);

/**
 * @brief Return the facility doorbells number
 *
 * Returns the number of doorbells configured for the facility
 * @param handle	Facility handle
 * @param target	the doorbell direction
 * @param num_dbells	the number of doorbells
 *
 * @return 0, on success and standard error numbers on failure.
 */
int mv_get_num_dbell(
	int			handle,
	enum mv_target		target,
	uint32_t		*num_dbells);

/**
 * @brief Send doorbell interrupt to remote Facility
 *
 * Send doorbell to counterpart of the Facility, host calls this to
 * interrupt Facility on target and vice-versa.
 * @param handle	Facility handle
 * @param dbell		the doorbell to use
 *
 * @return 0, on success and standard error numbers on failure.
 */
int mv_send_dbell(
	int			handle,
	uint32_t		dbell);

/**
 * @brief Returns the Facility bar map
 *
 * Returns the Facility bar map structure that includes the host or target
 * address and memory size of this mapped memory
 * @param handle	Facility handle
 * @param bar_map	the returned bar map structure filled in by Facility
 *
 * @return 0, on success and standard error numbers on failure.
 */
int mv_get_bar_mem_map(
	int			handle,
	mv_bar_map_t		*bar_map);

#endif /* _MV_FACILITY_H_ */
