#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <string.h>
#include <ctype.h>
#include "smf.h"

smf_track_t *selected_track = NULL;
smf_event_t *selected_event = NULL;
smf_t *smf = NULL;
char *last_file_name = NULL;

void
usage(void)
{
	fprintf(stderr, "usage: smfsh [file]\n");

	exit(EX_USAGE);
}

void
log_handler(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer notused)
{
	fprintf(stderr, "%s: %s\n", log_domain, message);
}

int
cmd_load(char *file_name)
{
	if (file_name == NULL) {
		if (last_file_name == NULL) {
			g_critical("Please specify file name.");
			return -1;
		}

		file_name = last_file_name;
	}

	if (smf != NULL)
		smf_delete(smf);

	selected_track = NULL;
	selected_event = NULL;

	last_file_name = strdup(file_name);
	smf = smf_load(file_name);
	if (smf == NULL) {
		g_critical("Couln't load '%s'.", file_name);

		smf = smf_new();
		if (smf == NULL) {
			g_critical("Cannot initialize smf_t.");
			return -1;
		}

		return -2;
	}

	g_message("File '%s' loaded.", file_name);

	return 0;
}

int
cmd_save(char *file_name)
{
	int ret;

	if (file_name == NULL) {
		if (last_file_name == NULL) {
			g_critical("Please specify file name.");
			return -1;
		}

		file_name = last_file_name;
	}

	if (file_name == NULL) {
		g_critical("Please specify file name.");
		return -1;
	}

	last_file_name = strdup(file_name);
	ret = smf_save(smf, file_name);
	if (ret) {
		g_critical("Couln't save '%s'", file_name);
		return -1;
	}

	g_message("File '%s' saved.", file_name);

	return 0;
}

int
cmd_ppqn(char *new_ppqn)
{
	int tmp;

	if (new_ppqn == NULL) {
		g_message("Pulses Per Quarter Note (aka Division) is %d.", smf->ppqn);
	} else {
		/* XXX: Use strtol. */
		tmp = atoi(new_ppqn);
		if (tmp <= 0) {
			g_critical("Invalid PPQN, valid values are greater than zero.");
			return -1;
		}

		smf->ppqn = tmp;
		g_message("Pulses Per Quarter Note changed to %d.", smf->ppqn);
	}
	
	return 0;
}

int
cmd_format(char *new_format)
{
	int tmp;

	if (new_format == NULL) {
		g_message("Format is %d.", smf->format);
	} else {
		/* XXX: Use strtol. */
		tmp = atoi(new_format);
		if (tmp < 0 || tmp > 2) {
			g_critical("Invalid format value, valid values are in range 0 - 2, inclusive.");
			return -1;
		}

		smf->format = tmp;
		g_message("Forma changed to %d.", smf->format);
	}
	
	return 0;
}


int
cmd_tracks(char *notused)
{
	if (smf->number_of_tracks > 0)
		g_message("There are %d tracks, numbered from 1 to %d.", smf->number_of_tracks, smf->number_of_tracks);
	else
		g_message("There are no tracks.");

	return 0;
}

int
cmd_track(char *arg)
{
	int num;

	if (arg == NULL) {
		if (selected_track == NULL)
			g_message("No track currently selected.");
		else
			g_message("Currently selected is track number %d, containing %d events.",
				selected_track->track_number, selected_track->number_of_events);
	} else {
		if (smf->number_of_tracks == 0) {
			g_message("There are no tracks.");
			return -1;
		}

		num = atoi(arg);
		if (num < 1 || num > smf->number_of_tracks) {
			g_critical("Invalid track number specified; valid choices are 1 - %d.", smf->number_of_tracks);
			return -2;
		}

		selected_track = smf_get_track_by_number(smf, num);
		if (selected_track == NULL) {
			g_critical("smf_get_track_by_number() failed, track not selected.");
			return -3;
		}

		selected_event = NULL;

		g_message("Track number %d selected; it contains %d events.",
				selected_track->track_number, selected_track->number_of_events);
	}

	return 0;
}

int
cmd_trackadd(char *notused)
{
	selected_track = smf_track_new(smf);
	if (selected_track == NULL) {
		g_critical("smf_track_new() failed, track not created.");
		return -1;
	}

	selected_event = NULL;

	g_message("Created new track; track number %d selected.", selected_track->track_number);

	return 0;
}

