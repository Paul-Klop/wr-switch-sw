-include ../Makefile.specific

BRIDGE_DIRS= \
	dot1dBasePortTable \
	dot1dStaticTable \
	dot1dTpFdbTable \
	dot1qFdbTable \
	dot1qPortVlanTable \
	dot1qTpFdbTable \
	dot1qVlanStaticTable \

BRIDGE_INCLUDE_DIRS:=$(addsuffix /Makefile,$(BRIDGE_DIRS))
BRIDGE_INCLUDE_DIRS:=$(addprefix $(BRIDGE_MIB_DIR)/,$(BRIDGE_INCLUDE_DIRS))

include $(BRIDGE_INCLUDE_DIRS)

#BRIDGE_I_DIRS=$(addprefix -I$(BRIDGE_MIB_DIR)/,$(BRIDGE_DIRS))
CFLAGS += -I$(BRIDGE_MIB_DIR)

BRIDGE_MIB_SRCS += \
	dot1dBase.c \
	dot1qBase.c \
	init_bridge_mib.c

BRIDGE_MIB_SRCS:=$(addprefix $(BRIDGE_MIB_DIR)/,$(BRIDGE_MIB_SRCS))
