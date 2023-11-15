#!/bin/bash

# Jean-Claude BAU @CERN
# script to generate Kconfig SFP and fiber configuration.
#
# Parameters:
#     -o file Overwrite the default output file name
#

OUTPUT_FILE="Kconfig_sfp_fiber.in"

script_name="$0"

#decode script parameters
while getopts o: option
do
	case "${option}" in
		"o") OUTPUT_FILE=${OPTARG};;
	esac
done


function print_header() { 

	echo -e "menu \"SFP and Media Timing Configuration\"" >$OUTPUT_FILE
}

function print_footer() {
 
	echo -e "\nendmenu" >>$OUTPUT_FILE
 
}

function print_sfp_header() { 
	#remove leading zero 
	local maxSfp=$(expr $1 + 0) 
	local nbSfp=${#sfp_params[@]}
	
	echo -e "\nconfig N_SFP_ENTRIES" >>$OUTPUT_FILE
	echo -e "\tint \"Number of SFP entries in SFP configuration DB\"" >>$OUTPUT_FILE
	echo -e "\trange 0  $maxSfp" >>$OUTPUT_FILE
	echo -e "\tdefault  $nbSfp" >>$OUTPUT_FILE
	echo -e "\thelp"  >>$OUTPUT_FILE
	echo -e "\tThis parameter defines the number of SFP entries" >>$OUTPUT_FILE
	echo -e "\tthat can be set in the configuration database" >>$OUTPUT_FILE
	echo -e "\tIncrease this number to add a new SFP entry." >>$OUTPUT_FILE
	
	echo -e "\nmenu \"SFPs configuration DB\"" >>$OUTPUT_FILE

} 

function print_sfp_entry() { 
	#remove leading zero 
	local maxSfp=$(expr $1 + 0) 
	local idSfp=$(expr $2 + 0)
		
	echo -e "\nconfig SFP${2}_PARAMS" >>$OUTPUT_FILE
	echo -e "\tstring \"Parameters for SFP $idSfp\"" >>$OUTPUT_FILE
	depends=""
	fid=$(expr $idSfp + 1)
	for i in `seq $fid $maxSfp`; do
		if [ "$depends" != "" ] ; then depends="$depends ||"; fi
		depends="$depends N_SFP_ENTRIES=$i"
	done
	echo -e "\tdepends on $depends" >>$OUTPUT_FILE
	echo -e "\tdefault \"${sfp_params[$idSfp]}\"" >>$OUTPUT_FILE
	echo -e "\thelp"  >>$OUTPUT_FILE
	echo -e "\tThis parameter, and the following ones, are used to" >>$OUTPUT_FILE
	echo -e "\tconfigure the timing parameters of a specific SFP" >>$OUTPUT_FILE
	echo -e "\ttransceiver. The transceiver name is autodected for each port" >>$OUTPUT_FILE
	echo -e "\tin the White Rabbit Switch, and you need one configuration" >>$OUTPUT_FILE
	echo -e "\tentry for each transceiver type that is installed in your" >>$OUTPUT_FILE
	echo -e "\tdevice." >>$OUTPUT_FILE
	echo -e "\tvn (optional) - Vendor Name of an SFP" >>$OUTPUT_FILE
	echo -e "\tpn - Part Number of an SFP" >>$OUTPUT_FILE
	echo -e "\tvs (optional) - Vendor Serial (serial number) of an SFP" >>$OUTPUT_FILE
	echo -e "\ttx - TX delay of an SFP" >>$OUTPUT_FILE
	echo -e "\trx - RX delay of an SFP" >>$OUTPUT_FILE
	echo -e "\twl_txrx - Tx wavelength separated by "+" with Rx wavelength of an SFP;" >>$OUTPUT_FILE
	echo -e "\tfor example wl_txrx=1490+1310" >>$OUTPUT_FILE
	echo -e "\tTo set a new SFP entry, increment the parameter "  >>$OUTPUT_FILE
	echo -e "\t\"Number of SFP entries in SFP configuration DB\" in the upper menu."  >>$OUTPUT_FILE	 
	 
} 

function print_sfp_footer() { 
  echo -e "\nendmenu" >>$OUTPUT_FILE
} 

function print_fiber_header() { 
	#remove leading zero 
	local maxFiber=$(expr $1 + 0) 
	local nbFiber=${#fiber_params[@]}
	
	echo -e "\nconfig N_FIBER_ENTRIES" >>$OUTPUT_FILE
	echo -e "\tint \"Number of fiber entries in fiber configuration DB\"" >>$OUTPUT_FILE
	echo -e "\trange 0  $maxFiber" >>$OUTPUT_FILE
	echo -e "\tdefault  $nbFiber" >>$OUTPUT_FILE
	echo -e "\thelp"  >>$OUTPUT_FILE
	echo -e "\tThis parameter defines the number of fiber entries" >>$OUTPUT_FILE
	echo -e "\tthat can be set in the configuration database" >>$OUTPUT_FILE
	echo -e "\tIncrease this number to add a new fiber entry." >>$OUTPUT_FILE
	

	echo -e "\nmenu \"Fibers configuration DB\"" >>$OUTPUT_FILE

} 

function print_fiber_entry() { 
	#remove leading zero 
	local maxFiber=$(expr $1 + 0) 
	local idFiber=$(expr $2 + 0)
		
	echo -e "\nconfig FIBER${2}_PARAMS" >>$OUTPUT_FILE
	echo -e "string \"Parameters for fiber type $idFiber\"" >>$OUTPUT_FILE
	depends=""
	fid=$(expr $idFiber + 1)
	for i in `seq $fid $maxFiber`; do
		if [ "$depends" != "" ] ; then depends="$depends ||"; fi
		depends="$depends N_FIBER_ENTRIES=$i"
	done
	echo -e "\tdepends on $depends" >>$OUTPUT_FILE
	echo -e "\tdefault \"${fiber_params[$idFiber]}\"" >>$OUTPUT_FILE
	echo -e "\thelp"  >>$OUTPUT_FILE
	echo -e "\tThis parameter specify the physical features of used fiber type."  >>$OUTPUT_FILE
	echo -e "\tSpecify the alpha value for each pair of used wavelengths."  >>$OUTPUT_FILE
	echo -e "\tThis parameter follows a format:"  >>$OUTPUT_FILE
	echo -e "\talpha_XXXX_YYYY=1.23e-04,alpha_AAAA_BBBB=4.56e-04,..."  >>$OUTPUT_FILE
	echo -e "\twhere XXX_YYYY and AAAA_BBBB are pairs of used wavelengths,"  >>$OUTPUT_FILE
	echo -e "\t1.23e-04, 4.56e-04 are alpha values to be used for particular"  >>$OUTPUT_FILE
	echo -e "\twavelengths."  >>$OUTPUT_FILE	 
	echo -e "\tTo set a new fiber entry, increment the parameter "  >>$OUTPUT_FILE
	echo -e "\t\"Number of fiber entries in fiber configuration DB\" in the upper menu."  >>$OUTPUT_FILE	 
} 

function print_fiber_footer() { 
  echo -e "\nendmenu" >>$OUTPUT_FILE
} 

declare sfp_params=(
	"vn=Axcen Photonics,pn=AXGE-1254-0531,tx=0,rx=0,wl_txrx=1310+1490"
	"vn=Axcen Photonics,pn=AXGE-3454-0531,tx=0,rx=0,wl_txrx=1490+1310"
	"vn=APAC Opto,pn=LS38-C3S-TC-N-B9,tx=761,rx=557,wl_txrx=1310+1490"
	"vn=APAC Opto,pn=LS48-C3S-TC-N-B4,tx=-29,rx=507,wl_txrx=1490+1310"
	"vn=ZyXEL,pn=SFP-BX1490-10-D,tx=0,rx=0,wl_txrx=1490+1310"
	"vn=ZyXEL,pn=SFP-BX1310-10-D,tx=0,rx=0,wl_txrx=1310+1490"
)

declare fiber_params=(
	"alpha_1310_1490=2.6787e-04"
	"alpha_1310_1490=2.6787e-04"
	"alpha_1310_1490=2.6787e-04"
	"alpha_1310_1490=2.6787e-04"
)

print_header

mxSFPs=18
print_sfp_header $mxSFPs 
lid=$(expr $mxSFPs - 1)
for i_sfp in `seq -f "%02.0f" 0 $lid`; do
	print_sfp_entry $mxSFPs $i_sfp
done
print_sfp_footer

mxFibers=18
print_fiber_header $mxFibers
lid=$(expr $mxFibers - 1)
for i_fiber in `seq -f "%02.0f" 0 $lid`; do
	print_fiber_entry $mxFibers $i_fiber
done
print_fiber_footer

	
print_footer

