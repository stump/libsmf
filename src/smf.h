#ifndef SMF_H
#define SMF_H

#include <stdio.h>
#include <gdk/gdk.h>

struct smf_struct {
	FILE		*stream;
	void		*buffer;
	int		buffer_length;

	int		next_chunk_offset;

	int		format;
	int		number_of_tracks;

	/* These fields are extracted from "division" field of MThd header.  Valid is _either_ ppqn or frames_per_second/resolution. */
	int		ppqn;
	int		frames_per_second;
	int		resolution;
	int		microseconds_per_quarter_note;

	GQueue		*tracks_queue;
	int		last_track_number;
};

typedef struct smf_struct smf_t;

struct smf_track_struct {
	smf_t		*smf;

	void		*buffer;
	int		buffer_length;

	int		track_number;

	int		next_event_offset;
	int		last_status; /* Used for "running status". */

	GQueue		*events_queue;
	int		time_of_next_event; /* Absolute time of next event on events_queue. */
};

typedef struct smf_track_struct smf_track_t;

#define MAX_EVENT_LENGTH 1024

struct smf_event_struct {
	smf_track_t	*track;

	int		time;
	int		track_number;
	unsigned char	midi_buffer[MAX_EVENT_LENGTH];
};

typedef struct smf_event_struct smf_event_t;

smf_t *smf_load(const char *file_name);
int smf_get_number_of_tracks(smf_t *smf);
smf_event_t *smf_get_next_event(smf_t *smf);
void smf_rewind(smf_t *smf);
double smf_milliseconds_per_time_unit(smf_t *smf);

#endif /* SMF_H */

