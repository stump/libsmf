#ifndef SMF_H
#define SMF_H

#include <stdio.h>
#include <arpa/inet.h>
#include <glib.h>

struct smf_struct {
	FILE		*stream;
	void		*file_buffer;
	int		file_buffer_length;

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

	double		last_seek_position;
};

typedef struct smf_struct smf_t;

struct smf_track_struct {
	smf_t		*smf;

	void		*file_buffer;
	int		file_buffer_length;

	int		track_number;

	int		next_event_offset; /* Offset into buffer, used in parse_next_event(). */
	int		next_event_number;
	int		last_status; /* Used for "running status". */

	GQueue		*events_queue;
	int		time_of_next_event; /* Absolute time of next event on events_queue. */
};

typedef struct smf_track_struct smf_track_t;

struct smf_event_struct {
	smf_track_t	*track;

	int		time_pulses;
	double		time_seconds;
	int		track_number;
	unsigned char	*midi_buffer;
	int		midi_buffer_length; /* Length of the MIDI message in the buffer, in bytes. */
};

typedef struct smf_event_struct smf_event_t;

smf_t *smf_load(const char *file_name);
smf_t *smf_load_from_memory(const void *buffer, const int buffer_length);

int smf_get_number_of_tracks(smf_t *smf);

smf_event_t *smf_get_next_event(smf_t *smf);
smf_event_t *smf_peek_next_event(smf_t *smf);

int smf_seek_to(smf_t *smf, double seconds);
int event_is_metadata(const smf_event_t *event); /* XXX: Needed for assertion in jack-smf-player.c. */

int smf_save(smf_t *smf, const char *file_name);

/* These are private. */
smf_t *smf_new(void);
void smf_free(smf_t *smf);
smf_track_t *smf_track_new(smf_t *smf);
void smf_track_free(smf_track_t *track);
smf_event_t *smf_event_new(smf_track_t *track);
void smf_event_free(smf_event_t *event);

char *smf_string_from_event(const smf_event_t *event);
void smf_rewind(smf_t *smf);
smf_event_t *smf_get_next_event_from_track(smf_track_t *track);

/* Definitions used in smf_load.c and smf_save.c. */
struct chunk_header_struct {
	char		id[4];
	uint32_t	length; 
} __attribute__((__packed__));

struct mthd_chunk_struct {
	struct chunk_header_struct	mthd_header;
	uint16_t			format;
	uint16_t			number_of_tracks;
	uint16_t			division;
} __attribute__((__packed__));

#endif /* SMF_H */

