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

	smf->tracks_queue = g_queue_new();
	assert(smf->tracks_queue);

	return smf;
}

/*
 * Frees smf and all it's descendant structures.
 */
void
smf_free(smf_t *smf)
{
	smf_track_t *track;

	while ((track = (smf_track_t *)g_queue_pop_head(smf->tracks_queue)) != NULL)
		smf_track_free(track);

	assert(g_queue_is_empty(smf->tracks_queue));
	assert(smf->number_of_tracks == 0);
	g_queue_free(smf->tracks_queue);

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
	g_queue_push_tail(smf->tracks_queue, (gpointer)track);

	track->events_queue = g_queue_new();
	assert(track->events_queue);

	smf->number_of_tracks++;
	track->track_number = smf->number_of_tracks;

	track->next_event_number = -1;

	return track;
}

/*
 * Detaches track from its smf and frees it.
 */
void
smf_track_free(smf_track_t *track)
{
	int i;
	smf_event_t *event;
	smf_t *smf;

	assert(track);
	assert(track->events_queue);

	/* Remove all the events. */
	while ((event = (smf_event_t *)g_queue_pop_head(track->events_queue)) != NULL)
		smf_event_free(event);

	assert(g_queue_is_empty(track->events_queue));
	assert(track->number_of_events == 0);
	g_queue_free(track->events_queue);

	/* Detach itself from smf. */
	assert(track->smf);
	smf = track->smf;
	track->smf->number_of_tracks--;

	assert(track->smf->tracks_queue);
	g_queue_remove(track->smf->tracks_queue, (gpointer)track);

	memset(track, 0, sizeof(smf_track_t));
	free(track);

	/* Renumber the rest of the tracks, so they are consecutively numbered. */
	for (i = 1; i <= g_queue_get_length(smf->tracks_queue); i++) {
		track = smf_get_track_by_number(smf, i);
		track->track_number = i;
	}
}

/*
 * Allocates new smf_event_t structure and attaches it to the given track.
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
	g_queue_push_tail(track->events_queue, (gpointer)event);
	event->track->number_of_events++;

	if (event->track->next_event_number == -1)
		event->track->next_event_number = 1;

	return event;
}

/*
 * Detaches event from its track and frees it.
 */
void
smf_event_free(smf_event_t *event)
{
	assert(event->track != NULL);
	assert(event->track->events_queue != NULL);

	event->track->number_of_events--;

	/* Remove event from its track. */
	g_queue_remove(event->track->events_queue, (gpointer)event);

	if (event->midi_buffer != NULL)
		free(event->midi_buffer);

	memset(event, 0, sizeof(smf_event_t));
	free(event);
}

int
event_is_metadata(const smf_event_t *event)
{
	if (event->midi_buffer[0] == 0xFF)
		return 1;

	return 0;
}

static void
print_metadata_event(const smf_event_t *event)
{
	int off = 0;
	char buf[256];

	/* XXX: smf_string_from_event() may return NULL. */
	switch (event->midi_buffer[1]) {
		case 0x00:
			off += snprintf(buf + off, sizeof(buf) - off, "Sequence number");
			break;

		case 0x01:
			off += snprintf(buf + off, sizeof(buf) - off, "Text: %s", smf_string_from_event(event));
			break;

		case 0x02:
			off += snprintf(buf + off, sizeof(buf) - off, "Copyright: %s", smf_string_from_event(event));
			break;

		case 0x03:
			off += snprintf(buf + off, sizeof(buf) - off, "Sequence/Track Name: %s", smf_string_from_event(event));
			break;

		case 0x04:
			off += snprintf(buf + off, sizeof(buf) - off, "Instrument: %s", smf_string_from_event(event));
			break;

		case 0x05:
			off += snprintf(buf + off, sizeof(buf) - off, "Lyric: %s", smf_string_from_event(event));
			break;

		case 0x06:
			off += snprintf(buf + off, sizeof(buf) - off, "Marker: %s", smf_string_from_event(event));
			break;

		case 0x07:
			off += snprintf(buf + off, sizeof(buf) - off, "Cue Point: %s", smf_string_from_event(event));
			break;

		case 0x08:
			off += snprintf(buf + off, sizeof(buf) - off, "Program Name: %s", smf_string_from_event(event));
			break;

		case 0x09:
			off += snprintf(buf + off, sizeof(buf) - off, "Device (Port) Name: %s", smf_string_from_event(event));
			break;

		/* http://music.columbia.edu/pipermail/music-dsp/2004-August/061196.html */
		case 0x20:
			off += snprintf(buf + off, sizeof(buf) - off, "Channel Prefix: %d.", event->midi_buffer[3]);
			break;

		case 0x21:
			off += snprintf(buf + off, sizeof(buf) - off, "Midi Port: %d.", event->midi_buffer[3]);
			break;

		case 0x2F:
			off += snprintf(buf + off, sizeof(buf) - off, "End Of Track");
			break;

		case 0x51:
			off += snprintf(buf + off, sizeof(buf) - off, "Tempo: %d microseconds per quarter note",
				(event->midi_buffer[3] << 16) + (event->midi_buffer[4] << 8) + event->midi_buffer[5]);
			break;

		case 0x54:
			off += snprintf(buf + off, sizeof(buf) - off, "SMPTE Offset");
			break;

		case 0x58:
			off += snprintf(buf + off, sizeof(buf) - off,
				"Time Signature: %d/%d, %d clocks per click, %d notated 32nd notes per quarter note",
				event->midi_buffer[3], (int)pow(2, event->midi_buffer[4]), event->midi_buffer[5],
				event->midi_buffer[6]);
			break;

		case 0x59:
			off += snprintf(buf + off, sizeof(buf) - off, "Key Signature");
			break;

		case 0x7F:
			off += snprintf(buf + off, sizeof(buf) - off, "Proprietary Event");
			break;

		default:
			off += snprintf(buf + off, sizeof(buf) - off, "Unknown Event: 0xFF 0x%x 0x%x 0x%x",
				event->midi_buffer[1], event->midi_buffer[2], event->midi_buffer[3]);

			break;
	}

	g_debug("Metadata: %s", buf);
}

