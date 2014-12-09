
/*
 * $Id: mem.c,v 1.77.2.1 2008/01/02 15:49:31 hno Exp $
 *
 * DEBUG: section 13    High Level Memory Pool Management
 * AUTHOR: Harvest Derived
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

/* module globals */

static MemPool *MemPools[MEM_MAX];

/* string pools */
#define mem_str_pool_count 3
static const struct {
    const char *name;
    size_t obj_size;
} StrPoolsAttrs[mem_str_pool_count] = {

    {
	"Short Strings", 36,
    },				/* to fit rfc1123 and similar */
    {
	"Medium Strings", 128,
    },				/* to fit most urls */
    {
	"Long Strings", 512
    }				/* other */
};
static struct {
    MemPool *pool;
} StrPools[mem_str_pool_count];
static MemMeter StrCountMeter;
static MemMeter StrVolumeMeter;

static MemMeter HugeBufCountMeter;
static MemMeter HugeBufVolumeMeter;

/* local routines */

static void
memStringStats(StoreEntry * sentry)
{
    const char *pfmt = "%-20s\t %d\t %d\n";
    int i;
    int pooled_count = 0;
    size_t pooled_volume = 0;
    /* heading */
    storeAppendPrintf(sentry,
	"String Pool\t Impact\t\t\n"
	" \t (%%strings)\t (%%volume)\n");
    /* table body */
    for (i = 0; i < mem_str_pool_count; i++) {
	const MemPool *pool = StrPools[i].pool;
	const int plevel = pool->meter.inuse.level;
	storeAppendPrintf(sentry, pfmt,
	    pool->label,
	    xpercentInt(plevel, StrCountMeter.level),
	    xpercentInt(plevel * pool->obj_size, StrVolumeMeter.level));
	pooled_count += plevel;
	pooled_volume += plevel * pool->obj_size;
    }
    /* malloc strings */
    storeAppendPrintf(sentry, pfmt,
	"Other Strings",
	xpercentInt(StrCountMeter.level - pooled_count, StrCountMeter.level),
	xpercentInt(StrVolumeMeter.level - pooled_volume, StrVolumeMeter.level));
    storeAppendPrintf(sentry, "\n");
}

static void
memBufStats(StoreEntry * sentry)
{
    storeAppendPrintf(sentry, "Large buffers: %d (%d KB)\n",
	(int) HugeBufCountMeter.level,
	(int) HugeBufVolumeMeter.level / 1024);
}

static void
memStats(StoreEntry * sentry)
{
    storeBuffer(sentry);
    memReport(sentry);
    memStringStats(sentry);
    memBufStats(sentry);
    storeBufferFlush(sentry);
#if WITH_VALGRIND
    if (RUNNING_ON_VALGRIND) {
	long int leaked = 0, dubious = 0, reachable = 0, suppressed = 0;
	storeAppendPrintf(sentry, "Valgrind Report:\n");
	storeAppendPrintf(sentry, "Type\tAmount\n");
	debug(13, 1) ("Asking valgrind for memleaks\n");
	VALGRIND_DO_LEAK_CHECK;
	debug(13, 1) ("Getting valgrind statistics\n");
	VALGRIND_COUNT_LEAKS(leaked, dubious, reachable, suppressed);
	storeAppendPrintf(sentry, "Leaked\t%ld\n", leaked);
	storeAppendPrintf(sentry, "Dubious\t%ld\n", dubious);
	storeAppendPrintf(sentry, "Reachable\t%ld\n", reachable);
	storeAppendPrintf(sentry, "Suppressed\t%ld\n", suppressed);
    }
#endif
}


/*
 * public routines
 */

/*
 * we have a limit on _total_ amount of idle memory so we ignore
 * max_pages for now
 */
void
memDataInit(mem_type type, const char *name, size_t size, int max_pages_notused)
{
    assert(name && size);
    MemPools[type] = memPoolCreate(name, size);
}

static void
memDataNonZero(mem_type type)
{
    memPoolNonZero(MemPools[type]);
}


