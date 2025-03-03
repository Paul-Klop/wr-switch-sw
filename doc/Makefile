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

SRC_DOT=\
    images/dot/autonegotiation_simple.dot
PDF_DOT=$(SRC_DOT:dot=pdf)

IMG_PDF=\
	images/dot-config_overwrite_flow.pdf \
	images/wr_mon.pdf \

IMG_PNG=\
	images/wrs_sfp_dump_A.png \
	images/wrs_sfp_dump_b.png \
	images/wrs_sfp_dump_m.png \
	images/wrs_sfp_dump_s.png \

MAKEINFO ?= makeinfo
DOT ?= dot

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

images/dot/%.pdf: images/dot/%.dot
	command -v $(DOT) >/dev/null 2>&1 || { echo "$(DOT) not found. Aborting." >&2; exit 1; }
	$(DOT) $< -Tpdf > $@

##############################################
DIRS=\
	autonegotiation_ha_wr \
	wrs_failures \

.PHONY: all check terse clean install $(DIRS)
.INTERMEDIATE: $(TEXI)


all: $(ALL) $(DIRS)
	$(MAKE) terse

wrs-user-manual.texi: ../userspace/ppsi/doc/ppsi_conf-manual.in $(PDF_DOT) $(IMG_PDF) $(IMG_PNG)

check: _err.ps
	gs -sDEVICE=linux -r320x200x16 $<

terse:
	for n in cp fn ky pg toc tp vr aux log; do rm -f *.$$n; done
	rm -f *~

clean: terse
	rm -f $(ALL) $(TEXI) $(PDF_DOT)
	for i in $(DIRS); do \
		$(MAKE) -C $$i clean ; \
	done

install:
	if [ -n $(WRS_OUTPUT_DIR) ]; then cp *.pdf  $(WRS_OUTPUT_DIR)/images/; fi

$(DIRS):
	$(MAKE) -C $@
