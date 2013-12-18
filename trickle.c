/*
 * trickle.c
 *
 * Copyright (c) 2002, 2003 Marius Aamodt Eriksen <marius@monkey.org>
 * All rights reserved.
 *
 * $Id: trickle.c,v 1.13 2003/03/06 05:49:36 marius Exp $
 */

#include <sys/types.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <sys/param.h>
#include <sys/stat.h>

#ifdef HAVE_ERR_H
#include <err.h>
#endif /* HAVE_ERR_H */
#include <errno.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "util.h"

size_t strlcat(char *, const char *, size_t);
void   usage(void);

#ifdef HAVE___PROGNAME
extern char *__progname;
#else
char *__progname;
#endif

int
main(int argc, char **argv)
{
	char *winsz = "512", verbosestr[16],
	    *uplim = "10", *downlim = "10";
	int opt, verbose = 0;
	char path[MAXPATHLEN + sizeof("/trickle-overload.so") - 1],
	    sockname[MAXPATHLEN];
	struct stat sb;

	__progname = get_progname(argv[0]);
	sockname[0] = '\0';

	while ((opt = getopt(argc, argv, "hvVw:n:u:d:")) != -1)
                switch (opt) {
		case 'v':
			verbose++;
			break;
		case 'w':
			winsz = optarg;
			break;
		case 'u':
			uplim = optarg;
			break;
		case 'd':
			downlim = optarg;
			break;
		case 'V':
			errx(1, "version " VERSION);
			break;
		case 'n':
			strlcpy(sockname, optarg, sizeof(sockname));
			break;
                case 'h':
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc == 0)
		usage();

	if (sockname[0] == '\0')
		strlcpy(sockname, "/tmp/.trickled.sock", sizeof(sockname));

	if (stat(sockname, &sb) == -1 && (errno == EACCES || errno == ENOENT))
		err(1, "Socket %s denied", sockname);

	snprintf(verbosestr, sizeof(verbosestr), "%d", verbose);

	strlcpy(path, LIBDIR, sizeof(path));
	strlcat(path, "/trickle-overload.so", sizeof(path));

	setenv("TRICKLE_DOWNLOAD_LIMIT", downlim, 1);
	setenv("TRICKLE_UPLOAD_LIMIT", uplim, 1);
	setenv("TRICKLE_VERBOSE", verbosestr, 1);
	setenv("TRICKLE_WINDOW_SIZE", winsz, 1);
	setenv("TRICKLE_ARGV", argv[0], 1);
	setenv("TRICKLE_SOCKNAME", sockname, 1);

	setenv("LD_PRELOAD", path, 1);

	execvp(argv[0], argv);
	err(1, "exec()");

	/* NOTREACHED */
	return (1);
}

void
usage(void)
{
	fprintf(stderr,
	    "Usage: %s: [-hv] [-u <rate>] [-d <rate>] [-w <size>] command ...\n"
	    "\t-h  Help (this)\n"
	    "\t-v  Increase the verbosity level\n"
	    "\t-V  Display version\n"
	    "\t-u  Limit upload bandwith usage to <rate> KBps\n"
	    "\t-d  Limit download bandwith usage to <rate> KBps\n"
	    "\t-w  Set window size to <size> KB \n"
	    "\t-n  Use trickled socket name <path>\n",
	    __progname);

	exit(1);
}
