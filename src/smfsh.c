#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <string.h>
#include <ctype.h>
#include "smf.h"

smf_track_t *selected_track = NULL;
smf_event_t *selected_event = NULL;

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
cmd_save(smf_t *smf, char *file_name)
{
	int ret;

	if (file_name == NULL) {
		g_critical("Please specify file name.");
		return -1;
	}

	ret = smf_save(smf, file_name);
	if (ret) {
		g_critical("Couln't save file.");
		return -1;
	} else {
		g_message("File saved.");
		return 0;
	}
}

int
cmd_ppqn(smf_t *smf, char *new_ppqn)
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
cmd_tracks(smf_t *smf, char *notused)
{
	if (smf->number_of_tracks > 0)
		g_message("There are %d tracks, numbered from 1 to %d.", smf->number_of_tracks, smf->number_of_tracks);
	else
		g_message("There are no tracks.");

	return 0;
}

int
cmd_track(smf_t *smf, char *arg)
{
	int num;

	if (arg == NULL) {
		if (selected_track == NULL)
			g_message("No track currently selected.");
		else
			g_message("Currently selected is track number %d, containing %d events.",
				selected_track->track_number, selected_track->number_of_events);
	} else {
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
cmd_trackadd(smf_t *smf, char *notused)
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
cmd_trackrm(smf_t *smf, char *notused)
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
show_event(smf_event_t *event)
{
	g_message("Time offset from previous event: %d pulses.", event->delta_time_pulses);
	g_message("Time since start of the song: %f seconds.", event->time_seconds);
	g_message("MIDI message length: %d bytes.", event->midi_buffer_length);
	/* XXX: Don't read past the end of the buffer. */
	g_message("First three bytes of MIDI message: 0x%x 0x%x 0x%x",
			event->midi_buffer[0], event->midi_buffer[1], event->midi_buffer[2]);

	return 0;
}

int
cmd_events(smf_t *smf, char *notused)
{
	smf_event_t *event;

	if (selected_track == NULL) {
		g_critical("No track selected - please use 'track [number]' command first.");
		return -1;
	}

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
cmd_event(smf_t *smf, char *arg)
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
cmd_eventadd(smf_t *smf, char *notused)
{
	return -1;
}

int
cmd_eventrm(smf_t *smf, char *notused)
{
	return -1;
}

int
cmd_exit(smf_t *smf, char *notused)
{
	g_debug("Good bye.");
	exit(0);
}

int cmd_help(smf_t *smf, char *notused);

struct command_struct {
	char *name;
	int (*function)(smf_t *smf, char *command);
	char *help;
} commands[] = {{"help", cmd_help, "show this help."},
		{"save", cmd_save, "save to named file."},
		{"ppqn", cmd_ppqn, "show ppqn, or set ppqn if used with parameter."},
		{"tracks", cmd_tracks, "show number of tracks."},
		{"track", cmd_track, "show number of currently selected track, or select a track."},
		{"trackadd", cmd_trackadd, "add a track and select it."},
		{"trackrm", cmd_trackrm, "remove currently selected track."},
		{"events", cmd_events, "show events in the currently selected track."},
		{"event", cmd_event, "show number of currently selected event, or select an event."},
		{"eventadd", cmd_eventadd, "add an event and select it."},
		{"eventrm", cmd_eventrm, "remove currently selected event."},
		{"exit", cmd_exit, "exit to shell."},
		{"quit", cmd_exit, NULL},
		{"bye", cmd_exit, NULL},
		{NULL, NULL, NULL}};

int
cmd_help(smf_t *smf, char *notused)
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
execute_command(smf_t *smf, char *line)
{
	char *command, *args;
	struct command_struct *tmp;

	args = line;
	command = strsep(&args, " ");

	for (tmp = commands; tmp->name != NULL; tmp++) {
		if (strcmp(tmp->name, command) == 0)
			return (tmp->function)(smf, args);
	}

	g_warning("No such command: '%s'.  Type 'help' to see available commands.", command);

	return -1;
}

void
read_and_execute_command(smf_t *smf)
{
	int ret;
	char *command;

	command = read_command();

	ret = execute_command(smf, command);
	if (ret) {
		g_warning("Command finished with error.");
	}
}

int main(int argc, char *argv[])
{
	smf_t *smf;

	if (argc > 2) {
		usage();
	}

	g_log_set_default_handler(log_handler, NULL);

	if (argc == 2) {
		smf = smf_load(argv[1]);
		if (smf == NULL) {
			g_critical("Cannot load SMF file.");
			return -1;
		}
	} else {
		smf = smf_new();
		if (smf == NULL) {
			g_critical("Cannot initialize smf_t.");
			return -1;
		}
	}

	for (;;)
		read_and_execute_command(smf);

	return 0;
}

