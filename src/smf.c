/*
 * This is Standard MIDI File format implementation.
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
 * Allocates new smf_t structure.
 */
smf_t *
smf_new(void)
{
	smf_t *smf = malloc(sizeof(smf_t));
	if (smf == NULL) {
		g_critical("Cannot allocate smf_t structure: %s", strerror(errno));
		return NULL;
	}

	memset(smf, 0, sizeof(smf_t));

	smf->tracks_array = g_ptr_array_new();
	assert(smf->tracks_array);

	smf->tempo_array = g_ptr_array_new();
	assert(smf->tempo_array);

	/* Initial tempo is 120 BPM. */
	smf_tempo_add(smf, 0, 500000);

	return smf;
}

/*
 * Frees smf and all it's descendant structures.
 */
void
smf_delete(smf_t *smf)
{
	/* Remove all the tracks, from last to first. */
	while (smf->tracks_array->len > 0)
		smf_track_delete(g_ptr_array_index(smf->tracks_array, smf->tracks_array->len - 1));

	assert(smf->tracks_array->len == 0);
	assert(smf->number_of_tracks == 0);
	g_ptr_array_free(smf->tracks_array, TRUE);
	g_ptr_array_free(smf->tempo_array, TRUE);

	memset(smf, 0, sizeof(smf_t));
	free(smf);
}

/*
 * Allocates new smf_track_t structure and attaches it to the given smf.
 */
smf_track_t *
smf_track_new(smf_t *smf)
{
	smf_track_t *track = malloc(sizeof(smf_track_t));
	if (track == NULL) {
		g_critical("Cannot allocate smf_track_t structure: %s", strerror(errno));
		return NULL;
	}

	memset(track, 0, sizeof(smf_track_t));

	track->smf = smf;
	g_ptr_array_add(smf->tracks_array, track);

	track->events_array = g_ptr_array_new();
	assert(track->events_array);

	smf->number_of_tracks++;
	track->track_number = smf->number_of_tracks;

	track->next_event_number = -1;

	return track;
}

/*
 * Detaches track from its smf and frees it.
 */
void
smf_track_delete(smf_track_t *track)
{
	int i, track_number;
	smf_t *smf;

	assert(track);
	assert(track->events_array);

	/* Remove all the events, from last to first. */
	while (track->events_array->len > 0)
		smf_event_delete(g_ptr_array_index(track->events_array, track->events_array->len - 1));

	assert(track->events_array->len == 0);
	assert(track->number_of_events == 0);
	g_ptr_array_free(track->events_array, TRUE);

	/* Detach itself from smf. */
	smf = track->smf;
	track_number = track->track_number;

	track->smf->number_of_tracks--;

	assert(track->smf->tracks_array);
	g_ptr_array_remove(track->smf->tracks_array, track);

	memset(track, 0, sizeof(smf_track_t));
	free(track);

	/* Renumber the rest of the tracks, so they are consecutively numbered. */
	for (i = track_number; i <= smf->number_of_tracks; i++) {
		track = smf_get_track_by_number(smf, i);
		track->track_number = i;
	}
}

/*
 * Allocates new smf_event_t structure and attaches it to the given track.
 */
/*
 * XXX: Handle adding entries in the middle of the track.
 */
smf_event_t *
smf_event_new(smf_track_t *track)
{
	smf_event_t *event = malloc(sizeof(smf_event_t));
	if (event == NULL) {
		g_critical("Cannot allocate smf_event_t structure: %s", strerror(errno));
		return NULL;
	}

	memset(event, 0, sizeof(smf_event_t));

	/* Add new event to its track. */
	event->track = track;
	event->track_number = track->track_number;
	g_ptr_array_add(track->events_array, event);
	event->track->number_of_events++;
	event->event_number = event->track->number_of_events;

	if (event->track->next_event_number == -1)
		event->track->next_event_number = 1;

	event->delta_time_pulses = -1;
	event->time_pulses = -1;
	event->time_seconds = -1;

	return event;
}

