/*
 * wrs_version.c
 *
 * Obtain the HW version and FPGA type.
 *
 *  Created on: Jan 20, 2012
 *  Authors:
 * 		- Benoit RAT
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License...
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <libwr/shw_io.h>
#include <libwr/hwiu.h>
#include <libwr/switch_hw.h>
#include <libwr/wrs-msg.h>
#include <fpga_io.h>
#include "libsdbfs.h"

#define SDBFS_NAME "/dev/mtd5ro"

#ifndef __GIT_VER__
#define __GIT_VER__ "x.x"
#endif

#ifndef __GIT_USR__
#define __GIT_USR__ "?"
#endif

/* TODO: Should be taken from generated files, but there are not present in
 * WRS repo */
#define GPIO_LJD_BOARD_DETECT	4
#define GPIO_REG_PSR 		12


void help(const char* pgrname)
{
	printf("usage: %s <command>\n", pgrname);
	printf("available commands are:\n"
				"   -b backplane hardware version (also '-p' for WWW)\n"
				"   -s scb hardware version (without dot in version number)\n"
				"   -f FPGA type\n"
				"   -g Gateware version\n"
				"   -c Compiling time\n"
				"   -v version (git)\n"
				"   -t tagged-output for other programs\n"
				"   -a All (default)\n"
				"\n");
		exit(1);
}

/*
 * This is the backend of sdb access, using the sdb library. I don't run
 * sdb-read because it pretends to mmap the file, and mtd can't be mapped
 * (this I'll fix in fpga-config-space); and it would add overhead
 */
struct drvdata {
	FILE *f;
};

static struct drvdata drvdata;

static int sdb_read(struct sdbfs *fs, int offset, void *buf, int count)
{
	struct drvdata *dd = fs->drvdata;
	FILE *f = dd->f;

	if (fseek(f, offset, SEEK_SET) < 0)
		return -1;
	return fread(buf, 1, count, f);
}

static struct sdbfs sdb_instance = {
	.drvdata = &drvdata,
	.entrypoint = 0, /* unfortunately: to be fixed */
	.read = sdb_read,
};

/* This uses the sdb library, with backend above */
static char *sdb_get(char *fname, char *tagname)
{
	static char buf[0x420];
	static char result[64];
	char *unknown = "UNKNOWN";
	static FILE *f = (FILE *)-1;
	int i, j;
	char *s;

	if (f == (FILE *)-1) {
		f = fopen(SDBFS_NAME, "r");
		if (!f) {
			fprintf(stderr, "%s: %s\n", SDBFS_NAME,
				strerror(errno));
			return unknown;
		}
		drvdata.f = f;
		i = sdbfs_dev_create(&sdb_instance);
		if (i != 0) {
			fprintf(stderr,
				"Error accessing SDB filesystem in \"%s\"\n",
			       SDBFS_NAME);
			f = NULL;
			return unknown;
		}
	}

	if (!f) /* already failed, already reported */
		return unknown;

	i = sdbfs_open_name(&sdb_instance, fname);
	if (i < 0) {
		fprintf(stderr, "Can't open \"%s\" in \"%s\"\n",
			fname, SDBFS_NAME);
		return unknown;
	}
	i = sdbfs_fread(&sdb_instance, -1, buf, sizeof(buf) - 1);
	if (i <= 0)
		return unknown;
	j = i - 1;
	/* trim trailing garbage as the file is assumed to be text */
	while (j >= 0 && (buf[j] == 0xff  || buf[j] == 0x00))
		j--;
	if (j >= 0 && buf[j] == '\n')
		j--; /* trailing newline too */
	buf [j + 1] = '\0';

	if (!tagname) {
		strncpy(result, buf, sizeof(result));
		return result;
	}
	/* Look for the tag */
	s = strstr(buf, tagname);
	if (!s)
		return unknown;
	sscanf(s, "%*[^:]:%*c%[^\n]", result);
	return result;
}


static char *get_fpga(void)
{
	return sdb_get("hw_info", "fpga");
}

