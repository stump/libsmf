/*
 * This is Standard MIDI File writer.
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
#include "smf_private.h"

/*
 * Extends (reallocates) smf->file_buffer and returns pointer to the newly added space,
 * that is, pointer to the first byte after the previous buffer end.  Returns NULL in case
 * of error.
 */
static void *
smf_extend(smf_t *smf, const int length)
{
	int i, previous_file_buffer_length = smf->file_buffer_length;
	char *previous_file_buffer = smf->file_buffer;

	/* XXX: Not terribly efficient. */
	smf->file_buffer_length += length;
	smf->file_buffer = realloc(smf->file_buffer, smf->file_buffer_length);
	if (smf->file_buffer == NULL) {
		g_critical("realloc(3) failed: %s", strerror(errno));
		smf->file_buffer_length = 0;
		return NULL;
	}

	/* Fix up pointers.  XXX: omgwtf. */
	for (i = 1; i <= smf->number_of_tracks; i++) {
		smf_track_t *track;
		track = smf_get_track_by_number(smf, i);
		if (track->file_buffer != NULL)
			track->file_buffer = (char *)track->file_buffer + ((char *)smf->file_buffer - previous_file_buffer);
	}

	return (char *)smf->file_buffer + previous_file_buffer_length;
}

/*
 * Appends "buffer_length" bytes pointed to by "buffer" to the smf, reallocating storage as needed.  Returns 0
 * if everything went ok, different value if there was any problem.
 */
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

/*
 * Appends MThd header to the track.  Returns 0 if everything went ok, different value if not.
 */
static int
write_mthd_header(smf_t *smf)
{
	struct mthd_chunk_struct mthd_chunk;

	memcpy(mthd_chunk.mthd_header.id, "MThd", 4);
	mthd_chunk.mthd_header.length = htonl(6);
	mthd_chunk.format = htons(smf->format);
	mthd_chunk.number_of_tracks = htons(smf->number_of_tracks);
	mthd_chunk.division = htons(smf->ppqn);

	return smf_append(smf, &mthd_chunk, sizeof(mthd_chunk));
}

/*
 * Extends (reallocates) track->file_buffer and returns pointer to the newly added space,
 * that is, pointer to the first byte after the previous buffer end.  Returns NULL in case
 * of error.
 */
static void *
track_extend(smf_track_t *track, const int length)
{
	void *buf;

	assert(track->smf);

	buf = smf_extend(track->smf, length);
	if (buf == NULL)
		return NULL;

	track->file_buffer_length += length;
	if (track->file_buffer == NULL)
		track->file_buffer = buf;

	return buf;
}

/*
 * Appends "buffer_length" bytes pointed to by "buffer" to the track, reallocating storage as needed.  Returns 0
 * if everything went ok, different value if there was any problem.
 */
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

/*
 * Appends event time as Variable Length Quantity.  Returns 0 if everything went 0,
 * different value in case of error.
 */
static int
write_event_time(smf_event_t *event)
{
	unsigned long value = event->delta_time_pulses, buffer;
	int ret;

	assert(event->delta_time_pulses >= 0);

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

	return 0;
}

/*
 * Appends contents of event->midi_buffer.  Returns 0 if everything went 0,
 * different value in case of error.
 */
static int
write_event_midi_buffer(smf_event_t *event)
{
	return track_append(event->track, event->midi_buffer, event->midi_buffer_length);
}

/*
 * Writes out an event.
 */
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

/*
 * Writes out MTrk header, except of MTrk chunk length, which is written by write_mtrk_length().
 */
static int
write_mtrk_header(smf_track_t *track)
{
	struct chunk_header_struct mtrk_header;

	memcpy(mtrk_header.id, "MTrk", 4);

	return track_append(track, &mtrk_header, sizeof(mtrk_header));
}

/*
 * Updates MTrk chunk length of a given track.
 */
static int
write_mtrk_length(smf_track_t *track)
{
	struct chunk_header_struct *mtrk_header;

	assert(track->file_buffer != NULL);
	assert(track->file_buffer_length >= 6);

	mtrk_header = (struct chunk_header_struct *)track->file_buffer;
	mtrk_header->length = htonl(track->file_buffer_length - sizeof(struct chunk_header_struct));

	return 0;
}

