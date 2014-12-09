
/*
 * $Id: pconn.c,v 1.36.2.1 2008/01/22 14:37:17 hno Exp $
 *
 * DEBUG: section 48    Persistent Connections
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

struct _pconn {
    hash_link hash;		/* must be first */
    int *fds;
    int nfds_alloc;
    int nfds;
};

#define PCONN_FDS_SZ	8	/* pconn set size, increase for better memcache hit rate */
#define PCONN_HIST_SZ (1<<16)
int client_pconn_hist[PCONN_HIST_SZ];
int server_pconn_hist[PCONN_HIST_SZ];

static PF pconnRead;
static PF pconnTimeout;
static hash_link *pconnLookup(const char *peer, u_short port, const char *domain, struct in_addr *client_address, u_short client_port);
static int pconnKey(char *buf, const char *host, u_short port, const char *domain, struct in_addr *client_address, u_short client_port);
static hash_table *table = NULL;
static struct _pconn *pconnNew(const char *key);
static void pconnDelete(struct _pconn *p);
static void pconnRemoveFD(struct _pconn *p, int fd);
static OBJH pconnHistDump;
static MemPool *pconn_data_pool = NULL;
static MemPool *pconn_fds_pool = NULL;

#define	PCONN_KEYLEN	(SQUIDHOSTNAMELEN + 30)

static int
pconnKey(char *buf, const char *host, u_short port, const char *domain,
    struct in_addr *client_address, u_short client_port)
{
    if (domain && client_address)
	return snprintf(buf, PCONN_KEYLEN, "%s.%d:%s.%d/%s", host, (int) port,
	    inet_ntoa(*client_address), (int) client_port, domain);
    else if (domain && (!client_address))
	return snprintf(buf, PCONN_KEYLEN, "%s.%d/%s", host, (int) port, domain);
    else if ((!domain) && client_address)
	return snprintf(buf, PCONN_KEYLEN, "%s.%d:%s.%d", host, (int) port,
	    inet_ntoa(*client_address), (int) client_port);
    else
	return snprintf(buf, PCONN_KEYLEN, "%s:%d", host, (int) port);
}

static struct _pconn *
pconnNew(const char *key)
{
    struct _pconn *p = memPoolAlloc(pconn_data_pool);
    p->hash.key = xstrdup(key);
    p->nfds_alloc = PCONN_FDS_SZ;
    p->fds = memPoolAlloc(pconn_fds_pool);
    debug(48, 3) ("pconnNew: adding %s\n", hashKeyStr(&p->hash));
    hash_join(table, &p->hash);
    return p;
}

static void
pconnDelete(struct _pconn *p)
{
    debug(48, 3) ("pconnDelete: deleting %s\n", hashKeyStr(&p->hash));
    hash_remove_link(table, (hash_link *) p);
    if (p->nfds_alloc == PCONN_FDS_SZ)
	memPoolFree(pconn_fds_pool, p->fds);
    else
	xfree(p->fds);
    xfree(p->hash.key);
    memPoolFree(pconn_data_pool, p);
}

static void
pconnRemoveFD(struct _pconn *p, int fd)
{
    int i;
    for (i = p->nfds - 1; i >= 0; i--) {
	if (p->fds[i] == fd)
	    break;
    }
    assert(i >= 0);
    debug(48, 3) ("pconnRemoveFD: found FD %d at index %d\n", fd, i);
    for (; i < p->nfds - 1; i++)
	p->fds[i] = p->fds[i + 1];
    if (--p->nfds == 0)
	pconnDelete(p);
}

static void
pconnTimeout(int fd, void *data)
{
    struct _pconn *p = data;
    assert(table != NULL);
    debug(48, 3) ("pconnTimeout: FD %d %s\n", fd, hashKeyStr(&p->hash));
    pconnRemoveFD(p, fd);
    comm_close(fd);
}

static void
pconnRead(int fd, void *data)
{
    LOCAL_ARRAY(char, buf, 256);
    struct _pconn *p = data;
    int n;
    assert(table != NULL);
    statCounter.syscalls.sock.reads++;
    n = FD_READ_METHOD(fd, buf, 256);
    debug(48, 3) ("pconnRead: %d bytes from FD %d, %s\n", n, fd,
	hashKeyStr(&p->hash));
    pconnRemoveFD(p, fd);
    comm_close(fd);
}

