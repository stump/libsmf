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

#define INITIAL_FILE_BUFFER_SIZE	1024*1024

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

	smf->file_buffer_length = INITIAL_FILE_BUFFER_SIZE;
	smf->file_buffer = malloc(smf->file_buffer_length);

	if (smf->file_buffer == NULL) {
		g_critical("malloc(3) failed: %s", strerror(errno));

		return 5;
	}

	return 0;
}

static void *
smf_extend(smf_t *smf, const int length)
{
	/* XXX: implement. */

	return NULL;
}

static int
smf_append(smf_t *smf, const void *buffer, const int buffer_length)
{
	void *dest;

	dest = smf_extend(smf, buffer_length);
	if (dest == NULL) {
		g_critical("Cannot extend track buffer.");
		return -1;
	}

	memcpy(dest, buffer, buffer_length);

	return 0;
}

static int
write_mthd_header(smf_t *smf)
{
	struct mthd_chunk_struct mthd_chunk;

	memcpy(mthd_chunk.mthd_header.id, "MThd", 4);
	mthd_chunk.mthd_header.length = 6;
	mthd_chunk.format = htons(smf->format);
	mthd_chunk.number_of_tracks = htons(smf->number_of_tracks);
	mthd_chunk.division = htons(smf->ppqn);

	return smf_append(smf, &mthd_chunk, sizeof(mthd_chunk));
}

static void *
track_extend(smf_track_t *track, const int length)
{
	/* XXX: implement. */
	return NULL;
}

static int
track_append(smf_track_t *track, const void *buffer, const int buffer_length)
{
	void *dest;

	dest = track_extend(track, buffer_length);
	if (dest == NULL) {
		g_critical("Cannot extend track buffer.");
		return -1;
	}

	memcpy(dest, buffer, buffer_length);

	return 0;
}

static int
write_event_time(smf_event_t *event)
{
	unsigned long value = event->time_pulses, buffer;
	int ret;

	/* Taken from http://www.borg.com/~jglatt/tech/midifile/vari.htm */
	buffer = value & 0x7F;

	while ((value >>= 7)) {
		buffer <<= 8;
		buffer |= ((value & 0x7F) | 0x80);
	}

	while (TRUE) {
		ret = track_append(event->track, &buffer, 1);
		if (ret)
			return ret;

		if (buffer & 0x80)
			buffer >>= 8;
		else
			break;
	}

	/* XXX: verify. */

	return 0;
}

static int
write_event_midi_buffer(smf_event_t *event)
{
	return track_append(event->track, event->midi_buffer, event->midi_buffer_length);
}

static int
write_event(smf_event_t *event)
{
	int ret;

	ret = write_event_time(event);
	if (ret)
		return ret;

	ret = write_event_midi_buffer(event);
	if (ret)
		return ret;

	return 0;
}

static int
write_mtrd_header(smf_track_t *track)
{
	struct chunk_header_struct mtrd_header;

	memcpy(mtrd_header.id, "MTrd", 4);

	return track_append(track, &mtrd_header, sizeof(mtrd_header));
}

static int
write_mtrd_length(smf_track_t *track)
{
	/* XXX: implement. */

	return 0;
}

static int
write_track(smf_track_t *track)
{
	int ret;
	smf_event_t *event;

	ret = write_mtrd_header(track);
	if (ret)
		return ret;

	while ((event = smf_get_next_event_from_track(track)) != NULL) {
		ret = write_event(event);
		if (ret)
			return ret;
	}

	ret = write_mtrd_length(track);
	if (ret)
		return ret;

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

int
smf_save(smf_t *smf, const char *file_name)
{
	int i, ret;
	smf_track_t *track;

	if (allocate_buffer(smf))
		return -1;

	/* XXX: setup pointers. */

	smf_rewind(smf);

	if (write_mthd_header(smf))
		return -2;

	for (i = 0; i < smf->number_of_tracks; i++) {
		track = (smf_track_t *)g_queue_peek_nth(smf->tracks_queue, i);

		assert(track != NULL);

		ret = write_track(track);
		if (ret)
			return ret;
	}

	if (write_file_and_free_buffer(smf, file_name))
		return -3;

	return 0;
}