/* find appropriate pool and use it (pools always init buffer with 0s) */
void *
memAllocate(mem_type type)
{
    return memPoolAlloc(MemPools[type]);
}

/* give memory back to the pool */
void
memFree(void *p, int type)
{
    memPoolFree(MemPools[type], p);
}

/* allocate a variable size buffer using best-fit pool */
void *
memAllocString(size_t net_size, size_t * gross_size)
{
    int i;
    MemPool *pool = NULL;
    assert(gross_size);
    for (i = 0; i < mem_str_pool_count; i++) {
	if (net_size <= StrPoolsAttrs[i].obj_size) {
	    pool = StrPools[i].pool;
	    break;
	}
    }
    *gross_size = pool ? pool->obj_size : net_size;
    assert(*gross_size >= net_size);
    memMeterInc(StrCountMeter);
    memMeterAdd(StrVolumeMeter, *gross_size);
    return pool ? memPoolAlloc(pool) : xcalloc(1, net_size);
}

/* free buffer allocated with memAllocString() */
void
memFreeString(size_t size, void *buf)
{
    int i;
    MemPool *pool = NULL;
    assert(size && buf);
    for (i = 0; i < mem_str_pool_count; i++) {
	if (size <= StrPoolsAttrs[i].obj_size) {
	    assert(size == StrPoolsAttrs[i].obj_size);
	    pool = StrPools[i].pool;
	    break;
	}
    }
    memMeterDec(StrCountMeter);
    memMeterDel(StrVolumeMeter, size);
    pool ? memPoolFree(pool, buf) : xfree(buf);
}

/* Find the best fit MEM_X_BUF type */
static mem_type
memFindBufSizeType(size_t net_size, size_t * gross_size)
{
    mem_type type;
    size_t size;
    if (net_size <= 2 * 1024) {
	type = MEM_2K_BUF;
	size = 2 * 1024;
    } else if (net_size <= 4 * 1024) {
	type = MEM_4K_BUF;
	size = 4 * 1024;
    } else if (net_size <= 8 * 1024) {
	type = MEM_8K_BUF;
	size = 8 * 1024;
    } else if (net_size <= 16 * 1024) {
	type = MEM_16K_BUF;
	size = 16 * 1024;
    } else if (net_size <= 32 * 1024) {
	type = MEM_32K_BUF;
	size = 32 * 1024;
    } else if (net_size <= 64 * 1024) {
	type = MEM_64K_BUF;
	size = 64 * 1024;
    } else {
	type = MEM_NONE;
	size = net_size;
    }
    if (gross_size)
	*gross_size = size;
    return type;
}

/* allocate a variable size buffer using best-fit pool */
void *
memAllocBuf(size_t net_size, size_t * gross_size)
{
    mem_type type = memFindBufSizeType(net_size, gross_size);
    if (type != MEM_NONE)
	return memAllocate(type);
    else {
	memMeterInc(HugeBufCountMeter);
	memMeterAdd(HugeBufVolumeMeter, *gross_size);
	return xcalloc(1, net_size);
    }
}

/* resize a variable sized buffer using best-fit pool */
void *
memReallocBuf(void *oldbuf, size_t net_size, size_t * gross_size)
{
    /* XXX This can be optimized on very large buffers to use realloc() */
    size_t new_gross_size;
    void *newbuf = memAllocBuf(net_size, &new_gross_size);
    if (oldbuf) {
	int data_size = *gross_size;
	if (data_size > net_size)
	    data_size = net_size;
	memcpy(newbuf, oldbuf, data_size);
	memFreeBuf(*gross_size, oldbuf);
    }
    *gross_size = new_gross_size;
    return newbuf;
}

/* free buffer allocated with memAllocBuf() */
void
memFreeBuf(size_t size, void *buf)
{
    mem_type type = memFindBufSizeType(size, NULL);
    if (type != MEM_NONE)
	memFree(buf, type);
    else {
	xfree(buf);
	memMeterDec(HugeBufCountMeter);
	memMeterDel(HugeBufVolumeMeter, size);
    }
}


