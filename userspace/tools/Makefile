-include ../Makefile.specific
# We are now Kconfig-based
-include ../../.config

TOOLS = rtu_stat wr_mon wr_phytool wrs_pps_control spll_dbg_proxy load-lm32 load-virtex load-urv com
TOOLS += mapper wmapper
TOOLS += wrs_version wr_date lm32-vuart wrs_pstats wrs_leapsec
TOOLS += wrs_vlans wrs_dump_shmem
TOOLS += sdb-read
TOOLS += nbtee
TOOLS += wrs_auxclk
TOOLS += wrs_checkcfg
TOOLS += wrs_status_led
TOOLS += mkpasswd
TOOLS += wrs_sfp_dump
TOOLS += wrs_throttling
TOOLS += asix_ioctl
TOOLS += utc_leap_test
TOOLS += radiusvlan
TOOLS += wrsw_hal_conf
PPSI_CONFIG = ../ppsi/include/generated/autoconf.h

WR_INSTALL_ROOT ?= /usr/lib/white-rabbit
SCP             ?= scp -q -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no
SCP_DEV         ?= root@wrsdev
SCP_TARGET      ?= /wr

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

# LOTs of includes
export CFLAGS_OPTIMIZATION:= ${shell echo $(CONFIG_OPTIMIZATION)}

CFLAGS += $(CFLAGS_OPTIMIZATION)
CFLAGS += -Wall \
		-Wstrict-prototypes \
		-I$(LINUX)/arch/arm/mach-at91/include \
		-I../wrsw_rtud \
		-I../wrsw_hal \
		-I../wrsw_hal/nmea \
		-I../mini-rpc \
		-I../libsdb \
		-I../include \
		-I../libwr/include \
		-include $(PPSI_CONFIG) \
		-I../ppsi/include \
		-I../ppsi/arch-wrs/include \
		-I.

# for wrs_version alone (but others may use this)
GIT_VER = $(shell git describe --always --dirty | sed  's;^wr-switch-sw-;;')
GIT_USR = $(shell  git config --get-all user.name)
CFLAGS += -D__GIT_USR__="\"${GIT_USR}\"" -D__GIT_VER__="\"${GIT_VER}\""

LDFLAGS = -L../mini-rpc \
		-L../libwr \
		-L../libsdb \
		-lwr -lminipc -lsdbfs -lm -ldl

all:

all: 	check $(TOOLS)

check:
	@if [ ! -f $(PPSI_CONFIG) ]; then echo ""; echo ""; \
		echo "##### Please configure ppsi before building tools"; \
		echo ""; echo ""; \
		exit 1; \
	fi

%: %.c
	$(CC) $(CFLAGS) $*.c $(LDFLAGS) -o $*

%: %.o
	$(CC) $*.o $(LDFLAGS) -o $*

VPATH+=../ppsi/tools
wrs_dump_shmem: wrs_dump_shmem.o wrs_dump_shmem_ppsi.o time_lib.o
	${CC} -o $@ $^ $(LDFLAGS)

wr_mon: wr_mon.o term.o  time_lib.o
	${CC} -o $@ $^ $(LDFLAGS)

nmea/wr_nmea.o: nmea/serial.h nmea/wr_nmea.h nmea/nmea.h
nmea/nmea.o: nmea/nmea.h
wr_date: nmea/wr_nmea.o nmea/nmea.o nmea/serial_linux.o
wr_date: wr_date.o time_lib.o
	${CC} -o $@ $^ $(LDFLAGS)

wrs_leapsec: wrs_leapsec.o time_lib.o
	${CC} -o $@ $^ $(LDFLAGS)


fix_tai_offset: fix_tai_offset.o time_lib.o
	${CC} -o $@ $^ $(LDFLAGS)

wr_management: wr_management.o term.o
	${CC} -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TOOLS) *.o nmea/*.o *~

install: all
	install -d $(WR_INSTALL_ROOT)/bin
	install $(TOOLS) $(WR_INSTALL_ROOT)/bin

scp: all
	$(SCP) $(TOOLS) $(SCP_DEV):$(SCP_TARGET)/bin/

mkpasswd: mkpasswd.o utils.o
	${CC} -o $@ $^ $(LDFLAGS) -lcrypt
