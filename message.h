/*
 * message.h
 *
 * Copyright (c) 2003 Marius Aamodt Eriksen <marius@monkey.org>
 * All rights reserved.
 *
 * $Id: message.h,v 1.5 2003/04/15 05:44:53 marius Exp $
 */

#ifndef TRICKLE_MESSAGE_H
#define TRICKLE_MESSAGE_H

/* XXX */
#define SOCKNAME "trickle.sock"

#define MSG_TYPE_NEW       1
#define MSG_TYPE_CONF      2
#define MSG_TYPE_UPDATE    3
#define MSG_TYPE_CONT      4
#define MSG_TYPE_DELAY     5
#define MSG_TYPE_GETDELAY  6
#define MSG_TYPE_DELAYINFO 7 
#define MSG_TYPE_SPECTATOR 8
#define MSG_TYPE_GETINFO   100

#define MSG_STATUS_FAIL 1

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
	size_t len;
	short   dir;
};

struct msg_delayinfo {
	struct timeval delaytv;
	ssize_t        len;
};

struct msg_getinfo {
	struct {
		uint32_t lim;
		uint32_t rate;
	} dirinfo[2];
};

struct msg {
	short type;
	short status;
	union {
		struct msg_conf      conf;
		struct msg_delay     delay;
		struct msg_update    update;
		struct msg_delayinfo delayinfo;
		struct msg_getinfo   getinfo;
	} data;
};

#endif /* TRICKLE_MESSAGE_H */
