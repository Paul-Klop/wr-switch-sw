-include ../Makefile.specific

# We are now Kconfig-based
-include ../../.config

OBJS = hal_exports.o hal_main.o hal_ports.o hal_timing.o \
	hal_port_fsm.o hal_port_fsm_tx.o hal_port_fsm_rx.o  hal_port_fsm_pll.o\
	hal_port_leds.o hal_timer.o

BINARY = wrsw_hal

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
CFLAGS += -Wall \
	-Wstrict-prototypes \
	-I../include \
	-I../libwr/include \
	-I../mini-rpc \
	-I$(LINUX)/arch/arm/mach-at91/include \
	-I../ppsi/include \
	-I../ppsi/arch-wrs/include

GIT_VER = $(shell git describe --always --dirty | sed  's;^wr-switch-sw-;;')
CFLAGS += -D__GIT_VER__="\"${GIT_VER}\""

LDFLAGS = -L../libwr -L../mini-rpc \
	 -lm -ldl -lwr -lminipc

all: $(BINARY)

$(BINARY): $(OBJS)
	pwd
	$(CC) -o $@ $^ $(LDFLAGS)

install: all
	install -d        $(WR_INSTALL_ROOT)/bin
	install $(BINARY) $(WR_INSTALL_ROOT)/bin

clean:
	rm -f $(BINARY) *.o *~
