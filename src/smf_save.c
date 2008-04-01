/*
 * This is Standard MIDI File loader.
 *
 * For questions and comments, contact Edward Tomasz Napierala <trasz@FreeBSD.org>.
 * This code is public domain, you can do with it whatever you want.
 */

/* Reference: http://www.borg.com/~jglatt/tech/midifile.htm */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <errno.h>
#include <arpa/inet.h>
#include "smf.h"

int
create_file_and_allocate_buffer(smf_t *smf, const char *file_name)
{
	return 0;
}

int
smf_save(smf_t *smf, const char *file_name)
{
	int ret;

	ret = create_file_and_allocate_buffer(smf, file_name);
	if (ret)
		return ret;

	return 0;
}