smf_event_t *
smf_event_new_with_data(smf_track_t *track, int first_byte, int second_byte, int third_byte)
{
	int len;

	smf_event_t *event;

	event = smf_event_new(track);
	if (event == NULL)
		return NULL;

	if (first_byte < 0) {
		g_critical("First byte of MIDI message cannot be < 0");
		smf_event_delete(event);

		return NULL;
	}

	if (second_byte < 0)
		len = 1;
	else if (third_byte < 0)
		len = 2;
	else
		len = 3;

	/* XXX: check if other bytes have proper values. */
	event->midi_buffer_length = len;
	event->midi_buffer = malloc(event->midi_buffer_length);
	if (event->midi_buffer == NULL) {
		g_critical("Cannot allocate MIDI buffer structure: %s", strerror(errno));
		smf_event_delete(event);

		return NULL; 
	}

	event->midi_buffer[0] = first_byte;
	if (len > 1)
		event->midi_buffer[1] = second_byte;
	if (len > 2)
		event->midi_buffer[2] = third_byte;

	return event;
}

/*
 * Detaches event from its track and frees it.
 */
/*
 * XXX: Handle removing entries in the middle of the track (recompute delta_time_pulses.).
 */
void
smf_event_delete(smf_event_t *event)
{
	int i, event_number;

	smf_track_t *track;

	assert(event->track != NULL);
	assert(event->track->events_array != NULL);

	track = event->track;
	event_number = event->event_number;

	event->track->number_of_events--;

	/* Remove event from its track. */
	g_ptr_array_remove(event->track->events_array, event);

	if (event->midi_buffer != NULL)
		free(event->midi_buffer);

	memset(event, 0, sizeof(smf_event_t));
	free(event);

	/* Renumber the rest of the events, so they are consecutively numbered. */
	for (i = event_number; i <= track->number_of_events; i++) {
		event = smf_get_event_by_number(track, i);
		event->event_number = i;
	}
}

int
smf_event_is_metadata(const smf_event_t *event)
{
	assert(event->midi_buffer);
	assert(event->midi_buffer_length > 0);
	
	if (event->midi_buffer[0] == 0xFF)
		return 1;

	return 0;
}

#define BUFFER_SIZE 1024

static char *
smf_event_decode_metadata(const smf_event_t *event)
{
	int off = 0;
	char *buf;

	assert(smf_event_is_metadata(event));

	buf = malloc(BUFFER_SIZE);
	if (buf == NULL) {
		g_critical("smf_event_decode_metadata: malloc failed.");
		return NULL;
	}

	/* XXX: smf_string_from_event() may return NULL. */
	switch (event->midi_buffer[1]) {
		case 0x00:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Sequence number");
			break;

		case 0x01:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Text: %s", smf_string_from_event(event));
			break;

		case 0x02:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Copyright: %s", smf_string_from_event(event));
			break;

		case 0x03:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Sequence/Track Name: %s", smf_string_from_event(event));
			break;

		case 0x04:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Instrument: %s", smf_string_from_event(event));
			break;

		case 0x05:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Lyric: %s", smf_string_from_event(event));
			break;

		case 0x06:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Marker: %s", smf_string_from_event(event));
			break;

		case 0x07:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Cue Point: %s", smf_string_from_event(event));
			break;

		case 0x08:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Program Name: %s", smf_string_from_event(event));
			break;

		case 0x09:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Device (Port) Name: %s", smf_string_from_event(event));
			break;

		/* http://music.columbia.edu/pipermail/music-dsp/2004-August/061196.html */
		case 0x20:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Channel Prefix: %d.", event->midi_buffer[3]);
			break;

		case 0x21:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Midi Port: %d.", event->midi_buffer[3]);
			break;

		case 0x2F:
			off += snprintf(buf + off, BUFFER_SIZE - off, "End Of Track");
			break;

		case 0x51:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Tempo: %d microseconds per quarter note",
				(event->midi_buffer[3] << 16) + (event->midi_buffer[4] << 8) + event->midi_buffer[5]);
			break;

		case 0x54:
			off += snprintf(buf + off, BUFFER_SIZE - off, "SMPTE Offset");
			break;

		case 0x58:
			off += snprintf(buf + off, BUFFER_SIZE - off,
				"Time Signature: %d/%d, %d clocks per click, %d notated 32nd notes per quarter note",
				event->midi_buffer[3], (int)pow(2, event->midi_buffer[4]), event->midi_buffer[5],
				event->midi_buffer[6]);
			break;

		case 0x59:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Key Signature");
			break;

		case 0x7F:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Proprietary (aka Sequencer) Event, length %d",
				event->midi_buffer_length);
			break;

		default:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Unknown Event: 0xFF 0x%x 0x%x 0x%x",
				event->midi_buffer[1], event->midi_buffer[2], event->midi_buffer[3]);

			break;
	}

	return buf;
}