int
cmd_trackrm(char *notused)
{
	if (selected_track == NULL) {
		g_critical("No track selected - please use 'track [number]' command first.");
		return -1;
	}

	selected_event = NULL;
	smf_track_delete(selected_track);
	selected_track = NULL;

	return 0;
}

int
print_event(smf_event_t *event)
{
	int off = 0;
	char buf[256];

	if (smf_event_is_metadata(event))
		return smf_event_print_metadata(event);

	/* XXX: verify lengths. */
	switch (event->midi_buffer[0] & 0xF0) {
		case 0x80:
			off += snprintf(buf + off, sizeof(buf) - off, "Note Off, channel %d, note %d, velocity %d",
					event->midi_buffer[0] & 0x0F, event->midi_buffer[1], event->midi_buffer[2]);
			break;

		case 0x90:
			off += snprintf(buf + off, sizeof(buf) - off, "Note On, channel %d, note %d, velocity %d",
					event->midi_buffer[0] & 0x0F, event->midi_buffer[1], event->midi_buffer[2]);
			break;

		case 0xA0:
			off += snprintf(buf + off, sizeof(buf) - off, "Aftertouch, channel %d, note %d, pressure %d",
					event->midi_buffer[0] & 0x0F, event->midi_buffer[1], event->midi_buffer[2]);
			break;

		case 0xB0:
			off += snprintf(buf + off, sizeof(buf) - off, "Controller, channel %d, controller %d, value %d",
					event->midi_buffer[0] & 0x0F, event->midi_buffer[1], event->midi_buffer[2]);
			break;

		case 0xC0:
			off += snprintf(buf + off, sizeof(buf) - off, "Program Change, channel %d, controller %d",
					event->midi_buffer[0] & 0x0F, event->midi_buffer[1]);
			break;

		case 0xD0:
			off += snprintf(buf + off, sizeof(buf) - off, "Channel Pressure, channel %d, pressure %d",
					event->midi_buffer[0] & 0x0F, event->midi_buffer[1]);
			break;

		case 0xE0:
			off += snprintf(buf + off, sizeof(buf) - off, "Pitch Wheel, channel %d, value %d",
					event->midi_buffer[0] & 0x0F, ((int)event->midi_buffer[2] << 7) | (int)event->midi_buffer[2]);
			break;

		default:
			return 0;
	}

	g_message("Event: %s", buf);

	return 0;
}

int
show_event(smf_event_t *event)
{
	g_message("Event number %d, time offset from previous event: %d pulses.", event->event_number, event->delta_time_pulses);
	g_message("Time since start of the song: %d pulses, %f seconds.", event->time_pulses, event->time_seconds);

	if (event->midi_buffer_length == 1) {
		g_message("MIDI message: 0x%x", event->midi_buffer[0]);
	} else if (event->midi_buffer_length == 2) {
		g_message("MIDI message: 0x%x 0x%x", event->midi_buffer[0], event->midi_buffer[1]);
	} else if (event->midi_buffer_length == 3) {
		g_message("MIDI message: 0x%x 0x%x 0x%x", event->midi_buffer[0], event->midi_buffer[1], event->midi_buffer[2]);
	} else {
		g_message("Message length is %d bytes; first three bytes are: 0x%x 0x%x 0x%x",
			event->midi_buffer_length, event->midi_buffer[0], event->midi_buffer[1], event->midi_buffer[2]);
	}

	print_event(event);

	return 0;
}

int
cmd_events(char *notused)
{
	smf_event_t *event;

	if (selected_track == NULL) {
		g_critical("No track selected - please use 'track [number]' command first.");
		return -1;
	}

	g_message("List of events in track %d follows:", selected_track->track_number);

	smf_rewind(smf);

	while ((event = smf_get_next_event_from_track(selected_track)) != NULL) {
		g_message("----------------------------------");
		show_event(event);
	}

	g_message("----------------------------------");

	smf_rewind(smf);

	return 0;
}

