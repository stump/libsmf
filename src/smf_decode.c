/*
 * This is Standard MIDI File format implementation, event decoding routines.
 *
 * For questions and comments, contact Edward Tomasz Napierala <trasz@FreeBSD.org>.
 * This code is public domain, you can do with it whatever you want.
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <errno.h>
#include <arpa/inet.h>
#include "smf.h"
#include "smf_private.h"

#define BUFFER_SIZE 1024

/**
 * \return Nonzero if event is metaevent.  You should never send metaevents;
 * they are not really MIDI messages.  They carry information like track title,
 * time signature etc.
 */
int
smf_event_is_metadata(const smf_event_t *event)
{
	assert(event->midi_buffer);
	assert(event->midi_buffer_length > 0);
	
	if (event->midi_buffer[0] == 0xFF)
		return 1;

	return 0;
}

/**
 * \return Nonzero if event is system realtime.
 */
int
smf_event_is_system_realtime(const smf_event_t *event)
{
	assert(event->midi_buffer);
	assert(event->midi_buffer_length > 0);

	if (smf_event_is_metadata(event))
		return 0;
	
	if (event->midi_buffer[0] >= 0xF8)
		return 1;

	return 0;
}

/**
 * \return Nonzero if event is system common.
 */
int
smf_event_is_system_common(const smf_event_t *event)
{
	assert(event->midi_buffer);
	assert(event->midi_buffer_length > 0);

	if (event->midi_buffer[0] >= 0xF0 && event->midi_buffer[0] <= 0xF7)
		return 1;

	return 0;
}
/**
  * \return Nonzero if event is SysEx message.
  */
int
smf_event_is_sysex(const smf_event_t *event)
{
	assert(event->midi_buffer);
	assert(event->midi_buffer_length > 0);
	
	if (event->midi_buffer[0] == 0xF0)
		return 1;

	return 0;
}

static char *
smf_event_decode_textual(const smf_event_t *event, const char *name)
{
	int off = 0;
	char *buf, *extracted;

	buf = malloc(BUFFER_SIZE);
	if (buf == NULL) {
		g_critical("smf_event_decode_textual: malloc failed.");
		return NULL;
	}

	extracted = smf_string_from_event(event);
	if (extracted == NULL) {
		free(buf);
		return NULL;
	}

	snprintf(buf + off, BUFFER_SIZE - off, "%s: %s", name, extracted);

	return buf;
}

