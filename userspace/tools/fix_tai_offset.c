/* This is just a subset of wr_date, user to test on the host */
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time_lib.h>

#ifndef MOD_TAI
#define MOD_TAI 0x80
#endif

int opt_verbose = 1;
char *prgname;

int main(int argc, char **argv)
{

	int taiOffset;

	prgname = argv[0];

	if ( (taiOffset=fixHostTai(NULL,time(NULL),NULL,opt_verbose)) == -1 ) {
		fprintf(stderr, "%s: Cannot fix TAI offset\n" ,prgname);
		return 0;
	}
	return taiOffset;
}
