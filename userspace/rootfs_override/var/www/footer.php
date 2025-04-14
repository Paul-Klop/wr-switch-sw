<a href='index.php'><IMG SRC='img/wr_logo.png' align=left ,  vspace=7, hspace=5, width=50 , hight=100 , border=0 , alt='White Rabbit'></a>
<a href='https://gitlab.com/ohwr/project/wr-switch-sw/-/wikis' target="_blank"><IMG SRC='img/ohr.png' align=left ,  vspace=7, hspace=5, width=35 , hight=100 , border=0 , alt='OHR'></a>
<p>White Rabbit Project - Open Hardware and Source Project <a class="footer-link" target="_blank"
 href="https://gitlab.com/ohwr/project/white-rabbit/-/wikis">White-Rabbit OHR</a>
<?php
if(isset($_SESSION['myusername'])) echo "<a href='sysinfo.php' target='_blank'>";
echo "<IMG SRC='img/light_php.png' align=right , vspace=7, hspace=5, width=100 , hight=100 , border=0 , alt='PHP Info'>";
if(isset($_SESSION['myusername'])) echo "</a>";
?>
</p>
