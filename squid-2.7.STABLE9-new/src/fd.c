
/*
 * $Id: fd.c,v 1.58 2007/06/25 14:38:00 hno Exp $
 *
 * DEBUG: section 51    Filedescriptor Functions
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

int default_read_method(int, char *, int);
int default_write_method(int, const char *, int);
#ifdef _SQUID_MSWIN_
int socket_read_method(int, char *, int);
int socket_write_method(int, const char *, int);
int file_read_method(int, char *, int);
int file_write_method(int, const char *, int);
#endif

const char *fdTypeStr[] =
{
    "None",
    "Log",
    "File",
    "Socket",
    "Pipe",
    "Unknown"
};

static void fdUpdateBiggest(int fd, int);

static void
fdUpdateBiggest(int fd, int opening)
{
    if (fd < Biggest_FD)
	return;
    assert(fd < Squid_MaxFD);
    if (fd > Biggest_FD) {
	/*
	 * assert that we are not closing a FD bigger than
	 * our known biggest FD
	 */
	assert(opening);
	Biggest_FD = fd;
	return;
    }
    /* if we are here, then fd == Biggest_FD */
    /*
     * assert that we are closing the biggest FD; we can't be
     * re-opening it
     */
    assert(!opening);
    while (!fd_table[Biggest_FD].flags.open && Biggest_FD > 0)
	Biggest_FD--;
}

void
fd_close(int fd)
{
    fde *F = &fd_table[fd];
    if (F->type == FD_FILE) {
	assert(F->read_handler == NULL);
	assert(F->write_handler == NULL);
    }
    debug(51, 3) ("fd_close FD %d %s\n", fd, F->desc);
    commClose(fd);
    F->flags.open = 0;
#if DELAY_POOLS
    if (F->slow_id)
	commRemoveSlow(fd);
#endif
    fdUpdateBiggest(fd, 0);
    Number_FD--;
    memset(F, '\0', sizeof(fde));
    F->timeout = 0;
}

#ifdef _SQUID_MSWIN_
int
socket_read_method(int fd, char *buf, int len)
{
    return (recv(fd, buf, len, 0));
}

int
file_read_method(int fd, char *buf, int len)
{
    return (_read(fd, buf, len));
}

int
socket_write_method(int fd, const char *buf, int len)
{
    return (send(fd, buf, len, 0));
}

int
file_write_method(int fd, const char *buf, int len)
{
    return (_write(fd, buf, len));
}
#endif

int
default_read_method(int fd, char *buf, int len)
{
    return (read(fd, buf, len));
}

int
default_write_method(int fd, const char *buf, int len)
{
    return (write(fd, buf, len));
}

void
fd_open(int fd, unsigned int type, const char *desc)
{
    fde *F;
    assert(fd >= 0);
    F = &fd_table[fd];
    if (F->flags.open) {
	debug(51, 1) ("WARNING: Closing open FD %4d\n", fd);
	fd_close(fd);
    }
    assert(!F->flags.open);
    debug(51, 3) ("fd_open FD %d %s\n", fd, desc);
    F->type = type;
    F->flags.open = 1;
    commOpen(fd);
#ifdef _SQUID_MSWIN_
    F->win32.handle = _get_osfhandle(fd);
    switch (type) {
    case FD_SOCKET:
    case FD_PIPE:
	F->read_method = &socket_read_method;
	F->write_method = &socket_write_method;
	break;
    case FD_FILE:
    case FD_LOG:
	F->read_method = &file_read_method;
	F->write_method = &file_write_method;
	break;
    default:
	fatalf("fd_open(): unknown FD type - FD#: %i, type: %u, desc %s\n", fd, type, desc);
    }
#else
    F->read_method = &default_read_method;
    F->write_method = &default_write_method;
#endif
    fdUpdateBiggest(fd, 1);
    if (desc)
	fd_note(fd, desc);
    Number_FD++;
}

void
fd_note(int fd, const char *s)
{
    fde *F = &fd_table[fd];
    xstrncpy(F->descbuf, s, FD_DESC_SZ);
    F->desc = F->descbuf;
}

void
fd_note_static(int fd, const char *s)
{
    fde *F = &fd_table[fd];
    F->desc = s;
}

void
fd_bytes(int fd, int len, unsigned int type)
{
    fde *F = &fd_table[fd];
    if (len < 0)
	return;
    assert(type == FD_READ || type == FD_WRITE);
    if (type == FD_READ)
	F->bytes_read += len;
    else
	F->bytes_written += len;
}

void
fd_init(void)
{
    fd_table = xcalloc(Squid_MaxFD, sizeof(fde));
    /* XXX account fd_table */
}

void
fdFreeMemory(void)
{
    safe_free(fd_table);
}

void
fdDumpOpen(void)
{
    int i;
    fde *F;
    for (i = 0; i < Squid_MaxFD; i++) {
	F = &fd_table[i];
	if (!F->flags.open)
	    continue;
	if (i == fileno(debug_log))
	    continue;
	debug(51, 1) ("Open FD %-10s %4d %s\n",
	    F->bytes_read && F->bytes_written ? "READ/WRITE" :
	    F->bytes_read ? "READING" :
	    F->bytes_written ? "WRITING" : null_string,
	    i, F->desc);
    }
}

int
fdNFree(void)
{
    return Squid_MaxFD - Number_FD - Opening_FD;
}

int
fdUsageHigh(void)
{
    int nrfree = fdNFree();
    if (nrfree < (RESERVED_FD << 1))
	return 1;
    if (nrfree < (Number_FD >> 2))
	return 1;
    return 0;
}

/* Called when we runs out of file descriptors */
void
fdAdjustReserved(void)
{
    int new;
    int x;
    static time_t last = 0;
    /*
     * don't update too frequently
     */
    if (last + 5 > squid_curtime)
	return;
    /*
     * Calculate a new reserve, based on current usage and a small extra
     */
    new = Squid_MaxFD - Number_FD + XMIN(25, Squid_MaxFD / 16);
    if (new <= RESERVED_FD)
	return;
    x = Squid_MaxFD - 20 - XMIN(25, Squid_MaxFD / 16);
    if (new > x) {
	/* perhaps this should be fatal()? -DW */
	debug(51, 0) ("WARNING: This machine has a serious shortage of filedescriptors.\n");
	new = x;
    }
    if (Squid_MaxFD - new < XMIN(256, Squid_MaxFD / 2))
	fatalf("Too few filedescriptors available in the system (%d usable of %d).\n", Squid_MaxFD - new, Squid_MaxFD);
    debug(51, 0) ("Reserved FD adjusted from %d to %d due to failures\n",
	RESERVED_FD, new);
    RESERVED_FD = new;
}
