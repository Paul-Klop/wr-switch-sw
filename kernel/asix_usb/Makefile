obj-m		:= asix.o

# LINUX		?= ../../../kernel

export ARCH ?= arm
export CROSS_COMPILE ?= $(CROSS_COMPILE_ARM)

# CURRENT	= $(shell uname -r)
# TARGET	= asix
# OBJS	= asix.o
# MDIR	= drivers/net/usb
# KDIR	= /lib/modules/$(CURRENT)/build
# SUBLEVEL= $(shell uname -r | cut -d '.' -f 3 | cut -d '.' -f 1 | cut -d '-' -f 1 | cut -d '_' -f 1)

# ifneq (,$(filter $(SUBLEVEL),14 15 16 17 18 19 20 21))
# MDIR = drivers/usb/net
# endif

# EXTRA_CFLAGS = -DEXPORT_SYMTAB
# PWD = $(shell pwd)
# DEST = /lib/modules/$(CURRENT)/kernel/$(MDIR)

all: modules

modules:
	$(MAKE) -C $(LINUX) SUBDIRS=$(shell /bin/pwd) modules

clean:
	rm -f *.mod.c *.o *.ko *.i .*cmd  modules.order *~
	rm -rf .tmp_versions

.PHONY: all clean