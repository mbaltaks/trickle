/*
 * trickle-overload.c
 *
 * Copyright (c) 2002, 2003 Marius Aamodt Eriksen <marius@monkey.org>
 * All rights reserved.
 *
 * $Id: trickle-overload.c,v 1.21 2003/03/07 09:35:17 marius Exp $
 */

#include <sys/types.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/uio.h>
#include <sys/un.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif /* HAVE_SYS_TIME_H */

#include <netinet/in.h>

#ifdef HAVE_ERR_H
#include <err.h>
#endif /* HAVE_ERR_H */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <limits.h>
#include <math.h>
#if defined(HAVE_TIME_H) && defined(TIME_WITH_SYS_TIME)
#include <time.h>
#endif /* defined(HAVE_TIME_H) && defined(TIME_WITH_SYS_TIME) */
#include <syslog.h>
#include <pwd.h>
#include <stdarg.h>
#include <string.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif /* HAVE_STDINT_H */

#include "bwstat.h"
#include "trickle.h"
#include "message.h"
#include "util.h"
#include "trickledu.h"

#define SD_INSELECT 0x01

struct sockdesc {
	int                    sock;
	int                    flags;
	struct bwstat         *stat;
	struct {
		int     flags;
		size_t  lastlen;
	}                      data[2];

	TAILQ_ENTRY(sockdesc)  next;
};

struct delay {
	struct sockdesc    *sd;
	struct timeval      tv;
	struct timeval      abstv;
	short               which;

	TAILQ_ENTRY(delay)  next;	
};

TAILQ_HEAD(delayhead, delay);

static TAILQ_HEAD(sockdeschead, sockdesc) sdhead;
static uint32_t winsz, verbose;
static uint lim[2];
static char *argv0;
static int trickled, initialized, initializing;
/* XXX initializing - volatile? */

#define DECLARE(name, ret, args) static ret (*libc_##name) args

DECLARE(socket, int, (int, int, int));
DECLARE(close, int, (int));
/* DECLARE(setsockopt, int, (int, int, int, const void *, socklen_t)); */

DECLARE(read, ssize_t, (int, void *, size_t));
DECLARE(recv, ssize_t, (int, void *, size_t, int));
DECLARE(readv, ssize_t, (int, const struct iovec *, int));
#ifdef __sun__
DECLARE(recvfrom, ssize_t, (int, void *, size_t, int, struct sockaddr *,
	    Psocklen_t));
#else
DECLARE(recvfrom, ssize_t, (int, void *, size_t, int, struct sockaddr *,
	    socklen_t *));
#endif /* __sun__ */

DECLARE(write, ssize_t, (int, const void *, size_t));
DECLARE(send, ssize_t, (int, const void *, size_t, int));
DECLARE(writev, ssize_t, (int, const struct iovec *, int));
DECLARE(sendto, ssize_t, (int, const void *, size_t, int,
	    const struct sockaddr *, socklen_t));

DECLARE(select, int, (int, fd_set *, fd_set *, fd_set *, struct timeval *));
/* DECLARE(poll, int, (struct pollfd *, int, int)); */

#ifdef __sun__
DECLARE(accept, int, (int, struct sockaddr *, Psocklen_t));
#else
DECLARE(accept, int, (int, struct sockaddr *, socklen_t *));
#endif /* __sun__ */
DECLARE(dup, int, (int));
DECLARE(dup2, int, (int, int));

static int             delay(int, ssize_t *, short);
static struct timeval *getdelay(struct sockdesc *, ssize_t *, short);
static void            update(int, ssize_t, short);
static void            updatesd(struct sockdesc *, ssize_t, short);
static void            trickle_init(void);
void                   safe_printv(int, const char *, ...);

#define errx(l, fmt, arg...) do {		\
	safe_printv(0, fmt, ##arg);		\
	exit(l);				\
} while (0)

