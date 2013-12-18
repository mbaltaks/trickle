/*
 * client.c
 *
 * Copyright (c) 2003 Marius Aamodt Eriksen <marius@monkey.org>
 * All rights reserved.
 *
 * $Id: client.c,v 1.7 2003/03/06 05:49:36 marius Exp $
 */

#include <sys/types.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <sys/queue.h>
#include <sys/tree.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif /* HAVE_SYS_TIME_H */

#include <event.h>
#ifdef HAVE_ERR_H
#include <err.h>
#endif /* HAVE_ERR_H */
#include <stdio.h>
#include <unistd.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif /* HAVE_STDINT_H */
#if defined(HAVE_TIME_H) && defined(TIME_WITH_SYS_TIME)
#include <time.h>
#endif /* defined(HAVE_TIME_H) && defined(TIME_WITH_SYS_TIME) */
#include <string.h>

#include "print.h"
#include "trickle.h"
#include "message.h"
#include "client.h"
#include "bwstat.h"
#include "util.h"

static int
clicmp(struct client *a, struct client *b)
{
	if (a->pid == b->pid)
		return (0);

	if (a->pid > b->pid)
		return (1);

	return (-1);
}

SPLAY_HEAD(clitree, client) clients;
SPLAY_PROTOTYPE(clitree, client, next, clicmp);
SPLAY_GENERATE(clitree, client, next, clicmp);

static void client_delaycb(int, short, void *);

void
client_init(void)
{	
	/* 512 kB windows.  XXX Make this configurable. */
	bwstat_init(50 * 1024);

	SPLAY_INIT(&clients);
}

int
client_register(struct client *cli)
{
	if ((cli->stat = bwstat_new()) == NULL)
		return (-1);

	SPLAY_INSERT(clitree, &clients, cli);

	return (0);
}

int
client_configure(struct client *cli)
{
	struct bwstat *bs = cli->stat;

	if (cli->pri > 20)
		return (-1);
	bs->pts = 21 - cli->pri;	

	if (cli->tsmooth < 0.0)
		return (-1);
	bs->tsmooth = cli->tsmooth;

	if (cli->lsmooth == 0)
		return (-1);
	bs->lsmooth = cli->lsmooth;

	return (0);
}

void
client_unregister(struct client *cli)
{
	if (evtimer_initialized(&cli->delayev))
		evtimer_del(&cli->delayev);

	SPLAY_REMOVE(clitree, &clients, cli);
}

void
client_delay(struct client *cli, short which, int len, uint lim)
{
	struct timeval *tv;

	if ((tv = bwstat_getdelay(cli->stat, &len, lim, which)) != NULL) {
		warnxv(4, "Delay %d (%s/%s) by: %d bytes in %dsec%dusec START",
		    cli->pid, cli->argv0, cli->uname, len, tv->tv_sec,
		    tv->tv_usec);
		cli->delaytv = *tv;
		cli->delaylen = len;
		cli->delaywhich = which;
		evtimer_set(&cli->delayev, client_delaycb, cli);
		evtimer_add(&cli->delayev, &cli->delaytv);
	} else {
		struct msg msg;
		struct msg_delayinfo *delayinfo = &msg.data.delayinfo;

		msg.type = MSGTYPE_CONT;
		delayinfo->len = len;
		if (client_sendmsg(cli, &msg) == -1)
			;	/* XXX delete client */
	}
}

void
client_getdelay(struct client *cli, short which, int len, uint lim)
{
	struct timeval *tv;
	struct msg msg;
	struct msg_delayinfo *delayinfo = &msg.data.delayinfo;

	memset(&msg, 0, sizeof(msg));

	if ((tv = bwstat_getdelay(cli->stat, &len, lim, which)) != NULL)
		delayinfo->delaytv = *tv;
	else
		SET(msg.status, MSGSTATUS_FAIL);

	delayinfo->len = len;
	tv = &delayinfo->delaytv;

	warnxv(3, "Returning delay %d (%s/%s) info: %d bytes in %dsec%dusec",
	    cli->pid, cli->argv0, cli->uname, len, tv->tv_sec, tv->tv_usec);

	msg.type = MSGTYPE_DELAYINFO;

	client_sendmsg(cli, &msg);
}

static void
client_delaycb(int fd, short which, void *arg)
{
	struct client *cli = arg;
	struct msg msg;
	struct msg_delayinfo *delayinfo = &msg.data.delayinfo;

	warnxv(4, "Delay %d (%s/%s) by %dsec%dusec END", cli->pid,
	    cli->argv0, cli->uname, cli->delaytv.tv_sec, cli->delaytv.tv_usec);

	delayinfo->len = cli->delaylen;

	msg.type = MSGTYPE_CONT;

	/* XXX on error */
	client_sendmsg(cli, &msg);
}

void
client_update(struct client *cli, short which, int len)
{
	struct bwstatdata *bsd = &cli->stat->data[which];

	warnxv(4, "Statistics (%s) for %d (%s/%s):",
	    which == TRICKLEDIR_SEND ? "SEND" : "RECV",
	    cli->pid, cli->argv0, cli->uname);

	bwstat_update(cli->stat, len, which);

	warnxv(4, "\tavg: %d.%d KB/s; win: %d.%d KB/s", 
	    bsd->rate / 1024, (bsd->rate % 1024) * 100 / 1024,
	    bsd->winrate / 1024, (bsd->winrate % 1024) * 100 / 1024);
}

void
client_printrates(void)
{
	struct bwstat *bs = bwstat_gettot();
	struct bwstatdata *bsdrecv = &bs->data[TRICKLEDIR_RECV],
	    *bsdsend = &bs->data[TRICKLEDIR_SEND], *bsd;

	bsd = bsdsend;

	warnxv(2, "UPLOAD total:\n"
	    "\tavg: %d.%d KB/s; win: %d.%d KB/s", 
	    bsd->rate / 1024, (bsd->rate % 1024) * 100 / 1024,
	    bsd->winrate / 1024, (bsd->winrate % 1024) * 100 / 1024);

	bsd = bsdrecv;

	warnxv(2, "DOWNLOAD total:\n"
	    "\tavg: %d.%d KB/s; win: %d.%d KB/s", 
	    bsd->rate / 1024, (bsd->rate % 1024) * 100 / 1024,
	    bsd->winrate / 1024, (bsd->winrate % 1024) * 100 / 1024);
}

int
client_sendmsg(struct client *cli, struct msg *msg)
{
	return (atomicio(write, cli->s, msg, sizeof(*msg)) == sizeof(*msg) ?
	    0 : -1);
}

int
client_recvmsg(struct client *cli, struct msg *msg)
{
	return (atomicio(read, cli->s, msg, sizeof(*msg)) == sizeof(*msg) ?
	    0 : -1);
}