#SPDX-License-Identifier: BSD-3-Clause
#Copyright (c) 2022 Marvell.

PLAT ?= x86_64

ifeq ($(PLAT), aarch64)
CROSS_COMPILE=aarch64-marvell-linux-gnu-
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

ifeq ($(PLAT), x86_64)
LIBCONFIG_PKGCONFIG_PATH ?= /usr/lib/x86_64-linux-gnu/pkgconfig
else
ifeq ($(LIBCONFIG_PKGCONFIG_PATH),)
$(error Provide path to libconfig.pc with LIBCONFIG_PKGCONFIG_PATH)
endif
endif

LIBCONFIG_PKGCONF = PKG_CONFIG_PATH=$(LIBCONFIG_PKGCONFIG_PATH) pkg-config

ifneq ($(shell $(LIBCONFIG_PKGCONF) --exists libconfig && echo 1),1)
$(info Could not find libconfig.pc in $(LIBCONFIG_PKGCONFIG_PATH))
$(info Use LIBCONFIG_PKGCONFIG_PATH to provide correct path to libconfig.pc)
$(error Aborting...)
endif

LIBCONFIG_SHARED_LIB_FLAGS = $(shell $(LIBCONFIG_PKGCONF) --libs libconfig)
LIBCONFIG_STATIC_LIB_FLAGS = $(shell $(LIBCONFIG_PKGCONF) --static --libs libconfig)

SRCS = main.c

CFLAGS = -O3 -Werror -Wall -I$(CP_LIB_DIR)/include

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
