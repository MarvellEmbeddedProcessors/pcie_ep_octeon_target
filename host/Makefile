#  Copyright (C) 2020 Marvell International Ltd.
#
#  This program is provided "as is" without any warranty of any kind, and is
#  distributed under the applicable Marvell limited use license agreement.

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
