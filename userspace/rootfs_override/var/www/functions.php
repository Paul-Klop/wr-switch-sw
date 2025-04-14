<?php

//Global Variables
$etcdir="/usr/wr/etc/"; //configuration file folder for WRS
$etcdir_ram="/etc/"; //configuration file folder for WRS in ramdisk
$snmpconf="snmpd.conf";
$ppsipreconf="ppsi-pre.conf";
$sfpdatabaseconf="sfp_database.conf";
$wrdateconf="wr_date.conf";
$vlancolor = array("#27DE2A", "#B642A8", "#6E42B6", "#425DB6" , "#428DB6", "#4686B6", "#43B88B", "#42B65F", "#82B642", "#B6AE42", "#B67E42");
$MAX_PHP_FILESIZE = 40;
$phpusersfile="/usr/etc/phpusers";
$profilefile="/usr/etc/profile";
$interfacesfile = "/usr/etc/network/interfaces";
$kconfigfile = "/wr/etc/dot-config";
$kconfigfilename = "dot-config";



/*if (empty($_SESSION["WRS_INFO"])){
	generate_wrs_info();
	include_once "data/wrs-info.php";
	$_SESSION["WRS_INFO"] = $GLOBALS["WRS_INFO"];
}*/
include "data/wrs-data.php";

//if (empty($_SESSION["WRS_TABLE_INFO"])){
	$_SESSION["WRS_TABLE_INFO"] = $GLOBALS["WRS_TABLE_INFO"];
//}

//if (empty($_SESSION["WRS_FORMS"])){
	$_SESSION["WRS_FORMS"] = $GLOBALS["WRS_FORMS"];
//}

if(empty($_SESSION["KCONFIG"])){
	load_kconfig();
}



/*
 * Displays the current status of each enpoint.
 *
 * @author José Luis Gutiérrez <jlgutierrez@ugr.es>
 * @author Benoit Rat <benoit<AT>sevensols.com>
 *
 * Displays the current status of each endpoint as wr_mon tool
 * in the swich does. It retrieves the information from wr_mon
 * and displays: master and slaves endpoints, calibration and locking
 * status.
 * If all endpoints looks disabled (but they are not) it means
 * PTP is not running.
 *
 */
function wrs_header_ports(){

	// Check whether $WRS_MANAGEMENT is set or we take the program
	// by default.

	if(!isset($_SESSION)){ 
		session_start(); 
    	} 

	if(empty($_SESSION['portsupdated'])){
		$_SESSION['portsupdated'] = intval(shell_exec("date +%s"));
	}

	// Let's update endpoints info every 15 seconds.
	$currenttime = intval(shell_exec("date +%s"));
	$interval = $currenttime - $_SESSION['portsupdated'];

	if(!file_exists("/tmp/www_ports.txt") || $interval>15){
		shell_exec("/wr/bin/wr_mon -w > /tmp/www_ports.txt");
		$_SESSION['portsupdated'] = intval(shell_exec("date +%s"));
	}
	
	// We parse and show the information comming from each endpoint.
	echo "<table border='0' align='center'  vspace='1'>";
	echo '<tr><th><h1 align=center>White-Rabbit Switch Manager</h1></th></tr>';
	echo '</table>';
	draw_table();
}

function draw_table(){
	$ports = shell_exec("/wr/bin/wr_mon -w | tail -20 | head -18 2>/dev/null");
	$ports = explode(PHP_EOL, $ports);
 	
	echo  "<table id='sfp_panel' border='0' align='center' vspace='15'>";
        echo '<tr class="port">';
        $cont = 0;
        for($i=0; $i<18; $i=$i+1){
                if (strpos($ports[$i], "up")){
                        if (strpos($ports[$i],"Master") === false){
                                $mode="slave";
                        }else{
                                $mode="master";
                        }
                }
                else $mode="linkdown";

                $desc=sprintf("#%02d: wri%d (%s)",$cont+1,$cont+1,$mode);
                echo '<th>'."<img class='".$mode."' src='img/".$mode.".png' alt='".$desc."', title='".$desc."'>".'</th>';
                $cont++;
        }

        echo '</tr>';
 	echo '<tr class="status">';
        for($i=0; $i<18; $i=$i+1){
                echo '<th>';
                if (strpos($ports[$i],"Calibrated")){
                        $mode="calibrated";
                        $img="check.png";
                }else{
                        $mode="uncalibrated";
                        $img="uncheck.png";
                }
                        echo "<img class='calibration ".$mode."' SRC='img/".$img."'  alt='".$mode."', title = '".$mode."'>";

                if (strstr($ports[$i],"Locked")){
                        $mode="locked";
	                echo "<img class='syntonization ".$mode."' SRC='img/".$mode.".png' alt='syntonization: ".$mode."', title = 'syntonization: ".$mode."'>";
                }
		else{
			$mode = "unlocked";
	                echo "<img class='syntonization ".$mode."' SRC='img/".$mode.".png' alt='syntonization: ".$mode."', title = 'syntonization: ".$mode."'>";

		}
	        echo '</th>';
        }
        echo '</tr>';

        echo '<tr>';
        echo '</tr>';

        echo '</table>';

}

/*
 * Displays OS, Hardware and protocols running on the switch
 *
 * @author José Luis Gutiérrez <jlgutierrez@ugr.es>
 *
 * Displays the info comming from the following commands:
 * 	 uname, wrs_version, wr_date and php.ini
 *
 */
