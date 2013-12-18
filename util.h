/*
 * trickledu.h
 *
 * Copyright (c) 2003 Marius Aamodt Eriksen <marius@monkey.org>
 * All rights reserved.
 *
 * $Id: util.h,v 1.2 2003/03/03 11:30:52 marius Exp $
 */

#ifndef TRICKLE_UTIL_H
#define TRICKLE_UTIL_H

#ifndef TIMEVAL_TO_TIMESPEC
#define TIMEVAL_TO_TIMESPEC(tv, ts) {                                   \
        (ts)->tv_sec = (tv)->tv_sec;                                    \
        (ts)->tv_nsec = (tv)->tv_usec * 1000;                           \
}
#endif /* !TIMEVAL_TO_TIMESPEC */

#undef SET
#undef CLR
#undef ISSET
#define SET(t, f)       ((t) |= (f))
#define CLR(t, f)       ((t) &= ~(f))
#define ISSET(t, f)     ((t) & (f))

#undef MAX
#undef MIN
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

ssize_t    atomicio(ssize_t (*)(), int, void *, size_t);
char      *get_progname(char *);


#endif /* TRICKLE_UTIL_H */
