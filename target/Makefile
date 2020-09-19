#  Copyright (C) 2020 Marvell International Ltd.
#
#  This program is provided "as is" without any warranty of any kind, and is
#  distributed under the applicable Marvell limited use license agreement.

OCTEONTX_TARGET_ROOT = $(shell pwd)
export OCTEONTX_TARGET_ROOT

SUBDIRS=drivers/dpi_dma
SUBDIRS+=drivers/pcie_ep/src
SUBDIRS+=drivers/mgmt_net
SUBIDRS+=apps/dpdk/l2fwd_pcie_ep

apps:
	$(MAKE) -C apps/dpdk/l2fwd_pcie_ep
	
all: $(SUBDIRS)
	for d in $(SUBDIRS); do \
		if test -d $$d; then cd $$d; $(OCTEONTX_BUILD_ENV) $(MAKE) || exit 1; cd -; fi; \
	done
clean:
	for d in $(SUBDIRS); do \
		if test -d $$d; then cd $$d; $(OCTEONTX_BUILD_ENV) $(MAKE) $@ || exit 1; cd -; fi; \
	done
