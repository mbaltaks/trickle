/*
 * tricklectl.c
 *
 * Copyright (c) 2003 Marius Aamodt Eriksen <marius@monkey.org>
 * All rights reserved.
 *
 * $Id: tricklectl.c,v 1.1 2003/04/15 05:44:50 marius Exp $
 */

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <stdio.h>
#include <err.h>
#include <unistd.h>

#include "message.h"
#include "trickledu.h"

static int trickled_sock;

void usage(void);
void handle_command(int, int, char **);

#define TRICKLED_SOCKNAME "/tmp/.trickled.sock"

#define COMMAND_GETRATES 0
#define COMMAND_LAST 1

static char *commands[] = {
	[COMMAND_GETRATES] = "getrates",
	[COMMAND_LAST]     = NULL
};

int
main(int argc, char **argv)
{
	char *sockname = TRICKLED_SOCKNAME;
	int opt, i;

	while ((opt = getopt(argc, argv, "hs:")) != -1)
                switch (opt) {
		case 's':
			sockname = optarg;
			break;
		case 'h':
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc == 0)
		usage();

	for (i = 0; commands[i] != NULL; i++)
		if (strlen(commands[i]) == strlen(argv[0]) &&
		    strcmp(commands[i], argv[0]) == 0)
			break;

	if (i == COMMAND_LAST)
		usage();

	argc -= 1;
	argv += 1;

	trickled_configure(sockname, &socket, &read, &write, argv[0]);
	trickled_ctl_open(&trickled_sock);

	if (!trickled_sock)
		err(1, sockname);

	handle_command(i, argc, argv);

	return (0);
}

void
handle_command(int cmd, int ac, char **av)
{
	switch (cmd) {
	case COMMAND_GETRATES: {
		uint32_t uplim, uprate, downlim, downrate;
		if (trickled_getinfo(&uplim, &uprate, &downlim, &downrate) == -1)
			err(1, "trickled_getinfo()");
		/* XXX testing downlim, too, etc */
		warnx("DOWNLOAD: %d.%d KB/s (utilization: %.1f%%)",
		    downrate / 1024, (downrate % 1024) * 100 / 1024,
		    ((1.0 * downrate) / (1.0 * downlim)) * 100);
		if (uprate == 0)
			uprate = 1;
		warnx("UPLOAD: %d.%d KB/s (utilization: %.1f%%)",
		    uprate / 1024, (uprate % 1024) * 100 / 1024,
		    ((1.0 * uprate) / (1.0 * uplim)) * 100);
	}
	default:
	}
}

void
usage(void)
{
	exit(1);
}
