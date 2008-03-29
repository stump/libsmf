#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
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

	smf->tracks_queue = g_queue_new();
	assert(smf->tracks_queue);

	return smf;
}

static smf_track_t *
smf_track_new(smf_t *smf)
{
	smf_track_t *track = malloc(sizeof(smf_track_t));

	assert(track != NULL);

	memset(track, 0, sizeof(smf_track_t));

	track->smf = smf;
	g_queue_push_tail(smf->tracks_queue, (gpointer)track);
	track->events_queue = g_queue_new();
	assert(track->events_queue);

	return track;
}

static smf_event_t *
smf_event_new(smf_track_t *track)
{
	smf_event_t *event = malloc(sizeof(smf_event_t));

	assert(event != NULL);

	memset(event, 0, sizeof(smf_event_t));

	event->track = track;
	g_queue_push_tail(track->events_queue, (gpointer)event);

	return event;
}

static struct chunk_header_struct *
next_chunk(smf_t *smf)
{
	struct chunk_header_struct *chunk;

	void *next_chunk_ptr = (unsigned char *)smf->buffer + smf->next_chunk_offset;

	chunk = (struct chunk_header_struct *)next_chunk_ptr;

	smf->next_chunk_offset += sizeof(struct chunk_header_struct) + ntohl(chunk->length);

	if (smf->next_chunk_offset > smf->buffer_length)
		return NULL;

	return chunk;
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

	mthd = next_chunk(smf);

	if (mthd == NULL) {
		g_critical("Truncated file.");

		return 1;
	}

	if (!signature_matches(mthd, "MThd")) {
		g_critical("MThd signature not found, is that a MIDI file?");
		
		return 2;
	}

	len = ntohl(mthd->length);
	if (len != 6) {
		g_critical("MThd chunk length %d, should be 6, please report this.", len);

		return 3;
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
	if (smf->format < 0 || smf->format > 2) {
		g_critical("Bad MThd format field value: %d, valid values are (0-2).", smf->format);
		return -1;
	}

	smf->number_of_tracks = ntohs(mthd->number_of_tracks);
	if (smf->number_of_tracks <= 0) {
		g_critical("Bad number of tracks: %d, should be greater than zero.", smf->number_of_tracks);
		return -2;
	}

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

static void
print_mthd(smf_t *smf)
{
	g_debug("**** Values from MThd ****");

	switch (smf->format) {
		case 0:
			g_debug("Format: 0 (single track)");
			break;

		case 1:
			g_debug("Format: 1 (sevaral simultaneous tracks)");
			break;

		case 2:
			g_debug("Format: 2 (sevaral independent tracks)");
			break;

		default:
			g_debug("Format: %d (INVALID FORMAT)", smf->format);
			break;
	}

	g_debug("Number of tracks: %d", smf->number_of_tracks);
	if (smf->format == 0 && smf->number_of_tracks != 0)
		g_warning("Warning: number of tracks is %d, but this is a single track file.", smf->number_of_tracks);

	if (smf->ppqn != 0)
		g_debug("Division: %d PPQN", smf->ppqn);
	else
		g_debug("Division: %d FPS, %d resolution", smf->frames_per_second, smf->resolution);
}

/*
 * Puts value extracted from from "buf" into "value" and number of consumed bytes into "len".
 */
static void
extract_packed_number(const unsigned char *buf, int *value, int *len)
{
	int val = 0;
	const unsigned char *c = buf;

	for (;;) {
		val = (val << 7) + (*c & 0x7F);

		if (*c & 0x80)
			c++;
		else
			break;
	};

	*value = val;
	*len = c - buf + 1;
}

/*
 * Puts MIDI data extracted from from "buf" into "event" and number of consumed bytes into "len".
 * In case valid status is not found, it uses "previous_status" (so called "running status").
 * Returns -1 in case of error.
 */
static int
extract_midi_event(const unsigned char *buf, smf_event_t *event, int *len, int previous_status)
{
	int i, status;
	const unsigned char *c = buf;

	/* Is the first byte the status byte? */
	if (*c & 0x80) {
		status = *c;
		c++;

	} else {
		/* No, we use running status then. */
		status = previous_status;
	}

	if ((status & 0x80) == 0) {
		g_critical("Bad status (MSB is zero).");
		return -1;
	}

	event->midi_buffer[0] = status;

	/* Is this a "meta event"? */
	if (status == 0xFF) {
		/* 0xFF 0xwhatever 0xlength + the actual length. */
		int len = *(c + 1) + 3;

		for (i = 1; i < len; i++, c++) {
			if (i >= 1024) {
				g_warning("Whoops, meta event too long.");
				continue;
			}

			event->midi_buffer[i] = *c;
		}

	} else {

		/* XXX: running status does not really work that way. */
		/* Copy the rest of the MIDI event into buffer. */
		for (i = 1; (*(c + 1) & 0x80) == 0; i++, c++) {
			if (i >= 1024) {
				g_warning("Whoops, MIDI event too long.");
				continue;
			}

			event->midi_buffer[i] = *c;
		}
	}

	*len = c - buf;

	return 0;
}

static smf_event_t *
parse_next_event(smf_track_t *track)
{
	int time = 0, len;
	unsigned char *c, *start;

	smf_event_t *event = smf_event_new(track);
	
	c = start = (unsigned char *)track->buffer + track->next_event_offset;

	/* First, extract time offset from previous event. */
	extract_packed_number(c, &time, &len);
	c += len;
	event->time = time;

	/* Now, extract the actual event. */
	if (extract_midi_event(c, event, &len, track->last_status))
		return NULL;

	c += len;
	track->last_status = event->midi_buffer[0];
	track->next_event_offset += c - start;

	return event;
}

static char *
make_string(const void *counted_string, int len)
{
	char *str = malloc(len + 1);
	assert(str);
	memcpy(str, counted_string, len);
	str[len] = '\0';

	return str;
}

int
is_real_event(const smf_event_t *event)
{
	if (event->midi_buffer[0] != 0xFF)
		return 1;

	return 0;
}

char *
string_from_event(const smf_event_t *event)
{
	int string_length, length_length;

	extract_packed_number((void *)&(event->midi_buffer[2]), &string_length, &length_length);

	return make_string((void *)(&event->midi_buffer[2] + length_length), string_length);
}

static void
print_event(smf_event_t *event)
{
	fprintf(stderr, "Event: time %d; status 0x%x (", event->time, event->midi_buffer[0]);
	
	switch (event->midi_buffer[0] & 0xF0) {
		case 0x80:
			fprintf(stderr, "Note Off");
			break;

		case 0x90:
			fprintf(stderr, "Note On");
			break;

		case 0xA0:
			fprintf(stderr, "Aftertouch");
			break;

		case 0xB0:
			fprintf(stderr, "Control Change");
			break;

		case 0xC0:
			fprintf(stderr, "Program Change");
			break;

		case 0xD0:
			fprintf(stderr, "Channel Pressure");
			break;

		case 0xE0:
			fprintf(stderr, "Pitch Wheel");
			break;

		default:
			break;
	}

	if (event->midi_buffer[0] == 0xFF) {
		switch (event->midi_buffer[1]) {
			case 0x00:
				fprintf(stderr, "Sequence Number");
				break;

			case 0x01:
				fprintf(stderr, "Text: %s", string_from_event(event));

				break;

			case 0x02:
				fprintf(stderr, "Copyright: %s", string_from_event(event));
				break;

			case 0x03:
				fprintf(stderr, "Sequence/Track Name: %s", string_from_event(event));
				break;

			case 0x04:
				fprintf(stderr, "Instrument: %s", string_from_event(event));
				break;

			case 0x05:
				fprintf(stderr, "Lyric: %s", string_from_event(event));
				break;

			case 0x06:
				fprintf(stderr, "Marker: %s", string_from_event(event));
				break;

			case 0x07:
				fprintf(stderr, "Cue Point: %s", string_from_event(event));
				break;

			case 0x08:
				fprintf(stderr, "Program Name: %s", string_from_event(event));
				break;

			case 0x09:
				fprintf(stderr, "Device (Port) Name: %s", string_from_event(event));
				break;

			case 0x2F:
				fprintf(stderr, "End Of Track");
				break;

			case 0x51:
				fprintf(stderr, "Tempo: %d microseconds per quarter note", (event->midi_buffer[3] << 16) +
						(event->midi_buffer[4] << 8) + event->midi_buffer[5]);
				break;

			case 0x54:
				fprintf(stderr, "SMPTE Offset");
				break;

			case 0x58:
				fprintf(stderr, "Time Signature: %d/%d, %d clocks per click, %d notated 32nd notes per quarter note",
						event->midi_buffer[3], (int)pow(2, event->midi_buffer[4]), event->midi_buffer[5],
						event->midi_buffer[6]);
				break;

			case 0x59:
				fprintf(stderr, "Key Signature");
				break;

			case 0x7F:
				fprintf(stderr, "Proprietary Event");
				break;

			default:
				fprintf(stderr, "Unknown Event: 0xFF 0x%x 0x%x 0x%x", event->midi_buffer[1], event->midi_buffer[2],
				      event->midi_buffer[3]);

				break;
		}
	}

	fprintf(stderr, ")\n");
}

static int
parse_mtrk_header(smf_track_t *track)
{
	struct chunk_header_struct *mtrk;

	/* Make sure compiler didn't do anything stupid. */
	assert(sizeof(struct chunk_header_struct) == 8);
	assert(track->smf != NULL);

	mtrk = next_chunk(track->smf);

	if (mtrk == NULL) {
		g_critical("Truncated file.");

		return 1;
	}

	if (!signature_matches(mtrk, "MTrk")) {
		g_critical("MTrk signature not found, skipping chunk.");
		
		return 2;
	}

	track->buffer = mtrk;
	track->buffer_length = sizeof(struct chunk_header_struct) + ntohl(mtrk->length);
	track->next_event_offset = sizeof(struct chunk_header_struct);

	return 0;
}

int
is_end_of_track(const smf_event_t *event)
{
	if (event->midi_buffer[0] == 0xFF && event->midi_buffer[1] == 0x2F)
		return 1;

	return 0;
}

static int
parse_mtrk_chunk(smf_track_t *track)
{
	if (parse_mtrk_header(track))
		return 1;

	for (;;) {
		smf_event_t *event = parse_next_event(track);

		if (is_end_of_track(event))
			break;

		if (event == NULL)
			return 2;

#if 0
		print_event(event);
#endif
	}

	return 0;
}

static int
load_file_into_buffer(smf_t *smf, const char *file_name)
{
	smf->stream = fopen(file_name, "r");
	if (smf->stream == NULL) {
		perror("Cannot open input file");

		return 1;
	}

	if (fseek(smf->stream, 0, SEEK_END)) {
		perror("fseek(3) failed");

		return 2;
	}

	smf->buffer_length = ftell(smf->stream);
	if (smf->buffer_length == -1) {
		perror("ftell(3) failed");

		return 3;
	}

	if (fseek(smf->stream, 0, SEEK_SET)) {
		perror("fseek(3) failed");

		return 4;
	}

	smf->buffer = malloc(smf->buffer_length);
	if (smf->buffer == NULL) {
		perror("malloc(3) failed");

		return 5;
	}

	if (fread(smf->buffer, 1, smf->buffer_length, smf->stream) != smf->buffer_length) {
		perror("fread(3) failed");

		return 6;
	}

	return 0;
}

smf_t *
smf_load(const char *file_name)
{
	int i;

	smf_t *smf = smf_new();

	if (load_file_into_buffer(smf, file_name))
		return NULL;

	if (parse_mthd_chunk(smf))
		return NULL;

	print_mthd(smf);

	for (i = 0; i < smf->number_of_tracks; i++) {
		smf_track_t *track = smf_track_new(smf);

		if (parse_mtrk_chunk(track))
			return NULL;
	}

	return smf;
}

void *
smf_get_next_message(smf_t *smf)
{
	int i;
	smf_event_t *event;
	smf_track_t *track;

	for (i = 0; i < g_queue_get_length(smf->tracks_queue); i++) {
		track = (smf_track_t *)g_queue_peek_nth(smf->tracks_queue, i);

		if (g_queue_is_empty(track->events_queue))
			continue;

		event = (smf_event_t *)g_queue_pop_head(track->events_queue);

		return event->midi_buffer;
	}

	return NULL;
}

