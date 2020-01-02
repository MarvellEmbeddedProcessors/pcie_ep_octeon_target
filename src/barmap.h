#ifndef  _BARMAP_H_
#define _BARMAP_H_

#define NPU_BARMAP_VERSION_MAJOR 0
#define NPU_BARMAP_VERSION_MINOR 1

#define NPU_BARMAP_VERSION \
	((NPU_BARMAP_VERSION_MAJOR << 16) | NPU_BARMAP_VERSION_MINOR)

struct npu_module_rmem_info
{
	uint32_t ctrl_offset;		// Offset in BAR1 where Control & Mgmt ring is stored
	uint32_t ctrl_size;		// Size of the BAR1 space allocated for Control & Mgmt ring
	uint32_t ctrl_dbell_offset;	// offset in BAR1 where host should write to trigger 
					// Control & Mgmt interrupt
	uint32_t ctrl_dbell_bit;	// bit to be set in ctrl_dbell_offset to trigger interrupt
	uint32_t ctrl_dbell_count;
};

struct npu_bar_map {
	uint32_t version;		// version of the memory map structure

	uint32_t ctrl_offset;		// Offset in BAR1 where Control & Mgmt ring is stored
	uint32_t ctrl_size;		// Size of the BAR1 space allocated for Control & Mgmt ring
	uint32_t ctrl_dbell_offset;	// offset in BAR1 where host should write to trigger 
					// Control & Mgmt interrupt
	uint32_t ctrl_dbell_bit;	// bit to be set in ctrl_dbell_offset to trigger interrupt
	uint32_t ctrl_dbell_count;

	uint32_t mgmt_netdev_offset;	// Offset in BAR1 where mgmt netdev ring is stored
	uint32_t mgmt_netdev_size;	// size of the BAR1 space allocated for mgmt netdev operations
	uint32_t mgmt_netdev_dbell_offset;	// offset in BAR1 where eth_mux should write to trigger mgmt netdev
						// interrupt to NPU.
	uint32_t mgmt_netdev_dbell_bit;		// bit to be set in mgmt_netdev_dbell_offset to trigger interrupt
	uint32_t mgmt_netdev_dbell_count;

	uint32_t nw_agent_offset;	// Offset in BAR1 where NPU network agent ring is stored
	uint32_t nw_agent_size;		// Size of the BAR1 space allocated for NPU network agent
	uint32_t nw_agent_dbell_offset;	// offset in BAR1 where host should write to trigger 
					// NPU network agent interrupt
	uint32_t nw_agent_dbell_bit;	// bit to be set in nw_agent_dbell_offset to trigger interrupt
	uint32_t nw_agent_dbell_count;

	uint32_t rpc_offset;		// Offset in BAR1 where RPC rings are stored
	uint32_t rpc_size;		// Size of BAR space allocated for RPC rings
	uint32_t rpc_dbell_offset;	// There are multiple priority rings and each ring has separate
					// interrupt bit; this is the bit for first ring;
	uint32_t rpc_dbell_bit;		// host shall set this bit in rpc_dbell_offset to trigger interrupt for 
					// priority ring 0;
	uint32_t rpc_dbell_count;	// number of doorbell interrupts assigned to RPC;
					// bits rpc_dbell_bit_0 to (rpc_dbell_bit_0+ rpc_dbell_count-1) are 
					// assigned to RPC rings.
					//       bit rpc_dbell_bit_0 for ring-0
					//       bit (rpc_dbell_bit_0 + 1) for ring-1 ring …
					//       bit (rpc_dbell_bit_0 + rpc_dbell_count - 1) for last ring.
};

/* BAR1 provides 64MB of OcteonTX memory for Host access in EndPoint mode.
 * Use second half of this 64MB for various rings used to facilitate
 * communication between modules/entities on NPU and Host.
 *
 * First 32MB can be used for pci-console and other purposes.
 */
#define MB (1024 * 1024)
#define NPU_BARMAP_FIREWALL_OFFSET (32 * MB)

#define NPU_BARMAP_CTRL_OFFSET (NPU_BARMAP_FIREWALL_OFFSET)
#define NPU_BARMAP_CTRL_SIZE   (1 * MB)

#define NPU_BARMAP_MGMT_NETDEV_OFFSET \
	(NPU_BARMAP_CTRL_OFFSET + NPU_BARMAP_CTRL_SIZE)
#define NPU_BARMAP_MGMT_NETDEV_SIZE   (1 * MB)