/*
 * Writes out the track.
 */
static int
write_track(smf_track_t *track)
{
	int ret;
	smf_event_t *event;

	ret = write_mtrk_header(track);
	if (ret)
		return ret;

	while ((event = smf_get_next_event_from_track(track)) != NULL) {
		ret = write_event(event);
		if (ret)
			return ret;
	}

	ret = write_mtrk_length(track);
	if (ret)
		return ret;

	return 0;
}

/*
 * Takes smf->file_buffer and saves it to the file.  Frees the buffer afterwards.
 */
static int
write_file_and_free_buffer(smf_t *smf, const char *file_name)
{
	int i;
	FILE *stream;
	smf_track_t *track;

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

	/* Clear the pointers. */
	free(smf->file_buffer);
	smf->file_buffer = NULL;
	smf->file_buffer_length = 0;

	for (i = 1; i <= smf->number_of_tracks; i++) {
		track = smf_get_track_by_number(smf, i);
		assert(track);
		track->file_buffer = NULL;
		track->file_buffer_length = 0;
	}

	return 0;
}

/*
 * Returns 1 if all pointers supposed to be NULL are NULL.  Triggers assertion if not.
 */
static int
pointers_are_clear(smf_t *smf)
{
	int i;

	smf_track_t *track;
	assert(smf->file_buffer == NULL);
	assert(smf->file_buffer_length == 0);

	for (i = 1; i <= smf->number_of_tracks; i++) {
		track = smf_get_track_by_number(smf, i);

		assert(track != NULL);
		assert(track->file_buffer == NULL);
		assert(track->file_buffer_length == 0);
	}

	return 1;
}

/*
 * Returns 1 if event is End Of Track event, 0 otherwise.
 */
int
smf_event_is_eot(smf_event_t *event)
{
	if (event->midi_buffer_length != 3)
		return 0;

	if (event->midi_buffer[0] != 0xFF || event->midi_buffer[1] != 0x2F || event->midi_buffer[2] != 0x00)
		return 0;

	return 1;
}

/*
 * Verifies if smf is consistent and returns 0 iff it is.
 */
int
smf_is_invalid(smf_t *smf)
{
	int i;
	smf_track_t *track;
	smf_event_t *event;

	if (smf->format < 0 || smf->format > 2) {
		g_critical("SMF error: smf->format is less than zero of greater than two.");
		return -1;
	}

	if (smf->number_of_tracks < 1) {
		g_critical("SMF error: number of tracks is less than one.");
		return -2;
	}

	if (smf->format == 0 && smf->number_of_tracks > 1) {
		g_critical("SMF error: format is 0, but number of tracks is more than one.");
		return -3;
	}

	if (smf->ppqn <= 0) {
		g_critical("SMF error: PPQN has to be > 0.");
		return -4;
	}

	for (i = 1; i <= smf->number_of_tracks; i++) {
		track = smf_get_track_by_number(smf, i);
		assert(track);

		if (track->number_of_events < 1) {
			g_critical("SMF error: track #%x is empty.", track->track_number);
			return -5;
		}

		event = smf_get_event_by_number(track, track->number_of_events);
		assert(event);

		if (!smf_event_is_eot(event)) {
			g_critical("SMF error: track #%x does not end with End Of Track event.", track->track_number);
			return -6;
		}
	}

	return 0;
}

int
smf_save(smf_t *smf, const char *file_name)
{
	int i, ret;
	smf_track_t *track;

	smf_rewind(smf);

	assert(pointers_are_clear(smf));

	if (smf_is_invalid(smf))
		return -1;

	if (write_mthd_header(smf))
		return -2;

	for (i = 1; i <= smf->number_of_tracks; i++) {
		track = smf_get_track_by_number(smf, i);

		assert(track != NULL);

		ret = write_track(track);
		if (ret)
			return ret;
	}

	if (write_file_and_free_buffer(smf, file_name))
		return -3;

	return 0;
}