int
cmd_event(char *arg)
{
	int num;

	if (arg == NULL) {
		if (selected_event == NULL) {
			g_message("No event currently selected.");
		} else {
			g_message("Currently selected is event %d, track %d.", selected_event->event_number, selected_track->track_number);
			show_event(selected_event);
		}
	} else {
		num = atoi(arg);
		if (num < 1 || num > selected_track->number_of_events) {
			g_critical("Invalid event number specified; valid choices are 1 - %d.", selected_track->number_of_events);
			return -1;
		}

		selected_event = smf_get_event_by_number(selected_track, num);
		if (selected_event == NULL) {
			g_critical("smf_get_event_by_number() failed, event not selected.");
			return -2;
		}

		g_message("Event number %d selected.", selected_event->event_number);
		show_event(selected_event);
	}

	return 0;
}

int
decode_hex(char *str, unsigned char **buffer, int *length)
{
	int i, value, midi_buffer_length;
	char buf[3];
	unsigned char *midi_buffer = NULL;
	char *end = NULL;

	if ((strlen(str) % 2) != 0) {
		g_critical("Hex value should have even number of characters, you know.");
		goto error;
	}

	midi_buffer_length = strlen(str) / 2;
	midi_buffer = malloc(midi_buffer_length);
	if (midi_buffer == NULL) {
		g_critical("malloc() failed.");
		goto error;
	}

	for (i = 0; i < midi_buffer_length; i++) {
		buf[0] = str[i * 2];
		buf[1] = str[i * 2 + 1];
		buf[2] = '\0';
		value = strtoll(buf, &end, 16);

		if (end - buf != 2) {
			g_critical("Garbage characters detected after hex.");
			goto error;
		}

		midi_buffer[i] = value;
	}

	*buffer = midi_buffer;
	*length = midi_buffer_length;

	return 0;

error:
	if (midi_buffer != NULL)
		free(midi_buffer);

	return -1;
}

void
eventadd_usage(void)
{
	g_critical("Usage: eventadd delta-time-in-pulses midi-in-hex.");
	g_critical("Example: 'eventadd 1 903C7F' will add Note On event, one pulse from the previous");
	g_critical("one on that particular track, channel 1, note C4, velocity 127.");
}

int
cmd_eventadd(char *str)
{
	int midi_buffer_length, pulses;
	unsigned char *midi_buffer;
	char *time, *endtime;

	if (selected_track == NULL) {
		g_critical("Please select a track first.");
		return -1;
	}

	if (str == NULL) {
		eventadd_usage();
		return -2;
	}

	/* Extract the time. */
	time = strsep(&str, " ");
	pulses = strtol(time, &endtime, 10);
	if (endtime - time != strlen(time)) {
		g_critical("Time is supposed to be a number, without trailing characters.");
		return -3;
	}

	/* Called with one parameter? */
	if (str == NULL) {
		eventadd_usage();
		return -4;
	}

	if (decode_hex(str, &midi_buffer, &midi_buffer_length)) {
		eventadd_usage();
		return -5;
	}

	selected_event = smf_event_new(selected_track);
	if (selected_event == NULL) {
		g_critical("smf_event_new() failed, event not created.");
		return -6;
	}

	selected_event->midi_buffer = midi_buffer;
	selected_event->midi_buffer_length = midi_buffer_length;
	selected_event->delta_time_pulses = pulses;

	if (smf_event_is_valid(selected_event) == 0) {
		g_critical("Event is invalid from the MIDI specification point of view, not created.");
		smf_event_delete(selected_event);
		selected_event = NULL;
		return -7;
	}

	g_message("Event created.");

	return 0;
}

int
cmd_eventaddeot(char *notused)
{
	smf_event_t *event;

	if (selected_track == NULL) {
		g_critical("Please select a track first.");
		return -1;
	}

	event = smf_event_new_with_data(selected_track, 0xFF, 0x2F, 0x00);
	if (event == NULL) {
		g_critical("smf_event_new() failed, event not created.");
		return -2;
	}

	event->delta_time_pulses = 0;

	g_message("Event created.");

	return 0;
}

int
cmd_eventrm(char *notused)
{
	if (selected_event == NULL) {
		g_critical("No event selected - please use 'event [number]' command first.");
		return -1;
	}

	smf_event_delete(selected_event);
	selected_event = NULL;

	g_message("Event removed.");

	return 0;
}