#define NPU_BARMAP_NW_AGENT_OFFSET \
	(NPU_BARMAP_MGMT_NETDEV_OFFSET + NPU_BARMAP_MGMT_NETDEV_SIZE)
#define NPU_BARMAP_NW_AGENT_SIZE (1 * MB)

#define NPU_BARMAP_RPC_OFFSET \
	(NPU_BARMAP_NW_AGENT_OFFSET + NPU_BARMAP_NW_AGENT_SIZE)
#define NPU_BARMAP_RPC_SIZE (1 * MB)

#define NPU_BARMAP_FIREWALL_SIZE \
	(NPU_BARMAP_CTRL_SIZE + NPU_BARMAP_MGMT_NETDEV_SIZE + \
	 NPU_BARMAP_NW_AGENT_SIZE + NPU_BARMAP_RPC_SIZE)

#define NPU_BARMAP_FIREWALL_FIRST_ENTRY 8
/* entry 8 to entry 14; entry-15 is reserved to map GICD space 
 * for host to interrupt NPU
 */
#define NPU_BARMAP_FIREWALL_MAX_ENTRY 7
#define CN83XX_PEM_BAR1_INDEX_MAX_ENTRIES 16
#define NPU_BARMAP_ENTRY_SIZE (4 * MB)
#define NPU_BARMAP_MAX_SIZE \
	(NPU_BARMAP_ENTRY_SIZE * NPU_BARMAP_FIREWALL_MAX_ENTRY)

/* Use last entry of BAR1_INDEX for host to trigger interrupt to NPU */
#define NPU_BARMAP_SPI_ENTRY  15
#define NPU_BARMAP_SPI_OFFSET \
	((NPU_BARMAP_SPI_ENTRY * NPU_BARMAP_ENTRY_SIZE) | \
	 NPU_GICD_DBELL_OFFSET)
#define NPU_GICD_BASE          0x801000000000
#define NPU_GICD_DBELL_OFFSET  0x204 /* GICD_ISPENDR(1..4) */

static inline void npu_barmap_get_info(struct npu_bar_map *map)
{
	if (map == NULL) {
		printk("%s: Error; NULL pointer\n", __func__);
		return;
	}

	map->version =  NPU_BARMAP_VERSION;
	map->ctrl_offset = NPU_BARMAP_CTRL_OFFSET;
	map->ctrl_size   = NPU_BARMAP_CTRL_SIZE;

	map->mgmt_netdev_offset = NPU_BARMAP_MGMT_NETDEV_OFFSET;
	map->mgmt_netdev_size   = NPU_BARMAP_MGMT_NETDEV_SIZE;

	map->nw_agent_offset = NPU_BARMAP_NW_AGENT_OFFSET;
	map->nw_agent_size   = NPU_BARMAP_NW_AGENT_SIZE;

	map->rpc_offset = NPU_BARMAP_RPC_OFFSET;
	map->rpc_size   = NPU_BARMAP_RPC_SIZE;
}

static inline void npu_barmap_dump(struct npu_bar_map *map)
{
	if (map == NULL) {
		printk("%s: Error; NULL pointer\n", __func__);
		return;
	}

	printk("Version: major=%d minor=%d\n",
	       map->version >> 16, map->version & 0xffff);

	printk("Control: Offset=%x, size=%x, db_offset=%x, db_bit=%d db_count=%d\n",
	       map->ctrl_offset, map->ctrl_size, map->ctrl_dbell_offset,
	       map->ctrl_dbell_bit, map->ctrl_dbell_count);

	printk("Mgmt-netdev: Offset=%x, size=%x, db_offset=%x, db_bit=%d db_count=%d\n",
	       map->mgmt_netdev_offset, map->mgmt_netdev_size,
	       map->mgmt_netdev_dbell_offset, map->mgmt_netdev_dbell_bit,
	       map->mgmt_netdev_dbell_count);

	printk("Network-Agent: Offset=%x, size=%x, db_offset=%x, db_bit=%d db_count=%d\n",
	       map->nw_agent_offset, map->nw_agent_size,
	       map->nw_agent_dbell_offset, map->nw_agent_dbell_bit,
	       map->nw_agent_dbell_count);

	printk("RPC: Offset=%x, size=%x, db_offset=%x, db_bit=%d db_count=%d\n",
	       map->rpc_offset, map->rpc_size, map->rpc_dbell_offset,
	       map->rpc_dbell_bit, map->rpc_dbell_count);
}
#endif /* _BARMAP_H_ */
