-include ../Makefile.specific

# We are now Kconfig-based
-include ../../.config

# include Makefile for bridge mib
BRIDGE_MIB_DIR:=bridge_mib
include ${BRIDGE_MIB_DIR}/Makefile

# if BRVER not defined use a wildcard
BRVER?=2*
SNMP_BUILD := $(wildcard $(WRS_OUTPUT_DIR)/build/buildroot-$(BRVER))
SNMP_BUILD := $(wildcard $(SNMP_BUILD)/output/build/netsnmp-*)

NET_SNMP_CONFIG ?= $(SNMP_BUILD)/net-snmp-config

WR_INSTALL_ROOT ?= /usr/lib/white-rabbit

# # Standard stanza for cross-compilation (courtesy of the linux makefile)
AS              = $(CROSS_COMPILE)as
LD              = $(CROSS_COMPILE)ld
CC              = $(CROSS_COMPILE)gcc
CPP             = $(CC) -E
AR              = $(CROSS_COMPILE)ar
NM              = $(CROSS_COMPILE)nm
STRIP           = $(CROSS_COMPILE)strip
OBJCOPY         = $(CROSS_COMPILE)objcopy
OBJDUMP         = $(CROSS_COMPILE)objdump

# There is a static variable in pp-printf.c to accumulate stuff
CONFIG_PRINT_BUFSIZE ?= 256

export CFLAGS_OPTIMIZATION:= ${shell echo $(CONFIG_OPTIMIZATION)}

CFLAGS += $(CFLAGS_OPTIMIZATION)
# defer running "net-snmp-config --cflags" so it is visible in make output
CFLAGS  += -fPIC -Wall  $$($(NET_SNMP_CONFIG) --cflags | sed s,-I/usr/include,,)
LDFLAGS = -shared $$($(NET_SNMP_CONFIG) --ldflags)
CFLAGS += -I../include -I../libwr/include -I../ppsi/include -I../ppsi/pp_printf -I../ppsi/arch-wrs/include -I.
CFLAGS += -DWRS_WITH_SNMP_HACKISH_LOG=0
CFLAGS += -DCONFIG_PRINT_BUFSIZE=$(CONFIG_PRINT_BUFSIZE)


SHLIB = wrsSnmp.so
PPSI_SOURCES=\
	../ppsi/lib/div64.c\
	../ppsi/lib/time-arith.c\
	../ppsi/pp_printf/printf.c\
	../ppsi/pp_printf/vsprintf-full.c

SOURCES = \
	shmem.c \
	dot-config.c \
	snmp_shmem.c \
	util.c \
	snmp_msg.c \
	wrsScalar.c \
	wrsGeneralStatusGroup.c \
	wrsOSStatusGroup.c \
	wrsTimingStatusGroup.c \
	wrsNetworkingStatusGroup.c \
	wrsVersionGroup.c \
	wrsCurrentTimeGroup.c \
	wrsBootStatusGroup.c \
	wrsTemperatureGroup.c \
	wrsMemoryGroup.c \
	wrsCpuLoadGroup.c \
	wrsDiskTable.c \
	wrsStartCntGroup.c \
	wrsSpllVersionGroup.c \
	wrsSpllStatusGroup.c \
	wrsPstatsHCTable.c \
	wrsPtpDataTable.c \
	wrsPortStatusTable.c \
	wrsPtpInstanceTable.c \
	$(BRIDGE_MIB_SRCS) \
	init.c \
	$(PPSI_SOURCES)
	
MIBS = WR-SWITCH-MIB.txt

OBJECTS = $(SOURCES:.c=.o)

OID_FILE = oids.txt

all: $(SHLIB) $(OID_FILE)

$(SHLIB): $(OBJECTS)
	$(CC) $(LDFLAGS) $^ -o $@

$(OID_FILE): $(MIBS)
	snmptranslate -Pu -Tz -M +./ -m ./$(MIBS) >$(OID_FILE)

clean:
	rm -f $(SHLIB) $(OBJECTS)

install:
	install -d  $(WR_INSTALL_ROOT)/lib
	install $(SHLIB) $(WR_INSTALL_ROOT)/lib
	install $(MIBS) $(WR_INSTALL_ROOT)/lib
