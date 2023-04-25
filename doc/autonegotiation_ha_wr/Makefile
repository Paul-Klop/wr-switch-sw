# Author: Adam Wujek for CERN 2023

all : autonegotiation_ha_wr.pdf

SRC_TEX=autonegotiation_ha_wr.tex

SRC_DOT=\
    dot/ha_auto_simple.dot \
    dot/ha_auto.dot \
    dot/servo.dot \

PDF_DOT=$(SRC_DOT:dot=pdf)


SRC_ODG=flowcharts/autonegotiation_flowcharts.odg

PDF_ODG=$(SRC_ODG:odg=pdf)


.PHONY : all clean

autonegotiation_ha_wr.pdf: $(SRC_TEX) $(PDF_DOT) $(PDF_ODG)
	pdflatex -dPDFSETTINGS=/prepress -dSubsetFonts=true -dEmbedAllFonts=true -dMaxSubsetPct=100 -dCompatibilityLevel=1.4 $(SRC_TEX)
	# 2nd pass to solve all references
	pdflatex -dPDFSETTINGS=/prepress -dSubsetFonts=true -dEmbedAllFonts=true -dMaxSubsetPct=100 -dCompatibilityLevel=1.4 $(SRC_TEX)

dot/%.pdf: dot/%.dot
	dot $< -Tpdf > $@

# Convert odg to PDF
flowcharts/%.pdf: flowcharts/%.odg
	# tested with LibreOffice 7.5.2.2 50(Build:2) on Ubuntu 20.04 LTS
	libreoffice --headless --convert-to pdf $< --outdir $(@D)
	# crop margins
	pdfcrop $@ $@

clean :
	rm -f  *.pdf *.dat *.log *.out *.aux *.toc $(PDF_DOT)  $(PDF_ODG)