static void
pconnHistDump(StoreEntry * e)
{
    int i;
    storeAppendPrintf(e,
	"Client-side persistent connection counts:\n"
	"\n"
	"\treq/\n"
	"\tconn      count\n"
	"\t----  ---------\n");
    for (i = 0; i < PCONN_HIST_SZ; i++) {
	if (client_pconn_hist[i] == 0)
	    continue;
	storeAppendPrintf(e, "\t%4d  %9d\n", i, client_pconn_hist[i]);
    }
    storeAppendPrintf(e,
	"\n"
	"Server-side persistent connection counts:\n"
	"\n"
	"\treq/\n"
	"\tconn      count\n"
	"\t----  ---------\n");
    for (i = 0; i < PCONN_HIST_SZ; i++) {
	if (server_pconn_hist[i] == 0)
	    continue;
	storeAppendPrintf(e, "\t%4d  %9d\n", i, server_pconn_hist[i]);
    }
}

/* ========== PUBLIC FUNCTIONS ============================================ */


void
pconnInit(void)
{
    int i;
    assert(table == NULL);
    table = hash_create((HASHCMP *) strcmp, 229, hash_string);
    for (i = 0; i < PCONN_HIST_SZ; i++) {
	client_pconn_hist[i] = 0;
	server_pconn_hist[i] = 0;
    }
    pconn_data_pool = memPoolCreate("pconn_data", sizeof(struct _pconn));
    pconn_fds_pool = memPoolCreate("pconn_fds", PCONN_FDS_SZ * sizeof(int));

    cachemgrRegister("pconn",
	"Persistent Connection Utilization Histograms",
	pconnHistDump, 0, 1);
    debug(48, 3) ("persistent connection module initialized\n");
}

void
pconnPush(int fd, const char *host, u_short port, const char *domain, struct in_addr *client_address, u_short client_port)
{
    struct _pconn *p;
    int *old;
    LOCAL_ARRAY(char, desc, FD_DESC_SZ);
    LOCAL_ARRAY(char, key, PCONN_KEYLEN);
    if (fdUsageHigh()) {
	debug(48, 3) ("pconnPush: Not many unused FDs\n");
	comm_close(fd);
	return;
    } else if (shutting_down) {
	comm_close(fd);
	return;
    }
    assert(table != NULL);
    pconnKey(key, host, port, domain, client_address, client_port);
    p = (struct _pconn *) hash_lookup(table, key);
    if (p == NULL)
	p = pconnNew(key);
    if (p->nfds == p->nfds_alloc) {
	debug(48, 3) ("pconnPush: growing FD array\n");
	p->nfds_alloc <<= 1;
	old = p->fds;
	p->fds = xmalloc(p->nfds_alloc * sizeof(int));
	xmemcpy(p->fds, old, p->nfds * sizeof(int));
	if (p->nfds == PCONN_FDS_SZ)
	    memPoolFree(pconn_fds_pool, old);
	else
	    xfree(old);
    }
    p->fds[p->nfds++] = fd;
    commSetSelect(fd, COMM_SELECT_READ, pconnRead, p, 0);
    commSetTimeout(fd, Config.Timeout.pconn, pconnTimeout, p);
    snprintf(desc, FD_DESC_SZ, "%s idle connection", host);
    fd_note(fd, desc);
    debug(48, 3) ("pconnPush: pushed FD %d for %s\n", fd, key);
}

int
pconnPop(const char *host, u_short port, const char *domain, struct in_addr *client_address, u_short client_port, int *idle)
{
    struct _pconn *p;
    hash_link *hptr;
    int fd = -1;
    assert(table != NULL);
    hptr = pconnLookup(host, port, domain, client_address, client_port);
    if (hptr != NULL) {
	p = (struct _pconn *) hptr;
	assert(p->nfds > 0);
	fd = p->fds[p->nfds - 1];
	if (idle)
	    *idle = p->nfds - 1;
	pconnRemoveFD(p, fd);
	commSetSelect(fd, COMM_SELECT_READ, NULL, NULL, 0);
	commSetTimeout(fd, -1, NULL, NULL);
    }
    return fd;
}

static hash_link *
pconnLookup(const char *peer, u_short port, const char *domain, struct in_addr *client_address, u_short client_port)
{
    LOCAL_ARRAY(char, key, PCONN_KEYLEN);
    assert(table != NULL);
    pconnKey(key, peer, port, domain, client_address, client_port);
    return hash_lookup(table, key);
}

void
pconnHistCount(int what, int i)
{
    if (i >= PCONN_HIST_SZ)
	i = PCONN_HIST_SZ - 1;
    /* what == 0 for client, 1 for server */
    if (what == 0)
	client_pconn_hist[i]++;
    else if (what == 1)
	server_pconn_hist[i]++;
    else
	assert(0);
}
