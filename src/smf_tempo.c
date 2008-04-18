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
	g_debug("Setting tempo (microseconds per quarter note) to %d.",
		smf_get_tempo_by_position(event->track->smf, event->time_pulses)->microseconds_per_quarter_note);

	return;
}

static double
seconds_from_pulses(smf_t *smf, int pulses)
{
	double seconds = 0.0;
	smf_tempo_t *tempo;

	for (;;) {
		tempo = smf_get_tempo_by_position(smf, pulses);
		assert(tempo);
		assert(tempo->time_pulses <= pulses);

		seconds += (double)(pulses - tempo->time_pulses) * (tempo->microseconds_per_quarter_note / ((double)smf->ppqn * 1000000.0));

		if (tempo->time_pulses == 0)
			return seconds;

		/* XXX: -1? */
		pulses = tempo->time_pulses - 1;
	}

	/* Not reached. */
	return -1;
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
 * XXX: Sort entries by ->time_pulses.  Remove duplicates (two tempos starting at the same time,
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

	tempo->time_pulses = pulses;
#if 0
	tempo->time_seconds = seconds_between_events(smf, 0, pulses);
#endif
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

smf_tempo_t *
smf_get_tempo_by_position(smf_t *smf, int pulses)
{
	int i;
	smf_tempo_t *tempo;

	assert(pulses >= 0);

	assert(smf->tempo_array != NULL);
	
	for (i = smf->tempo_array->len - 1; i >= 0; i--) {
		tempo = smf_get_tempo_by_number(smf, i);

		assert(tempo);
		if (tempo->time_pulses <= pulses)
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

