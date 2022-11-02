#include "compat.h"
#include "l2fwd_config.h"
#include "data.h"

#define RTE_LOGTYPE_L2FWD_DATA_STUB	RTE_LOGTYPE_USER1

/* data operations */
static struct data_ops *data_ops;

static uint16_t stub_tx_buffer(uint16_t port, uint16_t queue, void *buffer,
			       void *tx_pkt)
{
	RTE_LOG(INFO, L2FWD_DATA_STUB, "[%d]:[%d] dropping packet\n",
		port, queue);
	data_ops->drop_pkt(port, tx_pkt, queue);

	return 1;
}

static int stub_rx_burst(uint16_t port, uint16_t queue, void **rx_pkts,
			 const uint16_t nb_pkts)
{
	return 0;
}

static struct data_fn_ops stub_data_ops = {
	.rx_burst = stub_rx_burst,
	.tx_buffer = stub_tx_buffer
};

int data_stub_init(struct data_ops *ops, struct data_fn_ops **fn_ops)
{
	*fn_ops = &stub_data_ops;
	data_ops = ops;

	return 0;
}

int data_stub_uninit(void)
{
	return 0;
}
