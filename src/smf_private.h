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

smf_track_t *smf_event_free_and_dont_care_about_numbers(smf_event_t *event);

#endif /* SMF_PRIVATE_H */

