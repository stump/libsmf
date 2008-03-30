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
	smf->last_track_number++;
	track->track_number = smf->last_track_number;
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
	event->track_number = track->track_number;
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
		g_critical("SMF error: file is truncated.");

		return 1;
	}

	if (!signature_matches(mthd, "MThd")) {
		g_critical("SMF error: MThd signature not found, is that a MIDI file?");
		
		return 2;
	}

	len = ntohl(mthd->length);
	if (len != 6) {
		g_critical("SMF error: MThd chunk length %d, should be 6.", len);

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
		g_critical("SMF error: bad MThd format field value: %d, valid values are 0-2, inclusive.", smf->format);
		return -1;
	}

	if (smf->format == 2) {
		g_critical("SMF file uses format #2, no support for that yet.");
		return -2;
	}

	smf->number_of_tracks = ntohs(mthd->number_of_tracks);
	if (smf->number_of_tracks <= 0) {
		g_critical("SMF error: bad number of tracks: %d, should be greater than zero.", smf->number_of_tracks);
		return -3;
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

	if (smf->ppqn == 0) {
		g_critical("SMF file uses FPS timing instead of PPQN, no support for that yet.");
		return -4;
	}
	
	return 0;
}

static void
print_mthd(smf_t *smf)
{
	int off = 0;
	char buf[256];

	off += snprintf(buf + off, sizeof(buf) - off, "SMF header contents: format: %d ", smf->format);

	switch (smf->format) {
		case 0:
			off += snprintf(buf + off, sizeof(buf) - off, "(single track)");
			break;

		case 1:
			off += snprintf(buf + off, sizeof(buf) - off, "(several simultaneous tracks)");
			break;

		case 2:
			off += snprintf(buf + off, sizeof(buf) - off, "(several independent tracks)");
			break;

		default:
			off += snprintf(buf + off, sizeof(buf) - off, "(INVALID FORMAT)");
			break;
	}

	off += snprintf(buf + off, sizeof(buf) - off, "; number of tracks: %d", smf->number_of_tracks);

	if (smf->ppqn != 0)
		off += snprintf(buf + off, sizeof(buf) - off, "; division: %d PPN.", smf->ppqn);
	else
		off += snprintf(buf + off, sizeof(buf) - off, "; division: %d FPS, %d resolution.", smf->frames_per_second, smf->resolution);

	g_debug("%s", buf);

	if (smf->format == 0 && smf->number_of_tracks != 0)
		g_warning("Warning: number of tracks is %d, but this is a single track file.", smf->number_of_tracks);

}

/*
 * Puts value extracted from from "buf" into "value" and number of consumed bytes into "len".
 * Returns -1 in case of error.
 */
static int
extract_packed_number(const unsigned char *buf, const int buffer_length, int *value, int *len)
{
	int val = 0;
	const unsigned char *c = buf;

	assert(buffer_length > 0);

	for (;;) {
		if (c >= buf + buffer_length) {
			g_critical("End of buffer in extract_packed_number.");
			return -1;
		}

		val = (val << 7) + (*c & 0x7F);

		if (*c & 0x80)
			c++;
		else
			break;
	};

	*value = val;
	*len = c - buf + 1;

	return 0;
}

/*
 * Puts MIDI data extracted from from "buf" into "event" and number of consumed bytes into "len".
 * In case valid status is not found, it uses "previous_status" (so called "running status").
 * Returns -1 in case of error.
 */
