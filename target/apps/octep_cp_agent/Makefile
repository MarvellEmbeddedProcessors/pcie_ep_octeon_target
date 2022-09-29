#SPDX-License-Identifier: BSD-3-Clause
#Copyright (c) 2022 Marvell.

PLAT ?= aarch64

ifeq ($(PLAT), aarch64)
ifeq ($(CROSS_COMPILE),)
$(error "Please provide CROSS_COMPILE for platform $(PLAT)")
endif
endif

CC = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar
RANLIB = $(CROSS_COMPILE)ranlib
LD = $(CROSS_COMPILE)ld

NAME_PREFIX=octep_cp
APP_NAME=$(NAME_PREFIX)_agent

ifeq ($(CP_LIB_DIR),)
$(error "CP_LIB_DIR not defined. Aborting...")
endif

LIBCONFIG_PKGCONF = PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config

ifneq ($(shell $(LIBCONFIG_PKGCONF) --exists libconfig && echo 1),1)
$(info Could not find libconfig.pc in $(PKG_CONFIG_PATH))
$(info Use PKG_CONFIG_PATH to provide correct path to libconfig.pc)
$(error Aborting...)
endif

LIBCONFIG_CFLAGS = $(shell $(LIBCONFIG_PKGCONF) --cflags libconfig)
LIBCONFIG_SHARED_LIB_FLAGS = $(shell $(LIBCONFIG_PKGCONF) --libs libconfig)
LIBCONFIG_STATIC_LIB_FLAGS = $(shell $(LIBCONFIG_PKGCONF) --static --libs libconfig)

SRCS = main.c loop.c app_config.c

CFLAGS = -O3 -Werror -Wall \
	-I$(CP_LIB_DIR)/include \
	-I$(CURDIR)/compat/$(PLAT) \
	$(LIBCONFIG_CFLAGS)

LDFLAGS_SHARED = -L$(CP_LIB_DIR) -l$(NAME_PREFIX)
LDFLAGS_SHARED += $(LIBCONFIG_SHARED_LIB_FLAGS)

LDFLAGS_STATIC = -L$(CP_LIB_DIR) -l:lib$(NAME_PREFIX).a
LDFLAGS_STATIC += $(LIBCONFIG_STATIC_LIB_FLAGS)

STATIC_BIN = $(APP_NAME)
SHARED_BIN = $(APP_NAME)-shared

all:
	$(info ====Building $(SHARED_BIN)====)
	$(CC) $(CFLAGS) $(SRCS) -o $(SHARED_BIN) $(LDFLAGS_SHARED)
	$(info ====Building $(STATIC_BIN)====)
	$(CC) $(CFLAGS) $(SRCS) -o $(STATIC_BIN) $(LDFLAGS_STATIC)

clean:
	$(info ====Cleaning apps====)
	rm -f $(STATIC_BIN) $(SHARED_BIN) || true
