/* Alessandro Rubini for CERN, 2014 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <libwr/config.h>
#include <libwr/wrs-msg.h>

int main(int argc, char **argv)
{
	int err, verbose = 0;

	wrs_msg_init(argc, argv, LOG_USER);

me_lazy:
	if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
		printf("%s: Use: \"%s [-V] [-v] <dotcfg> [<Kconfig>]\"\n",
			argv[0], argv[0]);
		printf("<dotcfg> -- path to dot-config file to be checked\n");
		printf("<Kconfig> -- path to Kconfig file\n");
		printf("-v -- verbose\n");
		printf("-V -- print version\n");
		exit(1);
	}

	if (!strcmp(argv[1], "-V")) {
		printf("Version: %s\n", __GIT_VER__); /* see Makefile */
		exit(1);
	}

	if (!strcmp(argv[1], "-v")) {
		verbose++;
		argv++;
		argc--;
		goto me_lazy;
	}

	if (access(argv[1], R_OK) < 0) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], argv[1],
			strerror(errno));
		exit(1);
	}
	if (argc == 2) {
		err = libwr_cfg_read_file(argv[1]);
		if (err) {
			fprintf(stderr, "%s: Error in %s:%i: %s\n", argv[0],
				argv[1], -err, strerror(errno));
			exit(1);
		}
		if (verbose)
			libwr_cfg_dump(stdout);
		exit(0);
	}

	if (access(argv[2], R_OK) < 0) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], argv[2],
			strerror(errno));
		exit(1);
	}
	err = libwr_cfg_read_verify_file(argv[1], argv[2]);
	if (verbose)
		libwr_cfg_dump(stdout);
	if (err)
		exit(1); /* messages already printed */
	exit(0);

	return 0;
}