static int
extract_midi_event(const unsigned char *buf, const int buffer_length, smf_event_t *event, int *len, int previous_status)
{
	int i, status;
	const unsigned char *c = buf;

	assert(buffer_length > 0);

	/* Is the first byte the status byte? */
	if (*c & 0x80) {
		status = *c;
		c++;

	} else {
		/* No, we use running status then. */
		status = previous_status;
	}

	if ((status & 0x80) == 0) {
		g_critical("SMF error: bad status byte (MSB is zero).");
		return -1;
	}

	event->midi_buffer[0] = status;

	/* Is this a "meta event"? */
	if (status == 0xFF) {
		/* 0xFF 0xwhatever 0xlength + the actual length. */
		int len = *(c + 1) + 3;

		for (i = 1; i < len; i++, c++) {
			if (c >= buf + buffer_length) {
				g_critical("End of buffer in extract_midi_event.");
				return -2;
			}

			if (i >= MAX_EVENT_LENGTH) {
				g_warning("Whoops, meta event too long.");
				continue;
			}

			event->midi_buffer[i] = *c;
		}

	} else {

		/* XXX: running status does not really work that way. */
		/* Copy the rest of the MIDI event into buffer. */
		for (i = 1; (*(c + 1) & 0x80) == 0; i++, c++) {
			if (c >= buf + buffer_length) {
				g_critical("End of buffer in extract_midi_event.");
				return -3;
			}

			if (i >= MAX_EVENT_LENGTH) {
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
	int time = 0, len, buffer_length;
	unsigned char *c, *start;

	smf_event_t *event = smf_event_new(track);

	c = start = (unsigned char *)track->buffer + track->next_event_offset;

	buffer_length = track->buffer_length - track->next_event_offset;
	assert(buffer_length > 0);

	/* First, extract time offset from previous event. */
	if (extract_packed_number(c, buffer_length, &time, &len))
		return NULL;

	c += len;
	buffer_length -= len;
	event->time = time;

	if (buffer_length <= 0)
		return NULL;

	/* Now, extract the actual event. */
	if (extract_midi_event(c, buffer_length, event, &len, track->last_status))
		return NULL;

	c += len;
	buffer_length -= len;
	track->last_status = event->midi_buffer[0];
	track->next_event_offset += c - start;

	return event;
}

static char *
make_string(const unsigned char *buf, const int buffer_length, int len)
{
	char *str;

	if (len > buffer_length) {
		g_critical("End of buffer in make_string.");

		len = buffer_length;
	}

	str = malloc(len + 1);
	assert(str);
	memcpy(str, buf, len);
	str[len] = '\0';

	return str;
}

char *
string_from_event(const smf_event_t *event)
{
	int string_length, length_length;

	extract_packed_number((void *)&(event->midi_buffer[2]), MAX_EVENT_LENGTH - 3, &string_length, &length_length);

	return make_string((void *)(&event->midi_buffer[2] + length_length), MAX_EVENT_LENGTH - 3 - length_length, string_length);
}

#if 0
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
#endif

static int
parse_mtrk_header(smf_track_t *track)
{
	struct chunk_header_struct *mtrk;

	/* Make sure compiler didn't do anything stupid. */
	assert(sizeof(struct chunk_header_struct) == 8);
	assert(track->smf != NULL);

	mtrk = next_chunk(track->smf);

	if (mtrk == NULL) {
		g_critical("SMF error: file is truncated.");

		return 1;
	}

	if (!signature_matches(mtrk, "MTrk")) {
		g_critical("SMF error: MTrk signature not found.");
		
		return 2;
	}

	track->buffer = mtrk;
	track->buffer_length = sizeof(struct chunk_header_struct) + ntohl(mtrk->length);
	track->next_event_offset = sizeof(struct chunk_header_struct);

	return 0;
}

static int
event_is_end_of_track(const smf_event_t *event)
{
	if (event->midi_buffer[0] == 0xFF && event->midi_buffer[1] == 0x2F)
		return 1;

	return 0;
}

static int
parse_mtrk_chunk(smf_track_t *track)
{
	int time = 0;
	smf_event_t *event;

	if (parse_mtrk_header(track))
		return 1;

	for (;;) {
		event = parse_next_event(track);

		/* Replace "relative" event time with absolute one, i.e. relative to the start of the track. */
		event->time += time;
		time = event->time;

		if (event_is_end_of_track(event))
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

	if (smf->last_track_number != smf->number_of_tracks) {
		g_warning("SMF error: MThd header declared %d tracks, but only %d found; continuing anyway.",
				smf->number_of_tracks, smf->last_track_number);
	}

	return smf;
}

static int
event_is_metadata(const smf_event_t *event)
{
	if (event->midi_buffer[0] == 0xFF)
		return 1;

	return 0;
}

static void
parse_metadata_event(const smf_event_t *event)
{
	assert(event_is_metadata(event));

	/* "Tempo" metaevent. */
	if (event->midi_buffer[1] == 0x51) {
		assert(event->track != NULL);
		assert(event->track->smf != NULL);

		event->track->smf->microseconds_per_quarter_note = 
			(event->midi_buffer[3] << 16) + (event->midi_buffer[4] << 8) + event->midi_buffer[5];

		g_debug("Setting microseconds per quarter note: %d.", event->track->smf->microseconds_per_quarter_note);
	}
}

smf_event_t *
smf_get_next_event_from_track(smf_track_t *track)
{
	smf_event_t *event = (smf_event_t *)g_queue_pop_head(track->events_queue);
	smf_event_t *next_event = (smf_event_t *)g_queue_peek_head(track->events_queue);
	
	if (event == NULL) {
		g_debug("End of the track.");
		return NULL;
	}

	if (next_event != NULL)
		track->time_of_next_event = next_event->time;

	return event;
}


smf_event_t *
smf_get_next_event(smf_t *smf)
{
	int i;
	smf_event_t *event;
	smf_track_t *track = NULL, *min_time_track = NULL;

	int min_time = 0;

	/* Find track with event that should be played next. */
	for (i = 0; i < g_queue_get_length(smf->tracks_queue); i++) {
		track = (smf_track_t *)g_queue_peek_nth(smf->tracks_queue, i);

		if (g_queue_is_empty(track->events_queue))
				continue;

		if (track->time_of_next_event < min_time || min_time_track == NULL) {
			min_time = track->time_of_next_event;
			min_time_track = track;
		}
	}

	if (min_time_track == NULL) {
		g_debug("End of the song.");

		return NULL;
	}

	event = smf_get_next_event_from_track(min_time_track);

	if (event_is_metadata(event)) {
		parse_metadata_event(event);

		return smf_get_next_event(smf);
	}

	return event;
}

double
smf_milliseconds_per_time_unit(smf_t *smf)
{
	if (smf->ppqn == 0)
		return 0.0;

	return (double)smf->microseconds_per_quarter_note / (double)(smf->ppqn * 1000);
}

void
smf_rewind(smf_t *smf)
{
}

int
smf_get_number_of_tracks(smf_t *smf)
{
	return smf->number_of_tracks;
}

