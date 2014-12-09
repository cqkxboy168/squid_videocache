
/*
 * $Id: unlinkd.c,v 1.53 2006/09/08 19:41:24 serassio Exp $
 *
 * DEBUG: section 2     Unlink Daemon
 * AUTHOR: Duane Wessels
 *
 * SQUID Web Proxy Cache          http://www.squid-cache.org/
 * ----------------------------------------------------------
 *
 *  Squid is the result of efforts by numerous individuals from
 *  the Internet community; see the CONTRIBUTORS file for full
 *  details.   Many organizations have provided support for Squid's
 *  development; see the SPONSORS file for full details.  Squid is
 *  Copyrighted (C) 2001 by the Regents of the University of
 *  California; see the COPYRIGHT file for full details.  Squid
 *  incorporates software developed and/or copyrighted by other
 *  sources; see the CREDITS file for full details.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
 *
 */

#include "squid.h"

#ifdef UNLINK_DAEMON

/* This is the external unlinkd process */

#define UNLINK_BUF_LEN 1024

int
main(int argc, char *argv[])
{
    char buf[UNLINK_BUF_LEN];
    char *t;
    int x;
    setbuf(stdin, NULL);
    setbuf(stdout, NULL);
    close(2);
    open(_PATH_DEVNULL, O_RDWR);
    while (fgets(buf, UNLINK_BUF_LEN, stdin)) {
	if ((t = strchr(buf, '\n')))
	    *t = '\0';
#if USE_TRUNCATE
	x = truncate(buf, 0);
#else
	x = unlink(buf);
#endif
	if (x < 0)
	    printf("ERR\n");
	else
	    printf("OK\n");
    }
    exit(0);
}

#else /* UNLINK_DAEMON */

/* This code gets linked to Squid */

static int unlinkd_wfd = -1;
static int unlinkd_rfd = -1;

static void *hIpc;
static pid_t pid;

#define UNLINKD_QUEUE_LIMIT 20

void
unlinkdUnlink(const char *path)
{
    char buf[MAXPATHLEN];
    int l;
    int x;
    static int queuelen = 0;
    if (unlinkd_wfd < 0) {
	debug_trap("unlinkdUnlink: unlinkd_wfd < 0");
	safeunlink(path, 0);
	return;
    }
    /*
     * If the queue length is greater than our limit, then
     * we pause for up to 10ms, hoping that unlinkd
     * has some feedback for us.  Maybe it just needs a slice
     * of the CPU's time.
     */
    if (queuelen >= UNLINKD_QUEUE_LIMIT)
	xusleep(10000);
    /*
     * If there is at least one outstanding unlink request, then
     * try to read a response.  If there's nothing to read we'll
     * get an EWOULDBLOCK or whatever.  If we get a response, then
     * decrement the queue size by the number of newlines read.
     */
    if (queuelen > 0) {
	int x;
	int i;
	char rbuf[512];
#ifdef _SQUID_MSWIN_
	x = recv(unlinkd_rfd, rbuf, 511, 0);
#else
	x = read(unlinkd_rfd, rbuf, 511);
#endif
	if (x > 0) {
	    rbuf[x] = '\0';
	    for (i = 0; i < x; i++)
		if ('\n' == rbuf[i])
		    queuelen--;
	    assert(queuelen >= 0);
	}
    }
    l = strlen(path);
    assert(l < MAXPATHLEN);
    xstrncpy(buf, path, MAXPATHLEN);
    buf[l++] = '\n';
#ifdef _SQUID_MSWIN_
    x = send(unlinkd_wfd, buf, l, 0);
#else
    x = write(unlinkd_wfd, buf, l);
#endif
    if (x < 0) {
	debug(2, 1) ("unlinkdUnlink: write FD %d failed: %s\n",
	    unlinkd_wfd, xstrerror());
	safeunlink(path, 0);
	return;
    } else if (x != l) {
	debug(2, 1) ("unlinkdUnlink: FD %d only wrote %d of %d bytes\n",
	    unlinkd_wfd, x, l);
	safeunlink(path, 0);
	return;
    }
    statCounter.unlink.requests++;
    statCounter.syscalls.disk.unlinks++;
    queuelen++;
}

void
unlinkdClose(void)
{
#ifdef _SQUID_MSWIN_
    if (unlinkd_wfd > -1) {
	debug(2, 1) ("Closing unlinkd pipe on FD %d\n", unlinkd_wfd);
	shutdown(unlinkd_wfd, SD_BOTH);
	comm_close(unlinkd_wfd);
	if (unlinkd_wfd != unlinkd_rfd)
	    comm_close(unlinkd_rfd);
	unlinkd_wfd = -1;
	unlinkd_rfd = -1;
    } else
	debug(2, 0) ("unlinkdClose: WARNING: unlinkd_wfd is %d\n",
	    unlinkd_wfd);
    if (hIpc) {
	if (WaitForSingleObject(hIpc, 5000) != WAIT_OBJECT_0) {
	    getCurrentTime();
	    debug(2, 1)
		("unlinkdClose: WARNING: (unlinkd,%ld) didn't exit in 5 seconds\n",
		pid);
	}
	CloseHandle(hIpc);
    }
#else
    if (unlinkd_wfd < 0)
	return;
    debug(2, 1) ("Closing unlinkd pipe on FD %d\n", unlinkd_wfd);
    file_close(unlinkd_wfd);
    if (unlinkd_wfd != unlinkd_rfd)
	file_close(unlinkd_rfd);
    unlinkd_wfd = -1;
    unlinkd_rfd = -1;
#endif
}

void
unlinkdInit(void)
{
    const char *args[2];
    struct timeval slp;
    args[0] = "(unlinkd)";
    args[1] = NULL;
#if (HAVE_POLL && defined(_SQUID_OSF_)) || defined(_SQUID_MSWIN_)
    /* pipes and poll() don't get along on DUNIX -DW */
    /* On Windows select() will fail on a pipe */
    pid = ipcCreate(IPC_STREAM,
#else
    /* We currently need to use FIFO.. see below */
    pid = ipcCreate(IPC_FIFO,
#endif
	Config.Program.unlinkd,
	args,
	"unlinkd",
	&unlinkd_rfd,
	&unlinkd_wfd,
	&hIpc);
    if (pid < 0)
	fatal("Failed to create unlinkd subprocess");
    slp.tv_sec = 0;
    slp.tv_usec = 250000;
    select(0, NULL, NULL, NULL, &slp);
    fd_note(unlinkd_wfd, "squid -> unlinkd");
    fd_note(unlinkd_rfd, "unlinkd -> squid");
    commSetTimeout(unlinkd_rfd, -1, NULL, NULL);
    commSetTimeout(unlinkd_wfd, -1, NULL, NULL);
    /*
     * unlinkd_rfd should already be non-blocking because of
     * ipcCreate.  We change unlinkd_wfd to blocking mode because
     * we never want to lose an unlink request, and we don't have
     * code to retry if we get EWOULDBLOCK.  Unfortunately, we can
     * do this only for the IPC_FIFO case.
     */
    assert(fd_table[unlinkd_rfd].flags.nonblocking);
    if (FD_PIPE == fd_table[unlinkd_wfd].type)
	commUnsetNonBlocking(unlinkd_wfd);
    debug(2, 1) ("Unlinkd pipe opened on FD %d\n", unlinkd_wfd);
}

#endif /* ndef UNLINK_DAEMON */
