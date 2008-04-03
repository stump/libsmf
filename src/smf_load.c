/*
 * This is Standard MIDI File loader.
 *
 * For questions and comments, contact Edward Tomasz Napierala <trasz@FreeBSD.org>.
 * This code is public domain, you can do with it whatever you want.
 */

/* Reference: http://www.borg.com/~jglatt/tech/midifile.htm */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <errno.h>
#include <arpa/inet.h>
#include "smf.h"

/*
 * Returns pointer to the next SMF chunk in smf->buffer, based on length of the previous one.
 */
static struct chunk_header_struct *
next_chunk(smf_t *smf)
{
	struct chunk_header_struct *chunk;
	void *next_chunk_ptr;

	assert(smf->file_buffer != NULL);
	assert(smf->file_buffer_length > 0);
	assert(smf->next_chunk_offset >= 0);

	next_chunk_ptr = (unsigned char *)smf->file_buffer + smf->next_chunk_offset;

	chunk = (struct chunk_header_struct *)next_chunk_ptr;

	smf->next_chunk_offset += sizeof(struct chunk_header_struct) + ntohl(chunk->length);

	if (smf->next_chunk_offset > smf->file_buffer_length)
		return NULL;

	return chunk;
}

/*
 * Returns 1, iff signature of the "chunk" is the same as string passed as "signature".
 */
static int
chunk_signature_matches(const struct chunk_header_struct *chunk, const char *signature)
{
	if (!memcmp(chunk->id, signature, 4))
		return 1;

	return 0;
}

/*
 * Verifies if MThd header looks OK.  Returns 0 iff it does.
 */
static int
parse_mthd_header(smf_t *smf)
{
	int len;
	struct chunk_header_struct *mthd, *tmp_mthd;

	/* Make sure compiler didn't do anything stupid. */
	assert(sizeof(struct chunk_header_struct) == 8);

	/*
	 * We could just do "mthd = smf->file_buffer;" here, but this way we wouldn't
	 * get useful error messages.
	 */
	if (smf->file_buffer_length < 6) {
		g_critical("SMF error: file is too short, it cannot be a MIDI file.");

		return -1;
	}

	tmp_mthd = smf->file_buffer;

	if (!chunk_signature_matches(tmp_mthd, "MThd")) {
		g_critical("SMF error: MThd signature not found, is that a MIDI file?");
		
		return -2;
	}

	/* Ok, now use next_chunk(). */
	mthd = next_chunk(smf);

	assert(mthd == tmp_mthd);

	len = ntohl(mthd->length);
	if (len != 6) {
		g_critical("SMF error: MThd chunk length %d, must be 6.", len);

		return -3;
	}

	return 0;
}

/*
 * Parses MThd chunk, filling "smf" structure with values extracted from it.  Returns 0 iff everything went OK.
 */
static int
parse_mthd_chunk(smf_t *smf)
{
	signed char first_byte_of_division, second_byte_of_division;

	struct mthd_chunk_struct *mthd;

	assert(sizeof(struct mthd_chunk_struct) == 14);

	if (parse_mthd_header(smf))
		return 1;

	mthd = (struct mthd_chunk_struct *)smf->file_buffer;

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
		g_critical("SMF error: bad number of tracks: %d, must be greater than zero.", smf->number_of_tracks);
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

/*
 * Prints out one-line summary of data extracted from MThd header by parse_mthd_chunk().
 */
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
		off += snprintf(buf + off, sizeof(buf) - off, "; division: %d PPQN.", smf->ppqn);
	else
		off += snprintf(buf + off, sizeof(buf) - off, "; division: %d FPS, %d resolution.", smf->frames_per_second, smf->resolution);

	g_debug("%s", buf);

	if (smf->format == 0 && smf->number_of_tracks != 1)
		g_warning("Warning: number of tracks is %d, but this is a single track file.", smf->number_of_tracks);

}

/*
 * Puts value extracted from from "buf" into "value" and number of bytes consumed into "len",
 * making sure it does not read past "buf" + "buffer_length".
 * Explanation of "packed numbers" is here: http://www.borg.com/~jglatt/tech/midifile/vari.htm
 * Returns 0 iff everything went OK, different value in case of error.
 */
