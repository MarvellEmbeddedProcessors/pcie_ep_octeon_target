# SPDX-License-Identifier:           BSD-3-Clause
# https://spdx.org/licenses
# Copyright (C) 2020 Marvell.

OCTEONTX_HOST_ROOT = $(shell pwd)
export OCTEONTX_HOST_ROOT

SUBDIRS=drivers/legacy/modules/driver/src
SUBDIRS+=drivers/mgmt_net

all: $(SUBDIRS)
		for d in $(SUBDIRS); do \
				if test -d $$d; then $(OCTEONTX_BUILD_ENV) $(MAKE) -s -C $$d || exit 1; fi; \
		done
clean:
		for d in $(SUBDIRS); do \
				if test -d $$d; then $(OCTEONTX_BUILD_ENV) $(MAKE) -s -C $$d $@ || exit 1; fi; \
		done
