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

ifneq ($(CROSS_COMPILE),)
ifeq ($(BUILD_DEP_INC),)
$(info "Use BUILD_DEP_INC to provide paths for build dependency includes")
endif

ifeq ($(BUILD_DEP_LIB),)
$(info "Use BUILD_DEP_LIB to provide paths for build dependency libraries")
endif
endif

SRCS = main.c loop.c app_config.c

CFLAGS = -O3 -Werror -Wall \
	-I$(CP_LIB_DIR)/include \
	-I$(CURDIR)/compat/$(PLAT)

ifneq ($(BUILD_DEP_INC),)
CFLAGS += -I$(BUILD_DEP_INC)
endif

LDFLAGS_SHARED = -L$(CP_LIB_DIR) -l$(NAME_PREFIX)
ifneq ($(BUILD_DEP_LIB),)
LDFLAGS_SHARED += -L$(BUILD_DEP_LIB)
endif
LDFLAGS_SHARED += -lconfig -lrt

LDFLAGS_STATIC = -L$(CP_LIB_DIR) -l:lib$(NAME_PREFIX).a
ifneq ($(BUILD_DEP_LIB),)
LDFLAGS_STATIC += -L$(BUILD_DEP_LIB)
endif
LDFLAGS_STATIC += -lconfig -lrt

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
