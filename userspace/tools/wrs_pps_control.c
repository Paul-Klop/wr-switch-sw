#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <libwr/wrs-msg.h>
#include <libwr/switch_hw.h>
#include <libwr/pps_gen.h>

void help(char *prgname)
{
	fprintf(stderr, "%s: Use: \"%s [<options>] <cmd>\n",
		prgname, prgname);
	fprintf(stderr,
		"  The program has the following options\n"
		"  -h   print help\n"
		"\n"
		"  Commands are:\n"
		"      pps <on|off|read>           - switch PPS output on/off.\n"
		"      50ohm-term-in <on|off|read> - on/off/read 50ohm termination for 1-PPS input\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "h")) != -1) {
		switch (opt) {
		case 'h':
		default:
			help(argv[0]);
		}
	}

	if (argc > 1) {
		if (!strcmp(argv[1], "pps")) {
			if (argc < 3) {
				printf("No parameter given\n;");
				exit(1);
			}
			if (!strcmp(argv[2], "on")) {
				assert_init(shw_fpga_mmap_init());
				shw_pps_gen_enable_output(PPSG_PPS_OUT_ENABLE);
				exit(0);
			} else if (!strcmp(argv[2], "off")) {
				assert_init(shw_fpga_mmap_init());
				shw_pps_gen_enable_output(PPSG_PPS_OUT_DISABLE);
				exit(0);
			} else if (!strcmp(argv[2], "read")) {
				assert_init(shw_fpga_mmap_init());
				if (shw_pps_gen_enable_output_read()
				    == PPSG_PPS_OUT_ENABLE) {
					printf("PPS output on\n;");
				} else {
					printf("PPS output off\n;");
				}
				exit(0);
			} else {
				printf("Unknown parameter\n;");
				exit(1);
			}
		} else if (!strcmp(argv[1], "50ohm-term-in")) {
			if (argc < 3) {
				printf("No parameter given\n;");
				exit(1);
			}
			if (!strcmp(argv[2], "on")) {
				assert_init(shw_fpga_mmap_init());
				shw_pps_gen_in_term_enable(
						PPSG_PPS_IN_TERM_50OHM_ENABLE);
				exit(0);
			} else if (!strcmp(argv[2], "off")) {
				assert_init(shw_fpga_mmap_init());
				shw_pps_gen_in_term_enable(
					      PPSG_PPS_IN_TERM_50OHM_DISABLE);
				exit(0);
			} else if (!strcmp(argv[2], "read")) {
				assert_init(shw_fpga_mmap_init());
				if (shw_pps_gen_in_term_read()
				    == PPSG_PPS_IN_TERM_50OHM_ENABLE) {
					printf("50ohm termination enabled on "
					       "1-PPS\n;");
				} else {
					printf("50ohm termination disabled on "
					       "1-PPS\n;");
				}
				exit(0);
			} else {
				printf("Unknown parameter\n;");
				exit(1);
			}
		} else {
			printf("Unknown command\n;");
			exit(1);
		}
	} else {
		printf("No command given\n");
		exit(1);
	}
}
