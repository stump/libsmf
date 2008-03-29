#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include "smf.h"

/* Reference: http://www.borg.com/~jglatt/tech/midifile.htm */

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

static smf_t *
smf_new(void)
{
	smf_t *smf = malloc(sizeof(smf_t));

	assert(smf != NULL);

	memset(smf, 0, sizeof(smf_t));

	return smf;
}

static int
signature_matches(const struct chunk_header_struct *chunk, const char *signature)
{
	if (chunk->id[0] == signature[0] && chunk->id[1] == signature[1] && chunk->id[2] == signature[2] && chunk->id[3] == signature[3])
		return 1;

	return 0;
}

static int
parse_mthd_header(smf_t *smf)
{
	int len;
	struct chunk_header_struct *mthd;

	/* Make sure compiler didn't do anything stupid. */
	assert(sizeof(struct chunk_header_struct) == 8);

	mthd = (struct chunk_header_struct *)smf->buffer;

	if (!signature_matches(mthd, "MThd")) {
		fprintf(stderr, "MThd signature not found, is that a MIDI file?\n");
		
		return 1;
	}

	len = ntohl(mthd->length);
	if (len != 6) {
		fprintf(stderr, "MThd chunk length %d, should be 6, please report this.\n", len);

		return 2;
	}

	return 0;
}

static int
parse_mthd_chunk(smf_t *smf)
{
	signed char first_byte_of_division, second_byte_of_division;

	struct mthd_chunk_struct *mthd;

	assert(sizeof(struct mthd_chunk_struct) == 14);

	if (parse_mthd_header(smf))
		return 1;

	mthd = (struct mthd_chunk_struct *)smf->buffer;

	smf->format = ntohs(mthd->format);
	smf->number_of_tracks = ntohs(mthd->number_of_tracks);

	/* XXX: endianess? */
	first_byte_of_division = *((signed char *)&(mthd->division));
	second_byte_of_division = *((signed char *)&(mthd->division) + 1);

	if (first_byte_of_division >= 0) {
		smf->ppqn = ntohs(mthd->division);
		smf->frames_per_second = 0;
		smf->resolution = 0;
	} else {
		smf->ppqn = 0;
		smf->frames_per_second = - first_byte_of_division;
		smf->resolution = second_byte_of_division;
	}
	
	return 0;
}

void
print_things(smf_t *smf)
{
	fprintf(stderr, " **** Values from MThd ****\n");

	switch (smf->format) {
		case 0:
			fprintf(stderr, "Format: 0 (single track)\n");
			break;

		case 1:
			fprintf(stderr, "Format: 1 (sevaral simultaneous tracks)\n");
			break;

		case 2:
			fprintf(stderr, "Format: 2 (sevaral independent tracks)\n");
			break;

		default:
			fprintf(stderr, "Format: %d (INVALID FORMAT)\n", smf->format);
			break;
	}

	fprintf(stderr, "Number of tracks: %d\n", smf->number_of_tracks);
	if (smf->format == 0 && smf->number_of_tracks != 0)
		fprintf(stderr, "Warning: number of tracks is %d, but this is a single track file.\n", smf->number_of_tracks);

	if (smf->ppqn != 0)
		fprintf(stderr, "Division: %d PPQN\n", smf->ppqn);
	else
		fprintf(stderr, "Division: %d FPS, %d resolution\n", smf->frames_per_second, smf->resolution);
}

int
load_file_into_buffer(smf_t *smf, const char *file_name)
{
	int size;

	smf->stream = fopen(file_name, "r");
	if (smf->stream == NULL) {
		perror("Cannot open input file");

		return 1;
	}

	if (fseek(smf->stream, 0, SEEK_END)) {
		perror("fseek(3) failed");

		return 2;
	}

	size = ftell(smf->stream);
	if (size == -1) {
		perror("ftell(3) failed");

		return 3;
	}

	if (fseek(smf->stream, 0, SEEK_SET)) {
		perror("fseek(3) failed");

		return 4;
	}

	smf->buffer = malloc(size);
	if (smf->buffer == NULL) {
		perror("malloc(3) failed");

		return 5;
	}

	if (fread(smf->buffer, 1, size, smf->stream) != size) {
		perror("fread(3) failed");

		return 6;
	}

	return 0;
}

smf_t *
smf_open(const char *file_name)
{
	smf_t *smf = smf_new();

	if (load_file_into_buffer(smf, file_name))
		return NULL;

	if (parse_mthd_chunk(smf))
		return NULL;

	print_things(smf);

	return smf;
}

void
smf_close(smf_t *smf)
{
	if (smf->buffer != NULL) {
		free(smf->buffer);
		smf->buffer = NULL;
	}

	if (smf->stream != NULL) {
		fclose(smf->stream);
		smf->stream = NULL;
	}
}

