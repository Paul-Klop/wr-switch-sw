<?php include 'functions.php'; include 'head.php'; ?>
<body id="sfpcalib">
<div class="main">
<div class="page">
<div class="header" >
<!--<h1>White-Rabbit Switch Tool</h1>-->
<div class="header-ports" ><?php wrs_header_ports(); ?></div>
<div class="topmenu">
	<?php include 'topmenu.php' ?>
</div>
</div>
<div class="content">
<div class="leftpanel">
<h2>Main Menu</h2>
	<?php include 'menu.php' ?>
</div>
<div class="rightpanel">
<div class="rightbody">
<h1 class="title">SFP Database <a href='https://gitlab.com/ohwr/project/white-rabbit/-/wikis/calibration' target='_blank'><img align=right src="./img/question.png"></a></h1>

<?php session_is_started() ?>

	<br>


	<?php

		$formatID = "alternatecolor";
		$class = "altrowstablesmall firstcol";
		$infoname = "SFP Configuration";
		$vn = 0;
		$vs = 0;
		$counter = 0;

		$header = array ("Vendor Name","Vendor Serial","Model", "tx", "rx", "&#955_txrx");
		$matrix = array();

		for($i = 0; $i < 10; $i++){
			$sfp = intval($i);
			$sfp = sprintf("%02s", $sfp);
			$sfp = strval($sfp);

			if(!empty($_SESSION["KCONFIG"]["CONFIG_SFP".$sfp."_PARAMS"])){
				array_push($matrix, "key=CONFIG_SFP".$sfp."_PARAMS,".$_SESSION["KCONFIG"]["CONFIG_SFP".$sfp."_PARAMS"]);
				$last = $i;
			}
		}

		if(!empty($_GET["add"])){
			/* look for first empty */
			for($i = 0; $i < 10 && !$empty; $i++){
				$sfp = intval($i);
				$sfp = sprintf("%02s", $sfp);
				$sfp = strval($sfp);
				if(empty($_SESSION["KCONFIG"]["CONFIG_SFP".$sfp."_PARAMS"])){
					array_push($matrix, "key=CONFIG_SFP".$sfp."_PARAMS,vn=,vs=,pn=,tx=,rx=,wl_txrx=");
					$empty = 1;
				}
			}

			if (!$empty){
				echo "<div id='alert'><center>There is only space for 10 SFP configurations.</center></div><br>";
			}
		}

		echo '<FORM method="POST">
			<table border="0" align="center" class="'.$class.'" id="'.$formatID.'">';
		if (!empty($infoname)) echo '<tr><th>'.$infoname.'</th></tr>';

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
		foreach ($matrix as $elements) {
			echo "<tr>";
			$element = explode(",",$elements);
			for ($j = 0; $j < 7; $j++) {
				$columns = explode("=",$element[$j]);

				if($columns[0]=="key"){
					echo '<INPUT type="hidden" value="'.$columns[1].'" name="key'.$i.'" >';
					$sfp_number=$columns[1][11];
				}
				if($columns[0]=="vn"){
					echo '<td align="center"><INPUT type="text" value="'.$columns[1].'" name="vn'.$i.'" ></td>';
					$vn=1;
				}
				if($columns[0]=="vs"){
					if(!$vn) echo '<td align="center"><INPUT type="text" value="" name="vn'.$i.'" ></td>';
					echo '<td align="center"><INPUT type="text" value="'.$columns[1].'" name="vs'.$i.'" ></td>';
					$vs=1;
					$vn=1;
				}
				if($columns[0]=="pn"){
					if (!$vn) echo '<td align="center"><INPUT type="text" value="" name="vn'.$i.'" ></td>';
					if (!$vs) echo '<td align="center"><INPUT type="text" value=""  name="vs'.$i.'" ></td>';
					echo '<td align="center"><INPUT type="text" value="'.$columns[1].'" name="pn'.$i.'" ></td>';
				}
				if($columns[0]=="tx"){
					echo '<td align="center"><INPUT type="text" value="'.$columns[1].'" name="tx'.$i.'" ></td>';
				}
				if($columns[0]=="rx"){
					echo '<td align="center"><INPUT type="text" value="'.$columns[1].'" name="rx'.$i.'" ></td>';
				}
				if($columns[0]=="wl_txrx" ){
					echo '<td align="center"><INPUT type="text" value="'.$columns[1].'" name="wl_txrx'.$i.'" ></td>';
				}
			}
			echo '<td align="center"><a href="deletesfp.php?id='.$sfp_number.'"><img src="img/delete.png" title="Delete SFP"></a></td>';
			echo "</tr>";
			$i++;
			$vs=0;
			$vn=0;
		}
		echo '</table>';

		echo '<INPUT type="hidden" value="yes" name="update">';
		echo '<INPUT type="submit" value="Save New Configuration" class="btn last">';
		echo '</FORM>';

		$error = 0;
		if (!empty($_POST["update"])){
			for($j=0; $j<$i && !$error; $j++){

				if($_POST["pn".$j]=="" || $_POST["tx".$j]=="" || $_POST["rx".$j]=="" || $_POST["wl_txrx".$j]==""){
					echo "<p>Model, Tx, Rx and WL_TXRX cannot be empty.</p>";
					$error = 1;
				}
				else{
					$_SESSION["KCONFIG"][$_POST["key".$j]] = "";
					
					if(empty($_SESSION["KCONFIG"][$_POST["key".$j]]))
						$_SESSION["KCONFIG"][$_POST["key".$j]] .= empty($_POST["vn".$j]) ? "" : "vn=".$_POST["vn".$j];
					else
						$_SESSION["KCONFIG"][$_POST["key".$j]] .= empty($_POST["vn".$j]) ? "" : ",vn=".$_POST["vn".$j];
					if(empty($_SESSION["KCONFIG"][$_POST["key".$j]]))
						$_SESSION["KCONFIG"][$_POST["key".$j]] .= empty($_POST["vs".$j]) ? "" : "vs=".$_POST["vs".$j];
					else
						$_SESSION["KCONFIG"][$_POST["key".$j]] .= empty($_POST["vs".$j]) ? "" : ",vs=".$_POST["vs".$j];
					if(empty($_SESSION["KCONFIG"][$_POST["key".$j]]))
						$_SESSION["KCONFIG"][$_POST["key".$j]] .= empty($_POST["pn".$j]) ? "" : "pn=".$_POST["pn".$j];
					else
						$_SESSION["KCONFIG"][$_POST["key".$j]] .= empty($_POST["pn".$j]) ? "" : ",pn=".$_POST["pn".$j];
					$_SESSION["KCONFIG"][$_POST["key".$j]] .= ",tx=".$_POST["tx".$j];
					$_SESSION["KCONFIG"][$_POST["key".$j]] .= ",rx=".$_POST["rx".$j];
					$_SESSION["KCONFIG"][$_POST["key".$j]] .= ",wl_txrx=".$_POST["wl_txrx".$j];
				}

			}

			if(!$error){
				save_kconfig();
				apply_kconfig();

				header ('Location: sfpconfiguration.php');
			}
		}

		echo '<hr><div align="right">
				<A HREF="sfpconfiguration.php?add=y">
				<img src="img/add.png" style="width:30px;height:30px;vertical-align:center">
				<span style="">Add new SFP</span></A>
				</div>';

	?>
</div>
</div>
</div>
<div class="footer">
	<?php include 'footer.php' ?>
</div>
</div>
</div>
</body>
</html>
