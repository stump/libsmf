/*
 * This is the include file for libsmf, Standard Midi File format library.
 * For questions and comments, contact Edward Tomasz Napierala <trasz@FreeBSD.org>.
 * Libsmf is public domain, you can do with it whatever you want.
 */

#ifndef SMF_H
#define SMF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <arpa/inet.h>
#include <glib.h>

#define SMF_VERSION "0.9"

struct smf_struct {
	int		format;
	int		expected_number_of_tracks;

	/** These fields are extracted from "division" field of MThd header.  Valid is _either_ ppqn or frames_per_second/resolution. */
	int		ppqn;
	int		frames_per_second;
	int		resolution;
	int		number_of_tracks;

	/** These are private fields using only by loading and saving routines. */
	FILE		*stream;
	void		*file_buffer;
	int		file_buffer_length;
	int		next_chunk_offset;

	/** Private, used by smf.c. */
	GPtrArray	*tracks_array;
	double		last_seek_position;

	/** Private, used by smf_tempo.c. */
	/** Array of pointers to smf_tempo_struct. */
	GPtrArray	*tempo_array;
};

typedef struct smf_struct smf_t;

/** This structure describes a single tempo change. */
struct smf_tempo_struct {
	int time_pulses;
	double time_seconds;
	int microseconds_per_quarter_note;
	int numerator;
	int denominator;
	int clocks_per_click;
	int notes_per_note;
};

typedef struct smf_tempo_struct smf_tempo_t;

struct smf_track_struct {
	smf_t		*smf;

	int		track_number;
	int		number_of_events;

	/** These are private fields using only by loading and saving routines. */
	void		*file_buffer;
	int		file_buffer_length;
	int		last_status; /* Used for "running status". */

	/** Private, used by smf.c. */
	/** Offset into buffer, used in parse_next_event(). */
	int		next_event_offset;
	int		next_event_number;

	/** Absolute time of next event on events_queue. */
	int		time_of_next_event;
	GPtrArray	*events_array;
};

typedef struct smf_track_struct smf_track_t;

struct smf_event_struct {
	/** Pointer to the track, or NULL if event is not attached. */
	smf_track_t	*track;

	/** Number of this event in the track.  Events are numbered consecutively, starting from one. */
	int		event_number;

	/** Note that the time fields are invalid, if event is not attached to a track. */
	/** Time, in pulses, since the previous event on this track. */
	int		delta_time_pulses;

	/** Time, in pulses, since the start of the song. */
	int		time_pulses;

	/** Time, in seconds, since the start of the song. */
	double		time_seconds;

	/** Tracks are numbered consecutively, starting from 1. */
	int		track_number;

	/** Pointer to the buffer containing MIDI message.  This is freed by smf_event_delete. */
	unsigned char	*midi_buffer;

	/** Length of the MIDI message in the buffer, in bytes. */
	int		midi_buffer_length; 
};

typedef struct smf_event_struct smf_event_t;

/* Routines for manipulating smf_t. */
smf_t *smf_new(void);
void smf_delete(smf_t *smf);

int smf_set_format(smf_t *smf, int format);
int smf_set_ppqn(smf_t *smf, int format);

smf_track_t *smf_get_track_by_number(smf_t *smf, int track_number);
smf_event_t *smf_get_next_event(smf_t *smf);
smf_event_t *smf_peek_next_event(smf_t *smf);
void smf_rewind(smf_t *smf);
int smf_seek_to_seconds(smf_t *smf, double seconds);
int smf_seek_to_event(smf_t *smf, const smf_event_t *event);

void smf_add_track(smf_t *smf, smf_track_t *track);
void smf_remove_track(smf_track_t *track);

/* Routines for manipulating smf_track_t. */
smf_track_t *smf_track_new(void);
void smf_track_delete(smf_track_t *track);

smf_event_t *smf_track_get_next_event(smf_track_t *track);
smf_event_t *smf_track_get_event_by_number(const smf_track_t *track, int event_number);
smf_event_t *smf_track_get_last_event(const smf_track_t *track);

void smf_track_add_event_delta_pulses(smf_track_t *track, smf_event_t *event, int pulses);
void smf_track_add_event_pulses(smf_track_t *track, smf_event_t *event, int pulses);
void smf_track_add_event_seconds(smf_track_t *track, smf_event_t *event, double seconds);
int smf_track_add_eot(smf_track_t *track);
void smf_track_remove_event(smf_event_t *event);

/* Routines for manipulating smf_event_t. */
smf_event_t *smf_event_new(void);
smf_event_t *smf_event_new_from_pointer(void *midi_data, int len);
smf_event_t *smf_event_new_from_bytes(int first_byte, int second_byte, int third_byte);
void smf_event_delete(smf_event_t *event);

int smf_event_is_valid(const smf_event_t *event);
int smf_event_is_metadata(const smf_event_t *event);
char *smf_event_decode(const smf_event_t *event);
char *smf_string_from_event(const smf_event_t *event);

/* Routines for loading SMF files. */
smf_t *smf_load(const char *file_name);
smf_t *smf_load_from_memory(const void *buffer, const int buffer_length);

/* Routine for writing SMF files. */
int smf_save(smf_t *smf, const char *file_name);

/* Routines for manipulating smf_tempo_t. */
smf_tempo_t *smf_get_tempo_by_pulses(const smf_t *smf, int pulses);
smf_tempo_t *smf_get_tempo_by_seconds(const smf_t *smf, double seconds);
smf_tempo_t *smf_get_tempo_by_number(const smf_t *smf, int number);
smf_tempo_t *smf_get_last_tempo(const smf_t *smf);

const char *smf_get_version(void);

#ifdef __cplusplus
}
#endif

#endif /* SMF_H */

