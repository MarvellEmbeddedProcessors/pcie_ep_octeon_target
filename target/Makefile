# SPDX-License-Identifier:           BSD-3-Clause
# https://spdx.org/licenses
# Copyright (C) 2020 Marvell.

OCTEONTX_TARGET_ROOT = $(shell pwd)
export OCTEONTX_TARGET_ROOT

ifneq (,$(findstring PORT_EXTENDER_ENABLED=1, $(OCTEONTX_BUILD_ENV)))
SUBDIRS=drivers/dpi_dma
SUBDIRS+=drivers/pcie_ep/src
SUBDIRS+=drivers/mgmt_net
endif
SUBIDRS+=apps/dpdk/l2fwd_pcie_ep
SUBIDRS+=apps/dpdk/rawdev/dpi_test
SUBIDRS+=apps/dpdk/dmadev/dpi_test

.PHONY: apps
apps:
	$(MAKE) -C apps/dpdk/l2fwd_pcie_ep
	$(MAKE) -C apps/dpdk/rawdev/dpi_test
	$(MAKE) -C apps/dpdk/dmadev/dpi_test

all: $(SUBDIRS)
	for d in $(SUBDIRS); do \
		if test -d $$d; then cd $$d; $(OCTEONTX_BUILD_ENV) $(MAKE) || exit 1; cd -; fi; \
	done
clean:
	for d in $(SUBDIRS); do \
		if test -d $$d; then cd $$d; $(OCTEONTX_BUILD_ENV) $(MAKE) $@ || exit 1; cd -; fi; \
	done
