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

#define FILE_BUFFER_SIZE	1024*1024

static int
allocate_buffer(smf_t *smf)
{
	assert(smf->file_buffer == NULL);
	assert(smf->file_buffer_length == 0);

	smf->file_buffer_length = FILE_BUFFER_SIZE;
	smf->file_buffer = malloc(smf->file_buffer_length);

	if (smf->file_buffer == NULL) {
		g_critical("malloc(3) failed: %s", strerror(errno));

		return 5;
	}

	return 0;
}

static int
write_file_and_free_buffer(smf_t *smf, const char *file_name)
{
	/* XXX: write file. */

	free(smf->file_buffer);
	smf->file_buffer_length = 0;

	return 0;
}

static int
write_mthd_header(smf_t *smf)
{
	struct mthd_chunk_struct *mthd_chunk = smf->file_buffer;

	mthd_chunk->mthd_header.id[0] = 'M';
	mthd_chunk->mthd_header.id[1] = 'T';
	mthd_chunk->mthd_header.id[2] = 'h';
	mthd_chunk->mthd_header.id[3] = 'd';
	mthd_chunk->mthd_header.length = 6;
	mthd_chunk->format = htons(smf->format);
	mthd_chunk->number_of_tracks = htons(smf->number_of_tracks);
	mthd_chunk->division = htons(smf->ppqn);

	return 0;
}

int
smf_save(smf_t *smf, const char *file_name)
{
	if (allocate_buffer(smf))
		return -1;

	if (write_mthd_header(smf))
		return -2;

	if (write_file_and_free_buffer(smf, file_name))
		return -3;

	return 0;
}

