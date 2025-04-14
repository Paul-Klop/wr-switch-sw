<?php include 'functions.php'; include 'head.php'; ?>
<body id="epcalib">
<div class="main">
<div class="page">
<div class="header" >
<script type="text/javascript" src="js/func.js"></script>
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
<h1 class="title">Fiber Configuration<a href='https://gitlab.com/ohwr/project/white-rabbit/-/wikis/calibration' target='_blank'><img align=right src="./img/question.png"></a></h1>
<br>

<?php session_is_started() ?>


	<?php
		// Warning message
		echo "<hr>
				<center>NOTE: If you do not know how to calibrate endpoints
				please click on <a href='https://gitlab.com/ohwr/project/white-rabbit/-/wikis/calibration'
				target='_blank'> here </a>*<hr><br>";
		$formatID = "alternatecolor1";
		$class = "altrowstablesmall firstcol fiberstable";
		$infoname = "Available Fibers";
		$size = "3";

		$header = array ("#","&#955tx", "&#955rx","Value");
		$matrix = array ("key=CONFIG_FIBER00_PARAMS, id=0,".$_SESSION["KCONFIG"]["CONFIG_FIBER00_PARAMS"],
				"key=CONFIG_FIBER01_PARAMS, id=1,".$_SESSION["KCONFIG"]["CONFIG_FIBER01_PARAMS"],
				"key=CONFIG_FIBER02_PARAMS,id=2,".$_SESSION["KCONFIG"]["CONFIG_FIBER02_PARAMS"],
				"key=CONFIG_FIBER03_PARAMS, id=3,".$_SESSION["KCONFIG"]["CONFIG_FIBER03_PARAMS"]);
		

		//change string to match drawing function
		$length = count($matrix);
                for ($i = 0; $i < $length; $i++) {
                	$matrix[$i] = str_replace("alpha_", "tx=", $matrix[$i]);
                	$matrix[$i] = lreplace("_", ",rx=", $matrix[$i]);
			$matrix[$i] = lreplace("=", ",val=", $matrix[$i]);
                }
		
		print_multi_form($matrix, $header, $formatID, $class, $infoname, $size);

		if(process_multi_form($matrix)){
	                save_kconfig();
                        apply_kconfig();
						
                        header ('Location: fiberconfiguration.php');
                }

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
