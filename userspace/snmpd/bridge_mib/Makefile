-include ../Makefile.specific

BRIDGE_DIRS= \
	dot1qFdbTable

BRIDGE_INCLUDE_DIRS:=$(addsuffix /Makefile,$(BRIDGE_DIRS))
BRIDGE_INCLUDE_DIRS:=$(addprefix $(BRIDGE_MIB_DIR)/,$(BRIDGE_INCLUDE_DIRS))

include $(BRIDGE_INCLUDE_DIRS)

#BRIDGE_I_DIRS=$(addprefix -I$(BRIDGE_MIB_DIR)/,$(BRIDGE_DIRS))
CFLAGS += -I$(BRIDGE_MIB_DIR)

BRIDGE_MIB_SRCS += \
	init_bridge_mib.c

BRIDGE_MIB_SRCS:=$(addprefix $(BRIDGE_MIB_DIR)/,$(BRIDGE_MIB_SRCS))