char *
smf_event_decode(const smf_event_t *event)
{
	int off = 0;
	char *buf;

	if (smf_event_is_metadata(event))
		return smf_event_decode_metadata(event);

	buf = malloc(BUFFER_SIZE);
	if (buf == NULL) {
		g_critical("smf_event_decode: malloc failed.");
		return NULL;
	}

	/* XXX: verify lengths. */
	switch (event->midi_buffer[0] & 0xF0) {
		case 0x80:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Note Off, channel %d, note %d, velocity %d",
					event->midi_buffer[0] & 0x0F, event->midi_buffer[1], event->midi_buffer[2]);
			break;

		case 0x90:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Note On, channel %d, note %d, velocity %d",
					event->midi_buffer[0] & 0x0F, event->midi_buffer[1], event->midi_buffer[2]);
			break;

		case 0xA0:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Aftertouch, channel %d, note %d, pressure %d",
					event->midi_buffer[0] & 0x0F, event->midi_buffer[1], event->midi_buffer[2]);
			break;

		case 0xB0:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Controller, channel %d, controller %d, value %d",
					event->midi_buffer[0] & 0x0F, event->midi_buffer[1], event->midi_buffer[2]);
			break;

		case 0xC0:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Program Change, channel %d, controller %d",
					event->midi_buffer[0] & 0x0F, event->midi_buffer[1]);
			break;

		case 0xD0:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Channel Pressure, channel %d, pressure %d",
					event->midi_buffer[0] & 0x0F, event->midi_buffer[1]);
			break;

		case 0xE0:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Pitch Wheel, channel %d, value %d",
					event->midi_buffer[0] & 0x0F, ((int)event->midi_buffer[2] << 7) | (int)event->midi_buffer[2]);
			break;

		default:
			return NULL;
	}

	return buf;
}

smf_event_t *
smf_get_next_event_from_track(smf_track_t *track)
{
	smf_event_t *event, *next_event;

	/* End of track? */
	if (track->next_event_number == -1)
		return NULL;

	assert(track->next_event_number >= 1);
	assert(track->number_of_events > 0);

	event = smf_get_event_by_number(track, track->next_event_number);

	assert(event != NULL);

	/* Is this the last event in the track? */
	if (track->next_event_number < track->number_of_events) {
		next_event = smf_get_event_by_number(track, track->next_event_number + 1);
		assert(next_event);

		track->time_of_next_event = next_event->time_pulses;
		track->next_event_number++;
	} else {
		track->next_event_number = -1;
	}

	return event;
}

smf_event_t *
smf_peek_next_event_from_track(smf_track_t *track)
{
	smf_event_t *event;

	/* End of track? */
	if (track->next_event_number == -1)
		return NULL;

	assert(track->next_event_number >= 1);
	assert(track->events_array->len != 0);

	event = smf_get_event_by_number(track, track->next_event_number);

	return event;
}