void
memInit(void)
{
    int i;
    memInitModule();
    /* set all pointers to null */
    memset(MemPools, '\0', sizeof(MemPools));
    /*
     * it does not hurt much to have a lot of pools since sizeof(MemPool) is
     * small; someday we will figure out what to do with all the entries here
     * that are never used or used only once; perhaps we should simply use
     * malloc() for those? @?@
     */
    memDataInit(MEM_2K_BUF, "2K Buffer", 2048, 10);
    memDataNonZero(MEM_2K_BUF);
    memDataInit(MEM_4K_BUF, "4K Buffer", 4096, 10);
    memDataNonZero(MEM_4K_BUF);
    memDataInit(MEM_8K_BUF, "8K Buffer", 8192, 10);
    memDataNonZero(MEM_8K_BUF);
    memDataInit(MEM_16K_BUF, "16K Buffer", 16384, 10);
    memDataNonZero(MEM_16K_BUF);
    memDataInit(MEM_32K_BUF, "32K Buffer", 32768, 10);
    memDataNonZero(MEM_32K_BUF);
    memDataInit(MEM_64K_BUF, "64K Buffer", 65536, 10);
    memDataNonZero(MEM_64K_BUF);
    memDataInit(MEM_STORE_CLIENT_BUF, "Store Client Buffer", STORE_CLIENT_BUF_SZ, 0);
    memDataNonZero(MEM_STORE_CLIENT_BUF);
    memDataInit(MEM_ACL, "acl", sizeof(acl), 0);
    memDataInit(MEM_ACL_DENY_INFO_LIST, "acl_deny_info_list",
	sizeof(acl_deny_info_list), 0);
    memDataInit(MEM_ACL_IP_DATA, "acl_ip_data", sizeof(acl_ip_data), 0);
    memDataInit(MEM_ACL_LIST, "acl_list", sizeof(acl_list), 0);
    memDataInit(MEM_ACL_NAME_LIST, "acl_name_list", sizeof(acl_name_list), 0);
#if USE_SSL
    memDataInit(MEM_ACL_CERT_DATA, "acl_cert_data", sizeof(acl_cert_data), 0);
#endif
    memDataInit(MEM_ACL_TIME_DATA, "acl_time_data", sizeof(acl_time_data), 0);
    memDataInit(MEM_ACL_REQUEST_TYPE, "acl_request_type", sizeof(acl_request_type), 0);
    memDataInit(MEM_AUTH_USER_T, "auth_user_t",
	sizeof(auth_user_t), 0);
    memDataInit(MEM_AUTH_USER_HASH, "auth_user_hash_pointer",
	sizeof(auth_user_hash_pointer), 0);
    memDataInit(MEM_ACL_PROXY_AUTH_MATCH, "acl_proxy_auth_match_cache",
	sizeof(acl_proxy_auth_match_cache), 0);
    memDataInit(MEM_ACL_USER_DATA, "acl_user_data",
	sizeof(acl_user_data), 0);
#if USE_CACHE_DIGESTS
    memDataInit(MEM_CACHE_DIGEST, "CacheDigest", sizeof(CacheDigest), 0);
#endif
    memDataInit(MEM_LINK_LIST, "link_list", sizeof(link_list), 10);
    memDataInit(MEM_DLINK_NODE, "dlink_node", sizeof(dlink_node), 10);
    memDataInit(MEM_DREAD_CTRL, "dread_ctrl", sizeof(dread_ctrl), 0);
    memDataInit(MEM_DWRITE_Q, "dwrite_q", sizeof(dwrite_q), 0);
    memDataInit(MEM_FWD_SERVER, "FwdServer", sizeof(FwdServer), 0);
    memDataInit(MEM_HTTP_REPLY, "HttpReply", sizeof(HttpReply), 0);
    memDataInit(MEM_HTTP_HDR_ENTRY, "HttpHeaderEntry", sizeof(HttpHeaderEntry), 0);
    memDataInit(MEM_HTTP_HDR_CC, "HttpHdrCc", sizeof(HttpHdrCc), 0);
    memDataInit(MEM_HTTP_HDR_RANGE_SPEC, "HttpHdrRangeSpec", sizeof(HttpHdrRangeSpec), 0);
    memDataInit(MEM_HTTP_HDR_RANGE, "HttpHdrRange", sizeof(HttpHdrRange), 0);
    memDataInit(MEM_HTTP_HDR_CONTENT_RANGE, "HttpHdrContRange", sizeof(HttpHdrContRange), 0);
    memDataInit(MEM_INTLIST, "intlist", sizeof(intlist), 0);
    memDataInit(MEM_MEMOBJECT, "MemObject", sizeof(MemObject),
	Squid_MaxFD >> 3);
    memDataInit(MEM_MEM_NODE, "mem_node", sizeof(mem_node), 0);
    memDataNonZero(MEM_MEM_NODE);
    memDataInit(MEM_NETDBENTRY, "netdbEntry", sizeof(netdbEntry), 0);
    memDataInit(MEM_NET_DB_NAME, "net_db_name", sizeof(net_db_name), 0);
    memDataInit(MEM_RELIST, "relist", sizeof(relist), 0);
    memDataInit(MEM_REQUEST_T, "request_t", sizeof(request_t),
	Squid_MaxFD >> 3);
    memDataInit(MEM_STOREENTRY, "StoreEntry", sizeof(StoreEntry), 0);
    memDataInit(MEM_WORDLIST, "wordlist", sizeof(wordlist), 0);
    memDataInit(MEM_CLIENT_INFO, "ClientInfo", sizeof(ClientInfo), 0);
    memDataInit(MEM_MD5_DIGEST, "MD5 digest", SQUID_MD5_DIGEST_LENGTH, 0);
    memDataInit(MEM_HELPER_REQUEST, "helper_request",
	sizeof(helper_request), 0);
    memDataInit(MEM_HELPER_STATEFUL_REQUEST, "helper_stateful_request",
	sizeof(helper_stateful_request), 0);
    memDataInit(MEM_TLV, "storeSwapTLV", sizeof(tlv), 0);
    memDataInit(MEM_SWAP_LOG_DATA, "storeSwapLogData", sizeof(storeSwapLogData), 0);

    /* init string pools */
    for (i = 0; i < mem_str_pool_count; i++) {
	StrPools[i].pool = memPoolCreate(StrPoolsAttrs[i].name, StrPoolsAttrs[i].obj_size);
	memPoolNonZero(StrPools[i].pool);
    }
    cachemgrRegister("mem",
	"Memory Utilization",
	memStats, 0, 1);
}

