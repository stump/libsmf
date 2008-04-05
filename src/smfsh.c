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
		smf_free(smf);

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
	if (new_ppqn == NULL) {
		g_message("Pulses Per Quarter Note is %d.", smf->ppqn);
	} else {
		/* XXX: Use strtol. */
		smf->ppqn = atoi(new_ppqn);
		g_message("Pulses Per Quarter Note changed to %d.", smf->ppqn);
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
			return -1;
		}

		selected_track = smf_get_track_by_number(smf, num);
		if (selected_track == NULL) {
			g_critical("smf_get_track_by_number() failed, track not selected.");
			return -2;
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
	smf_track_free(selected_track);
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
	g_message("Time offset from previous event: %d pulses.", event->delta_time_pulses);
	g_message("Time since start of the song: %f seconds.", event->time_seconds);
	g_message("MIDI message length: %d bytes.", event->midi_buffer_length);
	/* XXX: Don't read past the end of the buffer. */
	g_message("First three bytes of MIDI message: 0x%x 0x%x 0x%x",
			event->midi_buffer[0], event->midi_buffer[1], event->midi_buffer[2]);
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
		if (selected_event == NULL)
			g_message("No event currently selected.");
		else
			g_message("Currently selected is event %d, track %d.", -1, selected_track->track_number);
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

		g_message("Event number %d selected.", -1);
	}

	return 0;
}

int
decode_hex(char *str, unsigned char **buffer, int *length)
{
	int i, value, midi_buffer_length;
	char buf[2];
	unsigned char *midi_buffer;
	char *end;

	midi_buffer_length = strlen(str);
	midi_buffer = malloc(midi_buffer_length);
	if (midi_buffer == NULL) {
		g_critical("malloc() failed.");
		goto error;
	}

	for (i = 0; i < midi_buffer_length; i++) {
		buf[0] = str[i];
		buf[1] = '\0';
		value = strtoll(buf, &end, 16);

		if (end - buf != 1) {
			g_critical("Garbage characters detected after hex");
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

int
cmd_eventadd(char *str)
{
	int midi_buffer_length;
	unsigned char *midi_buffer;

	if (selected_track == NULL) {
		g_critical("Please select a track first.");
		return -1;
	}

	if (decode_hex(str, &midi_buffer, &midi_buffer_length)) {
		g_critical("UR DOIN IT WRONG.");
		return -2;
	}

	selected_event = smf_event_new(selected_track);
	if (selected_event == NULL) {
		g_critical("smf_event_new() failed, event not created.");
		return -2;
	}

	selected_event->midi_buffer = midi_buffer;
	selected_event->midi_buffer_length = midi_buffer_length;

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

	smf_event_free(selected_event);
	selected_event = NULL;

	g_message("Event removed.");

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
		{"ppqn", cmd_ppqn, "show ppqn, or set ppqn if used with parameter."},
		{"tracks", cmd_tracks, "show number of tracks."},
		{"track", cmd_track, "show number of currently selected track, or select a track."},
		{"trackadd", cmd_trackadd, "add a track and select it."},
		{"trackrm", cmd_trackrm, "remove currently selected track."},
		{"events", cmd_events, "show events in the currently selected track."},
		{"event", cmd_event, "show number of currently selected event, or select an event."},
		{"eventadd", cmd_eventadd, "add an event and select it."},
		{"eventaddeot", cmd_eventaddeot, "add an End Of Track event."},
		{"eot", cmd_eventaddeot, NULL},
		{"eventrm", cmd_eventrm, "remove currently selected event."},
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
	}

	for (;;)
		read_and_execute_command();

	return 0;
}