static char *
smf_event_decode_metadata(const smf_event_t *event)
{
	int off = 0;
	char *buf;

	assert(smf_event_is_metadata(event));

	switch (event->midi_buffer[1]) {
		case 0x01:
			return smf_event_decode_textual(event, "Text");

		case 0x02:
			return smf_event_decode_textual(event, "Copyright");

		case 0x03:
			return smf_event_decode_textual(event, "Sequence/Track Name");

		case 0x04:
			return smf_event_decode_textual(event, "Instrument");

		case 0x05:
			return smf_event_decode_textual(event, "Lyric");

		case 0x06:
			return smf_event_decode_textual(event, "Marker");

		case 0x07:
			return smf_event_decode_textual(event, "Cue Point");

		case 0x08:
			return smf_event_decode_textual(event, "Program Name");

		case 0x09:
			return smf_event_decode_textual(event, "Device (Port) Name");

		default:
			break;
	}

	buf = malloc(BUFFER_SIZE);
	if (buf == NULL) {
		g_critical("smf_event_decode_metadata: malloc failed.");
		return NULL;
	}

	switch (event->midi_buffer[1]) {
		case 0x00:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Sequence number");
			break;

		/* http://music.columbia.edu/pipermail/music-dsp/2004-August/061196.html */
		case 0x20:
			if (event->midi_buffer_length < 4) {
				g_critical("smf_event_decode_metadata: truncated MIDI message.");
				goto error;
			}

			off += snprintf(buf + off, BUFFER_SIZE - off, "Channel Prefix: %d.", event->midi_buffer[3]);
			break;

		case 0x21:
			if (event->midi_buffer_length < 4) {
				g_critical("smf_event_decode_metadata: truncated MIDI message.");
				goto error;
			}

			off += snprintf(buf + off, BUFFER_SIZE - off, "Midi Port: %d.", event->midi_buffer[3]);
			break;

		case 0x2F:
			off += snprintf(buf + off, BUFFER_SIZE - off, "End Of Track");
			break;

		case 0x51:
			if (event->midi_buffer_length < 6) {
				g_critical("smf_event_decode_metadata: truncated MIDI message.");
				goto error;
			}

			off += snprintf(buf + off, BUFFER_SIZE - off, "Tempo: %d microseconds per quarter note",
				(event->midi_buffer[3] << 16) + (event->midi_buffer[4] << 8) + event->midi_buffer[5]);
			break;

		case 0x54:
			off += snprintf(buf + off, BUFFER_SIZE - off, "SMPTE Offset");
			break;

		case 0x58:
			if (event->midi_buffer_length < 7) {
				g_critical("smf_event_decode_metadata: truncated MIDI message.");
				goto error;
			}

			off += snprintf(buf + off, BUFFER_SIZE - off,
				"Time Signature: %d/%d, %d clocks per click, %d notated 32nd notes per quarter note",
				event->midi_buffer[3], (int)pow(2, event->midi_buffer[4]), event->midi_buffer[5],
				event->midi_buffer[6]);
			break;

		case 0x59:
			if (event->midi_buffer_length < 5) {
				g_critical("smf_event_decode_metadata: truncated MIDI message.");
				goto error;
			}

			off += snprintf(buf + off, BUFFER_SIZE - off, "Key Signature, %d", abs(event->midi_buffer[3]));
			if (event->midi_buffer[3] == 0)
				off += snprintf(buf + off, BUFFER_SIZE - off, " flat");
			else if ((int)(signed char)(event->midi_buffer[3]) >= 0)
				off += snprintf(buf + off, BUFFER_SIZE - off, " sharp");
			else
				off += snprintf(buf + off, BUFFER_SIZE - off, " flat");

			off += snprintf(buf + off, BUFFER_SIZE - off, ", %s", event->midi_buffer[4] == 0 ? "major" : "minor");

			break;

		case 0x7F:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Proprietary (aka Sequencer) Event, length %d",
				event->midi_buffer_length);
			break;

		default:
			goto error;
	}

	return buf;

error:
	free(buf);

	return NULL;
}

static char *
smf_event_decode_system_realtime(const smf_event_t *event)
{
	int off = 0;
	char *buf;

	assert(smf_event_is_system_realtime(event));

	if (event->midi_buffer_length != 1) {
		g_critical("smf_event_decode_system_realtime: event length is not 1.");
		return NULL;
	}

	buf = malloc(BUFFER_SIZE);
	if (buf == NULL) {
		g_critical("smf_event_decode_system_realtime: malloc failed.");
		return NULL;
	}

	switch (event->midi_buffer[0]) {
		case 0xF8:
			off += snprintf(buf + off, BUFFER_SIZE - off, "MIDI Clock (realtime)");
			break;

		case 0xF9:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Tick (realtime)");
			break;

		case 0xFA:
			off += snprintf(buf + off, BUFFER_SIZE - off, "MIDI Start (realtime)");
			break;

		case 0xFB:
			off += snprintf(buf + off, BUFFER_SIZE - off, "MIDI Continue (realtime)");
			break;

		case 0xFC:
			off += snprintf(buf + off, BUFFER_SIZE - off, "MIDI Stop (realtime)");
			break;

		case 0xFE:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Active Sense (realtime)");
			break;

		default:
			free(buf);
			return NULL;
	}

	return buf;
}

