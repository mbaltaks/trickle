/*
 * message.h
 *
 * Copyright (c) 2003 Marius Aamodt Eriksen <marius@monkey.org>
 * All rights reserved.
 *
 * $Id: message.h,v 1.2 2003/02/27 16:25:09 marius Exp $
 */

#ifndef TRICKLE_MESSAGE_H
#define TRICKLE_MESSAGE_H

/* XXX */
#define SOCKNAME "trickle.sock"

#define MSGTYPE_NEW       1
#define MSGTYPE_CONF      2
#define MSGTYPE_UPDATE    3
#define MSGTYPE_CONT      4
#define MSGTYPE_DELAY     5
#define MSGTYPE_GETDELAY  6
#define MSGTYPE_DELAYINFO 7 

#define MSGSTATUS_FAIL 1

struct msg_conf {
	uint   lim[2];
	pid_t  pid;
	char   argv0[256];
	uid_t  uid;
	gid_t  gid;
};

struct msg_delay {
	ssize_t len;
	short   dir;
};

struct msg_update {
	ssize_t len;
	short   dir;
};

struct msg_delayinfo {
	struct timeval delaytv;
	ssize_t        len;
};

struct msg {
	short type;
	short status;
	union {
		struct msg_conf      conf;
		struct msg_delay     delay;
		struct msg_update    update;
		struct msg_delayinfo delayinfo;
	} data;
};

#endif /* TRICKLE_MESSAGE_H */
