#SPDX-License-Identifier: BSD-3-Clause
#Copyright (c) 2022 Marvell.

PLAT ?= aarch64

ifeq ($(PLAT), aarch64)
CROSS_COMPILE=aarch64-marvell-linux-gnu-
endif

CC = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar
RANLIB = $(CROSS_COMPILE)ranlib
LD = $(CROSS_COMPILE)ld

LIB = liboctep_cp

LIBCONFIG_PKGCONF = PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config

ifneq ($(shell $(LIBCONFIG_PKGCONF) --exists libconfig && echo 1),1)
$(info Could not find libconfig.pc in $(PKG_CONFIG_PATH))
$(info Use PKG_CONFIG_PATH to provide correct path to libconfig.pc)
$(error Aborting...)
endif

LIBCONFIG_CFLAGS = $(shell $(LIBCONFIG_PKGCONF) --cflags libconfig)
LIBCONFIG_SHARED_LIB_FLAGS = $(shell $(LIBCONFIG_PKGCONF) --libs libconfig)

CFLAGS = -O3 -Werror -Wall -fPIC -g \
	-I$(CURDIR) \
	-I$(CURDIR)/include \
	-I$(CURDIR)/soc \
	-I$(CURDIR)/compat/$(PLAT) \
	$(LIBCONFIG_CFLAGS)

LDFLAGS = -shared -fvisibility=hidden $(LIBCONFIG_SHARED_LIB_FLAGS)

SRCS = main.c config.c
SRCS += soc/soc.c soc/cnxk.c
SRCS += soc/octep_ctrl_mbox.c

OBJS = main.o config.o
OBJS += soc.o cnxk.o octep_ctrl_mbox.o

STATIC_BIN = $(LIB).a
SHARED_BIN = $(LIB).so

all: shared static
.PHONY: shared static

static:
	$(info ====Building $(STATIC_BIN)====)
	$(CC) $(CFLAGS) -c $(SRCS)
	$(AR) rc $(STATIC_BIN) $(OBJS)
	$(RANLIB) $(STATIC_BIN)

shared:
	$(info ====Building $(SHARED_BIN)====)
	$(CC) $(CFLAGS) $(SRCS) -o $(SHARED_BIN) $(LDFLAGS)

clean:
	$(info ====Cleaning lib====)
	rm -f $(OBJS) || true
	rm -f $(SHARED_BIN) || true
	rm -f $(STATIC_BIN) || true
