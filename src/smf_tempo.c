/*
 * This is Standard MIDI File format implementation, tempo map related part.
 *
 * For questions and comments, contact Edward Tomasz Napierala <trasz@FreeBSD.org>.
 * This code is public domain, you can do with it whatever you want.
 */

#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include "smf.h"
#include "smf_private.h"

static double seconds_from_pulses(const smf_t *smf, int pulses);

/**
 * If there is tempo starting at "pulses" already, return it.  Otherwise,
 * allocate new one, fill it with values from previous one (or default ones,
 * if there is no previous one) and attach it to "smf".
 */
static smf_tempo_t *
new_tempo(smf_t *smf, int pulses)
{
	smf_tempo_t *tempo, *previous_tempo = NULL;

	if (smf->tempo_array->len > 0) {
		previous_tempo = smf_get_last_tempo(smf);

		/* If previous tempo starts at the same time as new one, reuse it, updating in place. */
		if (previous_tempo->time_pulses == pulses)
			return previous_tempo;
	}

	tempo = malloc(sizeof(smf_tempo_t));
	if (tempo == NULL) {
		g_critical("Cannot allocate smf_tempo_t.");
		return NULL;
	}

	tempo->time_pulses = pulses;

	if (previous_tempo != NULL) {
		tempo->microseconds_per_quarter_note = previous_tempo->microseconds_per_quarter_note;
		tempo->numerator = previous_tempo->numerator;
		tempo->denominator = previous_tempo->denominator;
		tempo->clocks_per_click = previous_tempo->clocks_per_click;
		tempo->notes_per_note = previous_tempo->notes_per_note;
	} else {
		tempo->microseconds_per_quarter_note = 500000; /* Initial tempo is 120 BPM. */
		tempo->numerator = 4;
		tempo->denominator = 4;
		tempo->clocks_per_click = -1;
		tempo->notes_per_note = -1;
	}

	g_ptr_array_add(smf->tempo_array, tempo);

	if (pulses == 0)
		tempo->time_seconds = 0.0;
	else
		tempo->time_seconds = seconds_from_pulses(smf, pulses);

	return tempo;
}

static int
add_tempo(smf_t *smf, int pulses, int tempo)
{
	smf_tempo_t *smf_tempo = new_tempo(smf, pulses);
	if (smf_tempo == NULL)
		return -1;

	smf_tempo->microseconds_per_quarter_note = tempo;

	return 0;
}

static int
add_time_signature(smf_t *smf, int pulses, int numerator, int denominator, int clocks_per_click, int notes_per_note)
{
	smf_tempo_t *smf_tempo = new_tempo(smf, pulses);
	if (smf_tempo == NULL)
		return -1;

	smf_tempo->numerator = numerator;
	smf_tempo->denominator = denominator;
	smf_tempo->clocks_per_click = clocks_per_click;
	smf_tempo->notes_per_note = notes_per_note;

	return 0;
}

void
maybe_add_to_tempo_map(smf_event_t *event)
{
	if (!smf_event_is_metadata(event))
		return;

	assert(event->track != NULL);
	assert(event->track->smf != NULL);
	assert(event->midi_buffer_length >= 1);

	/* Tempo Change? */
	if (event->midi_buffer[1] == 0x51) {
		int new_tempo = (event->midi_buffer[3] << 16) + (event->midi_buffer[4] << 8) + event->midi_buffer[5];
		if (new_tempo <= 0) {
			g_critical("Ignoring invalid tempo change.");
			return;
		}

		add_tempo(event->track->smf, event->time_pulses, new_tempo);
	}

	/* Time Signature? */
	if (event->midi_buffer[1] == 0x58) {
		int numerator, denominator, clocks_per_click, notes_per_note;

		if (event->midi_buffer_length < 7) {
			g_critical("Time Signature event seems truncated.");
			return;
		}

		numerator = event->midi_buffer[3];
		denominator = (int)pow(2, event->midi_buffer[4]);
		clocks_per_click = event->midi_buffer[5];
		notes_per_note = event->midi_buffer[6];

		add_time_signature(event->track->smf, event->time_pulses, numerator, denominator, clocks_per_click, notes_per_note);
	}

	return;
}

