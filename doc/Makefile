#
# Makefile for the documentation directory
#
# Copyright 1994,2000,2010,2011   Alessandro Rubini <rubini@linux.it>
#
#################

# There is not basenames here, all *.in are considered input
INPUT = $(wildcard *.in)

TEXI = $(INPUT:.in=.texi)
INFO = $(INPUT:.in=.info)
HTML = $(INPUT:.in=.html)
TXT  = $(INPUT:.in=.txt)
PDF  = $(INPUT:.in=.pdf)

ALL    = $(INFO) $(HTML) $(TXT) $(PDF)

MAKEINFO ?= makeinfo

RELEASE=$(shell git describe --always --dirty)

%.texi: %.in
	@rm -f $@
	sed s/__RELEASE_GIT_ID__/$(RELEASE)/ $< | sed -f ./infofilter > $@
	emacs -batch --no-site-file -l fixinfo $@
	chmod -w $@

%.pdf: %.texi
	texi2pdf --batch $<

%.info: %.texi
	$(MAKEINFO) $< -o $@

%.html: %.texi
	$(MAKEINFO) --html --no-split -o $@ $<

%.txt: %.texi
	$(MAKEINFO) --no-headers $< > $@

##############################################
DIRS=\
	autonegotiation_ha_wr \
	wrs_failures \

.PHONY: all check terse clean install $(DIRS)
.INTERMEDIATE: $(TEXI)


all: $(ALL) $(DIRS)
	$(MAKE) terse

check: _err.ps
	gs -sDEVICE=linux -r320x200x16 $<

terse:
	for n in cp fn ky pg toc tp vr aux log; do rm -f *.$$n; done
	rm -f *~

clean: terse
	rm -f $(ALL) $(TEXI)
	for i in $(DIRS); do \
		$(MAKE) -C $$i clean ; \
	done

install:
	if [ -n $(WRS_OUTPUT_DIR) ]; then cp *.pdf  $(WRS_OUTPUT_DIR)/images/; fi

$(DIRS):
	$(MAKE) -C $@
