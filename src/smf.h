#ifndef SMF_H
#define SMF_H

#include <stdio.h>
#include <arpa/inet.h>
#include <glib.h>

struct smf_struct {
	int		format;
	int		expected_number_of_tracks;

	/* These fields are extracted from "division" field of MThd header.  Valid is _either_ ppqn or frames_per_second/resolution. */
	int		ppqn;
	int		frames_per_second;
	int		resolution;
	int		microseconds_per_quarter_note;
	int		number_of_tracks;

	/* These are private fields using only by loading and saving routines. */
	FILE		*stream;
	void		*file_buffer;
	int		file_buffer_length;
	int		next_chunk_offset;

	/* Private, used by smf.c. */
	GQueue		*tracks_queue;
	double		last_seek_position;
};

typedef struct smf_struct smf_t;

struct smf_track_struct {
	smf_t		*smf;

	int		track_number;
	int		number_of_events;

	/* These are private fields using only by loading and saving routines. */
	void		*file_buffer;
	int		file_buffer_length;
	int		last_status; /* Used for "running status". */

	/* Private, used by smf.c. */
	int		next_event_offset; /* Offset into buffer, used in parse_next_event(). */
	int		next_event_number;
	int		time_of_next_event; /* Absolute time of next event on events_queue. */
	GQueue		*events_queue;
};

typedef struct smf_track_struct smf_track_t;

struct smf_event_struct {
	smf_track_t	*track;

	int		delta_time_pulses;
	int		time_pulses;
	double		time_seconds;
	int		track_number; /* Tracks are numbered consecutively, starting from 1. */
	unsigned char	*midi_buffer;
	int		midi_buffer_length; /* Length of the MIDI message in the buffer, in bytes. */
};

typedef struct smf_event_struct smf_event_t;

/* Routines for creating and freeing basic structures defined above. */
smf_t *smf_new(void);
void smf_free(smf_t *smf);
smf_track_t *smf_track_new(smf_t *smf);
void smf_track_free(smf_track_t *track);
smf_event_t *smf_event_new(smf_track_t *track);
void smf_event_free(smf_event_t *event);

/* Routines for loading SMF files. */
smf_t *smf_load(const char *file_name);
smf_t *smf_load_from_memory(const void *buffer, const int buffer_length);

/* Routine for writing SMF files. */
int smf_save(smf_t *smf, const char *file_name);

int smf_get_number_of_tracks(smf_t *smf);

smf_event_t *smf_get_next_event(smf_t *smf);
smf_event_t *smf_peek_next_event(smf_t *smf);

int smf_seek_to(smf_t *smf, double seconds);

int event_is_metadata(const smf_event_t *event); /* XXX: Needed for assertion in jack-smf-player.c. */

char *smf_string_from_event(const smf_event_t *event);
void smf_rewind(smf_t *smf);
smf_event_t *smf_get_next_event_from_track(smf_track_t *track);
smf_track_t *smf_get_track_by_number(smf_t *smf, int track_number);
smf_event_t *smf_get_event_by_number(smf_track_t *track, int event_number);

#endif /* SMF_H */

