# Author: Adam Wujek 2024

IMG=\
    wr_mon.pdf \

all: $(IMG)

# Convert odg to PDF
%.pdf: %.odg
	# tested with LibreOffice 7.5.2.2 50(Build:2) on Ubuntu 20.04 LTS
	libreoffice --headless --convert-to pdf $< --outdir $(@D)
	# crop margins
	pdfcrop $@ $@

clean :
	rm -f  $(IMG) *.dat *.log *.out *.aux *.toc
