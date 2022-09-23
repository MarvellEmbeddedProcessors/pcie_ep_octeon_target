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

CFLAGS = -O3 -Werror -Wall -fPIC -g \
	-I$(CURDIR) \
	-I$(CURDIR)/include \
	-I$(CURDIR)/soc \
	-I$(CURDIR)/compat/$(PLAT)

LDFLAGS = -shared -fvisibility=hidden

SRCS = main.c
SRCS += soc/soc.c soc/cnxk.c
SRCS += soc/octep_ctrl_mbox.c

OBJS = main.o
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
