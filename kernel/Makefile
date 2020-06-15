
DIRS = coht_vic wr_nic wr_rtu wr_pstats wr_clocksource wrs_devices asix_usb

# We may "LINUX ?= /usr/src/linux-wrswitch", but it's better to leave it empty

export ARCH ?= arm
export CROSS_COMPILE ?= $(CROSS_COMPILE_ARM)

all modules clean:
	for n in $(DIRS); do \
		$(MAKE) -C $$n $@ || exit 1; \
	done

all modules: check

check:
	@if [ "x$(LINUX)" = "x" ]; then \
		echo "Please set \$$LINUX in the environment or cmdline" >& 2;\
		exit 1; \
	fi

# One extra rule, that was used by some previous users
MAKEDEFS = ../../Makedefs
ifeq ($(wildcard $(MAKEDEFS)),$(MAKEDEFS))
include $(MAKEDEFS)
deploy: all
	mkdir -p $(WR_INSTALL_ROOT)/lib/modules
	cp $$(find . -name '*.ko') $(WR_INSTALL_ROOT)/lib/modules
endif
