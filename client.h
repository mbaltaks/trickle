/*
 * client.h
 *
 * Copyright (c) 2003 Marius Aamodt Eriksen <marius@monkey.org>
 * All rights reserved.
 *
 * $Id: client.h,v 1.3 2003/03/05 08:06:39 marius Exp $
 */

#ifndef TRICKLE_CLIENT_H
#define TRICKLE_CLIENT_H

#define CLIENT_CONFIGURED 0x01
#define CLIENT_ONQUEUE    0x02

struct client {
	SPLAY_ENTRY(client)  next;
	int                  s;
	struct event         ev;
	int                  flags;

	pid_t                pid;
	char                 argv0[256];
	uid_t                uid;
	char                 uname[256];
	gid_t                gid;
	char                 gname[256];

	struct timeval       starttv;

	uint                 lim[2];
	uint                 pri;
	struct bwstat       *stat;
	uint                 lsmooth;
	double               tsmooth;

	struct timeval       delaytv;
	struct event         delayev;
	int                  delaylen;
	short                delaywhich;

	TAILQ_ENTRY(client)  nextq;
	TAILQ_ENTRY(client)  nextp;
};

void            client_init(void);
int             client_register(struct client *);
int             client_configure(struct client *);
void            client_unregister(struct client *);
void            client_delay(struct client *, short, int, uint);
void            client_getdelay(struct client *, short, int, uint);
void            client_update(struct client *, short, int);
int             client_sendmsg(struct client *, struct msg *);
int             client_recvmsg(struct client *, struct msg *);
void            client_printrates(void);

#endif /* TRICKLE_CLIENT_H */
