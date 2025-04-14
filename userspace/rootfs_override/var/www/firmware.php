<?php include 'functions.php'; include 'head.php'; ?>
<body id="management">
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
<h1 class="title">Firmware Management <a href='help.php?help_id=firmware' onClick='showPopup(this.href);return(false);'><img align=right src="./img/question.png"></a></h1>

	<?php
		session_is_started();
		$size=wrs_php_filesize();
		if (wrs_php_filesize() < 30) {
			echo '<p align=center ><font color="red">';
			echo 'Warning upload_max_filesize in php.ini is set to '.$size.'M. Upload probably will not work!';
			echo '</font></p>';
			}
	?>

	<table border="0" align="center">
		<tr>
			<FORM method="POST" ENCTYPE="multipart/form-data" onsubmit="return confirm('Are you sure you want to upload and flash a new firmware?');">
			<th><INPUT type=file name="firmware" ></th>
			<th><INPUT type=submit value="Flash Firmware" class="btn" ><INPUT type=hidden name=MAX_FILE_SIZE  VALUE=<?php echo wrs_php_filesize();?>000></th>
			</FORM>
		</tr>
<!--
		<tr></tr><tr></tr><tr>
			<FORM method="POST" ENCTYPE="multipart/form-data" >
			<th align=center>Download a backup of the entire Switch</th>
			<th><INPUT type=submit value="Backup Firmware" class="btn"><input type="hidden" name="cmd" value="backup-wrs"></th>
			</FORM>
		</tr>
-->
		<?php
		//Include downloading and flashing from OHWR if file wr-switch-sw-v4.0-rc1-20140710_binaries.tar is available
		//$ohwrlink="http://www.ohwr.org/attachments/download/3095/wr-switch-sw-v4.0-rc1-20140710_binaries.tar";

		//echo '<tr></tr><tr></tr><tr><tr></tr><tr></tr><tr><tr></tr><tr></tr><tr>';
		//echo '	<tr>
				//<th >Download binaries from OHWR <FORM method="POST" onsubmit="return confirm("Are you sure you want to upload and flash a new firmware?");"></th>
				//<th ><INPUT type=hidden name="cmd" value="remoteflash" >
				//<INPUT type=submit value="Download&Flash Firmware from OHWR" class="btn" ></th>
				//</FORM>
				//</tr>';

		?>
	</table>
		<div id="bottommsg">
		<hr>
		<p align=center ><font color="red">NOTE: Flashing the switch with a wrong binary file might damage your device. <br>Please visit the
			<A HREF="https://gitlab.com/ohwr/project/wr-switch-sw/-/wikis/Documents" TARGET="_new">WR Switch website</A> for more details.</font>
		</p>
		<hr>
	</div>
	<?php
		wrs_management();
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
