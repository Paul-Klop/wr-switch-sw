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

CFLAGS =  -D KBUILD_NO_NLS

# most of this is just copying stuff in
RFILES = Kconfig_sfp_fiber.in Kconfig_port_timing.in Kconfig Kconfig_vlans.in \
		dot-config wrs_release_defconfig
XFILES = conf mconf nconf \
		dot-config_converter_6_x_to_7_0_script.sh
FILES = $(RFILES) $(XFILES)

all: $(FILES)

install: all
	install -d $(WR_INSTALL_ROOT)/etc
	install -m 644 $(RFILES) $(WR_INSTALL_ROOT)/etc
	install -m 755 $(XFILES) $(WR_INSTALL_ROOT)/etc
	install -m 644 Makefile-for-target $(WR_INSTALL_ROOT)/etc/Makefile

clean:
	rm -f $(FILES)

dot-config: $(WRS_BASE_DIR)/../.config
	cp $^ $@

Kconfig: $(WRS_BASE_DIR)/../Kconfig
	cp $^ $@

Kconfig_port_timing.in: $(WRS_BASE_DIR)/../Kconfig_port_timing.in
	cp $^ $@

Kconfig_sfp_fiber.in: $(WRS_BASE_DIR)/../Kconfig_sfp_fiber.in
	cp $^ $@

Kconfig_vlans.in: $(WRS_BASE_DIR)/../Kconfig_vlans.in
	cp $^ $@

dot-config_converter_6_x_to_7_0_script.sh: $(WRS_BASE_DIR)/../userspace/host_tools/dot-config_converter_6_x_to_7_0_script.sh
	cp $^ $@

CONF_LOC=$(WRS_BASE_DIR)/../scripts/kconfig

conf:
	$(CC) $(CFLAGS) -o $@ $(CONF_LOC)/conf.c \
		$(CONF_LOC)/zconf.tab.c

# if BRVER not defined use a wildcard
BRVER?=2*
CURSES_BUILD := $(wildcard $(WRS_OUTPUT_DIR)/build/buildroot-$(BRVER))
CURSES_BUILD := $(wildcard $(CURSES_BUILD)/output/build/ncurses-*)

lxdialog := $(CONF_LOC)/lxdialog/checklist.c
lxdialog += $(CONF_LOC)/lxdialog/util.c
lxdialog += $(CONF_LOC)/lxdialog/inputbox.c
lxdialog += $(CONF_LOC)/lxdialog/textbox.c
lxdialog += $(CONF_LOC)/lxdialog/yesno.c
lxdialog += $(CONF_LOC)/lxdialog/menubox.c

mconf:
	$(CC) $(CFLAGS) -o $@ $(CONF_LOC)/mconf.c $(CONF_LOC)/zconf.tab.c \
		$(lxdialog) \
		-DCURSES_LOC="<curses.h>" -I$(CURSES_BUILD)/include \
		-L$(CURSES_BUILD)/lib -lncurses

nconf:
	$(CC) $(CFLAGS) -o $@ $(CONF_LOC)/nconf.c $(CONF_LOC)/zconf.tab.c \
		$(CONF_LOC)/nconf.gui.c \
		-DCURSES_LOC="<curses.h>" -I$(CURSES_BUILD)/include \
		-L$(CURSES_BUILD)/lib -lncurses -lmenu -lpanel
#	nconf.o zconf.tab.o nconf.gui.o

wrs_release_defconfig: $(WRS_BASE_DIR)/../configs/wrs_release_defconfig
	cp $^ $@

