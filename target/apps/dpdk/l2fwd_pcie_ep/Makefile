# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2010-2014 Intel Corporation
# Copyright(c) 2020 Marvell.

# binary name
APP = l2fwd_pcie_ep

# all source are stored in SRCS-y
SRCS-y := l2fwd_main.c \
	l2fwd/src/l2fwd.c \
	l2fwd/src/control/*.c \
	l2fwd/src/data/*.c \
	l2fwd/src/config/*.c \
	l2fwd/src/api-server/*.c

PKGCONF ?= pkg-config
# Build using pkg-config variables if possible
ifneq ($(PKG_CONFIG_PATH),)
PKGCONF = PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) PKG_CONFIG_LIBDIR=$(PKG_CONFIG_PATH)  pkg-config --define-variable=prefix=$(PKG_CONFIG_OVERRIDE_PREFIX)  --define-variable=execprefix=$(PKG_CONFIG_OVERRIDE_PREFIX)

endif

ifneq ($(shell $(PKGCONF) --exists libdpdk && echo 1),1)
$(error "libdpdk not found.")
endif

ifneq ($(shell $(PKGCONF) --exists libconfig && echo 1),1)
$(error "libconfig not found.")
endif


all: static
.PHONY: shared static
shared: build/$(APP)-shared
	ln -sf $(APP)-shared build/$(APP)
static: build/$(APP)-static
	ln -sf $(APP)-static build/$(APP)

APP_CFLAGS = $(CFLAGS)
APP_CFLAGS += -O3 -Werror -Wall -Wno-unused-function -Wno-strict-aliasing
APP_CFLAGS += $(shell $(PKGCONF) --cflags libdpdk)
# Add flag to allow experimental API as l2fwd uses rte_ethdev_set_ptype API
APP_CFLAGS += -DALLOW_EXPERIMENTAL_API
APP_CFLAGS += $(shell $(PKGCONF) --cflags libconfig)
APP_CFLAGS += -I$(CURDIR) -I$(CURDIR)/l2fwd/include -I$(CURDIR)/l2fwd/src/include

APP_LDFLAGS_SHARED = $(LDFLAGS)
APP_LDFLAGS_SHARED += $(shell $(PKGCONF) --libs libdpdk)
APP_LDFLAGS_SHARED += $(shell $(PKGCONF) --libs libconfig)
APP_LDFLAGS_SHARED += -lliboctep_cp -lm -lpthread -lcjson

APP_LDFLAGS_STATIC = $(LDFLAGS)
APP_LDFLAGS_STATIC += $(shell $(PKGCONF) --static --libs libdpdk)
APP_LDFLAGS_STATIC += $(shell $(PKGCONF) --static --libs libconfig)
APP_LDFLAGS_STATIC += -l:liboctep_cp.a -lm -lpthread -lcjson

CC=$(CROSS_COMPILE)gcc

build/$(APP)-shared: $(SRCS-y) Makefile | build
	$(CC) $(APP_CFLAGS) $(SRCS-y) -o $@ $(APP_LDFLAGS_SHARED)

build/$(APP)-static: $(SRCS-y) Makefile | build
	$(CC) $(APP_CFLAGS) $(SRCS-y) -o $@ $(APP_LDFLAGS_STATIC)

build:
	@mkdir -p $@

.PHONY: clean
clean:
	rm -f build/$(APP) build/$(APP)-static build/$(APP)-shared
	test -d build && rmdir -p build || true
