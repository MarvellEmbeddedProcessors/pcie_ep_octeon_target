# SPDX-License-Identifier: BSD-3-Clause
# Copyright (C) 2020 Marvell.
#

# binary name
APP = dpi_test

# all source are stored in SRCS-y
SRCS-y := main.c

# Build using pkg-config variables if possible
ifeq ($(PKG_CONFIG_PATH),)
$(error "Please provide PKG_CONFIG_PATH=<path to 21.11 libdpdk.pc and its dependencies>.")
endif

PKGCONF ?= PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config

ifeq ($(shell $(PKGCONF) --atleast-version="21.11.0" libdpdk && echo 0),0)
ifeq ($(shell $(PKGCONF) --exists libdpdk && echo 0),0)
all: static
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
LDFLAGS_STATIC = $(shell $(PKGCONF) --static --libs libdpdk)

CC=$(CROSS_COMPILE)gcc

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
	rm -f .dpi_test_21_11_not_built
else
all:
	$(info "Only meson based build is supported")
	touch .dpi_test_21_11_not_built
endif
else
all:
	$(info "Please provide dpdk install directory for atleast dpdk-21.11.")
	touch .dpi_test_21_11_not_built
endif

