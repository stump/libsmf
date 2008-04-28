#ifndef SMF_PRIVATE_H
#define SMF_PRIVATE_H

/* Structures used in smf_load.c and smf_save.c. */
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

void smf_track_add_event(smf_track_t *track, smf_event_t *event);

int smf_init_tempo(smf_t *smf);
int smf_create_tempo_map_and_compute_seconds(smf_t *smf);
void maybe_add_to_tempo_map(smf_event_t *event);
int smf_event_is_tempo_change_or_time_signature(const smf_event_t *event);
int smf_event_length_is_valid(const smf_event_t *event);
int smf_event_is_sysex(const smf_event_t *event);
int is_status_byte(const unsigned char status);

#endif /* SMF_PRIVATE_H */

