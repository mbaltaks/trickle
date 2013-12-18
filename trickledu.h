/*
 * trickledu.h
 *
 * Copyright (c) 2003 Marius Aamodt Eriksen <marius@monkey.org>
 * All rights reserved.
 *
 * $Id: trickledu.h,v 1.3 2003/03/04 07:11:51 marius Exp $
 */

#ifndef TRICKLE_TRICKLEDU_H
#define TRICKLE_TRICKLEDU_H

void            trickled_configure(char *, int (*)(int, int, int),
                    ssize_t (*)(int, void *, size_t),
                    ssize_t (*)(int, const void *, size_t), char *);
int             trickled_open(void);
int             trickled_update(short, int);
int             trickled_delay(short, size_t *);
struct timeval *trickled_getdelay(short, size_t *);
int             trickled_sendmsg(struct msg *);
int             trickled_recvmsg(struct msg *);

#endif /* TRICKLE_TRICKLEDU_H */