static char *
smf_event_decode_sysex(const smf_event_t *event)
{
	int off = 0;
	char *buf, manufacturer, subid, subid2;

	assert(smf_event_is_sysex(event));

	if (event->midi_buffer_length < 5) {
		g_critical("smf_event_decode_sysex: truncated MIDI message.");
		return NULL;
	}

	buf = malloc(BUFFER_SIZE);
	if (buf == NULL) {
		g_critical("smf_event_decode_sysex: malloc failed.");
		return NULL;
	}

	manufacturer = event->midi_buffer[1];

	if (manufacturer == 0x7F) {
		off += snprintf(buf + off, BUFFER_SIZE - off, "SysEx, realtime, channel %d", event->midi_buffer[2]);
	} else if (manufacturer == 0x7E) {
		off += snprintf(buf + off, BUFFER_SIZE - off, "SysEx, non-realtime, channel %d", event->midi_buffer[2]);
	} else {
		off += snprintf(buf + off, BUFFER_SIZE - off, "SysEx, manufacturer 0x%x", manufacturer);

		return buf;
	}

	subid = event->midi_buffer[3];
	subid2 = event->midi_buffer[4];

	if (subid == 0x01)
		off += snprintf(buf + off, BUFFER_SIZE - off, ", Sample Dump Header");

	else if (subid == 0x02)
		off += snprintf(buf + off, BUFFER_SIZE - off, ", Sample Dump Data Packet");

	else if (subid == 0x03)
		off += snprintf(buf + off, BUFFER_SIZE - off, ", Sample Dump Request");

	else if (subid == 0x04 && subid2 == 0x01)
		off += snprintf(buf + off, BUFFER_SIZE - off, ", Master Volume");

	else if (subid == 0x05 && subid2 == 0x01)
		off += snprintf(buf + off, BUFFER_SIZE - off, ", Sample Dump Loop Point Retransmit");

	else if (subid == 0x05 && subid2 == 0x02)
		off += snprintf(buf + off, BUFFER_SIZE - off, ", Sample Dump Loop Point Request");

	else if (subid == 0x06 && subid2 == 0x01)
		off += snprintf(buf + off, BUFFER_SIZE - off, ", Identity Request");

	else if (subid == 0x06 && subid2 == 0x02)
		off += snprintf(buf + off, BUFFER_SIZE - off, ", Identity Reply");

	else if (subid == 0x08 && subid2 == 0x00)
		off += snprintf(buf + off, BUFFER_SIZE - off, ", Bulk Tuning Dump Request");

	else if (subid == 0x08 && subid2 == 0x01)
		off += snprintf(buf + off, BUFFER_SIZE - off, ", Bulk Tuning Dump");

	else if (subid == 0x08 && subid2 == 0x02)
		off += snprintf(buf + off, BUFFER_SIZE - off, ", Single Note Tuning Change");

	else if (subid == 0x08 && subid2 == 0x03)
		off += snprintf(buf + off, BUFFER_SIZE - off, ", Bulk Tuning Dump Request (Bank)");

	else if (subid == 0x08 && subid2 == 0x04)
		off += snprintf(buf + off, BUFFER_SIZE - off, ", Key Based Tuning Dump");

	else if (subid == 0x08 && subid2 == 0x05)
		off += snprintf(buf + off, BUFFER_SIZE - off, ", Scale/Octave Tuning Dump, 1 byte format");

	else if (subid == 0x08 && subid2 == 0x06)
		off += snprintf(buf + off, BUFFER_SIZE - off, ", Scale/Octave Tuning Dump, 2 byte format");

	else if (subid == 0x08 && subid2 == 0x07)
		off += snprintf(buf + off, BUFFER_SIZE - off, ", Single Note Tuning Change (Bank)");

	else if (subid == 0x09)
		off += snprintf(buf + off, BUFFER_SIZE - off, ", General Midi %s", subid2 == 0 ? "disable" : "enable");

	else if (subid == 0x7C)
		off += snprintf(buf + off, BUFFER_SIZE - off, ", Sample Dump Wait");

	else if (subid == 0x7D)
		off += snprintf(buf + off, BUFFER_SIZE - off, ", Sample Dump Cancel");

	else if (subid == 0x7E)
		off += snprintf(buf + off, BUFFER_SIZE - off, ", Sample Dump NAK");

	else if (subid == 0x7F)
		off += snprintf(buf + off, BUFFER_SIZE - off, ", Sample Dump ACK");

	else
		off += snprintf(buf + off, BUFFER_SIZE - off, ", Unknown");

	return buf;
}

