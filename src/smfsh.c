#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <string.h>
#include "smf.h"

smf_track_t *current_track;

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
cmd_help(smf_t *smf, char *notused)
{
	g_message("Available commands (arguments in square brackets are optional):");
	g_message("help                 - show this help.");
	g_message("save file_name       - save to a named file.");
	g_message("ppqn [pulses]        - show ppqn, or set ppqn if used with parameter.");
	g_message("tracks               - show number of tracks.");
	g_message("track [track_number] - show number of currently selected track, or select a track.");
	g_message("trackadd             - add another track.");
	g_message("trackrm track_number - remove given track.");
	g_message("events               - shows events on currently selected track.");
	g_message("exit                 - exit to shell.");

	return 0;
}

int
cmd_save(smf_t *smf, char *file_name)
{
	if (strlen(file_name) == 0) {
		g_critical("Please specify file name.");
		return -1;
	}

	return smf_save(smf, file_name);
}

int
cmd_ppqn(smf_t *smf, char *new_ppqn)
{
	if (strlen(new_ppqn) == 0) {
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
	g_message("There are %d tracks, numbered from 1 to %d.", smf->number_of_tracks, smf->number_of_tracks);

	return 0;
}

int
cmd_track(smf_t *smf, char *number)
{
	if (strlen(number) == 0) {
		g_message("Currently selected track is number %d.", current_track->track_number);
	} else {
		/* XXX */
	}

	return 0;
}

int
cmd_exit(smf_t *smf, char *notused)
{
	g_debug("Good bye.");
	exit(0);
}

struct command_struct {
	char *name;
	int (*function)(smf_t *smf, char *command);
} commands[] = {{"help", cmd_help},
		{"save", cmd_save},
		{"ppqn", cmd_ppqn},
		{"tracks", cmd_tracks},
		{"track", cmd_track},
		{"exit", cmd_exit},
		{"quit", cmd_exit},
		{"bye", cmd_exit},
		{NULL, NULL}};

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

	len = strlen(buf);

	if (len == 1)
		return read_command();

	if (buf[len - 1] == '\n')
		buf[len - 1] = '\0';

	return buf;
}

int
execute_command(smf_t *smf, char *command)
{
	struct command_struct *tmp;

	for (tmp = commands; tmp->name; tmp++) {
		if (strcmp(tmp->name, command) == 0)
			return (tmp->function)(smf, command);
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

