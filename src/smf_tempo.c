/*
 * This is Standard MIDI File format implementation, tempo map related part.
 *
 * For questions and comments, contact Edward Tomasz Napierala <trasz@FreeBSD.org>.
 * This code is public domain, you can do with it whatever you want.
 */

#include <stdlib.h>
#include <assert.h>
#include "smf.h"
#include "smf_private.h"

static void
maybe_add_to_tempo_map(smf_event_t *event)
{
	int new_tempo;

	if (!smf_event_is_metadata(event))
		return;

	/* Not a Tempo Change? */
	if (event->midi_buffer[1] != 0x51)
		return;

	assert(event->track != NULL);
	assert(event->track->smf != NULL);

	new_tempo = (event->midi_buffer[3] << 16) + (event->midi_buffer[4] << 8) + event->midi_buffer[5];
	if (new_tempo <= 0) {
		g_critical("Ignoring invalid tempo change.");
		return;
	}

	smf_tempo_add(event->track->smf, event->time_pulses, new_tempo);
#if 0
	g_debug("Setting tempo (microseconds per quarter note) to %d.",
		smf_get_tempo_by_position(event->track->smf, event->time_pulses)->microseconds_per_quarter_note);
#endif

	return;
}

static double
seconds_from_pulses(smf_t *smf, int pulses)
{
	double seconds = 0.0;
	smf_tempo_t *tempo;

	/* Go through tempos, from the last before pulses to the first, adding time. */
	for (;;) {
		tempo = smf_get_tempo_by_position(smf, pulses);
		assert(tempo);
		assert(tempo->time_pulses <= pulses);

		seconds += (double)(pulses - tempo->time_pulses) * (tempo->microseconds_per_quarter_note / ((double)smf->ppqn * 1000000.0));

		if (tempo->time_pulses == 0)
			return seconds;

		pulses = tempo->time_pulses;
	}

	/* Not reached. */
	return -1;
}

static int
pulses_from_seconds(smf_t *smf, double seconds)
{
	int pulses = 0;
	smf_tempo_t *tempo;

	tempo = smf_get_last_tempo(smf);

	/* XXX: Obviously unfinished. */
	pulses += seconds * ((double)smf->ppqn * 1000000.0 / tempo->microseconds_per_quarter_note);

	return pulses;
}

/*
 * Computes value of event->time_seconds for all events in smf.
 * Warning: rewinds the smf.
 */
int
smf_compute_seconds(smf_t *smf)
{
	smf_event_t *event;

	smf_rewind(smf);
	smf_remove_tempos(smf);

	for (;;) {
		event = smf_get_next_event(smf);
		
		if (event == NULL)
			return 0;

		maybe_add_to_tempo_map(event);

		event->time_seconds = seconds_from_pulses(smf, event->time_pulses);
	}

	/* Not reached. */
	return -1;
}

/*
 * XXX: Sort entries by ->time_pulses.
 */
int
smf_tempo_add(smf_t *smf, int pulses, int new_tempo)
{
	smf_tempo_t *tempo;

	if (smf->tempo_array->len > 0) {
		tempo = smf_get_last_tempo(smf);

		/* If previous tempo starts at the same time as new one, reuse it, updating in place. */
		if (tempo->time_pulses == pulses) {
			tempo->microseconds_per_quarter_note = new_tempo;
			return 0;
		}
	}

	tempo = malloc(sizeof(smf_tempo_t));
	if (tempo == NULL) {
		g_critical("Malloc failed.");
		return -1;
	}

	tempo->time_pulses = pulses;
	tempo->microseconds_per_quarter_note = new_tempo;

	g_ptr_array_add(smf->tempo_array, tempo);

	return 0;
}

smf_tempo_t *
smf_get_tempo_by_number(smf_t *smf, int number)
{
	assert(number >= 0);

	if (number >= smf->tempo_array->len)
		return NULL;

	return g_ptr_array_index(smf->tempo_array, number);
}