static void
parse_metadata_event(const smf_event_t *event)
{
	assert(event_is_metadata(event));

	print_metadata_event(event);

	/* "Tempo" metaevent. */
	if (event->midi_buffer[1] == 0x51) {
		assert(event->track != NULL);
		assert(event->track->smf != NULL);

		event->track->smf->microseconds_per_quarter_note = 
			(event->midi_buffer[3] << 16) + (event->midi_buffer[4] << 8) + event->midi_buffer[5];

		g_debug("Setting microseconds per quarter note: %d", event->track->smf->microseconds_per_quarter_note);

		return;
	}
}

smf_event_t *
smf_get_next_event_from_track(smf_track_t *track)
{
	smf_event_t *event, *next_event;

	/* End of track? */
	if (track->next_event_number == -1)
		return NULL;

	assert(track->next_event_number >= 1);

	assert(!g_queue_is_empty(track->events_queue));

	/* XXX: inefficient; use some different data structure. */
	event = (smf_event_t *)g_queue_peek_nth(track->events_queue, track->next_event_number - 1);
	next_event = (smf_event_t *)g_queue_peek_nth(track->events_queue, track->next_event_number);

	assert(event != next_event);
	assert(event != NULL);

	/* Is this the last event in the track? */
	if (next_event != NULL) {
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
	assert(!g_queue_is_empty(track->events_queue));

	event = smf_get_event_by_number(track, track->next_event_number);

	return event;
}

smf_track_t *
smf_get_track_by_number(smf_t *smf, int track_number)
{
	smf_track_t *track;

	assert(track_number >= 1);
	assert(track_number <= smf->number_of_tracks);

	track = (smf_track_t *)g_queue_peek_nth(smf->tracks_queue, track_number - 1);

	assert(track);

	return track;
}

smf_event_t *
smf_get_event_by_number(smf_track_t *track, int event_number)
{
	smf_event_t *event;

	assert(event_number >= 1);
	assert(event_number <= track->number_of_events);

	/* XXX: inefficient; use some different data structure. */
	event = (smf_event_t *)g_queue_peek_nth(track->events_queue, event_number - 1);

	assert(event);

	return event;
}

smf_track_t *
smf_find_track_with_next_event(smf_t *smf)
{
	int i, min_time = 0;
	smf_track_t *track = NULL, *min_time_track = NULL;

	/* Find track with event that should be played next. */
	for (i = 1; i <= g_queue_get_length(smf->tracks_queue); i++) {
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

void
smf_compute_time(smf_event_t *event)
{
	assert(event);
	assert(event->track);
	assert(event->track->smf);

	if (event->track->smf->ppqn == 0)
		event->time_seconds = 0.0;

	event->time_seconds = event->time_pulses *
		((double)event->track->smf->microseconds_per_quarter_note / ((double)event->track->smf->ppqn * 1000000.0));
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

	if (event_is_metadata(event)) {
		parse_metadata_event(event);

		return smf_get_next_event(smf);
	}

	smf_compute_time(event);

#if 0
	fprintf(stderr, "smf_get_next_event: next event time = %f;\n", event->time_seconds);
#endif

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

	/* XXX: we cannot do the metadata trick described above. */

	smf_compute_time(event);

	return event;
}

void
smf_rewind(smf_t *smf)
{
	int i;
	smf_track_t *track = NULL;
	smf_event_t *event;

	assert(smf);

	g_debug("Rewinding.");

	smf->last_seek_position = 0.0;

	for (i = 1; i <= g_queue_get_length(smf->tracks_queue); i++) {
		track = smf_get_track_by_number(smf, i);

		assert(track != NULL);

		track->next_event_number = 1;

		event = smf_peek_next_event_from_track(track);
		if (event) {
			track->time_of_next_event = event->time_pulses;
		} else {
			g_warning("Warning: empty track.");
			track->time_of_next_event = 0;
		}
	}
}

int
smf_seek_to(smf_t *smf, double seconds)
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

int
smf_get_number_of_tracks(smf_t *smf)
{
	return smf->number_of_tracks;
}