#ifdef __linux__
#define UNDERSCORE ""
#else
#define UNDERSCORE "_"
#endif /* __linux__ */

#define INIT do {				\
	if (!initialized && !initializing)	\
		trickle_init();			\
} while (0);

#define GETADDR(x) do {							\
	if ((libc_##x = dlsym(dh, UNDERSCORE #x)) == NULL)		\
		errx(0, "[trickle] Failed to get " #x "() address");	\
} while (0);

static void
trickle_init(void)
{
	void *dh;
	char *winszstr, *verbosestr,
	    *recvlimstr, *sendlimstr, *sockname;

	initializing = 1;

#if defined(__linux__) || defined(__sun__)
	dh = (void *) -1L;
#else
 	if ((dh = dlopen("libc.so", RTLD_LAZY)) == NULL)
		errx(1, "[trickle] Failed to open libc");
#endif /* __linux__ */

	GETADDR(socket);
/*	GETADDR(setsockopt); */
	GETADDR(close);

	GETADDR(read);
	GETADDR(readv);
#ifndef __FreeBSD__
	GETADDR(recv);
#endif /* !__FreeBSD__ */
	GETADDR(recvfrom);

	GETADDR(write);
	GETADDR(writev);
#ifndef __FreeBSD__
	GETADDR(send);
#endif /* !__FreeBSD__ */
	GETADDR(sendto);

	GETADDR(select);
/*	GETADDR(poll); */

	GETADDR(dup);
	GETADDR(dup2);

	GETADDR(accept);

	if ((winszstr = getenv("TRICKLE_WINDOW_SIZE")) == NULL)
		errx(1, "[trickle] Failed to get window size");

	if ((recvlimstr = getenv("TRICKLE_DOWNLOAD_LIMIT")) == NULL)
		errx(1, "[trickle] Failed to get limit");

	if ((sendlimstr = getenv("TRICKLE_UPLOAD_LIMIT")) == NULL)
		errx(1, "[trickle] Failed to get limit");

	if ((verbosestr = getenv("TRICKLE_VERBOSE")) == NULL)
		errx(1, "[trickle] Failed to get verbosity level");

	if ((argv0 = getenv("TRICKLE_ARGV")) == NULL)
		errx(1, "[trickle] Failed to get argv");

	if ((sockname = getenv("TRICKLE_SOCKNAME")) == NULL)
		errx(1, "[trickle] Failed to get socket name");

#ifndef __linux__
	dlclose(dh);
#endif /* !__linux__ */

	winsz = atoi(winszstr) * 1024;
	lim[TRICKLEDIR_RECV] = atoi(recvlimstr) * 1024;
	lim[TRICKLEDIR_SEND] = atoi(sendlimstr) * 1024;
	verbose = atoi(verbosestr);

	TAILQ_INIT(&sdhead);

	/*
	 * Open controlling socket
	 */

	trickled_configure(sockname, libc_socket, libc_read, libc_write, argv0);
	trickled = trickled_open();

	bwstat_init(winsz);

	safe_printv(1, "[trickle] Initialized");

	initialized = 1;
}

int
socket(int domain, int type, int protocol)
{
	int sock;
	struct sockdesc *sd;

	INIT;

	sock = (*libc_socket)(domain, type, protocol);

	if (sock != -1 && domain == AF_INET && type == SOCK_STREAM) {
		if ((sd = calloc(1, sizeof(*sd))) == NULL)
			return (-1);
		if ((sd->stat = bwstat_new()) == NULL) {
			free(sd);
			return (-1);
		}

		/* All sockets are equals. */
		sd->stat->pts = 1;
		sd->stat->lsmooth = 10;
		sd->stat->tsmooth = 5.0;
		sd->sock = sock;

		TAILQ_INSERT_TAIL(&sdhead, sd, next);
	}

	return (sock);
}

int
close(int fd)
{
	struct sockdesc *sd, *next;

	INIT;

	for (sd = TAILQ_FIRST(&sdhead); sd != NULL; sd = next) {
		next = TAILQ_NEXT(sd, next);
		if (sd->sock == fd) {
			TAILQ_REMOVE(&sdhead, sd, next);
			bwstat_free(sd->stat);
			free(sd);
			break;
		}
	}

	return ((*libc_close)(fd));
}

static int
select_delay(struct sockdesc *sd, fd_set *fds, short which,
    struct timeval *tv, struct delayhead *dhead)
{
	struct timeval *delaytv;
	struct delay *d, *xd;
	int len = -1;

	updatesd(sd, 0, which);

	if ((delaytv = getdelay(sd, &len, which)) != NULL) {
		safe_printv(3, "[trickle] Delaying socket (%s) %d "
		    "by %ld seconds %ld microseconds",
		    which == 0 ? "write" : "read", sd->sock,
		    delaytv->tv_sec, delaytv->tv_usec);

		if ((d = calloc(1, sizeof(*d))) == NULL)
			return (-1);
		gettimeofday(&d->abstv, NULL);
		d->tv = *delaytv;
		d->which = which;
		d->sd = sd;

		FD_CLR(sd->sock, fds);

		if (timercmp(delaytv, tv, <)) {
			*tv = *delaytv;
			TAILQ_INSERT_HEAD(dhead, d, next);
		} else {
			TAILQ_FOREACH(xd, dhead, next)
				if (timercmp(delaytv, &xd->tv, <))
					TAILQ_INSERT_BEFORE(xd, d, next);
		}
	}

	return (0);
}

int
select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
    struct timeval *timeout)
{
	struct timeval tv, tvref, *tvin, lasttv, curtv, difftv, begintv,
	    beforetv, nulltv;
	fd_set *fdsets[] = { writefds, readfds }, *fds;
	short which;
	int ret;
	struct delayhead dhead;
	struct delay *d;
	struct sockdesc *sd;

	INIT;

	/* XXX check for FD_SETSIZE */

	memset(&nulltv, 0, sizeof(nulltv));
	gettimeofday(&begintv, NULL);

	TAILQ_INIT(&dhead);

	tv.tv_sec = tv.tv_usec = LONG_MAX;
	tvref = tv;

	/*
	 * Make list of sockets which are trickled *and* are currently
	 * being selected.  This is ordered by ascending tvs.
	 */
	TAILQ_FOREACH(sd, &sdhead, next)
		for (which = 0; which < 2; which++)
			if ((fds = fdsets[which]) != NULL &&
			    FD_ISSET(sd->sock, fds))
				select_delay(sd, fds, which, &tv, &dhead);

	gettimeofday(&beforetv, NULL);
 again:
	if (timercmp(&tv, &tvref, <)) {
		if (timeout != NULL && timercmp(timeout, &tv, <))
			tvin = timeout;
		else
			tvin = &tv;
	} else {
		if (timeout != NULL)
			tvin = timeout;
		else
			tvin = NULL;
	}

	if (tvin != NULL && tvin != timeout)
		safe_printv(2, "[trickle] select() timeout: %ld seconds %ld microseconds",
		    tvin->tv_sec, tvin->tv_usec);

	/* XXX */
	if (timercmp(tvin, &nulltv, <))
		timerclear(tvin);

	ret = (*libc_select)(nfds, readfds, writefds, exceptfds, tvin);

	if (ret == 0) {
		if (tvin == timeout) {
			ret = 0;
			goto out;
		} else {
			memset(&lasttv, 0, sizeof(lasttv));
			gettimeofday(&curtv, NULL);

			timersub(&curtv, &beforetv, &difftv);

			while ((d = TAILQ_FIRST(&dhead)) != NULL) {
				if (timercmp(&d->tv, &difftv, >))
					break;

				updatesd(d->sd, 0, d->which);
				SET(d->sd->data[d->which].flags, SD_INSELECT);
				FD_SET(d->sd->sock, fdsets[d->which]);
				TAILQ_REMOVE(&dhead, d, next);
				free(d);
			}

			if ((d = TAILQ_FIRST(&dhead)) != NULL) {
				timersub(&curtv, &d->abstv, &difftv);
				timersub(&d->tv, &difftv, &tv);
			} else {
				tv = tvref;
				if (timeout != NULL) {
					timersub(&curtv, &begintv, &difftv);
					timersub(timeout, &difftv, timeout);
					begintv = curtv;
				}
			}

			goto again;
		}
	}

 out:
	while ((d = TAILQ_FIRST(&dhead)) != NULL) {
		if (!FD_ISSET(d->sd->sock, fdsets[d->which]) || ret == 0)
			CLR(d->sd->data[d->which].flags, SD_INSELECT);
		TAILQ_REMOVE(&dhead, d, next);
		free(d);
	}

	return (ret);
}

#if 0
int
poll(struct pollfd *fds, int nfds, int timeout)
{
	return ((*libc_poll)(fds, nfds, timeout));
}
#endif /* 0 */

ssize_t
read(int fd, void *buf, size_t nbytes)
{
	ssize_t ret;
	size_t xnbytes = nbytes;

	INIT;

	if (delay(fd, &xnbytes, TRICKLEDIR_RECV) == TRICKLE_WOULDBLOCK) {
		errno = EAGAIN;
		ret = -1;
	} else {
		ret = (*libc_read)(fd, buf, xnbytes);
	}

	update(fd, ret, TRICKLEDIR_RECV);

	return (ret);
}

/*
 * XXX defunct for smoothing ... for now
 */
ssize_t
readv(int fd, const struct iovec *iov, int iovcnt)
{
	size_t len = 0;
	ssize_t ret;
	int i;

	INIT;

	for (i = 0; i < iovcnt; i++)
		len += iov[i].iov_len;

	if (delay(fd, &len, TRICKLEDIR_RECV) == TRICKLE_WOULDBLOCK) {
		errno = EAGAIN;
		ret = -1;
	} else {
		ret = (*libc_readv)(fd, iov, iovcnt);
	}

	update(fd, ret, TRICKLEDIR_RECV);

	return (ret);
}

#ifndef __FreeBSD__ 
ssize_t
recv(int sock, void *buf, size_t len, int flags)
{
	ssize_t ret;
	size_t xlen = len;

	INIT;

	if (delay(sock, &xlen, TRICKLEDIR_RECV) == TRICKLE_WOULDBLOCK) {
		errno = EAGAIN;
		ret = -1;
	} else {
		ret = (*libc_recv)(sock, buf, xlen, flags);
	}

	update(sock, ret, TRICKLEDIR_RECV);

	return (ret);
}
#endif /* !__FreeBSD__ */

#ifdef __sun__
recvfrom(int sock, void *buf, size_t len, int flags, struct sockaddr *from,
    Psocklen_t fromlen)
#else
ssize_t
recvfrom(int sock, void *buf, size_t len, int flags, struct sockaddr *from,
    socklen_t *fromlen)
#endif /* __sun__ */
{
	ssize_t ret;
	size_t xlen = len;

	INIT;

	if (delay(sock, &xlen, TRICKLEDIR_RECV) == TRICKLE_WOULDBLOCK) {
		errno = EAGAIN;
		ret = -1;
	} else {
		ret = (*libc_recvfrom)(sock, buf, xlen, flags, from, fromlen);
	}

	update(sock, ret, TRICKLEDIR_RECV);

	return (ret);
}

ssize_t
write(int fd, const void *buf, size_t len)
{
	ssize_t ret;
	size_t xlen = len;

	INIT;

	if (delay(fd, &xlen, TRICKLEDIR_SEND) == TRICKLE_WOULDBLOCK) {
		errno = EAGAIN;
		ret = -1;
	} else {
		ret = (*libc_write)(fd, buf, xlen);
	}

	update(fd, ret, TRICKLEDIR_SEND);

	return (ret);
}

/*
 * XXX defunct for smoothing ... for now
 */
ssize_t
writev(int fd, const struct iovec *iov, int iovcnt)
{
	ssize_t ret;
	size_t len = 0;
	int i;

	INIT;

	for (i = 0; i < iovcnt; i++)
		len += iov[i].iov_len;

	if (delay(fd, &len, TRICKLEDIR_SEND) == TRICKLE_WOULDBLOCK) {
		errno = EAGAIN;
		ret = -1;
	} else {
		ret = (*libc_writev)(fd, iov, iovcnt);
	}

	update(fd, ret, TRICKLEDIR_SEND);

	return (ret);
}

#ifndef __FreeBSD__
ssize_t
send(int sock, const void *buf, size_t len, int flags)
{
	ssize_t ret;
	size_t xlen = len;

	INIT;

	if (delay(sock, &xlen, TRICKLEDIR_SEND) == TRICKLE_WOULDBLOCK) {
		errno = EAGAIN;
		ret = -1;
	} else {
		ret = (*libc_send)(sock, buf, xlen, flags);
	}

	update(sock, ret, TRICKLEDIR_SEND);

	return (ret);
}
#endif /* !__FreeBSD__ */

ssize_t
sendto(int sock, const void *buf, size_t len, int flags, const struct sockaddr *to,
    socklen_t tolen)
{
	ssize_t ret;
	size_t xlen = len;

	INIT;

	if (delay(sock, &xlen, TRICKLEDIR_SEND) == TRICKLE_WOULDBLOCK) {
		errno = EAGAIN;
		ret = -1;
	} else {
		ret = (*libc_sendto)(sock, buf, xlen, flags, to, tolen);
	}

	update(sock, ret, TRICKLEDIR_SEND);

	return (ret);
}

#if 0
int
setsockopt(int sock, int level, int optname, const void *optval,
    socklen_t option)
{
	INIT;

	/* blocking, etc. */
	return ((*libc_setsockopt)(sock, level, optname, optval, option));
}
#endif /* 0 */

int
dup(int oldfd)
{
	int newfd;
	struct sockdesc *sd, *nsd;

	INIT;

	newfd = (*libc_dup)(oldfd);

	TAILQ_FOREACH(sd, &sdhead, next) {
	        if (oldfd == sd->sock)
			break;
	}

	if (sd != NULL && newfd != -1) {
		if ((nsd = malloc(sizeof(*nsd))) == NULL) {
			(*libc_close)(newfd);
			return (-1);
		}
		sd->sock = newfd;
		memcpy(nsd, sd, sizeof(*nsd));
		TAILQ_INSERT_TAIL(&sdhead, nsd, next);
	}

	return (newfd);
}

int
dup2(int oldfd, int newfd)
{
	struct sockdesc *sd, *nsd;
	int ret;

	INIT;

	ret = (*libc_dup2)(oldfd, newfd);

	TAILQ_FOREACH(sd, &sdhead, next)
		if (oldfd == sd->sock)
			break;

	if (sd != NULL && ret != -1) {
		if ((nsd = malloc(sizeof(*nsd))) == NULL)
			return (-1);
		sd->sock = newfd;
		memcpy(nsd, sd, sizeof(*nsd));
		TAILQ_INSERT_TAIL(&sdhead, nsd, next);
	}

	return (ret);
}

#ifdef __sun__
int
accept(int sock, struct sockaddr *addr, Psocklen_t addrlen)
#else
int
accept(int sock, struct sockaddr *addr, socklen_t *addrlen)
#endif /* __sun__ */
{
	int ret;
	struct sockdesc *sd;

	INIT;

	ret = (*libc_accept)(sock, addr, addrlen);

	if (ret != -1) {
		if ((sd = calloc(1, sizeof(*sd))) == NULL)
			return (ret);

		if ((sd->stat = bwstat_new()) == NULL) {
			free(sd);
			return (ret);
		}

		sd->sock = ret;
		sd->stat->pts = 1;
		sd->stat->lsmooth = 10;
		sd->stat->tsmooth = 5.0;
		TAILQ_INSERT_TAIL(&sdhead, sd, next);
	}

	return (ret);
}

static int
delay(int sock, ssize_t *len, short which)
{
	struct sockdesc *sd;
	struct timeval *tv;
	struct timespec ts, rm;

	TAILQ_FOREACH(sd, &sdhead, next)
		if (sock == sd->sock)
			break;

	if (sd == NULL)
		return (-1);

	if (ISSET(sd->data[which].flags, SD_INSELECT))
		return (0);

	/*
	 * Try trickled delay first, then local delay
	 */
	/* XXX nonblock */
	if (trickled && trickled_delay(which, len) != -1)
		return (0);

	if ((tv = getdelay(sd, len, which)) != NULL) {
		TIMEVAL_TO_TIMESPEC(tv, &ts);

		safe_printv(2, "[trickle] Delaying %lds%ldus",
		    tv->tv_sec, tv->tv_usec);

		if (ISSET(sd->flags, TRICKLE_NONBLOCK))
			return (TRICKLE_WOULDBLOCK);

		while (nanosleep(&ts, &rm) == -1 && errno == EINTR)
			ts = rm;
	}

	return (0);
}

static struct timeval *
getdelay(struct sockdesc *sd, ssize_t *len, short which)
{
	struct timeval *xtv;
	uint xlim = lim[which];

	if (*len < 0)
		*len = sd->data[which].lastlen;

	if (trickled)
		xlim = (xtv = trickled_getdelay(which, len)) != NULL ? 
		    *len / (xtv->tv_sec + xtv->tv_usec / 1000000.0) : 0;

	if (xlim == 0)
		return (NULL);

	return (bwstat_getdelay(sd->stat, len, xlim, which));
}

static void
update(int sock, ssize_t len, short which)
{
	struct sockdesc *sd;

	TAILQ_FOREACH(sd, &sdhead, next)
		if (sock == sd->sock)
			break;

	if (sd == NULL)
		return;

	updatesd(sd, len, which);
}

static void
updatesd(struct sockdesc *sd, ssize_t len, short which)
{
	struct bwstatdata *bsd;
	int ret;

	if (len < 0)
		len = 0;

	if ((ret = fcntl(sd->sock, F_GETFL, O_NONBLOCK)) && ret != -1)
		SET(sd->flags, TRICKLE_NONBLOCK);
	else if (ret != -1)
		CLR(sd->flags, TRICKLE_NONBLOCK);

	if (len > 0)
		sd->data[which].lastlen = len;

	if (trickled)
		trickled_update(which, len);

	bwstat_update(sd->stat, len, which);

	bsd = &sd->stat->data[which];

	safe_printv(1, "[trickle] avg: %d.%d KB/s; win: %d.%d KB/s",
	    (bsd->rate / 1024), ((bsd->rate % 1024) * 100 / 1024),
	    (bsd->winrate / 1024), ((bsd->winrate % 1024) * 100 / 1024));
}

void
safe_printv(int level, const char *fmt, ...)
{
	va_list ap;
	char str[1024];
	int n;

	if (level > verbose)
		return;

	va_start(ap, fmt);

	if ((n = snprintf(str, sizeof(str), "%s: ", argv0)) == -1) {
		str[0] = '\0';
		n = 0;
	}

        if (fmt != NULL)
		n = vsnprintf(str + n, sizeof(str) - n, fmt, ap);

	if (n == -1)
		return;

	strlcat(str, "\n", sizeof(str));

	(*libc_write)(STDERR_FILENO, str, strlen(str));
	va_end(ap);
}