smf_track_t *
smf_get_track_by_number(smf_t *smf, int track_number)
{
	smf_track_t *track;

	assert(track_number >= 1);
	assert(track_number <= smf->number_of_tracks);

	track = (smf_track_t *)g_ptr_array_index(smf->tracks_array, track_number - 1);

	assert(track);

	return track;
}

smf_event_t *
smf_get_event_by_number(smf_track_t *track, int event_number)
{
	smf_event_t *event;

	assert(event_number >= 1);
	assert(event_number <= track->number_of_events);

	event = g_ptr_array_index(track->events_array, event_number - 1);

	assert(event);

	return event;
}

smf_track_t *
smf_find_track_with_next_event(smf_t *smf)
{
	int i, min_time = 0;
	smf_track_t *track = NULL, *min_time_track = NULL;

	/* Find track with event that should be played next. */
	for (i = 1; i <= smf->number_of_tracks; i++) {
		track = smf_get_track_by_number(smf, i);

		assert(track);

		/* No more events in this track? */
		if (track->next_event_number == -1)
			continue;

		if (track->time_of_next_event < min_time || min_time_track == NULL) {
			min_time = track->time_of_next_event;
			min_time_track = track;
		}
	}

	return min_time_track;
}

smf_event_t *
smf_get_next_event(smf_t *smf)
{
	smf_event_t *event;
	smf_track_t *track = smf_find_track_with_next_event(smf);

	if (track == NULL) {
		g_debug("End of the song.");

		return NULL;
	}

	event = smf_get_next_event_from_track(track);
	
	assert(event != NULL);

	event->track->smf->last_seek_position = -1.0;

	return event;
}

smf_event_t *
smf_peek_next_event(smf_t *smf)
{
	smf_event_t *event;
	smf_track_t *track = smf_find_track_with_next_event(smf);

	if (track == NULL) {
		g_debug("End of the song.");

		return NULL;
	}

	event = smf_peek_next_event_from_track(track);
	
	assert(event != NULL);

	return event;
}

void
smf_rewind(smf_t *smf)
{
	int i;
	smf_track_t *track = NULL;
	smf_event_t *event;

	assert(smf);

	smf->last_seek_position = 0.0;

	for (i = 1; i <= smf->number_of_tracks; i++) {
		track = smf_get_track_by_number(smf, i);

		assert(track != NULL);

		if (track->number_of_events > 0) {
			track->next_event_number = 1;
			event = smf_peek_next_event_from_track(track);
			assert(event);
			track->time_of_next_event = event->time_pulses;
		} else {
			track->next_event_number = -1;
			track->time_of_next_event = 0;
			g_warning("Warning: empty track.");
		}
	}
}

int
smf_seek_to_event(smf_t *smf, const smf_event_t *target)
{
	smf_event_t *event;

	smf_rewind(smf);

	g_debug("Seeking to event %d, track %d.", target->event_number, target->track->track_number);

	for (;;) {
		event = smf_peek_next_event(smf);

		/* There can't be NULL here, unless "target" is not in this smf. */
		assert(event);

		if (event != target)
			smf_get_next_event(smf);
		else
			break;
	}	

	smf->last_seek_position = event->time_seconds;

	return 0;
}

int
smf_seek_to_seconds(smf_t *smf, double seconds)
{
	smf_event_t *event;
	double time;

	assert(seconds >= 0.0);

	if (seconds == smf->last_seek_position) {
		g_debug("Avoiding seek to %f seconds.", seconds);
		return 0;
	}

	smf_rewind(smf);

	g_debug("Seeking to %f seconds.", seconds);

	for (;;) {
		event = smf_peek_next_event(smf);

		if (event == NULL) {
			g_critical("Trying to seek past end of song.");
			return -1;
		}

		time = event->time_seconds;

		if (time < seconds)
			smf_get_next_event(smf);
		else
			break;
	}

	smf->last_seek_position = seconds;

	return 0;
}