/*
 * Remove last tempo (i.e. tempo with greatest time_pulses) that happens before "pulses".
 */
smf_tempo_t *
smf_get_tempo_by_position(smf_t *smf, int pulses)
{
	int i;
	smf_tempo_t *tempo;

	assert(pulses >= 0);

	if (pulses == 0)
		return smf_get_tempo_by_number(smf, 0);

	assert(smf->tempo_array != NULL);
	
	for (i = smf->tempo_array->len - 1; i >= 0; i--) {
		tempo = smf_get_tempo_by_number(smf, i);

		assert(tempo);
		if (tempo->time_pulses < pulses)
			return tempo;
	}

	return NULL;
}

smf_tempo_t *
smf_get_last_tempo(smf_t *smf)
{
	smf_tempo_t *tempo;

	tempo = smf_get_tempo_by_number(smf, smf->tempo_array->len - 1);
	assert(tempo);

	return tempo;
}

void
smf_remove_tempos(smf_t *smf)
{
	while (smf->tempo_array->len > 0) {
		smf_tempo_t *tempo = g_ptr_array_index(smf->tempo_array, smf->tempo_array->len - 1);
		assert(tempo);
		free(tempo);
		g_ptr_array_remove_index(smf->tempo_array, smf->tempo_array->len - 1);
	}

	assert(smf->tempo_array->len == 0);
	smf_tempo_add(smf, 0, 500000);
}

/*
 * Returns ->time_pulses of last event on the given track, or 0, if track is empty.
 */
static int
last_event_pulses(smf_track_t *track)
{
	/* Get time of last event on this track. */
	if (track->number_of_events > 0) {
		smf_event_t *previous_event = smf_track_get_last_event(track);
		assert(previous_event);
		assert(previous_event->time_pulses >= 0);

		return previous_event->time_pulses;
	}

	return 0;
}


/*
 * Appends event to the track at the time "pulses" clocks from the previous event in this track.
 */
void
smf_track_append_event_delta_pulses(smf_track_t *track, smf_event_t *event, int pulses)
{
	int previous_time_pulses;

	assert(pulses >= 0);
	assert(event->delta_time_pulses == -1);
	assert(event->time_pulses == -1);
	assert(event->time_seconds == -1.0);

	previous_time_pulses = last_event_pulses(track);

	event->delta_time_pulses = pulses;
	event->time_pulses = previous_time_pulses + pulses;
	event->time_seconds = seconds_from_pulses(track->smf, pulses);
	smf_track_append_event(track, event);
}

/*
 * Appends event to the track at the time "pulses" clocks from the start of song.
 */
void
smf_track_append_event_pulses(smf_track_t *track, smf_event_t *event, int pulses)
{
	int previous_time_pulses;

	assert(pulses >= 0);
	assert(event->delta_time_pulses == -1);
	assert(event->time_pulses == -1);
	assert(event->time_seconds == -1.0);

	previous_time_pulses = last_event_pulses(track);

	event->time_pulses = pulses;
	event->delta_time_pulses = event->time_pulses - previous_time_pulses;
	event->time_seconds = seconds_from_pulses(track->smf, pulses);
	smf_track_append_event(track, event);
}

/*
 * Appends event to the track at the time "seconds" seconds from the start of song.
 */
void
smf_track_append_event_seconds(smf_track_t *track, smf_event_t *event, double seconds)
{
	int previous_time_pulses;

	assert(seconds >= 0.0);
	assert(event->delta_time_pulses == -1);
	assert(event->time_pulses == -1);
	assert(event->time_seconds == -1.0);
	assert(track->smf != NULL);

	previous_time_pulses = last_event_pulses(track);

	event->time_seconds = seconds;
	event->time_pulses = pulses_from_seconds(track->smf, seconds);
	event->delta_time_pulses = event->time_pulses - previous_time_pulses;
	smf_track_append_event(track, event);
}


