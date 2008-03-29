#ifndef SMF_H
#define SMF_H

#include <stdio.h>

struct smf_struct {
	FILE		*stream;
	void		*buffer;

	int		format;
	int		number_of_tracks;

	/* These fields are extracted from "division" field of MThd header.  Valid is _either_ ppqn or frames_per_second/resolution. */
	int		ppqn;
	int		frames_per_second;
	int		resolution;
};

typedef struct smf_struct smf_t;

smf_t *smf_open(const char *file_name);
void smf_close(smf_t *smf);

#endif /* SMF_H */

