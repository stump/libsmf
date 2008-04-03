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
extend_buffer(smf_t *smf)
{
	assert(smf->file_buffer == NULL);
	assert(smf->file_buffer_length > 0);

	smf->file_buffer_length *= 2;
	smf->file_buffer = realloc(smf->file_buffer, smf->file_buffer_length);
	if (smf->file_buffer == NULL) {
		g_critical("realloc(3) failed.");
		smf->file_buffer_length = 0;
		return -1;
	}

	return 0;
}

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
	FILE *stream;

	stream = fopen(file_name, "w+");
	if (stream == NULL) {
		g_critical("Cannot open input file: %s", strerror(errno));

		return -1;
	}

	if (fwrite(smf->file_buffer, 1, smf->file_buffer_length, stream) != smf->file_buffer_length) {
		g_critical("fwrite(3) failed: %s", strerror(errno));

		return -2;
	}

	if (fclose(stream)) {
		g_critical("fclose(3) failed: %s", strerror(errno));

		return -3;
	}

	free(smf->file_buffer);
	smf->file_buffer_length = 0;

	return 0;
}

static int
write_mthd_header(smf_t *smf)
{
	struct mthd_chunk_struct *mthd_chunk;

	assert(smf->file_buffer != NULL);
	assert(smf->file_buffer_length >= 6);

	mthd_chunk = smf->file_buffer;

	memcpy(mthd_chunk->mthd_header.id, "MThd", 4);
	mthd_chunk->mthd_header.length = 6;
	mthd_chunk->format = htons(smf->format);
	mthd_chunk->number_of_tracks = htons(smf->number_of_tracks);
	mthd_chunk->division = htons(smf->ppqn);

	return 0;
}

static int
write_mtrd_header(smf_track_t *track)
{
	struct chunk_header_struct *mtrd_chunk;

	assert(track->file_buffer != NULL);
	assert(track->file_buffer_length >= 6);

	mtrd_chunk = track->file_buffer;
	memcpy(mtrd_chunk->id, "MTrd", 4);

	/* XXX: where is the chunk length saved? */

	return 0;
}

static int
write_mtrd_contents(smf_track_t *track)
{
	return 0;
}

static int
write_mtrd_length(smf_track_t *track)
{
	return 0;
}

int
smf_save(smf_t *smf, const char *file_name)
{
	int i;
	smf_track_t *track;

	if (allocate_buffer(smf))
		return -1;

	if (write_mthd_header(smf))
		return -2;

	for (i = 0; i < smf->number_of_tracks; i++) {
		track = (smf_track_t *)g_queue_peek_nth(smf->tracks_queue, i);

		write_mtrd_header(track);
		write_mtrd_contents(track);
		write_mtrd_length(track);
	}

	if (write_file_and_free_buffer(smf, file_name))
		return -3;

	return 0;
}

