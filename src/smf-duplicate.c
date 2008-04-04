/*
 * This is a simple program to read in (parse) contents of SMF file
 * and write it out.  Contents of the output file should be exactly
 * the same as the input.
 */

#include <stdlib.h>
#include <sysexits.h>
#include "smf.h"

void
usage(void)
{
	fprintf(stderr, "usage: smf-duplicate source_file target_file\n");

	exit(EX_USAGE);
}

int main(int argc, char *argv[])
{
	int ret;
	smf_t *smf;

	if (argc != 3) {
		usage();
	}

	smf = smf_load(argv[1]);
	if (smf == NULL) {
		g_critical("Cannot load SMF file.");
		return -1;
	}

	ret = smf_save(smf, argv[2]);
	if (ret) {
		g_critical("Cannot save SMF file.");
		return -2;
	}

	return 0;
}