int
cmd_tempo(char *notused)
{
	int i;
	smf_tempo_t *tempo;

	for (i = 0;; i++) {
		tempo = smf_get_tempo_by_number(smf, i);
		if (tempo == NULL)
			break;

		g_message("Tempo #%d: Starts at %d pulses, setting %d microseconds per quarter note.",
			i, tempo->time_pulses, tempo->microseconds_per_quarter_note);
	}

	return 0;
}

int
cmd_exit(char *notused)
{
	g_debug("Good bye.");
	exit(0);
}

int cmd_help(char *notused);

struct command_struct {
	char *name;
	int (*function)(char *command);
	char *help;
} commands[] = {{"help", cmd_help, "show this help."},
		{"load", cmd_load, "load named file."},
		{"save", cmd_save, "save to named file."},
		{"ppqn", cmd_ppqn, "show ppqn (aka division), or set ppqn if used with parameter."},
		{"format", cmd_format, "show format, or set format if used with parameter."},
		{"tracks", cmd_tracks, "show number of tracks."},
		{"track", cmd_track, "show number of currently selected track, or select a track."},
		{"trackadd", cmd_trackadd, "add a track and select it."},
		{"trackrm", cmd_trackrm, "remove currently selected track."},
		{"events", cmd_events, "show events in the currently selected track."},
		{"event", cmd_event, "show number of currently selected event, or select an event."},
		{"eventadd", cmd_eventadd, "add an event and select it."},
		{"add", cmd_eventadd, NULL},
		{"eventaddeot", cmd_eventaddeot, "add an End Of Track event."},
		{"eot", cmd_eventaddeot, NULL},
		{"eventrm", cmd_eventrm, "remove currently selected event."},
		{"tempo", cmd_tempo, "show tempo map."},
		{"exit", cmd_exit, "exit to shell."},
		{"quit", cmd_exit, NULL},
		{"bye", cmd_exit, NULL},
		{NULL, NULL, NULL}};

int
cmd_help(char *notused)
{
	struct command_struct *tmp;

	g_message("Available commands:");

	for (tmp = commands; tmp->name != NULL; tmp++) {
		/* Skip commands with no help string. */
		if (tmp->help == NULL)
			continue;
		g_message("%s: %s", tmp->name, tmp->help);
	}

	return 0;
}

void
strip_unneeded_whitespace(char *str, int len)
{
	char *src, *dest;
	int skip_white = 1;

	for (src = str, dest = str; src < dest + len; src++) {
		if (*src == '\n') {
			*dest = '\0';
			break;
		}

		if (*src == '\0')
			break;

		if (isspace(*src)) {
			if (skip_white)
				continue;

			skip_white = 1;
		} else {
			skip_white = 0;
		}

		*dest = *src;
		dest++;
	}
}

char *
read_command(void)
{
	static char cmd[1024];
	char *buf;
	int len;

	fprintf(stdout, "smfsh> ");
	fflush(stdout);

	buf = fgets(cmd, 1024, stdin);

	if (buf == NULL) {
		fprintf(stdout, "exit\n");
		return "exit";
	}

	strip_unneeded_whitespace(buf, 1024);

	len = strlen(buf);

	if (len == 0)
		return read_command();

	return buf;
}

int
execute_command(char *line)
{
	char *command, *args;
	struct command_struct *tmp;

	args = line;
	command = strsep(&args, " ");

	for (tmp = commands; tmp->name != NULL; tmp++) {
		if (strcmp(tmp->name, command) == 0)
			return (tmp->function)(args);
	}

	g_warning("No such command: '%s'.  Type 'help' to see available commands.", command);

	return -1;
}

void
read_and_execute_command(void)
{
	int ret;
	char *command;

	command = read_command();

	ret = execute_command(command);
	if (ret) {
		g_warning("Command finished with error.");
	}
}

int main(int argc, char *argv[])
{
	if (argc > 2) {
		usage();
	}

	g_log_set_default_handler(log_handler, NULL);

	smf = smf_new();
	if (smf == NULL) {
		g_critical("Cannot initialize smf_t.");
		return -1;
	}

	if (argc == 2) {
		last_file_name = argv[1];
		cmd_load(last_file_name);
		cmd_track("1");
	} else {
		cmd_trackadd(NULL);
	}

	for (;;)
		read_and_execute_command();

	return 0;
}

