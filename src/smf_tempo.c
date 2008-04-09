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
maybe_parse_metadata(smf_event_t *event)
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
	g_debug("Setting tempo (microseconds per quarter note) to %d.",
		smf_get_tempo_by_position(event->track->smf, event->time_pulses)->microseconds_per_quarter_note);

	return;
}

/* XXX: Add tempo map. */
static double
seconds_between_events(int previous_pulses, smf_event_t *event)
{
	int pulses, tempo;

	assert(event);

	pulses = event->time_pulses - previous_pulses;

	tempo = smf_get_tempo_by_position(event->track->smf, previous_pulses)->microseconds_per_quarter_note;

	assert(pulses >= 0);

	return pulses * ((double)tempo / ((double)event->track->smf->ppqn * 1000000.0));
}

/*
 * Computes value of event->time_seconds for all events in smf.
 * Warning: rewinds the smf.
 */
int
smf_compute_seconds(smf_t *smf)
{
	smf_event_t *event, *previous = NULL;

	smf_rewind(smf);

	for (;;) {
		event = smf_get_next_event(smf);
		
		if (event == NULL)
			return 0;

		maybe_parse_metadata(event);

		if (event->event_number == 1) {
			event->time_seconds = seconds_between_events(0, event);

		} else {
			previous = smf_get_event_by_number(event->track, event->event_number - 1);

			assert(previous);
			assert(previous->time_seconds >= 0);

			event->time_seconds = previous->time_seconds + seconds_between_events(previous->time_pulses, event);
		}
	}
}

/*
 * XXX: Sort entries by ->pulses.  Remove duplicates (two tempos starting at the same time,
 * at pulse 0, for example.
 */
int
smf_tempo_add(smf_t *smf, int pulses, int new_tempo)
{
	smf_tempo_t *tempo = malloc(sizeof(smf_tempo_t));
	if (tempo == NULL) {
		g_critical("Malloc failed.");
		return -1;
	}

	tempo->pulses = pulses;
	tempo->microseconds_per_quarter_note = new_tempo;

	g_ptr_array_add(smf->tempo_map, tempo);

	return 0;
}

smf_tempo_t *
smf_get_tempo_by_number(smf_t *smf, int number)
{
	assert(number >= 0);

	if (number >= smf->tempo_map->len)
		return NULL;

	return g_ptr_array_index(smf->tempo_map, number);
}

smf_tempo_t *
smf_get_tempo_by_position(smf_t *smf, int pulses)
{
	int i;
	smf_tempo_t *tempo;

	assert(pulses >= 0);

	assert(smf->tempo_map != NULL);
	
	for (i = smf->tempo_map->len - 1; i >= 0; i--) {
		tempo = smf_get_tempo_by_number(smf, i);

		assert(tempo);
		if (tempo->pulses <= pulses)
			return tempo;
	}

	return NULL;
}