static double
seconds_from_pulses_old(const smf_t *smf, int pulses)
{
	double seconds = 0.0;
	smf_tempo_t *tempo;

	/* Go through tempos, from the last before pulses to the first, adding time. */
	for (;;) {
		tempo = smf_get_tempo_by_pulses(smf, pulses);
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

static double
seconds_from_pulses(const smf_t *smf, int pulses)
{
	double seconds;
	smf_tempo_t *tempo;

	tempo = smf_get_tempo_by_pulses(smf, pulses);
	assert(tempo);
	assert(tempo->time_pulses <= pulses);

	seconds = tempo->time_seconds + (double)(pulses - tempo->time_pulses) *
		(tempo->microseconds_per_quarter_note / ((double)smf->ppqn * 1000000.0));

	assert(fabs(seconds - seconds_from_pulses_old(smf, pulses)) < 0.00001);

	return seconds;
}

static int
pulses_from_seconds(const smf_t *smf, double seconds)
{
	int pulses = 0;
	smf_tempo_t *tempo;

	tempo = smf_get_tempo_by_seconds(smf, seconds);
	assert(tempo);
	assert(tempo->time_seconds <= seconds);

	pulses = tempo->time_pulses + (seconds - tempo->time_seconds) *
		((double)smf->ppqn * 1000000.0 / tempo->microseconds_per_quarter_note);

	return pulses;
}

/**
 * Computes value of event->time_seconds for all events in smf.
 * Warning: rewinds the smf.
 */
int
smf_create_tempo_map_and_compute_seconds(smf_t *smf)
{
	smf_event_t *event;

	smf_rewind(smf);
	smf_init_tempo(smf);

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

smf_tempo_t *
smf_get_tempo_by_number(const smf_t *smf, int number)
{
	assert(number >= 0);

	if (number >= smf->tempo_array->len)
		return NULL;

	return g_ptr_array_index(smf->tempo_array, number);
}

/**
 * Return last tempo (i.e. tempo with greatest time_pulses) that happens before "pulses".
 */
smf_tempo_t *
smf_get_tempo_by_pulses(const smf_t *smf, int pulses)
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

/**
 * Return last tempo (i.e. tempo with greatest time_seconds) that happens before "seconds".
 */
smf_tempo_t *
smf_get_tempo_by_seconds(const smf_t *smf, double seconds)
{
	int i;
	smf_tempo_t *tempo;

	assert(seconds >= 0.0);

	if (seconds == 0.0)
		return smf_get_tempo_by_number(smf, 0);

	assert(smf->tempo_array != NULL);
	
	for (i = smf->tempo_array->len - 1; i >= 0; i--) {
		tempo = smf_get_tempo_by_number(smf, i);

		assert(tempo);
		if (tempo->time_seconds < seconds)
			return tempo;
	}

	return NULL;
}


/**
 * Return last tempo.
 */
smf_tempo_t *
smf_get_last_tempo(const smf_t *smf)
{
	smf_tempo_t *tempo;

	tempo = smf_get_tempo_by_number(smf, smf->tempo_array->len - 1);
	assert(tempo);

	return tempo;
}

/**
 * Remove any existing tempos and add default one.
 */
int
smf_init_tempo(smf_t *smf)
{
	smf_tempo_t *tempo;

	while (smf->tempo_array->len > 0) {
		smf_tempo_t *tempo = g_ptr_array_index(smf->tempo_array, smf->tempo_array->len - 1);
		assert(tempo);
		free(tempo);
		g_ptr_array_remove_index(smf->tempo_array, smf->tempo_array->len - 1);
	}

	assert(smf->tempo_array->len == 0);

	tempo = new_tempo(smf, 0);
	if (tempo == NULL)
		return -1;

	return 0;
}

/**
 * Returns ->time_pulses of last event on the given track, or 0, if track is empty.
 */
static int
last_event_pulses(const smf_track_t *track)
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

/**
 * Adds event to the track at the time "pulses" clocks from the previous event in this track.
 * The remaining two time fields will be computed automatically based on the third argument
 * and current tempo map.  Note that ->delta_pulses is computed by smf.c:smf_track_add_event,
 * not here.
 */
void
smf_track_add_event_delta_pulses(smf_track_t *track, smf_event_t *event, int delta)
{
	assert(delta >= 0);
	assert(event->time_pulses == -1);
	assert(event->time_seconds == -1.0);
	assert(track->smf != NULL);

	smf_track_add_event_pulses(track, event, last_event_pulses(track) + delta);
}

/**
 * Adds event to the track at the time "pulses" clocks from the start of song.
 * The remaining two time fields will be computed automatically based on the third argument
 * and current tempo map.
 */
void
smf_track_add_event_pulses(smf_track_t *track, smf_event_t *event, int pulses)
{
	assert(pulses >= 0);
	assert(event->time_pulses == -1);
	assert(event->time_seconds == -1.0);
	assert(track->smf != NULL);

	event->time_pulses = pulses;
	event->time_seconds = seconds_from_pulses(track->smf, pulses);
	smf_track_add_event(track, event);
}

/**
 * Adds event to the track at the time "seconds" seconds from the start of song.
 * The remaining two time fields will be computed automatically based on the third argument
 * and current tempo map.
 */
void
smf_track_add_event_seconds(smf_track_t *track, smf_event_t *event, double seconds)
{
	assert(seconds >= 0.0);
	assert(event->time_pulses == -1);
	assert(event->time_seconds == -1.0);
	assert(track->smf != NULL);

	event->time_seconds = seconds;
	event->time_pulses = pulses_from_seconds(track->smf, seconds);
	smf_track_add_event(track, event);
}