/* Previous stuff follows */
static void print_gw_info(void)
{
	struct gw_info info;

	shw_fpga_mmap_init();
	if( shw_hwiu_gwver(&info) < 0 ) {
		fprintf(stderr, "Could not get GW version info\n");
		exit(1);
	}
	if( info.struct_ver != HWIU_STRUCT_VERSION )
		fprintf(stderr, "WARNING: dealing with unsupported "
			"info struct, sw:%u, gw:%u\n",
			HWIU_STRUCT_VERSION, info.struct_ver);

	/* Use tagged format, this is a subset of the "-t" operation */
	printf("gateware-version: %u.%u\n", info.ver_major, info.ver_minor);
	printf("gateware-build: %02u/%02u/%02u.%02u\n", info.build_day,
		info.build_month, info.build_year, info.build_no);
	printf("wr_switch_hdl-commit: %07x\n", info.switch_hdl_hash);
	printf("general-cores-commit: %07x\n", info.general_cores_hash);
	printf("wr-cores-commit: %07x\n", info.wr_cores_hash);
}

/* Print everything in tagged format, for snmp parsing etc */
static void wrsw_tagged_versions(void)
{
	int feature_ljd;

	printf("software-version: %s\n", __GIT_VER__); /* see Makefile */
	printf("built-by: %s\n", __GIT_USR__); /* see Makefile */
	printf("build-date: %s %s\n", __DATE__, __TIME__);
	printf("backplane-version: %s\n", get_shw_info('p'));
	printf("fpga-type: %s\n", get_fpga());
	printf("manufacturer: %s\n", sdb_get("manufacturer", NULL));
	printf("serial-number: %s\n", sdb_get("hw_info", "scb_serial"));
	printf("scb-version: %s\n", sdb_get("scb_version", NULL));
	print_gw_info(); /* This is already tagged */
	feature_ljd =_fpga_readl(FPGA_BASE_RT_GPIO + GPIO_REG_PSR)
		     & 1 << GPIO_LJD_BOARD_DETECT;
	printf("features: %s\n", feature_ljd ? "LJD" : "");
}

/* remove dots from strings */
static char *remove_dots(char *str)
{
	char *src, *dst;

	for (src = dst = str; *src != '\0'; src++) {
		*dst = *src;
		if (*dst != '.')
			dst++;
	}
	*dst = '\0';

	return str;
}

int main(int argc, char **argv)
{
	char func='a';

	/* argc forced to 1: -t and -v are not "terse" and "verbose" */
	wrs_msg_init(1, argv, LOG_USER);

	if(argc>=2 && argv[1][0]=='-')
	{
		func=argv[1][1];
	}
	else func='a';

	assert_init(shw_pio_mmap_init());
	shw_io_init();
	shw_io_configure_all();

	switch(func)
	{
	case 'f': /* Warning: this -p and -f is used by the web interface */
		printf("%s\n", get_fpga());
		break;
	case 'b':
		func='p';
	case 'p': /* Warning: this -p and -f is used by the web interface */
		printf("%s\n",get_shw_info(func));
		break;
	case 'g':
		print_gw_info();
		break;
	case 'c':
		/* Warning: this -c is used by the web interface */
		printf("%s %s\n",__DATE__,__TIME__);
		break;
	case 'v':
		printf("%s %s\n",__GIT_VER__,__GIT_USR__);
		break;
	case 't':
		wrsw_tagged_versions();
		break;
	case 'a':
		/* Warning: this with "awk '{print $4}'" is ued by the web if */
		printf("SCB HW:%s, ", sdb_get("scb_version", NULL)); 
		/* Printing this line is intentionally divided into two printf()
		   functions. The reason being:
		   function get_fpga() uses sdb_get(). The sdb_get() returns
		   static char result[] buffer. If the sdb_get() is called two
		   times in a single printf(), both instances of sdb_get() will
		   return the content of buffer acquired when sdb_get() was
		   called last.
                */
		printf("FPGA:%s; version: %s (%s); compiled at %s %s\n",
		       get_fpga(),__GIT_VER__, __GIT_USR__, __DATE__, __TIME__);
		break;
	case 's':
		printf("%s\n", remove_dots(sdb_get("scb_version", NULL)));
		break;
	case 'h':
	default:
		help(argv[0]);
		return 1;
	}
	return 0;
}
