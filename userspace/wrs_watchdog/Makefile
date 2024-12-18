
# We are now Kconfig-based
-include ../../.config

OBJS = wrs_watchdog.o

BINARY = wrs_watchdog

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
CFLAGS = -Wall \
	-Wstrict-prototypes \
	-I../include \
	-I../libwr/include \
	-I../mini-rpc \
	-I$(LINUX)/arch/arm/mach-at91/include

GIT_VER = $(shell git describe --always --dirty | sed  's;^wr-switch-sw-;;')
CFLAGS += -D__GIT_VER__="\"${GIT_VER}\""

LDFLAGS = -L../libwr -L../mini-rpc \
	 -lm -ldl -lwr -lminipc

all: $(BINARY)

$(BINARY): $(OBJS) ../libwr/libwr.a
	$(CC) -o $@ $^ $(LDFLAGS)

install: all
	install -d        $(WR_INSTALL_ROOT)/bin
	install $(BINARY) $(WR_INSTALL_ROOT)/bin

clean:
	rm -f $(BINARY) *.o *~