function wrs_main_info(){

	$formatID = "alternatecolor";
	$class = "altrowstable firstcol";
	$infoname = "Switch Info";
	$format = "table";
	$section = "WRS_TABLE_INFO";
	$subsection = "DASHBOARD";

	print_info($section, $subsection, $formatID, $class, $infoname, $format);

	// Print dinamic stuff (PPSi status, WR Date, SNMP Server & NTP)
	$class = "altrowstable firstcol";
	$formatID = "alternatecolor1";
	$infoname = "WRS Services";

	// Load variablesm�m
	$wr_date =  str_replace("\n","<br>",
		shell_exec("/wr/bin/wr_date -n get | tail -2"));
	$PPSi = wrs_check_ptp_status() ?
		'[<a href="ptp.php">on</A>]' : '[<a href="ptp.php">off</A>]';
	$SNMP = check_snmp_status() ? '[on] ' : '[off] ';
	$SNMP_version = '&nbsp;&nbsp;ver. '.
		shell_exec("snmpd -v | grep version | awk '{print $3}'");
	$SNMP_port = shell_exec("cat ".$GLOBALS['etcdir_ram'].$GLOBALS['snmpconf']." |
		grep agent | cut -d: -f3 | awk '{print $1}'");
	$NTP = $_SESSION['KCONFIG']["CONFIG_NTP_SERVER"];
	$Monitor = check_monit_status() ? '[on] ' : '[off] ';

	$WRSmode = check_switch_mode();
	$WRSmode_xtra="";
	if(!strcmp($WRSmode, "GM")) {
		$WRSmode="GrandMaster";
		$ports = shell_exec("cat /tmp/www_ports.txt");
		if(empty($ports)) $WRSmode_xtra="<br>Waiting PPS/10MHz ...";
	}
	else if (!strcmp($WRSmode, "BC"))
		$WRSmode="Boundary Clock";
	else if (!strcmp($WRSmode, "FM"))
		$WRSmode="Free-Running Master";
	else
		$WRSmode="Unknown";
	
	#Obtain the temperatures using the last line of (wr-mon -w)
	$temperatures=shell_exec("cat /tmp/www_ports.txt 2>/dev/null | tail -1");
	$arr = split(" ", $temperatures);
	array_shift($arr);
	$temperatures = implode($arr, " ");

	$ports = shell_exec("/wr/bin/wr_mon -w | tail -2 | head -1");
        $arr2 = explode(" ", $ports);
	$ports = $arr2[3];

	// Print services table
	echo '<br><table class="'.$class.'" id="'.$formatID.'" width="100%">';
	echo '<tr><th>'.$infoname.'</th></tr>';

	echo '<tr><td>PTP Mode</td><td> <a href="ptp.php">'.$WRSmode.'</a>'.$WRSmode_xtra.'</td></tr>';
	echo '<tr><td>NTP Server</td><td> <a href="management.php">'.$NTP.'</td></tr>';
	echo '<tr><td>White-Rabbit Date</td><td id="datewr">'.$wr_date.'</td></tr>';
	echo '<tr><td>WR Status</td><td id="timing">'. "loading..." .'</td></tr>';
	echo '<tr><td>PPSi</td><td>'.$PPSi.'</td></tr>';
	echo '<tr><td>System Monitor</td><td> <a href="management.php">'.$Monitor.'</td></tr>';
	echo '<tr><td>Net-SNMP Server</td><td>'.$SNMP.'( port '.$SNMP_port.")</td></tr>";
	if(!empty($temperatures)) echo '<tr><td>Temperature (ºC)</td><td id="temp">'.$temperatures.'</td></tr>';
	echo '</table>';

}

function print_info($section, $subsection, $formatID, $class, $infoname, $format){

	switch ($format) {
		case "table":

			echo "<table class='".$class."'  id='".$formatID."'  width='100%'>";
			if (!empty($infoname)) echo '<tr><th>'.$infoname.'</th></tr>';

			foreach ($_SESSION[$section][$subsection] as $row) {
				echo "<tr>";
				echo "<td>".$row["name"]."</td>"."<td>".(($row["value"]=="") ? "(not set)" : $row['value'])."</td>";
				echo "</tr>";
			}
			echo '</table>';

			break;

		case "list":

			echo '<ul>';
			foreach ($_SESSION[$section][$subsection] as $row) {
				echo "<li>";
				echo $row["name"].": ".(($row["value"]=="") ? "(not set)" : $row['value']);
				echo "</li>";
			}
			echo '<ul>';

			break;
	}
}

function print_form($section, $subsection, $formatID, $class, $infoname, $format){
	echo '<FORM method="POST"><table border="0" align="center" class="'.$class.'" id="'.$formatID.'">';
	if (!empty($infoname)) echo '<tr><th>'.$infoname.'</th></tr>';
	foreach ($_SESSION[$section][$subsection] as $row) {
		echo "<tr>";
		echo "<td>".$row["name"]."</td>";
		echo '<td align="center"><INPUT type="text" value="'.$row["value"].'" name="'.$row["vname"].'"></td>';
		echo "</tr>";
	}
	echo '</table>';

	echo '<INPUT type="submit" value="Save New Configuration" class="btn last">';
	echo '</FORM>';

}

function process_form($section, $subsection){

	$modified = false;

	if(!empty($_POST)){
		foreach ($_SESSION[$section][$subsection] as $row) {
			$_SESSION["KCONFIG"][$row["key"]]=$_POST[$row["vname"]];
			$_SESSION[$section][$subsection][$row["value"]] = $_POST[$row["vname"]];
			$modified = true;
		}
	}
	return $modified;
}

function print_multi_form($matrix, $header, $formatID, $class, $infoname, $size){

	echo '<FORM method="POST">
			<table border="0" align="center" class="'.$class.'" id="'.$formatID.'"  width="100%" >';
	if (!empty($infoname)) echo '<tr><th colspan='.count($matrix).'>'.$infoname.'</th></tr>';

	// Printing fist line with column names.
	if (!empty($header)){
		echo "<tr class='sub'>";
		foreach ($header as $column){
			echo "<td>".($column)."</td>";
		}
		echo "</tr>";
	}

	$i = 0;

	// Printing the content of the form.
	foreach ($matrix as $array){
		$elements = explode(",",$array);

		$first = 0;
		echo "<tr>";
		foreach ($elements as $element){
			$column = explode("=",$element);
			if ($column[0]=="key"){
				echo '<INPUT type="hidden" value="'.$column[1].'" name="key'.$i.'" >';
			}else{
				echo '<td align="center"><INPUT size="'.$size.'" type="text" value="'.$column[1].'" name="'.$column[0].$i.'" ></td>';
				$first = 1;
			}
		}
		echo "</tr>";
		$i++;
		$first = 0;
	}
	echo '</table>';

	echo '<INPUT type="submit" value="Save New Configuration" class="btn last">';
	echo '</FORM>';
}

function strposa($haystack, $needles=array(), $offset=0) {
		$i = 0;
		                
        foreach($needles as $needle) {	
				if (strstr($needle,$haystack, true)!==false) return $i;
				else $i++;
        }
		return -1;
}

function print_dynamic_multi_form($matrix, $header, $formatID, $class, $infoname, $size){
	$names = array("slave", "auto", "non_wr", "none", "master"); 
	$monitor = array("y","n"); 
	$ext = array("WR", "none"); 
	$dm = array("e2e", "p2p"); 

	echo '<FORM method="POST" id="endpointconfig">
			<table border="0" align="center" class="'.$class.'" id="'.$formatID.'" >';
	if (!empty($infoname)) echo '<tr><th colspan='.count($matrix).'>'.$infoname.'</th></tr>';

	// Printing fist line with column names.
	if (!empty($header)){
		echo "<tr class='sub'>";
		foreach ($header as $column =>$c){
			if($column!="key"){
				echo "<td>".($c)."</td>";
			}	
		}
		echo "</tr>";
	}

	$i = 0;

	// Printing the content of the form.
	foreach ($matrix as $array){
		$first = 0;
		$elements = explode(",",$array);
		
		foreach ($header as $column =>$c) {
			if(strposa($column, $elements)>-1){
				$column2 = explode("=",$elements[strposa($column, $elements)]); 

				if ($column=="key"){
					echo '<INPUT type="hidden" value="'.$column2[1].'" name="key'.$i.'" >';
				}
				//role
				else if ($column=="role"){
					echo '<td><select value="'.$column2[1].'" name="'.$column2[0].$i.'" id="selected" class="drop">';
					for ($j=0; $j<sizeof($names);$j++){
						echo $names[$j];
						if($column2[1] == $names[$j]){
							echo '<option name="'.$column2[0].$i.'" selected="selected" ">'.$column2[1].'</option>';
						}
						else{
							echo '<option value='. $names[$j] .'>'. $names[$j] .'</option>';
						}
					}
					echo '</select></td>';
				}
				//monitor
				else if ($column=="monitor"){
					if($column2[1] == "y")
						echo '<td align="center"><INPUT type="checkbox" value="y" name="'.$column2[0].$i.'" class="checkbox" checked></td>';
					else
						echo '<td align="center"><INPUT type="checkbox" value="n" name="'.$column2[0].$i.'" class="checkbox" ></td>';
				}
				//extension
				else if ($column=="ext"){
					echo '<td><select value="'.$column2[1].'"name="'.$column2[0].$i.'"  id="selected" class="drop">';
					for ($j=0; $j<sizeof($ext);$j++){
						if($column2[1] == $ext[$j]){
							echo '<option selected="selected" ">'.$column2[1].'</option>';
						}
						else{
							echo '<option value='. $ext[$j] .'>'. $ext[$j] .'</option>';
						}
					}
					echo '</select></td>';
				}
				//delay mechanistm
				else if ($column=="dm"){
					echo '<td><select value="'.$column2[1].'" name="'.$column2[0].$i.'" id="selected" class="drop">';
					for ($j=0; $j<sizeof($dm);$j++){
						if($column2[1] == $dm[$j]){
							echo '<option selected="selected" ">'.$column2[1].'</option>';
						}
						else{
							echo '<option value='. $dm[$j] .'>'. $dm[$j] .'</option>';
						}
					}
					echo '</select></td>';
				}
				//other params
				else{
					echo '<td align="center"><INPUT size="'.$size.'" type="text" value="'.$column2[1].'" name="'.$column2[0].$i.'" ></td>';
					$first = 1;
				}
			}
		}
		
		echo "<tr>";
		
		$i++;
		$first = 0;
	}
	echo '</table>';

	echo '<INPUT type="submit" value="Save New Configuration" class="btn last">';
	echo '</FORM>';
}


function process_multi_form($matrix){
	$modified = false;
	$i=0;
	if(!empty($_POST)){
		$tmp = implode(" ",$_POST);
		$fiber = strstr($tmp,'FIBER');
		foreach ($matrix as $array){
			
			$elements = explode(",",$array);

			foreach ($elements as $element){
				$column = explode("=",$element);

				if($column[0]!="key"){
					$var = trim($column[0].$i);
					$output .= preg_replace('/[0-9]+/', '', $var)."=".$_POST[$var].",";
				}else{
					$key = $_POST[$column[0].$i];
				}
				
				//$_SESSION["KCONFIG"][$row["key"]]=$_POST[$row["vname"]];
				//$_SESSION[$section][$subsection][$row["value"]] = $_POST[$row["vname"]];

			}
			$output = rtrim($output, ",");
			
			//change matrix fiber to match format to save at dot-config
			if($fiber != FALSE ){
				$output = str_replace("id=".$i."," , "" ,$output);
				$output = lreplace(",rx=", "_", $output);
				$output = str_replace("tx=","alpha_", $output);
				$output = str_replace(",val=", "=", $output);
        	        }
	
			// We have the line, put it in kconfig.
			$_SESSION["KCONFIG"][$key]=$output;

			// Clean output
			$output="";
			$i++;

		}
		$modified = true;
	}

	return $modified;
}

function check_ntp_server(){
	$ntpserver = $_SESSION['KCONFIG']["CONFIG_NTP_SERVER"];
	if(!strcmp($ntpserver, "")){
		return "not set";
	}else{
		return $ntpserver;
	}

}
function check_snmp_status(){
	$output = intval(shell_exec("ps aux | grep -c snmpd"));
	return ($output>2) ? 1 : 0;
}

function check_monit_status(){
	$output = shell_exec("/bin/ps axo command,state | grep '^/usr/bin/monit' | awk '{print \$NF}'");

	if(empty($output) || (strpos($output,'T') !== false)){
		return 0; /* monit is disabled or not in dotconfig */
	}else{
		return 1; /* monit is running */
	}
}

function wrs_interface_setup(){

    if(!empty($_SESSION["KCONFIG"]["CONFIG_ETH0_DHCP"]))
		return "dhcponly";
	else if (!empty($_SESSION["KCONFIG"]["CONFIG_ETH0_DHCP_ONCE"]))
		return "dhcponce";
	else if (!empty($_SESSION["KCONFIG"]["CONFIG_ETH0_STATIC"]))
		return "static";
	else
		return "dhcponly";
}

function wrs_dotconf_source_setup(){

    if(!empty($_SESSION["KCONFIG"]["CONFIG_DOTCONF_SOURCE_LOCAL"]))
		return "source_local";
	else if (!empty($_SESSION["KCONFIG"]["CONFIG_DOTCONF_SOURCE_REMOTE"]))
		return "source_remote";
	else if (!empty($_SESSION["KCONFIG"]["CONFIG_DOTCONF_SOURCE_TRY_DHCP"]))
		return "source_try_dhcp";
	else if (!empty($_SESSION["KCONFIG"]["CONFIG_DOTCONF_SOURCE_FORCE_DHCP"]))
		return "source_force_dhcp";
	else
		return "source_try_dhcp";
}

/*
 * It checks whether the filesystem is writable or not.
 *
 * @author José Luis Gutiérrez <jlgutierrez@ugr.es>
 *
 * It checks whether the filesystem is writable or not and prints a
 * warning message if so.
 *
 */
function wrs_check_writeable(){

	$output = shell_exec('mount | grep "(ro,"');
	echo (!isset($output) || trim($output) == "") ? "" : "<br><font color='red'>WARNING: WRS is mounted as READ-ONLY, please contact the maintainer</font>";

}

/*
 * It checks whether the ptpd daemon is running or not.
 *
 * @author José Luis Gutiérrez <jlgutierrez@ugr.es>
 *
 * It checks whether the ptpd daemon is running or not by counting
 * the number of entries in ps command.
 *
 * @return ptp is true or false
 *
 */
function wrs_check_ptp_status(){
	$output = intval(shell_exec("ps aux | grep -c ppsi"));
	return ($output>2) ? 1 : 0;
}

/*
 * It modifies each endpoint configuration
 *
 * @author José Luis Gutiérrez <jlgutierrez@ugr.es>
 *
 * It modifies each endpoint configuration by using wrs_phytool program
 * in switch.
 * Operations: show registers, modify registers, enable/disable
 * transmission, lock and master/grandmaster configuration.
 *
 * @param string $option1 New PHP sent filename value.
 * @param string $endpoint Endpoint to apply new configuration.
 *
 */
function wr_endpoint_phytool($option1, $endpoint){

	$cont=0;

	// User wants to display endpoint's registers
	if(!strcmp($option1, "dump")){

		$output=shell_exec("/wr/bin/wr_phytool ".$endpoint." dump");
		$ports = explode(" ", $output);
		echo 'wri'.$endpoint.'<BR>';
		echo "<table border='0' align='center'>";
		echo '<tr>';
		echo '<th>Register</th>';
		echo '<th>Value</th>';
		echo '</tr>';

		for($i=7; $i<20*2; $i=$i+2){
			echo '<tr>';
			echo '<th>R'.$cont.'</th>';
			echo '<th>'.substr($ports[($i)],0,10).'</th>';
			echo '</tr>';
			$cont++;
		}

		echo '</tr>';
		echo '</table>';

		//if (!strcmp($_POST['update'], "yes")){
			//echo 'aki stamos!';
		//}

	// User wants to modify endpoint's registers
	} else if(!strcmp($option1, "wri")){

		echo '<br>';
		echo '<center></center><form  method=POST>';
		echo "<table border='0' align='center'>";
		echo '<tr>';
		echo '<th>wri'.$endpoint.' Registers</th>';
		echo '<th><center>Value</center></th>';
		echo '</tr>';
		/* wr_phytool prints 17 registers */
		for($i=0; $i<17; $i++){
			echo '<tr>';
			echo '<th>R'.$i.'</th>';
			echo '<th><input type="text" name="r'.$i.'" value="'.$_POST['r'.$i].'"></th>';
			echo '</tr>';
			$cont++;
		}

		echo '</tr>';
		echo '</table>';
		echo '<input type="hidden" name="option1" value="wri">';
		echo '<input type="hidden" name="wri" value="yes">';
		echo '<input type="hidden" name="endpoint" value="'.$endpoint.'">';
		echo '<center><input type="submit" value="Update" class="btn"></center></form><center>';

		if(!empty($_POST['wri'])){
			for($i=0; $i<18 ; $i++){
				if (!empty($_POST['r'.$i])){
					$cmd = '/wr/bin/wr_phytool '.$_POST['endpoint'].' wr '.dechex($i).' '.$_POST['r'.$i].'';
					$output=shell_exec($cmd);
					echo $endpoint.':R'.$i.' modified';
				}
			}
		}

	// User wants to enable transmission on endpoint
	} else if (!strcmp($option1, "txcal1")){
		$output=shell_exec('/wr/bin/wr_phytool '.$endpoint.' txcal 1');
		echo $endpoint.' is now transmitting calibration';

	// User wants to disable transmission on endpoint
	} else if(!strcmp($option1, "txcal0")){
		$output=shell_exec('/wr/bin/wr_phytool '.$endpoint.' txcal 0');
		echo $endpoint.' stopped transmitting calibration';

	// User wants to lock endpoint
	} else if(!strcmp($option1, "lock")){
		$output=shell_exec('/wr/bin/wr_phytool '.$endpoint.' rt lock');
		echo 'Locking finished';

	// User wants to make endpoint master
	} else if(!strcmp($option1, "master")){
		$output=shell_exec('/wr/bin/wr_phytool '.$endpoint.' rt master');
		echo 'Mastering finished' ;

	// User wants to make endpoint grandmaster
	} else if(!strcmp($option1, "gm")){
		$output=shell_exec('/wr/bin/wr_phytool '.$endpoint.' rt gm');
		echo 'Grandmastering finished' ;

	} else if(!strcmp($option1, "hal_conf")){

	}
}


function wr_show_endpoint_rt_show(){
	/* use port number between 1 and 18 */
	$output=shell_exec('/wr/bin/wr_phytool 1 rt show');
	$rts = nl2br($output);
	echo $rts;
}

/*
 * It returns the max. filesize that can be upload to the switch.
 *
 * @author José Luis Gutiérrez <jlgutierrez@ugr.es>
 *
 *
 *
 */
function wrs_php_filesize(){
	 $size=ini_get('upload_max_filesize');
	 $size=substr($size, 0, -1);
	 return $size;
}

/*
 * It loads binaries to the FPGA, LM32 and firmware folder.
 *
 * @author José Luis Gutiérrez <jlgutierrez@ugr.es>
 *
 * It runs:
 * 	load-virtex for loading .bin to the FPGA
 * 	load-lm32 for loading .bin to the lm32 processor
 *
 *
 *
 */
function wrs_load_files(){

		// Loading  and executing binary file with load-virtex
		if (!empty($_FILES['fpgafile']['name'])){
			$uploaddir = '/tmp/';
			$uploadfile = $uploaddir . basename($_FILES['fpgafile']['name']);
			echo '<pre>';
			if ((!strcmp(extension($_FILES['fpgafile']['name']), "bin")) && move_uploaded_file($_FILES['fpgafile']['tmp_name'], $uploadfile)) {
				echo "<center>File is valid, and was successfully uploaded.</center>\n";

				print "</pre>";

				echo '<center>Loading FPGA binary '.$_FILES['fpgafile']['name'].', please wait for the system to reboot</center>';
				$str = shell_exec("/wr/bin/load-virtex ".$uploadfile);
				echo $str;

				wrs_reboot();

			} else {
				echo "<center>File is not valid, please upload a .bin file.</center>\n";
			}

		// Loading  and executing binary file with load-lm32
		} else if (!empty($_FILES['lm32file']['name'])){
			$uploaddir = '/tmp/';
			$uploadfile = $uploaddir . basename($_FILES['lm32file']['name']);
			echo '<pre>';
			if ((!strcmp(extension($_FILES['lm32file']['name']), "bin")) && move_uploaded_file($_FILES['lm32file']['tmp_name'], $uploadfile)) {
				echo "<center>File is valid, and was successfully uploaded.</center>\n";

				print "</pre>";

				echo '<center>Loading lm32 binary '.$_FILES['lm32file']['name'].',, please wait for the system to reboot</center>';
				$str = shell_exec("/wr/bin/load-lm32 ".$uploadfile);
				echo $str;

				wrs_reboot();

			}  else {
				echo "<center>File is not valid, please upload a .bin file</center>\n";
			}

		// Loading  and copying binary file to /tmp folder on the switch.
		} else if (!empty($_FILES['file']['name'])){
			$uploaddir = '/tmp/';
			$uploadfile = $uploaddir . basename($_FILES['file']['name']);
			echo '<pre>';
			if (/*(!strcmp(extension($_FILES['file']['name']), "bin")) &&*/ move_uploaded_file($_FILES['file']['tmp_name'], $uploadfile)) {
				echo "<center>File is valid, and was successfully uploaded to tmp folder\n";
			}  else {
				echo "<center>File is not valid, please upload a .bin file</center>\n";
			}

			echo "</pre>";
		}
}

/*
 * Used for halting, rebooting and mounting partitions as read-only or
 * writable on switch.
 *
 * @author José Luis Gutiérrez <jlgutierrez@ugr.es>
 *
 * Used for halting, rebooting and mounting partitions as read-only or
 * writable on switch.
 *
 *
 */
function wrs_management(){

		$cmd =  htmlspecialchars($_POST["cmd"]);

		if (!strcmp($cmd, "reboot")){
			wrs_reboot();
		} else if (!empty($_FILES['firmware']['name'])){
			shell_exec("rm /tmp/wrs-firmware.tar"); //Clean previously uploaded files
			shell_exec("rm /tmp/wr-switch-sw-v*_binaries.tar"); //Clean previously uploaded files
			$uploaddir = '/tmp/';
			$uploadfname= basename($_FILES['firmware']['name']);
			$uploadfile = $uploaddir . $uploadfname;
			echo '<pre>';
			if (move_uploaded_file($_FILES['firmware']['tmp_name'], $uploadfile)) {
				echo '<p align=center ><font color="red"><br>Upgrade procedure will take place after reboot.<br>Please do not switch off the device during flashing procedure.</font></p>';
				if ($uploadfname=="barebox.bin" || $uploadfname=="wrs-firmware.tar" || $uploadfname=="zImage")
				{
					shell_exec("mv -f $uploadfile /update/".($_FILES['firmware']['name']));
					//Reboot switch
					sleep(1);
					wrs_reboot(90); //Updating only one part of the firmware take ~90s.
				}
				else if(substr($uploadfname,0,14)=="wr-switch-sw-v" && substr($uploadfname,-13)=="_binaries.tar")
				{
					shell_exec("mv -f $uploadfile /update/wrs-firmware.tar");
					//Reboot switch
					sleep(1);
					wrs_reboot(150); //120s should be enough but we prefer to keep safe
				}
				else
				{
					echo "<center class=\"error\">Incorrect filename, please choose a filename as:<br> barebox.bin, zImage, wrs-firmware.tar or wr-switch-sw-vX.X-YYYYMMDD_binaries.tar.</center>\n";
					unlink($uploadfile);
				}
			}  else {
				echo "<center class=\"error\">Something went wrong. File was not uploaded.</center>\n";
			}

			echo "</pre>";
		} else if (!strcmp($cmd, "remoteflash")){

			echo '<p align=center>Downloading '.$ohwrlink.'</p>';

			$filename="/tmp/wr-switch-sw-v8.0-20250324_binaries.tar";
			$firmware="/update/wrs-firmware.tar";
			$ohwrlink="https://gitlab.com/ohwr/project/wr-switch-sw/-/wikis/uploads/fc02a12aaa57b63632cd448ec487d051/wr-switch-sw-v8.0-20250324_binaries.tar";

			file_put_contents($filename, file_get_contents($ohwrlink));
			rename($filename, $firmware);
			echo '<p align=center>File successfully downloaded. Rebooting.</p>';

			wrs_reboot();

		} else if (!empty($_FILES['kconfig']['name'])){

			$uploaddir = $GLOBALS['etcdir'];
			$uploadfile = $uploaddir.$GLOBALS['kconfigfilename'];
			echo '<pre>';
			if (move_uploaded_file($_FILES['kconfig']['tmp_name'], $uploadfile)) {
				echo "<center>File is valid, and was successfully uploaded to ".$GLOBALS['etcdir']." folder. Applying changes and rebooting\n";
				sleep(1);
				wrs_reboot();
			}  else {
				echo "<center>File is not valid, please upload a valid file.<br>Filename must be '".$GLOBALS['kconfigfilename']."'</center>\n";
			}

			echo "</pre>";

		} else if (!strcmp($cmd, "Backup")){

			//Prepare backup
			$date = date('Ymd-His');
			$backupfile="wrs_".$_SESSION["WRS_INFO"][SERIALNUMBER]."-".$GLOBALS['kconfigfilename'].$date.".backup";
			shell_exec("cd ".$GLOBALS['etcdir']."; cp ".$GLOBALS['kconfigfilename']." /var/www/download/".$backupfile);
			$backupfile="/download/$backupfile";

			//Download the file
			header('Location: '.$backupfile);

		} else if (!strcmp($cmd, "ntp")){

			$ntpserver = htmlspecialchars($_POST["ntpip"]);

			$_SESSION["KCONFIG"]["CONFIG_NTP_SERVER"] = $ntpserver;

			//Apply config
			save_kconfig();
			apply_kconfig();

			//if(!empty($_SESSION["KCONFIG"]["CONFIG_TIME_BC"]) && (!empty($_POST["ntpip"]))){
				//echo '<div id="alert">Please notice that UTC time from a NTP Server is overriden by Boundary Clocks in track phase.
						//<br>If you still want to use time from a NTP Server it is recommended to set GrandMaster or Free-Running Master mode.</div>';
			//}else{
				//header('Location: ptp.php');
			//}

			header('Location: ptp.php');

		} else if (!strcmp($cmd, "snmp")){

			if(check_snmp_status()){ //It is running

				//Stop SNMP
				shell_exec("killall snmpd");

			}else{ //Not running

				shell_exec("/etc/init.d/snmp start> /dev/null 2>&1 &");

			}

			header('Location: management.php');
		} else if (!strcmp($cmd, "monit")){

			if(check_monit_status()){ //It is running
				//Stop SNMP
				shell_exec("/etc/init.d/monit.sh stop > /dev/null 2>&1 &");
			}else{ //Not running
				shell_exec("/etc/init.d/monit.sh start > /dev/null 2>&1 &");
			}

			sleep(2);
			header('Location: management.php');
		}
}

/**
     * Download file
     *
     * @param string $path
     * @param string $type
     * @param string $name
     * @param bool $force_download
     * @return bool
     */
function download($path, $name = '', $type = 'application/octet-stream', $force_download = true) {

	if (!is_file($path) || connection_status() !== 0);

	if($force_download) {
		header("Cache-Control: public");
	} else {
		header("Cache-Control: no-store, no-cache, must-revalidate");
		header("Cache-Control: post-check=0, pre-check=0", false);
		header("Pragma: no-cache");
	}

	header("Expires: ".gmdate("D, d M Y H:i:s", mktime(date("H")+2, date("i"), date("s"), date("m"), date("d"), date("Y")))." GMT");
	header("Last-Modified: ".gmdate("D, d M Y H:i:s")." GMT");
	header("Content-Type: $type");
	header("Content-Length: ".(string)(filesize($path)));

	$disposition = $force_download ? 'attachment' : 'inline';

	if(trim($name) == '') {
		header("Content-Disposition: $disposition; filename=" . basename($path));
	} else {
		header("Content-Disposition: $disposition; filename=\"" . trim($name)."\"");
	}

	header("Content-Transfer-Encoding: binary\n");

	if ($file = fopen($path, 'rb')) {
		while(!feof($file) and (connection_status()==0)) {
			print(fread($file, 1024*8));
			flush();
		}
		fclose($file);
	}

	return((connection_status() == 0) && !connection_aborted());
}

/*
 * This function configures the PTP daemon.
 *
 * @author José Luis Gutiérrez <jlgutierrez@ugr.es>
 *
 * It configures PTP daemon:
 * - By default (-A -c)
 * - By changing the following values:
 *
 *
 */
function wrs_ptp_configuration(){

	echo '<center>';

	if(!strcmp($_POST['cmd'],"ppsiupdate")){

		if(wrs_check_ptp_status()){ //PPSi is enabled.
			shell_exec("killall ppsi");
		}else{  //PPSi is disabled.
			$ptp_command = "/wr/bin/ppsi > /dev/null 2>&1 &";
			$output = shell_exec($ptp_command);
		}
		header('Location: ptp.php');
	}
	if(!strcmp($_POST['cmd'],"ppsibootupdate")){

		$_SESSION["KCONFIG"]["CONFIG_PPSI"] = ($_SESSION["KCONFIG"]["CONFIG_PPSI"]=="y") ? 'n' : 'y';
		save_kconfig();

		header('Location: ptp.php');
	}
	if (!empty($_POST["clkclass"])){
		$old_value= rtrim(shell_exec("cat ".$GLOBALS['etcdir'].$GLOBALS['ppsipreconf']." | grep class "));
		$new_value="clock-class ".htmlspecialchars($_POST["clkclass"]);
		$sed = 'sed -i "s/'.$old_value.'/'.$new_value.'/g" '.$GLOBALS['etcdir'].$GLOBALS['ppsipreconf'];echo $sed;
		shell_exec($sed);
		echo '<br>Clock Class changed to '.htmlspecialchars($_POST["clkclass"]);
	}
	if (!empty($_POST["clkacc"])){
		$old_value= rtrim(shell_exec("cat ".$GLOBALS['etcdir'].$GLOBALS['ppsipreconf']." | grep accuracy "));
		$new_value="clock-accuracy ".htmlspecialchars($_POST["clkacc"]);
		$sed ='sed -i "s/'.$old_value.'/'.$new_value.'/g" '.$GLOBALS['etcdir'].$GLOBALS['ppsipreconf'];echo $sed;
		shell_exec($sed);
		echo '<br>Clock Accuracy changed to '.htmlspecialchars($_POST["clkacc"]);
	}
	if ((!empty($_POST["clkclass"])) || !empty($_POST["clkacc"])){
		wrs_reboot();
	}
	echo '</center>';
}

function wrs_vlan_configuration($input){

	//Stop previous daemon and delete configuration file.
	shell_exec('killall wrsw_rtud_new');
	shell_exec('rm /tmp/vlan.conf');

	$wrsw_rtud= '/wr/bin/wrsw_rtud_new -w 1 ';
	for ($id=0; $id<18; $id++){
		$wrsw_rtud .=  $input[$id].' ';

		//echo 'Endpoint '.$id.' added to VLAN'.$input[$id];
		//echo '<br>';
		shell_exec( 'echo '.$input[$id].' >>/tmp/vlan.conf');
	}

	$wrsw_rtud .= " > /dev/null 2>&1 &";
	shell_exec($wrsw_rtud);

}

/*
 * It gets the text that the help window should desplay.
 *
 * @author José Luis Gutiérrez <jlgutierrez@ugr.es>
 *
 *
 */
function wrs_vlan_display(){

	$rtu_running=shell_exec('ps aux | grep -c wrsw_rtud_new');
	$rtu_running=(int)$rtu_running;

	if($rtu_running >2){
		$vlan_config = shell_exec('cat /tmp/vlan.conf');

		echo "<br><table border='1' align='center' class='altrowstable' id='alternatecolor'>";
		echo '<tr><td><b><center>Current Configuration </center></b></td></tr>';

		echo '<tr><th><center>'.$vlan_config.'</center></th></tr>';
		echo '</tr></table><br>';
	}

}

function wrs_display_help($help_id, $name){

	if(!strcmp($help_id, "dashboard")){
		$message = "<p>
				<table border=0 align=center>
					<tr>
						<td><img src='./img/master.png' width='20' ></td>
						<td>The violet connector means that the endpoint is master</td>
					</tr>
					<tr>
						<td><img src='./img/slave.png' width='20'></td>
						<td>The blue connector means that the endpoint is slave</td>
					</tr>
					<tr>
						<td><img src='./img/linkdown.png' width='20'></td>
						<td>The gray connector means that the endpoint is not connected</td>
					</tr>
					<tr>
						<td><img src='./img/unlocked.png' width='20'></td>
						<td>The unlocked icon means that the endpoint is not locked.</td>
					</tr>
					<tr>
						<td><img src='./img/locked.png' width='20'></td>
						<td>The locked icon means that the endpoint is locked.</td>
					</tr>
					<tr>
						<td><img src='./img/check.png' width='20'></td>
						<td>The green check icon means that the endpoint is calibrated.</td>
					</tr>
					<tr>
						<td><img src='./img/uncheck.png' width='20'></td>
						<td>The red cross icon means that the endpoint is not calibrated.</td>
					</tr>
				</table>
				</p>";
	} else if (!strcmp($help_id, "load")){
		$message = "<p>Loading files: <br>
					- <b>Load FPGA File</b>: Loads a .bin file for the gateware on the FPGA.<br>
					- <b>Load LM32 File</b>: Loads a .bin file into the lm32 processor.<br>
					</p>";
	} else if (!strcmp($help_id, "endpoint")){
		$message = "<p>It is used to configure each point of the switch with different parameters as well as wrs_phytool program does. <br>
						First <b>select an enpoint</b>. <br>
						Then, select an option from the list: <br>
							- <b>Enable Calibration Transmission</b>: enables calibration transmission on endpoint<br>
							- <b>Disable Calibration Transmission</b>: disables calibration transmission on endpoint<br>
							- <b>See registers</b>: displays each endpoint registers (R0-R16) <br>
							- <b>Modify registers</b>: used to modify endpoint registers (R0-R16) <br>
							- <b>Lock endpoint</b>: used to lock endpoint <br>
							- <b>Make switch master</b>: Switch works as master <br>
							- <b>Make switch grandmaster</b>: Switch works as grandmaster<br>
							</p>";
	} else if (!strcmp($help_id, "login")){
		$message = "<p>login</p>";
	} else if (!strcmp($help_id, "logout")){
		$message = "<p>logout</p>";
	} else if (!strcmp($help_id, "management")){
		$message = "<p>
			Options: <br>
			- <b>Reboot switch</b>: Reboots the switch.<br>
			- <b>System Monitor</b>: Enable/Disable the monit daemon.<br>
			- <b>Dot-config source during start</b>: Set the source of dot-config file at WRS startup. <br>
			  -- <b>Local</b>: The White Rabbit Switch is configured at run-time, according to a dot-config file stored locally.
			  If you select this option, dot-config is not replaced at run time.<br>
			  -- <b>Remote</b>: Use provided URL to the dot-config.<br>
			  -- <b>Force DHCP</b>: Retrieve a URL to the dot-config via DHCP at boot. The URL can be configured in the \"filename\"
			  configuration field of the DHCP server.<br>
			  -- <b>Try DHCP</b>: The same as Force DHCP, but this option does not cause errors in SNMP's objects if the switch
			  fails to retrieve the URL to the dot-config via DHCP.<br>
			  - <b>Load Configuration Files</b>: Upload a backup dotconfig file to the WRS and reboot.<br>
			- <b>Backup Configuration Files</b>: Downloads a copy of dotconfig file, which contains all the setup of the WRS.<br>
			</p>";
	} else if (!strcmp($help_id, "ptp")){
		$message = "<p>
					- <b>Switch Mode</b>: WRS can work on GrandMaster, Boundary Clock and Free-Running Master modes.<br>
					- <b>NTP Server</b>: Set a NTP server for UTC time. Please notice that White-Rabbit time will override the received NTP time.<br>
					- <b>Clock CLass and Clock Accuracy</b> fields modifies ppsi.conf file for those values and relanches the service again. <br></p>";
	} else if (!strcmp($help_id, "console")){
		$message = "<p>This is a switch console emulator windows. Use it as if you were using a ssh session.</p>";
	} else if (!strcmp($help_id, "gateware")){

		$msg = $_SESSION["WRSVERSION"];
		$msg = explode("\n", $msg);
		$message .= "<ul>";

		for($i=0; $i<5; $i++){

			$message .= "<li>".$msg[$i]."</li>";
		}
		$message .= "</ul>";

	}  else if (!strcmp($help_id, "file")){
		$msg = shell_exec("cat ".$GLOBALS['etcdir'].$name);
		$msg = explode("\n", $msg);
		for($i=0; $i<count($msg); $i++){

			$message .= $i.":   ".$msg[$i]."<br>";
		}
	} else if (!strcmp($help_id, "endpointmode")){
		$message = "<br><b>Change endpoint mode to master/slave/auto/non-wr/none by clicking on one of the items.</b><br>";
		$message .= "<b>It modifies dot-config and ppsi.conf files</b>";
	} else if (!strcmp($help_id, "snmp")){
		$message = "<p align=left>List of public SNMP OIDs</p><br>";
		$message .= shell_exec("snmpwalk -v1 -c public localhost");
		$message = str_replace("\n","<br>",$message);

	} else if (!strcmp($help_id, "vlan")){

		$message = "<br><b>Add new VLANs to the WRS</b>";
		$message .= "<br><b>- VID --> VLAN ID in rtud</b>";
		$message .= "<br><b>- FID --> Assign FID to configured VLAN</b>";
		$message .= "<br><b>- DROP --> Enable/Disable drop frames on VLAN</b>";
		$message .= "<br><b>- PRIO --> Sets Priority</b>";
		$message .= "<br><b>- MASK --> Mask for ports belonging to configured VLAN</b>";
		$message .= "<br><br>If you want to assign port to VLANs, please add VLANs first and then click on <strong>Assign Ports to VLANs</strong>. ";

	} else if (!strcmp($help_id, "vlanassignment")){

		$message = "<br><b>Assign ports to created VLANs</b>";
		$message .= "<br><b>VLANs ID --> VLANs ID already created in rtud</b>";
		$message .= "<br><b>Mode --> Sets mode for the endpoint:</b>";
		$message .= "<br><b><ul><li>Access --> tags untagged frames, drops tagged frames not belinging to configured VLAN</b></li>";
		$message .= "<br><b><li>Trunk --> passes only tagged frames, drops all untagged frames</b></li>";
		$message .= "<br><b><li>Disable --> passess all frames as is</b></li>";
		$message .= "<br><b><li>Unqualified Port --> passess all frames regardless VLAN config</b></li></ul>";
		$message .= "<br><b>Priority --> sets priority for retagging</b>";
		$message .= "<br><b>Mask --> sets untag mask for port</b>";


	} else if (!strcmp($help_id, "network")){
		$message = "<br><b>Set a DHCP network interface configuration or a static one for the management port.</b>";
		$message .= "<br><b>DHCP only will always try to get an IP address using DHCP. DHCP + Static will try only one time to get an IP address using DHCP, in case it fails, it will
						use a static configuration given by the user. For the static option no DHCP action is performed.</b>";
		$message .= "<br><b>If you set a Static configuration or DHCP + Static, you have to define: </b>";
		$message .= "<br><b><ul><li>IP Address --> IP Address of your switch</b></li>";
		$message .= "<br><b><li>Netmask --> Netmask</b></li>";
		$message .= "<br><b><li>Broadcast --> Broadcast address</b></li>";
		$message .= "<br><br><b>NOTE: This network configuration only works for NAND-flashed switches. If you are using a NFS server, the configuration is set by default in busybox and it is not possible to be changed.</b>";

	} else if (!strcmp($help_id, "firmware")){
		$message = "<p>Firmware features: <br>
					- <b>Flash firmware</b>: It flashes a new firmware to the switch. Do it under your own risk.<br>
					</p>";
	} else if (!strcmp($help_id, "auxclk")){
		$message = "<p>Aux. Clock configuration: <br>";
		$message .= "These are the parameters of a clock signal generated on the
						clk2 SMC connector.";
		$message .= "By default wrs auxclk is called by init scripts to generate 10MHz clock signal with 50% duty
						cycle. This configuration can be modified by using various options:";
		$message .= "<br><b><ul><li> freq <f> </b>--> Desired frequency of the generated clock signal in MHz. Available range from 4kHz
						to 250MHz.</li>";
		$message .= "<br><b><li>duty <frac></b> --> Desired duty cycle given as a fraction (e.g. 0.5, 0.4).</li>";
		$message .= "<br><b><li>cshift <csh></b> --> Coarse shift (granularity 2ns) of the generated clock signal. This parameter can be
						used to get desired delay relation between generated 1-PPS and clk2. The delay
						between 1-PPS and clk2 is constant for a given bitstream but may be different
						for various hardware versions and re-synthesized gateware. Therefore it should be
						measured and adjusted only once for given hardware and gateware version.</li>";
		$message .= "<br><b><li>sigdel <steps></b> --> Clock signal generated from the FPGA is cleaned by a discrete flip-flop. It may
						happen that generated aux clock is in phase with the flip-flop clock. In that case
						it is visible on the oscilloscope that clk2 clock is jittering by 4ns. The --sigdel
						parameter allows to add a precise delay to the FPGA-generated clock to avoid such
						jitter. This delay is specified in steps, where each step is around 150ps. This value,
						same as the --cshift parameter, is constant for a given bitstream so should be
						verified only once..</li>";
		$message .= "<br><b><li>ppshift <steps></b>--> If one needs to precisely align 1-PPS output with the clk2 aux clock using --cshift
						parameter is not enough as it has 4ns granularity. In that case --ppshift lets you
						shift 1-PPS output by a configured number of 150ps steps. However, please have in
						mind that 1-PPS output is used as a reference for WR calibration procedure. There-
						fore, once this parameter is modified, the device should be re-calibrated. Otherwise,
						1-PPS output will be shifted from the WR timescale by <steps>*150ps.</li>";
		$message .= "<br><center><b>Please refer to the WRS manual to configure correctly this feature.<br></center>
					</p>";
	} else if (!strcmp($help_id, "dotconfig")){
		$message = file_get_contents($GLOBALS['kconfigfile']);
		$message = str_replace("\n", "<br>", $message);
	} else if (!strcmp($help_id, "logs")){
		$message = "<p>Log files for the following services: <br>
					- Logging directions for <b>HAL daemon</b>, <b>RTU daemon</b>, <b>PPSi/PTP daemon</b>, <b>other programs</b>
					  (wrs_watchdog, wrs_auxclk, wrs_custom_boot_script.sh, vlan.sh)
					  -  The string can
					  be a pathname (e.g. /dev/kmsg) or a &lt;facility&gt;.&lt;level&gt; spefification like \"daemon.debug\".
					  An empty strings is used to represent no logging (like /dev/null). Please note that unknown facility
					  names will generate a runtime error on the switch. Please note that all messages produced by programs
					  if syslog is configured will be passed to the syslog at the same &lt;facility&gt;.&lt;level&gt,
					  no matter of verbosity of a message.<br>
					- Logging verbosity level for <b>HAL daemon</b>, <b>RTU daemon</b>, <b>PPSi/PTP daemon</b>, <b>other programs</b>
					  (wrs_watchdog, wrs_auxclk, wrs_custom_boot_script.sh, vlan.sh)
					  - Specify verbosity of programs as a string.
					  By leaving this item empty, a daemon will use its default verbosity level, which is usually INFO.
					  Please note that all messages produced by a program if syslog is configured will be passed to the syslog
					  at the same &lt;facility&gt;.&lt;level&gt, no matter of verbosity of a message.<br>
					- <b>Monit</b> - The string can be a pathname (e.g. /dev/kmsg) or a \"syslog\" string. An empty strings
					  is used  to represent no logging. If it is needed to select facility and level please leave here empty
					  string and change /etc/monitrc file directly. Please note that unknown facility names will generate a
					  runtime error on the switch.<br>
					- <b>SNMP service</b> - The string can be a pathname (e.g. /dev/kmsg) or a valid snmpd log option (without -L).
					  \"sd\" or \"s daemon\" will forward messages to syslog with daemon as facility. To set level (i.e. 5) use
					  \"S 5 daemon\". For details please check \"man snmpcmd\". An empty strings is used  to represent no
					  logging (like /dev/null). Please note that unknown facility names will generate a runtime error on the
					  switch.  NOTE: It looks like Notice is not a default logging priority as writen in the manual.<br>
					</p>";
	} else if (!strcmp($help_id, "wrinfo")){
		$message = "<p>Display live information about switch taken from the <b>wr_mon</b>. Information is updated every second.</p>";
	}

	echo $message;

}

/*
 * Obtains the file extension that will be loaded into the switch.
 *
 * @author José Luis Gutiérrez <jlgutierrez@ugr.es>
 *
 *
 */
function extension($filename){
    return substr(strrchr($filename, '.'), 1);
}

function parse_endpoint_modes(){

	$modes = array();

	for($i = 1; $i <= 18; $i++){
		$endpoint = intval($i);
		$endpoint = sprintf("%02s", $endpoint);
		$endpoint = strval($endpoint);

		$role = $_SESSION["KCONFIG"]["CONFIG_PORT".$endpoint."_PARAMS"];
		$role = explode(",",$role);
		
		foreach ($role as $column){
			if (strpos($column,'role=') !== false) {
				$role = str_replace("role=","",$column);
				array_push($modes,$role);
			}
		}
	}
	return $modes;
}
/*
 * Obtains the content the switch mode from dotconfig file
 *
 * @author José Luis Gutiérrez <jlgutierrez@ugr.es>
 *
 * @return GM, BC or FM mode.
 *
 */
function check_switch_mode(){
	if(!empty($_SESSION["KCONFIG"]["CONFIG_TIME_GM"]))
		return "GM";
	else if (!empty($_SESSION["KCONFIG"]["CONFIG_TIME_BC"]))
		return "BC";
	else if (!empty($_SESSION["KCONFIG"]["CONFIG_TIME_FM"]))
		return "FM";
}

function session_is_started(){

	ob_start();

	$login_url = "./index.php";

	if (!isset($_SESSION['myusername'])) {
		echo '<br><br><br><center>Please <a href="' . $login_url . '">login.</center></a>';
		exit;
	}


}

function parse_mask2ports($vlanmask){
	$vlanmask = str_replace("0x","", $vlanmask);
	$bin = decbin(hexdec($vlanmask));
	$bin = strrev($bin);
	$size = strlen($bin);
	$counter = 0;
	$ports = "";

	for($i=0; $i<18; $i++){
		if($bin[$i]=="1"){
			$ports .= "wri".($i+1)." ";
		}
	}

    return $ports;

}

function echoSelectedClassIfRequestMatches($requestUri)
{
    $current_file_name = basename($_SERVER['REQUEST_URI'], ".php");

    if ($current_file_name == $requestUri)
        return 'class="selected"';
}

function wrs_reboot($timeout=60){
	sleep(1);
	header ('Location: reboot.php?timeout='.$timeout);
}

/*
 * Parses kconfig (/wr/etc/dot-config) file and it is stored in a user
 * session variable.
 *
 * @author José Luis Gutiérrez <jlgutierrez@ugr.es>
 *
 */

function load_kconfig(){
	$_SESSION['LASTIME'] = filectime($GLOBALS['kconfigfile']);
	$_SESSION['KCONFIG'] = parse_ini_file($GLOBALS['kconfigfile']);
	/* Ignore everything after "#". It is intended to ignore comments at
	 * the end of a line. However, it will also affect items with "#" in
	 * the value.
	 * NOTE: When dot-config is written back all coments will be gone!
	 */
	foreach ($_SESSION['KCONFIG'] as &$row) {
		$no_comments = explode("#",$row);
		$row = $no_comments[0];
	}
}

/*
 * It checks whether a CONFIG__ element in the web interface exists or not
 * in kconfig. In case it doesn't, it includes it at the end of the file
 * with an empty value.
 *
 * @author José Luis Gutiérrez <jlgutierrez@ugr.es>
 * @pre: this is done when a new non-existing CONFIG__ value must be added
 * in kconfig, after using this function, the value must be fulfilled and
 * call save_kconfig().
 *
 */
function check_add_existing_kconfig($element){

	$file = file_get_contents($GLOBALS['kconfigfile']);
	$pos = strrpos($file, $element);
	$element .= '""'."\n";

	if ($pos === false){
		file_put_contents($GLOBALS['kconfigfile'], $element, FILE_APPEND | LOCK_EX);
	}
}

function delete_from_kconfig($element){

	shell_exec("sed -i '/".$element."/d' ".$GLOBALS['kconfigfile']);
}

/*
 * It saves the dotconfig configuration to dotfile file
 * and changes it from master to Grandmaster and vicecersa
 *
 * @author José Luis Gutiérrez <jlgutierrez@ugr.es>
 * @precondition: For disabling binary options, the web interface expects
 * internally a "=n". A blank space "" would think the field is string and
 * not binary.
 * i.e,
 * 	$_SESSION['KCONFIG'][CONFIG_PTP_WR_DEFAULT]=y; enables CONFIG_PTP_WR_DEFAULT
 * 		by adding "CONFIG_PTP_WR_DEFAULT=y"
 * 	$_SESSION['KCONFIG'][CONFIG_PTP_WR_DEFAULT]=n; disables CONFIG_PTP_WR_DEFAULT
 * 		by adding "# CONFIG_PTP_WR_DEFAULT is not set"
 *
 */
function save_kconfig(){

	$file = file_get_contents($GLOBALS['kconfigfile']);
	$reading = fopen($GLOBALS['kconfigfile'], 'r');
	$writing = fopen($GLOBALS['kconfigfile'].".tmp", 'w');

	$last_time = filectime($GLOBALS['kconfigfile']);
	$modified = 0;
	if (strcmp($last_time,$_SESSION['LASTIME']))
		$modified = 1;

	if (!$modified){
		while (!feof($reading)) {
		  $line = fgets($reading);
		  $line_aux = $line;
		  $element = explode("=",$line);

			// Dealing with enabled options
			if ((!empty($element)) && ($element[0]!="\n") && (!feof($reading)) && ($element[0][0]!="#")){
				if($_SESSION['KCONFIG'][$element[0]]=="y") //Already enabled binary options
					$line=$element[0].'='.$_SESSION['KCONFIG'][$element[0]]."\n";
				else if ($_SESSION['KCONFIG'][$element[0]]=="n") //Binary must be disabled
					$line="# ".$element[0]." is not set"."\n";
				else
					$line=$element[0].'="'.$_SESSION['KCONFIG'][$element[0]].'"'."\n";
			}

			// Dealing with enabled options and comments
			if (strpos($line_aux,'is not set') !== false){
				$element = explode(" ",$line_aux);
				if (!empty($element[1]) && empty($_SESSION['KCONFIG'][$element[1]]))
					$line="# ".$element[1]." is not set"."\n";
				else if (!empty($element[1]) && !empty($_SESSION['KCONFIG'][$element[1]]))
					$line=$element[1].'='.$_SESSION['KCONFIG'][$element[1]]."\n";
			}

		  fputs($writing, $line);
		}
		fclose($reading); fclose($writing);
		rename( $GLOBALS['kconfigfile'].".tmp", $GLOBALS['kconfigfile']);
		$_SESSION['LASTIME'] = filectime($GLOBALS['kconfigfile']);
	}else{
		load_kconfig();
		echo '<div id="alert">ERROR: Changes were not saved because dotconfig file was modified by other user.
				<br>Dotconfig has now been reloaded.</div>';
		exit;
	}
}

function safefilerewrite($dotconfig, $tmpdotconfig){

	if ($fp = fopen($dotconfig, 'w'))
	{
        	$startTime = microtime();
        do
        {            $canWrite = flock($fp, LOCK_EX);
           // If lock not obtained sleep for 0 - 100 milliseconds, to avoid collision and CPU load
           if(!$canWrite) usleep(round(rand(0, 100)*1000));
        } while ((!$canWrite)and((microtime()-$startTime) < 1000));

        //file was locked so now we can store information
        if ($canWrite)
        {            rename($tmpdotconfig, $dotconfig);
            flock($fp, LOCK_UN);
        }
        fclose($fp);
    }

}

function apply_kconfig(){
	$dotconfigapp = "/usr/wr/bin/apply_dot-config";
	/* So far all changes via web-interface are on local file. Notify apply_dot-config that changes are on local file */
	shell_exec($dotconfigapp. " local_config > /dev/null 2>&1 &");
}

function encrypt_password($password, $salt, $rounds, $method){

	$encrypted_passwd = "";

	switch ($method) {
		case "CRYPT_STD_DES":
			$encrypted_passwd = shell_exec('/wr/bin/mkpasswd --method=des "'.$password.'" --salt="'.$salt.'"');
			$encrypted_passwd = str_replace("\n", "", $encrypted_passwd);
			break;
		case "CRYPT_MD5":
			$encrypted_passwd = shell_exec('/wr/bin/mkpasswd --method=md5 "'.$password.'" --salt="'.$salt.'"');
			$encrypted_passwd = str_replace("\n", "", $encrypted_passwd);
			break;
		case "CRYPT_BLOWFISH":
			$encrypted_passwd = shell_exec('/wr/bin/mkpasswd --method=bf "'.$password.'" --salt="'.$salt.'"');
			$encrypted_passwd = str_replace("\n", "", $encrypted_passwd);
			break;
		case "CRYPT_SHA256":
			$encrypted_passwd = shell_exec('/wr/bin/mkpasswd --method=sha-256 "'.$password.'" --salt="'.$salt.'" --rounds="'.$rounds.'"');
			$encrypted_passwd = str_replace("\n", "", $encrypted_passwd);
			break;
		case "CRYPT_SHA512":
			$encrypted_passwd = shell_exec('/wr/bin/mkpasswd --method=sha-512 "'.$password.'" --salt="'.$salt.'" --rounds="'.$rounds.'"');
			$encrypted_passwd = str_replace("\n", "", $encrypted_passwd);
			break;
	}

	return $encrypted_passwd;
}

function get_encrypt_method($enc_password){

	$method = "";
	if (strpos($enc_password,'$1$') !== false)
		$method = "CRYPT_MD5";
	else if (strpos($enc_password,'$2a$07$') !== false)
		$method = "CRYPT_BLOWFISH";
	else if (strpos($enc_password,'$5$') !== false)
		$method = "CRYPT_SHA256";
	else if (strpos($enc_password,'$6$') !== false)
		$method = "CRYPT_SHA512";

	return $method;
}

function get_encrypt_rounds($enc_password){

	$elements = explode("$", $enc_password);
	$rounds = "";

	foreach ($elements as $element){
		if (strpos($element,'rounds=') !== false){
			$rounds = str_replace("rounds=","",$element);
		}
	}
	return $rounds;
}

function get_encrypt_salt($enc_password){
	$method = get_encrypt_method($enc_password);
	$salt = "";

	$elements = explode("$", $enc_password);

	switch ($method) {
		case "CRYPT_MD5":
			$salt = $elements[2];
			break;
		case "CRYPT_BLOWFISH":
			$salt = $elements[3];
			break;
		case "CRYPT_SHA256":
			$salt = $elements[3];
			break;
		case "CRYPT_SHA512":
			$salt = $elements[3];
			break;
	}
	return $salt;
}

/**
 * Function to reset switch to default
 *
 * @author Anne M. <anne@sevensols.com>
**/
function resetswitch(){ 
		shell_exec("make -C /wr/etc/ defconfig");
		wrs_reboot(); 
}

/**
 * Replaces last ocurrence in string
 *
 * @author Anne M. <anne@sevensols.com>
**/

function lreplace($search, $replace, $subject){
    $pos = strrpos($subject, $search);
    if($pos !== false){
         $subject = substr_replace($subject, $replace, $pos, strlen($search));
    }
    return $subject;
}

?>