static int
extract_packed_number(const unsigned char *buf, const int buffer_length, int *value, int *len)
{
	int val = 0;
	const unsigned char *c = buf;

	assert(buffer_length > 0);

	for (;;) {
		if (c >= buf + buffer_length) {
			g_critical("End of buffer in extract_packed_number().");
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
 * Returns 1 if the given byte is a valid status byte, 0 otherwise.
 */
static int
is_status_byte(const unsigned char status)
{
	return (status & 0x80);
}

/*
 * Returns 1 if the given byte is status byte for realtime message, 0 otherwise.
 */
static int
is_realtime_byte(const unsigned char status)
{
	if (status >= 0xF8 && status <= 0xFE)
		return 1;

	return 0;
}

/*
 * Creates new realtime event and attaches it to "track".  Returns 0 iff everything went OK, < 0 otherwise.
 */
static int
parse_realtime_event(const unsigned char status, smf_track_t *track)
{
	smf_event_t *event = smf_event_new(track);

	assert(is_realtime_byte(status));
	
	event->midi_buffer = malloc(1);
	if (event->midi_buffer == NULL) {
		g_critical("Cannot allocate memory in parse_realtime_event().");
		smf_event_free(event);

		return -1;
	}

	event->midi_buffer[0] = status;
	event->midi_buffer_length = 1;

	/* We don't need to do anything more; smf_event_new() already added the new event to the track. */

	return 0;
}

/*
 * Just like expected_message_length(), but only for System Exclusive messages.
 */
static int
expected_sysex_length(const unsigned char status, const unsigned char *second_byte, const int buffer_length)
{
	int len;

	assert(status == 0xF0);

	if (buffer_length < 2) {
		g_critical("SMF error: end of buffer in expected_sysex_length().");
		return -1;
	}

	/* Any status byte terminates the SysEx. */
	for (len = 0; !is_status_byte(second_byte[len]); len++) {
		if (len >= buffer_length) {
			g_critical("SMF error: end of buffer in expected_sysex_length().");
			return -2;
		}
	}

	if (second_byte[len] != 0xF7) {
		g_warning("SMF warning: SysEx terminated by 0x%x instead of 0xF7.", second_byte[len]);

		/* "i" is the length minus starting (0xF0) status byte; terminating status byte is a part of another MIDI message. */
		return (len + 1);
	}

	/* "i" is the length minus starting (0xF0) status byte and second byte. */
	return (len + 2);
}

/*
 * Returns expected length of the midi message (including the status byte), in bytes, for the given status byte.
 * The "second_byte" points to the expected second byte of the MIDI message.  "buffer_length" is the buffer
 * length limit, counting from "second_byte".  Returns value < 0 iff there was an error.
 */
static int
expected_message_length(unsigned char status, const unsigned char *second_byte, const int buffer_length)
{
	/* Make sure this really is a valid status byte. */
	assert(is_status_byte(status));
	assert(buffer_length > 0);

	/* Is this a metamessage? */
	if (status == 0xFF) {
		if (buffer_length < 2) {
			g_critical("SMF error: end of buffer in expected_message_length().");
			return -1;
		}

		/*
		 * Format of this kind of messages is like this: 0xFF 0xwhatever 0xlength and then "length" bytes.
		 * Second byte points to this:                        ^^^^^^^^^^
		 */
		return *(second_byte + 1) + 3;
	}

	if ((status & 0xF0) == 0xF0) {
		switch (status) {
			case 0xF2: /* Song Position Pointer. */
				return 3;

			case 0xF1: /* MTC Quarter Frame. */
			case 0xF3: /* Song Select. */
				return 2;

			case 0xF6: /* Tune Request. */
			case 0xF8: /* MIDI Clock. */
			case 0xF9: /* Tick. */
			case 0xFA: /* MIDI Start. */
			case 0xFB: /* MIDI Continue. */
			case 0xFC: /* MIDI Stop. */
			case 0xFE: /* Active Sense. */
				return 1;

			case 0xF0: /* System Exclusive. */
				return expected_sysex_length(status, second_byte, buffer_length);

			case 0xF7: /* End of SysEx. */
				g_warning("SMF warning: status 0xF7 (End of SysEx) encountered without matching 0xF0 (Start of SysEx).");
				return 1; /* Ignore it. */

			default:
				g_critical("SMF error: unknown 0xFx-type status byte '0x%x'.", status);
				return -2;
		}
	}

	/* Filter out the channel. */
	status &= 0xF0;

	switch (status) {
		case 0x80: /* Note Off. */
		case 0x90: /* Note On. */
		case 0xA0: /* AfterTouch. */
		case 0xB0: /* Control Change. */
		case 0xE0: /* Pitch Wheel. */
			return 3;	

		case 0xC0: /* Program Change. */
		case 0xD0: /* Channel Pressure. */
			return 2;

		default:
			g_critical("SMF error: unknown status byte '0x%x'.", status);
			return -3;
	}
}

/*
 * Puts MIDI data extracted from from "buf" into "event" and number of consumed bytes into "len".
 * In case valid status is not found, it uses "previous_status" (so called "running status").
 * Returns 0 iff everything went OK, value < 0 in case of error.
 */
static int
extract_midi_event(const unsigned char *buf, const int buffer_length, smf_event_t *event, int *len, int previous_status)
{
	int i, status, message_length;
	const unsigned char *c = buf;

	assert(buffer_length > 0);

	/* Is the first byte the status byte? */
	if (is_status_byte(*c)) {
		status = *c;
		c++;

	} else {
		/* No, we use running status then. */
		status = previous_status;
	}

	if (!is_status_byte(status)) {
		g_critical("SMF error: bad status byte (MSB is zero).");
		return -1;
	}

	message_length = expected_message_length(status, c, buffer_length - (c - buf));

	if (message_length < 0)
		return -3;

	event->midi_buffer = malloc(message_length);
	if (event->midi_buffer == NULL) {
		g_critical("Cannot allocate memory in extract_midi_event().");
		return -4;
	}

	event->midi_buffer[0] = status;

	/* Copy the rest of the MIDI event into buffer. */
	for (i = 1; i < message_length; i++, c++) {
		if (c >= buf + buffer_length) {
			g_critical("End of buffer in extract_midi_event().");
			return -5;
		}

		/* Realtime message may occur anywhere, even in the middle of normal MIDI message. */
		if (is_realtime_byte(*c)) {
			if (parse_realtime_event(*c, event->track))
				return -6;

			c++;

			if (c >= buf + buffer_length) {
				g_critical("End of buffer in extract_midi_event().");
				return -7;
			}
		}

		event->midi_buffer[i] = *c;
	}

	*len = c - buf;

	event->midi_buffer_length = message_length;

	return 0;
}

/*
 * Locates, basing on track->next_event_offset, the next event data in track->buffer,
 * interprets it, allocates smf_event_t and fills it properly.  Returns smf_event_t
 * or NULL, if there was an error.  Allocating event means adding it to the track;
 * see smf_event_new().
 */
static smf_event_t *
parse_next_event(smf_track_t *track)
{
	int time = 0, len, buffer_length;
	unsigned char *c, *start;

	smf_event_t *event = smf_event_new(track);

	c = start = (unsigned char *)track->file_buffer + track->next_event_offset;

	assert(track->file_buffer != NULL);
	assert(track->file_buffer_length > 0);
	assert(track->next_event_offset > 0);

	buffer_length = track->file_buffer_length - track->next_event_offset;
	assert(buffer_length > 0);

	/* First, extract time offset from previous event. */
	if (extract_packed_number(c, buffer_length, &time, &len))
		goto error;

	c += len;
	buffer_length -= len;
	event->time_pulses = time;

	if (buffer_length <= 0)
		goto error;

	/* Now, extract the actual event. */
	if (extract_midi_event(c, buffer_length, event, &len, track->last_status))
		goto error;

	c += len;
	buffer_length -= len;
	track->last_status = event->midi_buffer[0];
	track->next_event_offset += c - start;

	return event;

error:
	smf_event_free(event);

	return NULL;
}

/*
 * Takes "len" characters starting in "buf", making sure it does not access past the length of the buffer,
 * and makes ordinary, zero-terminated string from it.  May return NULL if there was any problem.
 */ 
static char *
make_string(const unsigned char *buf, const int buffer_length, int len)
{
	char *str;

	if (len > buffer_length) {
		g_critical("End of buffer in make_string().");

		len = buffer_length;
	}

	str = malloc(len + 1);
	if (str == NULL) {
		g_critical("Cannot allocate memory in make_string().");
		return NULL;
	}

	memcpy(str, buf, len);
	str[len] = '\0';

	return str;
}

/*
 * Returns zero-terminated string extracted from "text events" or NULL, if there was any problem.
 */
char *
smf_string_from_event(const smf_event_t *event)
{
	int string_length, length_length;

	extract_packed_number((void *)&(event->midi_buffer[2]), event->midi_buffer_length - 2, &string_length, &length_length);

	return make_string((void *)(&event->midi_buffer[2] + length_length), event->midi_buffer_length - 2 - length_length, string_length);
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

/*
 * Verify if the next chunk really is MTrk chunk, and if so, initialize some track variables and return 0.
 * Return different value otherwise.
 */
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

	if (!chunk_signature_matches(mtrk, "MTrk")) {
		g_warning("SMF warning: Expected MTrk signature, got %c%c%c%c instead; ignoring this chunk.",
				mtrk->id[0], mtrk->id[1], mtrk->id[2], mtrk->id[3]);
		
		return 2;
	}

	track->file_buffer = mtrk;
	track->file_buffer_length = sizeof(struct chunk_header_struct) + ntohl(mtrk->length);
	track->next_event_offset = sizeof(struct chunk_header_struct);

	return 0;
}

/*
 * Return 1 if event is end-of-the-track, 0 otherwise.
 */
static int
event_is_end_of_track(const smf_event_t *event)
{
	if (event->midi_buffer[0] == 0xFF && event->midi_buffer[1] == 0x2F)
		return 1;

	return 0;
}

/*
 * Returns 1 if MIDI data in the event is valid, 0 otherwise.
 */
static int
event_is_valid(const smf_event_t *event)
{
	assert(event);
	assert(event->midi_buffer);
	assert(event->midi_buffer_length >= 1);
	assert(event->midi_buffer_length == expected_message_length(event->midi_buffer[0],
		&(event->midi_buffer[1]), event->midi_buffer_length - 1));

	return 1;
}

/*
 * Parse events and put it on the track.
 */
static int
parse_mtrk_chunk(smf_track_t *track)
{
	int time = 0;
	smf_event_t *event;

	if (parse_mtrk_header(track))
		return 1;

	for (;;) {
		event = parse_next_event(track);

		if (event == NULL)
			return 1;

		/* Replace "relative" event time with absolute one, i.e. relative to the start of the track. */
		event->time_pulses += time;
		time = event->time_pulses;

		assert(event_is_valid(event));

		if (event_is_end_of_track(event))
			break;
	
#if 0
		print_event(event);
#endif
	}

	track->file_buffer = NULL;
	track->file_buffer_length = 0;
	track->next_event_offset = -1;

	return 0;
}

/*
 * Allocate buffer of proper size and read file contents into it.  Close file afterwards.
 */
static int
load_file_into_buffer(void **file_buffer, int *file_buffer_length, const char *file_name)
{
	FILE *stream = fopen(file_name, "r");

	if (stream == NULL) {
		g_critical("Cannot open input file: %s", strerror(errno));

		return -1;
	}

	if (fseek(stream, 0, SEEK_END)) {
		g_critical("fseek(3) failed: %s", strerror(errno));

		return -2;
	}

	*file_buffer_length = ftell(stream);
	if (*file_buffer_length == -1) {
		g_critical("ftell(3) failed: %s", strerror(errno));

		return -3;
	}

	if (fseek(stream, 0, SEEK_SET)) {
		g_critical("fseek(3) failed: %s", strerror(errno));

		return -4;
	}

	*file_buffer = malloc(*file_buffer_length);
	if (*file_buffer == NULL) {
		g_critical("malloc(3) failed: %s", strerror(errno));

		return -5;
	}

	if (fread(*file_buffer, 1, *file_buffer_length, stream) != *file_buffer_length) {
		g_critical("fread(3) failed: %s", strerror(errno));

		return -6;
	}
	
	if (fclose(stream)) {
		g_critical("fclose(3) failed: %s", strerror(errno));

		return -7;
	}

	return 0;
}

smf_t *
smf_load_from_memory(const void *buffer, const int buffer_length)
{
	int i;

	smf_t *smf = smf_new();

	smf->file_buffer = (void *)buffer;
	smf->file_buffer_length = buffer_length;
	smf->next_chunk_offset = 0;

	if (parse_mthd_chunk(smf))
		return NULL;

	print_mthd(smf);

	for (i = 0; i < smf->number_of_tracks; i++) {
		smf_track_t *track = smf_track_new(smf);

		/* Skip unparseable chunks. */
		if (parse_mtrk_chunk(track)) {
			smf_track_free(track);

			continue;
		}

		track->file_buffer = NULL;
		track->file_buffer_length = 0;
		track->next_event_offset = -1;
	}

	if (smf->last_track_number != smf->number_of_tracks) {
		g_warning("SMF warning: MThd header declared %d tracks, but only %d found; continuing anyway.",
				smf->number_of_tracks, smf->last_track_number);
	}

	smf->file_buffer = NULL;
	smf->file_buffer_length = 0;
	smf->next_chunk_offset = -1;

	return smf;
}

/*
 * Takes a filename, loads it, parses and returns smf or NULL if there was an error.
 */
smf_t *
smf_load(const char *file_name)
{
	int file_buffer_length;
	void *file_buffer;
	smf_t *smf;

	if (load_file_into_buffer(&file_buffer, &file_buffer_length, file_name))
		return NULL;

	smf = smf_load_from_memory(file_buffer, file_buffer_length);

	free(file_buffer);

	return smf;
}