static char *
smf_event_decode_system_common(const smf_event_t *event)
{
	int off = 0;
	char *buf;

	assert(smf_event_is_system_common(event));

	if (smf_event_is_sysex(event))
		return smf_event_decode_sysex(event);

	buf = malloc(BUFFER_SIZE);
	if (buf == NULL) {
		g_critical("smf_event_decode_system_realtime: malloc failed.");
		return NULL;
	}

	switch (event->midi_buffer[0]) {
		case 0xF1:
			off += snprintf(buf + off, BUFFER_SIZE - off, "MTC Quarter Frame");
			break;

		case 0xF2:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Song Position Pointer");
			break;

		case 0xF3:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Song Select");
			break;

		case 0xF6:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Tune Request");
			break;

		default:
			free(buf);
			return NULL;
	}

	return buf;
}

static void
note_from_int(char *buf, int note_number)
{
	int note, octave;
	char *names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

	octave = note_number / 12 - 1;
	note = note_number % 12;

	sprintf(buf, "%s%d", names[note], octave);
}

/**
 * \return Textual representation of the event given, or NULL, if event is unknown.
 */
char *
smf_event_decode(const smf_event_t *event)
{
	int off = 0;
	char *buf, note[5];

	if (smf_event_is_metadata(event))
		return smf_event_decode_metadata(event);

	if (smf_event_is_system_realtime(event))
		return smf_event_decode_system_realtime(event);

	if (smf_event_is_system_common(event))
		return smf_event_decode_system_common(event);

	if (!smf_event_length_is_valid(event)) {
		g_critical("smf_event_decode: incorrect MIDI message length.");
		return NULL;
	}

	buf = malloc(BUFFER_SIZE);
	if (buf == NULL) {
		g_critical("smf_event_decode: malloc failed.");
		return NULL;
	}

	switch (event->midi_buffer[0] & 0xF0) {
		case 0x80:
			note_from_int(note, event->midi_buffer[1]);
			off += snprintf(buf + off, BUFFER_SIZE - off, "Note Off, channel %d, note %s, velocity %d",
					event->midi_buffer[0] & 0x0F, note, event->midi_buffer[2]);
			break;

		case 0x90:
			note_from_int(note, event->midi_buffer[1]);
			off += snprintf(buf + off, BUFFER_SIZE - off, "Note On, channel %d, note %s, velocity %d",
					event->midi_buffer[0] & 0x0F, note, event->midi_buffer[2]);
			break;

		case 0xA0:
			note_from_int(note, event->midi_buffer[1]);
			off += snprintf(buf + off, BUFFER_SIZE - off, "Aftertouch, channel %d, note %s, pressure %d",
					event->midi_buffer[0] & 0x0F, note, event->midi_buffer[2]);
			break;

		case 0xB0:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Controller, channel %d, controller %d, value %d",
					event->midi_buffer[0] & 0x0F, event->midi_buffer[1], event->midi_buffer[2]);
			break;

		case 0xC0:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Program Change, channel %d, controller %d",
					event->midi_buffer[0] & 0x0F, event->midi_buffer[1]);
			break;

		case 0xD0:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Channel Pressure, channel %d, pressure %d",
					event->midi_buffer[0] & 0x0F, event->midi_buffer[1]);
			break;

		case 0xE0:
			off += snprintf(buf + off, BUFFER_SIZE - off, "Pitch Wheel, channel %d, value %d",
					event->midi_buffer[0] & 0x0F, ((int)event->midi_buffer[2] << 7) | (int)event->midi_buffer[2]);
			break;

		default:
			free(buf);
			return NULL;
	}

	return buf;
}


