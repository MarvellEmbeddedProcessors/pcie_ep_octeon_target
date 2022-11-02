#include "compat.h"
#include "data.h"
#include "poll_mode.h"

/* data operations */
static struct data_ops *data_ops;

static int pm1_start(void)
{
	return 0;
}

static int pm1_pause(unsigned int port)
{
	return 0;
}

static int pm1_resume(unsigned int port)
{
	return 0;
}

static int pm1_stop(void)
{
	return 0;
}

static struct poll_mode_ops pm1_ops = {
	.start = pm1_start,
	.pause = pm1_pause,
	.resume = pm1_resume,
	.stop = pm1_stop
};

int poll_mode_1_init(struct data_ops *ops, struct poll_mode_ops **pm_ops)
{
	*pm_ops = &pm1_ops;
	data_ops = ops;

	return 0;
}

int poll_mode_1_uninit(void)
{
	return 0;
}
