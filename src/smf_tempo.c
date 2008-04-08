#include <assert.h>
#include "smf.h"
#include "smf_private.h"

/* XXX: Add tempo map. */
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

	event->track->smf->microseconds_per_quarter_note = new_tempo;
	g_debug("Setting microseconds per quarter note: %d", event->track->smf->microseconds_per_quarter_note);

	return;
}

/* XXX: Add tempo map. */
static double
seconds_between_events(int previous_pulses, smf_event_t *event)
{
	int pulses;

	assert(event);

	pulses = event->time_pulses - previous_pulses;

	assert(pulses >= 0);

	return pulses * ((double)event->track->smf->microseconds_per_quarter_note / ((double)event->track->smf->ppqn * 1000000.0));
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


