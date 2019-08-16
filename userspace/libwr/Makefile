
# We are now Kconfig-based
-include ../../.config

OBJS = init.o fpga_io.o util.o pps_gen.o i2c.o shw_io.o i2c_bitbang.o \
       i2c_fpga_reg.o pio.o libshw_i2c.o i2c_sfp.o fan.o i2c_io.o hwiu.o \
       ptpd_netif.o hal_client.o hal_minirpc.o\
       shmem.o rt_client.o \
       dot-config.o config-lpcalib.o wrs-msg.o \
       mac.o \
       timeout.o \
       config-lpcalib.o

LIB = libwr.a

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

export CFLAGS_OPTIMIZATION:= ${shell echo $(CONFIG_OPTIMIZATION)}

CFLAGS += $(CFLAGS_OPTIMIZATION)
CFLAGS += -Wall -I. -Wstrict-prototypes \
	-DLIBWR_INTERNAL \
	-I./include \
	-I../include \
	-I../mini-rpc \
	-I$(LINUX)/arch/arm/mach-at91/include

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) rc $@ $^

install: all
	install -d $(WR_INSTALL_ROOT)/lib
	install $(LIB) $(WR_INSTALL_ROOT)/lib

clean:
	rm -f $(LIB) $(OBJS)
