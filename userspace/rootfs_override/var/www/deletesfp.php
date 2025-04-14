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
<h1 class="title"><a href='https://gitlab.com/ohwr/project/white-rabbit/-/wikis/calibration' target='_blank'><img align=right src="./img/question.png"></a></h1>

<?php session_is_started() ?>

	<?php 
		$sfp = intval($_GET["id"]);
		$sfp = sprintf("%02s", $sfp);
		$sfp = strval($sfp);
		$_SESSION["KCONFIG"]["CONFIG_SFP".$sfp."_PARAMS"]="";
		save_kconfig();
		apply_kconfig();

		header ('Location: sfpconfiguration.php');
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