/*
 * Test that all entries are initialized
 */
void
memCheckInit(void)
{
    mem_type t;
    for (t = MEM_NONE, t++; t < MEM_MAX; t++) {
	if (MEM_DONTFREE == t)
	    continue;
	/*
	 * If you hit this assertion, then you forgot to add a
	 * memDataInit() line for type 't'.
	 */
	assert(MemPools[t]);
    }
}

void
memClean(void)
{
    memCleanModule();
}

int
memInUse(mem_type type)
{
    return memPoolInUseCount(MemPools[type]);
}

/* ick */

static void
memFree2K(void *p)
{
    memFree(p, MEM_2K_BUF);
}

void
memFree4K(void *p)
{
    memFree(p, MEM_4K_BUF);
}

void
memFree8K(void *p)
{
    memFree(p, MEM_8K_BUF);
}

static void
memFree16K(void *p)
{
    memFree(p, MEM_16K_BUF);
}

static void
memFree32K(void *p)
{
    memFree(p, MEM_32K_BUF);
}

static void
memFree64K(void *p)
{
    memFree(p, MEM_64K_BUF);
}

FREE *
memFreeBufFunc(size_t size)
{
    switch (size) {
    case 2 * 1024:
	return memFree2K;
    case 4 * 1024:
	return memFree4K;
    case 8 * 1024:
	return memFree8K;
    case 16 * 1024:
	return memFree16K;
    case 32 * 1024:
	return memFree32K;
    case 64 * 1024:
	return memFree64K;
    default:
	memMeterDec(HugeBufCountMeter);
	memMeterDel(HugeBufVolumeMeter, size);
	return xfree;
    }
}
