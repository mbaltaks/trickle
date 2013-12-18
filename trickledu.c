/*
 * trickledu.c
 *
 * Copyright (c) 2003 Marius Aamodt Eriksen <marius@monkey.org>
 * All rights reserved.
 *
 * $Id: trickledu.c,v 1.13 2003/04/15 05:44:54 marius Exp $
 */

#include <sys/types.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <sys/socket.h>
#include <sys/un.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif /* HAVE_SYS_TIME_H */

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif /* HAVE_STDINT_H */
#if defined(HAVE_TIME_H) && defined(TIME_WITH_SYS_TIME)
#include <time.h>
#endif /* defined(HAVE_TIME_H) && defined(TIME_WITH_SYS_TIME) */

#include "trickle.h"
#include "message.h"
#include "trickledu.h"
#include "util.h"

static void _trickled_open(struct msg *, int *);

#define DECLARE(name, ret, args) static ret (*libc_##name) args

DECLARE(socket, int, (int, int, int));
DECLARE(read, ssize_t, (int, void *, size_t));
DECLARE(write, ssize_t, (int, const void *, size_t));
static char *argv0, *sockname;
static int trickled_sock = -1, *trickled;

void
trickled_configure(char *xsockname, int (*xlibc_socket)(int, int, int),
    ssize_t (*xlibc_read)(int, void *, size_t),
    ssize_t (*xlibc_write)(int, const void *, size_t),
    char *xargv0)
{
	sockname = xsockname;
	libc_socket = xlibc_socket;
	libc_write = xlibc_write;
	libc_read = xlibc_read;
	argv0 = xargv0;
}

void
trickled_open(int *xtrickled)
{
	struct msg msg;
	struct msg_conf *conf;

	memset(&msg, 0, sizeof(msg));

	msg.type = MSG_TYPE_CONF;
	conf = &msg.data.conf;
	/* memcpy(conf->lim, lim, sizeof(conf->lim)); */
	conf->pid = getpid();
	strlcpy(conf->argv0, argv0, sizeof(conf->argv0));
	conf->uid = geteuid();
	conf->gid = getegid();

	_trickled_open(&msg, xtrickled);
}

void
trickled_ctl_open(int *xtrickled)
{
	struct msg msg;

	memset(&msg, 0, sizeof(msg));

	msg.type = MSG_TYPE_SPECTATOR;

	_trickled_open(&msg, xtrickled);
}


static void
_trickled_open(struct msg *msg, int *xtrickled)
{
	int s;
	struct sockaddr_un xsun;

	trickled = xtrickled;
	*trickled = 0;

	if ((s = (*libc_socket)(AF_UNIX, SOCK_STREAM, 0)) == -1)
		return;

	memset(&xsun, 0, sizeof(xsun));
	xsun.sun_family = AF_UNIX;
	strlcpy(xsun.sun_path, sockname, sizeof(xsun.sun_path));

	if (connect(s, (struct sockaddr *)&xsun, sizeof(xsun)) == -1) {
		close(s);
		return;
	}

	if (atomicio(libc_write, s, msg, sizeof(*msg)) != sizeof(*msg)) {
		close(s);
		return;
	}

	trickled_sock = *trickled = s;
}

int
trickled_sendmsg(struct msg *msg)
{
	if (trickled_sock == -1)
		goto fail;

	if (atomicio(libc_write, trickled_sock, msg, sizeof(*msg)) ==
	    sizeof(*msg))
		return (0);

 fail:
	*trickled = 0;
	trickled_sock = -1;

	return (-1);
}

int
trickled_recvmsg(struct msg *msg)
{
	if (trickled_sock == -1)
		goto fail;

	if (atomicio(libc_read, trickled_sock, msg, sizeof(*msg)) ==
	    sizeof(*msg))
		return (0);

 fail:
	*trickled = 0;
	trickled_sock = -1;

	return (-1);
}

int
trickled_update(short dir, size_t len)
{
	struct msg msg;
	struct msg_update *update = &msg.data.update;

	msg.type = MSG_TYPE_UPDATE;

	update->len = len;
	update->dir = dir;

	return (trickled_sendmsg(&msg));
}

int
trickled_delay(short dir, size_t *len)
{
	struct msg msg;
	struct msg_delay *delay = &msg.data.delay;
	struct msg_delayinfo *delayinfo = &msg.data.delayinfo;

	msg.type = MSG_TYPE_DELAY;

	delay->len = *len;
	delay->dir = dir;

	if (trickled_sendmsg(&msg) == -1)
		return (-1);

	/* Ignore all other messages in the meantime XXX for now. */
	do {
		if (trickled_recvmsg(&msg) == -1)
			return (-1);
	} while (msg.type != MSG_TYPE_CONT);

	*len = delayinfo->len;

	return (0);
}

struct timeval *
trickled_getdelay(short dir, size_t *len)
{
	struct msg msg;
	struct msg_delay *delay = &msg.data.delay;
	struct msg_delayinfo *delayinfo = &msg.data.delayinfo;
	static struct timeval tv;

	msg.type = MSG_TYPE_GETDELAY;

	delay->len = *len;
	delay->dir = dir;

	if (trickled_sendmsg(&msg) == -1)
		return (NULL);

	/* Ignore all other messages in the meantime XXX for now. */
	do {
		if (trickled_recvmsg(&msg) == -1)
			return (NULL);
	} while (msg.type != MSG_TYPE_DELAYINFO);

	if (ISSET(msg.status, MSG_STATUS_FAIL))
		return (NULL);

	tv = delayinfo->delaytv;
	*len = delayinfo->len;

	return (&tv);
}

int
trickled_getinfo(uint32_t *uplim, uint32_t *uprate,
    uint32_t *downlim, uint32_t *downrate)
{
	struct msg msg;
	struct msg_getinfo *getinfo = &msg.data.getinfo;

	msg.type = MSG_TYPE_GETINFO;

	if (trickled_sendmsg(&msg) == -1)
		return (-1);

	do {
		if (trickled_recvmsg(&msg) == -1)
			return (-1);
	} while (msg.type != MSG_TYPE_GETINFO);

	*uplim = getinfo->dirinfo[TRICKLE_SEND].lim;
	*uprate = getinfo->dirinfo[TRICKLE_SEND].rate;

	*downlim = getinfo->dirinfo[TRICKLE_RECV].lim;
	*downrate = getinfo->dirinfo[TRICKLE_RECV].rate;

	return (0);
}
