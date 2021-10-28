# SPDX-License-Identifier: BSD-3-Clause
# Copyright (C) 2020 Marvell.
#

# binary name
APP = dpi_test

# all source are stored in SRCS-y
SRCS-y := main.c

# Build using pkg-config variables if possible
DPDK_PC?=$(TARGET_DIR)/usr/lib/pkgconfig/
PKGCONF ?= PKG_CONFIG_PATH=$(DPDK_PC) PKG_CONFIG_SYSROOT_DIR=$(TARGET_DIR) pkg-config
ifeq ($(shell $(PKGCONF) --max-version="20.11.0" libdpdk && echo 0),0)
all: shared
.PHONY: shared static
shared: build/$(APP)-shared
	ln -sf $(APP)-shared build/$(APP)
static: build/$(APP)-static
	ln -sf $(APP)-static build/$(APP)

PC_FILE := $(shell $(PKGCONF) --path libdpdk 2>/dev/null)
CFLAGS += -O3 $(shell $(PKGCONF) --cflags libdpdk)
# Add flag to allow experimental API as l2fwd uses rte_ethdev_set_ptype API
CFLAGS += -DALLOW_EXPERIMENTAL_API
LDFLAGS_SHARED = $(shell $(PKGCONF) --libs libdpdk)
LDFLAGS_STATIC = -Wl,-Bstatic $(shell $(PKGCONF) --static --libs libdpdk)

CC=$(CROSS)gcc

build/$(APP)-shared: $(SRCS-y) Makefile $(PC_FILE) | build
	$(CC) $(CFLAGS) $(SRCS-y) -o $@ $(LDFLAGS) $(LDFLAGS_SHARED)

build/$(APP)-static: $(SRCS-y) Makefile $(PC_FILE) | build
	$(CC) $(CFLAGS) $(SRCS-y) -o $@ $(LDFLAGS) $(LDFLAGS_STATIC)

build:
	@mkdir -p $@

.PHONY: clean
clean:
	rm -f build/$(APP) build/$(APP)-static build/$(APP)-shared
	test -d build && rmdir -p build || true
endif
