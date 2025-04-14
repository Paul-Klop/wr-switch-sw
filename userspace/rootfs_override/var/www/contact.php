<?php include 'functions.php'; include 'head.php'; ?>
<body id="contact">
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
<h1 class="title">About</h1>

	<?php $_SESSION['advance']=""; ?>

	<div>
		<?php
			$formatID = "alternatecolor";
			$class = "altrowstable firstcol";
			$infoname = "";
			$format = "list";
			$section = "WRS_TABLE_INFO";
			$subsection = "CONTACT";
			print_info($section, $subsection, $formatID, $class, $infoname, $format);
		?>
	</div>
	<br>

	<div>
		<p align=right>
			<strong>Open Hardware Repository
			<a href="https://gitlab.com/ohwr/project/white-rabbit/wikis">
				https://gitlab.com/ohwr/project/white-rabbit/wikis</a>
			</strong>
		</p>
		<br>
		<p align=right>
			<strong>White-Rabbit forums
			<a href="https://forums.ohwr.org/c/white-rabbit-dev/">
				https://forums.ohwr.org/c/white-rabbit-dev</a>
			</strong>
		</p>
	</div>

	<br><br><br><br>

	<div id="logo">
		<center>
		 <IMG SRC="img/cern.jpg" WIDTH=80  ALT="CERN">
		 <IMG SRC="img/7s.png" WIDTH=80  ALT="Seven Solutions">
		 <IMG SRC="img/ugr.gif" WIDTH=140 ALT="University of Granada">
		</center>
	</div>

	<div>
		<p align=right><strong>Developers: </strong>Alessandro Rubini,
													Tomasz Wlostowski,
													Benoit Rat,
													Federico Vaga,
													Grzegorz Daniluk,
													Maciej Lipinski,
													Adam Wujek,
													Jose Luis Gutierrez</p>
	</div>

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
