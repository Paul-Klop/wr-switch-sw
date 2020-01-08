-include ../Makefile.specific

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

# defer running "net-snmp-config --cflags" so it is visible in make output
CFLAGS  += -fPIC -Wall  $$($(NET_SNMP_CONFIG) --cflags | sed s,-I/usr/include,,)
LDFLAGS = -shared $$($(NET_SNMP_CONFIG) --ldflags)
CFLAGS += -I../include -I../libwr/include -I../ppsi/include -I../ppsi/arch-wrs/include -I.
CFLAGS += -DWRS_WITH_SNMP_HACKISH_LOG=0

SHLIB = wrsSnmp.so
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
	init.c \

MIBS = WR-SWITCH-MIB.txt

OBJECTS = $(SOURCES:.c=.o)

all: $(SHLIB)

$(SHLIB): $(OBJECTS)
	$(CC) $(LDFLAGS) $^ -o $@

clean:
	rm -f $(SHLIB) $(OBJECTS)

install:
	install -d  $(WR_INSTALL_ROOT)/lib
	install $(SHLIB) $(WR_INSTALL_ROOT)/lib
	install $(MIBS) $(WR_INSTALL_ROOT)/lib
