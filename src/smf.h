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

	GQueue		*tracks_queue;
};

typedef struct smf_struct smf_t;

struct smf_track_struct {
	smf_t		*smf;

	void		*buffer;
	int		buffer_length;

	int		next_event_offset;
	int		last_status; /* Used for "running status". */

	GQueue		*events_queue;
	int		time_of_next_event; /* Absolute time of next event on events_queue. */
};

typedef struct smf_track_struct smf_track_t;

struct smf_event_struct {
	smf_track_t	*track;

	int		time;
	unsigned char	midi_buffer[1024];
};

typedef struct smf_event_struct smf_event_t;

smf_t *smf_load(const char *file_name);
smf_event_t *smf_get_next_event(smf_t *smf);
void smf_rewind(smf_t *smf);

#endif /* SMF_H */

