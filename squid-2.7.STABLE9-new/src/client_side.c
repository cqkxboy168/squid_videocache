
/*
 * $Id: client_side.c,v 1.754.2.29 2010/02/14 00:46:25 hno Exp $
 *
 * DEBUG: section 33    Client-side Routines
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

#if IPF_TRANSPARENT
#if HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#include <netinet/tcp.h>
#include <net/if.h>
/* SG - 14 Aug 2005
 * Workaround needed to allow the build of both ipfilter and ARP acl
 * support on Solaris x86.
 * 
 * Some defines, like
 * #define free +
 * are used in squid.h to block misuse of standard malloc routines
 * where the Squid versions should be used. This pollutes the C/C++
 * token namespace crashing any structures or classes having members
 * of the same names.
 */
#ifdef _SQUID_SOLARIS_
#undef free
#endif
#ifdef HAVE_IPL_H
#include <ipl.h>
#elif HAVE_NETINET_IPL_H
#include <netinet/ipl.h>
#endif
#if HAVE_IP_FIL_COMPAT_H
#include <ip_fil_compat.h>
#elif HAVE_NETINET_IP_FIL_COMPAT_H
#include <netinet/ip_fil_compat.h>
#elif HAVE_IP_COMPAT_H
#include <ip_compat.h>
#elif HAVE_NETINET_IP_COMPAT_H
#include <netinet/ip_compat.h>
#endif
#if HAVE_IP_FIL_H
#include <ip_fil.h>
#elif HAVE_NETINET_IP_FIL_H
#include <netinet/ip_fil.h>
#endif
#if HAVE_IP_NAT_H
#include <ip_nat.h>
#elif HAVE_NETINET_IP_NAT_H
#include <netinet/ip_nat.h>
#endif
#endif

#if PF_TRANSPARENT
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <net/pfvar.h>
#endif

#if LINUX_NETFILTER
#include <linux/types.h>
#include <linux/netfilter_ipv4.h>
#endif


#if LINGERING_CLOSE
#define comm_close comm_lingering_close
#endif

static const char *const crlf = "\r\n";

#define FAILURE_MODE_TIME 300

/* Local functions */

static CWCB clientWriteComplete;
static CWCB clientWriteBodyComplete;
static PF clientReadRequest;
static PF connStateFree;
static PF requestTimeout;
static PF clientLifetimeTimeout;
static int clientCheckTransferDone(clientHttpRequest *);
static int clientGotNotEnough(clientHttpRequest *);
static void checkFailureRatio(err_type, hier_code);
static void clientProcessMiss(clientHttpRequest *);
static void clientProcessHit(clientHttpRequest * http);
static void clientBuildReplyHeader(clientHttpRequest * http, HttpReply * rep);
static clientHttpRequest *parseHttpRequestAbort(ConnStateData * conn, const char *uri);
static clientHttpRequest *parseHttpRequest(ConnStateData *, HttpMsgBuf *, method_t *, int *);

static void clientCheckNoCache(clientHttpRequest *);
static void clientCheckNoCacheDone(int answer, void *data);
static STHCB clientHandleIMSReply;
static int clientGetsOldEntry(StoreEntry * new, StoreEntry * old, request_t * request);
#if USE_IDENT
static IDCB clientIdentDone;
#endif
#if FOLLOW_X_FORWARDED_FOR
static void clientFollowXForwardedForStart(void *data);
static void clientFollowXForwardedForNext(void *data);
static void clientFollowXForwardedForDone(int answer, void *data);
#endif /* FOLLOW_X_FORWARDED_FOR */
static int clientOnlyIfCached(clientHttpRequest * http);
static STCB clientSendMoreData;
static STHCB clientSendHeaders;
static STHCB clientCacheHit;
static void clientSetKeepaliveFlag(clientHttpRequest *);
static void clientPackRangeHdr(const HttpReply * rep, const HttpHdrRangeSpec * spec, String boundary, MemBuf * mb);
static void clientPackTermBound(String boundary, MemBuf * mb);
static void clientProcessRequest(clientHttpRequest *);
static void clientProcessExpired(clientHttpRequest *);
static void clientRefreshCheck(clientHttpRequest *);
static REFRESHCHECK clientRefreshCheckDone;
static void clientProcessOnlyIfCachedMiss(clientHttpRequest * http);
static int clientCachable(clientHttpRequest * http);
static int clientHierarchical(clientHttpRequest * http);
static int clientCheckContentLength(request_t * r);
static DEFER httpAcceptDefer;
static log_type clientProcessRequest2(clientHttpRequest * http);
static int clientReplyBodyTooLarge(clientHttpRequest *, squid_off_t clen);
static int clientRequestBodyTooLarge(squid_off_t clen);
static void clientProcessBody(ConnStateData * conn);
static void clientEatRequestBody(clientHttpRequest *);
static void clientAccessCheck(void *data);
static void clientAccessCheckDone(int answer, void *data);
static void clientAccessCheckDone2(int answer, void *data);
static BODY_HANDLER clientReadBody;
static void clientAbortBody(ConnStateData * conn, request_t * req);
#if USE_SSL
static void httpsAcceptSSL(ConnStateData * connState, SSL_CTX * sslContext);
#endif
static int varyEvaluateMatch(StoreEntry * entry, request_t * request);
static int modifiedSince(StoreEntry *, request_t *);
static StoreEntry *clientCreateStoreEntry(clientHttpRequest *, method_t, request_flags);
static inline int clientNatLookup(ConnStateData * conn);

/* Temporary here while restructuring stuff */
static void
storeClientCopyHeadersCB(void *data, char *buf, ssize_t size)
{
    clientHttpRequest *http = data;
    assert(http->header_callback);
    assert(http->header_entry);
    memFree(buf, MEM_STORE_CLIENT_BUF);
    if (!http->header_entry)
	return;
    if (size < 0 || !memHaveHeaders(http->header_entry->mem_obj)) {
	http->header_callback(data, NULL);
	return;
    }
    http->header_callback(data, http->header_entry->mem_obj->reply);
}
void
storeClientCopyHeaders(store_client * sc, StoreEntry * e, STHCB * callback, void *callback_data)
{
    clientHttpRequest *http = callback_data;
    http->header_callback = callback;
    http->header_entry = e;
    storeClientCopy(http->sc, e, 0, 0, STORE_CLIENT_BUF_SZ, memAllocate(MEM_STORE_CLIENT_BUF), storeClientCopyHeadersCB, http);
}

#if USE_IDENT
static void
clientIdentDone(const char *ident, void *data)
{
    ConnStateData *conn = data;
    xstrncpy(conn->rfc931, ident ? ident : dash_str, USER_IDENT_SZ);
}

#endif

aclCheck_t *
clientAclChecklistCreate(const acl_access * acl, const clientHttpRequest * http)
{
    aclCheck_t *ch;
    ConnStateData *conn = http->conn;
    ch = aclChecklistCreate(acl,
	http->request,
	conn->rfc931);

    /*
     * hack for ident ACL. It needs to get full addresses, and a
     * place to store the ident result on persistent connections...
     */
    /* connection oriented auth also needs these two lines for it's operation. */
    ch->conn = conn;
    cbdataLock(ch->conn);

    return ch;
}

#if FOLLOW_X_FORWARDED_FOR
/*
 * clientFollowXForwardedForStart() copies the X-Forwarded-For
 * header into x_forwarded_for_iterator and passes control to
 * clientFollowXForwardedForNext().
 *
 * clientFollowXForwardedForNext() checks the indirect_client_addr
 * against the followXFF ACL and passes the result to
 * clientFollowXForwardedForDone().
 *
 * clientFollowXForwardedForDone() either grabs the next address
 * from the tail of x_forwarded_for_iterator and loops back to
 * clientFollowXForwardedForNext(), or cleans up and passes control to
 * clientAccessCheck().
 */

static void
clientFollowXForwardedForStart(void *data)
{
    clientHttpRequest *http = data;
    request_t *request = http->request;
    request->x_forwarded_for_iterator = httpHeaderGetList(
	&request->header, HDR_X_FORWARDED_FOR);
    debug(33, 5) ("clientFollowXForwardedForStart: indirect_client_addr=%s XFF='%s'\n",
	inet_ntoa(request->indirect_client_addr),
	strBuf(request->x_forwarded_for_iterator));
    clientFollowXForwardedForNext(http);
}

static void
clientFollowXForwardedForNext(void *data)
{
    clientHttpRequest *http = data;
    request_t *request = http->request;
    debug(33, 5) ("clientFollowXForwardedForNext: indirect_client_addr=%s XFF='%s'\n",
	inet_ntoa(request->indirect_client_addr),
	strBuf(request->x_forwarded_for_iterator));
    if (strLen(request->x_forwarded_for_iterator) != 0) {
	/* check the acl to see whether to believe the X-Forwarded-For header */
	http->acl_checklist = clientAclChecklistCreate(
	    Config.accessList.followXFF, http);
	aclNBCheck(http->acl_checklist, clientFollowXForwardedForDone, http);
    } else {
	/* nothing left to follow */
	debug(33, 5) ("clientFollowXForwardedForNext: nothing more to do\n");
	clientFollowXForwardedForDone(-1, http);
    }
}

static void
clientFollowXForwardedForDone(int answer, void *data)
{
    clientHttpRequest *http = data;
    request_t *request = http->request;
    /*
     * answer should be be ACCESS_ALLOWED or ACCESS_DENIED if we are
     * called as a result of ACL checks, or -1 if we are called when
     * there's nothing left to do.
     */
    if (answer == ACCESS_ALLOWED) {
	/*
	 * The IP address currently in request->indirect_client_addr
	 * is trusted to use X-Forwarded-For.  Remove the last
	 * comma-delimited element from x_forwarded_for_iterator and use
	 * it to to replace indirect_client_addr, then repeat the cycle.
	 */
	const char *p;
	const char *asciiaddr;
	int l;
	struct in_addr addr;
	debug(33, 5) ("clientFollowXForwardedForDone: indirect_client_addr=%s is trusted\n",
	    inet_ntoa(request->indirect_client_addr));
	p = strBuf(request->x_forwarded_for_iterator);
	l = strLen(request->x_forwarded_for_iterator);

	/*
	 * XXX x_forwarded_for_iterator should really be a list of
	 * IP addresses, but it's a String instead.  We have to
	 * walk backwards through the String, biting off the last
	 * comma-delimited part each time.  As long as the data is in
	 * a String, we should probably implement and use a variant of
	 * strListGetItem() that walks backwards instead of forwards
	 * through a comma-separated list.  But we don't even do that;
	 * we just do the work in-line here.
	 */
	/* skip trailing space and commas */
	while (l > 0 && (p[l - 1] == ',' || xisspace(p[l - 1])))
	    l--;
	strCut(request->x_forwarded_for_iterator, l);
	/* look for start of last item in list */
	while (l > 0 && !(p[l - 1] == ',' || xisspace(p[l - 1])))
	    l--;
	asciiaddr = p + l;
	if (inet_aton(asciiaddr, &addr) == 0) {
	    /* the address is not well formed; do not use it */
	    debug(33, 3) ("clientFollowXForwardedForDone: malformed address '%s'\n",
		asciiaddr);
	    goto done;
	}
	debug(33, 3) ("clientFollowXForwardedForDone: changing indirect_client_addr from %s to '%s'\n",
	    inet_ntoa(request->indirect_client_addr),
	    asciiaddr);
	request->indirect_client_addr = addr;
	strCut(request->x_forwarded_for_iterator, l);
	if (!Config.onoff.acl_uses_indirect_client) {
	    /*
	     * If acl_uses_indirect_client is off, then it's impossible
	     * to follow more than one level of X-Forwarded-For.
	     */
	    goto done;
	}
	clientFollowXForwardedForNext(http);
	return;
    } else if (answer == ACCESS_DENIED) {
	debug(33, 5) ("clientFollowXForwardedForDone: indirect_client_addr=%s not trusted\n",
	    inet_ntoa(request->indirect_client_addr));
    } else {
	debug(33, 5) ("clientFollowXForwardedForDone: indirect_client_addr=%s nothing more to do\n",
	    inet_ntoa(request->indirect_client_addr));
    }
  done:
    /* clean up, and pass control to clientAccessCheck */
    debug(33, 6) ("clientFollowXForwardedForDone: cleanup\n");
    if (Config.onoff.log_uses_indirect_client) {
	/*
	 * Ensure that the access log shows the indirect client
	 * instead of the direct client.
	 */
	ConnStateData *conn = http->conn;
	conn->log_addr = request->indirect_client_addr;
	conn->log_addr.s_addr &= Config.Addrs.client_netmask.s_addr;
	debug(33, 3) ("clientFollowXForwardedForDone: setting log_addr=%s\n",
	    inet_ntoa(conn->log_addr));
    }
    stringClean(&request->x_forwarded_for_iterator);
    http->acl_checklist = NULL;	/* XXX do we need to aclChecklistFree() ? */
    clientAccessCheck(http);
}
#endif /* FOLLOW_X_FORWARDED_FOR */

static void
clientCheckFollowXForwardedFor(void *data)
{
    clientHttpRequest *http = data;
#if FOLLOW_X_FORWARDED_FOR
    if (Config.accessList.followXFF && httpHeaderHas(&http->request->header, HDR_X_FORWARDED_FOR)) {
	clientFollowXForwardedForStart(http);
	return;
    }
#endif
    clientAccessCheck(http);
}

static void
clientAccessCheck(void *data)
{
    clientHttpRequest *http = data;
    http->acl_checklist = clientAclChecklistCreate(Config.accessList.http, http);
    aclNBCheck(http->acl_checklist, clientAccessCheckDone, http);
}

void
clientAccessCheck2(void *data)
{
    clientHttpRequest *http = data;
    if (Config.accessList.http2 && !http->redirect.status) {
	http->acl_checklist = clientAclChecklistCreate(Config.accessList.http2, http);
	aclNBCheck(http->acl_checklist, clientAccessCheckDone2, http);
    } else {
	clientCheckNoCache(http);
    }
}

/*
 * returns true if client specified that the object must come from the cache
 * without contacting origin server
 */
static int
clientOnlyIfCached(clientHttpRequest * http)
{
    const request_t *r = http->request;
    assert(r);
    return r->cache_control &&
	EBIT_TEST(r->cache_control->mask, CC_ONLY_IF_CACHED);
}

static StoreEntry *
clientCreateStoreEntry(clientHttpRequest * h, method_t m, request_flags flags)
{
    StoreEntry *e;
    /*
     * For erroneous requests, we might not have a h->request,
     * so make a fake one.
     */
    if (h->request == NULL)
	h->request = requestLink(requestCreate(m, PROTO_NONE, null_string));
    e = storeCreateEntry(h->uri, flags, m);
    if (h->request->store_url)
	storeEntrySetStoreUrl(e, h->request->store_url);
    h->sc = storeClientRegister(e, h);
#if DELAY_POOLS
    if (h->log_type != LOG_TCP_DENIED)
	delaySetStoreClient(h->sc, delayClient(h));
#endif
    storeClientCopyHeaders(h->sc, e, clientSendHeaders, h);
    return e;
}

/*
 * This is called by the last client request rewriter chain thing.
 */
void
clientFinishRewriteStuff(clientHttpRequest * http)
{
    /* This is the final part of the rewrite chain - this should be broken out! */
    clientInterpretRequestHeaders(http);
    /* XXX This really should become a ref-counted string type pointer, not a copy! */
    fd_note(http->conn->fd, http->uri);
#if HEADERS_LOG
    headersLog(0, 1, http->request->method, http->request);
#endif
    clientAccessCheck2(http);

}

static void
clientAccessCheckDone(int answer, void *data)
{
    clientHttpRequest *http = data;
    err_type page_id;
    http_status status;
    ErrorState *err = NULL;
    char *proxy_auth_msg = NULL;
    debug(33, 2) ("The request %s %s is %s, because it matched '%s'\n",
	RequestMethods[http->request->method].str, http->uri,
	answer == ACCESS_ALLOWED ? "ALLOWED" : "DENIED",
	AclMatchedName ? AclMatchedName : "NO ACL's");
    proxy_auth_msg = authenticateAuthUserRequestMessage(http->conn->auth_user_request ? http->conn->auth_user_request : http->request->auth_user_request);
    http->acl_checklist = NULL;
    if (answer == ACCESS_ALLOWED) {
	safe_free(http->uri);
	http->uri = xstrdup(urlCanonical(http->request));
	assert(http->redirect_state == REDIRECT_NONE);
	http->redirect_state = REDIRECT_PENDING;
	clientRedirectStart(http);
    } else {
	int require_auth = (answer == ACCESS_REQ_PROXY_AUTH || aclIsProxyAuth(AclMatchedName)) && !http->request->flags.transparent;
	debug(33, 5) ("Access Denied: %s\n", http->uri);
	debug(33, 5) ("AclMatchedName = %s\n",
	    AclMatchedName ? AclMatchedName : "<null>");
	debug(33, 5) ("Proxy Auth Message = %s\n",
	    proxy_auth_msg ? proxy_auth_msg : "<null>");
	/*
	 * NOTE: get page_id here, based on AclMatchedName because
	 * if USE_DELAY_POOLS is enabled, then AclMatchedName gets
	 * clobbered in the clientCreateStoreEntry() call
	 * just below.  Pedro Ribeiro <pribeiro@isel.pt>
	 */
	page_id = aclGetDenyInfoPage(&Config.denyInfoList, AclMatchedName, answer != ACCESS_REQ_PROXY_AUTH);
	http->log_type = LOG_TCP_DENIED;
	http->entry = clientCreateStoreEntry(http, http->request->method,
	    null_request_flags);
	if (require_auth) {
	    if (!http->flags.accel) {
		/* Proxy authorisation needed */
		status = HTTP_PROXY_AUTHENTICATION_REQUIRED;
	    } else {
		/* WWW authorisation needed */
		status = HTTP_UNAUTHORIZED;
	    }
	    if (page_id == ERR_NONE)
		page_id = ERR_CACHE_ACCESS_DENIED;
	} else {
	    status = HTTP_FORBIDDEN;
	    if (page_id == ERR_NONE)
		page_id = ERR_ACCESS_DENIED;
	}
	err = errorCon(page_id, status, http->orig_request);
	if (http->conn->auth_user_request)
	    err->auth_user_request = http->conn->auth_user_request;
	else if (http->request->auth_user_request)
	    err->auth_user_request = http->request->auth_user_request;
	/* lock for the error state */
	if (err->auth_user_request)
	    authenticateAuthUserRequestLock(err->auth_user_request);
	err->callback_data = NULL;
	errorAppendEntry(http->entry, err);
    }
}

static void
clientAccessCheckDone2(int answer, void *data)
{
    clientHttpRequest *http = data;
    err_type page_id;
    http_status status;
    ErrorState *err = NULL;
    char *proxy_auth_msg = NULL;
    debug(33, 2) ("The request %s %s is %s, because it matched '%s'\n",
	RequestMethods[http->request->method].str, http->uri,
	answer == ACCESS_ALLOWED ? "ALLOWED" : "DENIED",
	AclMatchedName ? AclMatchedName : "NO ACL's");
    proxy_auth_msg = authenticateAuthUserRequestMessage(http->conn->auth_user_request ? http->conn->auth_user_request : http->request->auth_user_request);
    http->acl_checklist = NULL;
    if (answer == ACCESS_ALLOWED) {
	clientCheckNoCache(http);
    } else {
	int require_auth = (answer == ACCESS_REQ_PROXY_AUTH || aclIsProxyAuth(AclMatchedName));
	debug(33, 5) ("Access Denied: %s\n", http->uri);
	debug(33, 5) ("AclMatchedName = %s\n",
	    AclMatchedName ? AclMatchedName : "<null>");
	if (require_auth)
	    debug(33, 5) ("Proxy Auth Message = %s\n",
		proxy_auth_msg ? proxy_auth_msg : "<null>");
	/*
	 * NOTE: get page_id here, based on AclMatchedName because
	 * if USE_DELAY_POOLS is enabled, then AclMatchedName gets
	 * clobbered in the clientCreateStoreEntry() call
	 * just below.  Pedro Ribeiro <pribeiro@isel.pt>
	 */
	page_id = aclGetDenyInfoPage(&Config.denyInfoList, AclMatchedName, answer != ACCESS_REQ_PROXY_AUTH);
	http->log_type = LOG_TCP_DENIED;
	http->entry = clientCreateStoreEntry(http, http->request->method,
	    null_request_flags);
	if (require_auth) {
	    if (!http->flags.accel) {
		/* Proxy authorisation needed */
		status = HTTP_PROXY_AUTHENTICATION_REQUIRED;
	    } else {
		/* WWW authorisation needed */
		status = HTTP_UNAUTHORIZED;
	    }
	    if (page_id == ERR_NONE)
		page_id = ERR_CACHE_ACCESS_DENIED;
	} else {
	    status = HTTP_FORBIDDEN;
	    if (page_id == ERR_NONE)
		page_id = ERR_ACCESS_DENIED;
	}
	err = errorCon(page_id, status, http->orig_request);
	if (http->conn->auth_user_request)
	    err->auth_user_request = http->conn->auth_user_request;
	else if (http->request->auth_user_request)
	    err->auth_user_request = http->request->auth_user_request;
	/* lock for the error state */
	if (err->auth_user_request)
	    authenticateAuthUserRequestLock(err->auth_user_request);
	err->callback_data = NULL;
	errorAppendEntry(http->entry, err);
    }
}

static void
clientCheckNoCache(clientHttpRequest * http)
{
    if (Config.accessList.noCache && http->request->flags.cachable) {
	http->acl_checklist = clientAclChecklistCreate(Config.accessList.noCache, http);
	aclNBCheck(http->acl_checklist, clientCheckNoCacheDone, http);
    } else {
	clientCheckNoCacheDone(http->request->flags.cachable, http);
    }
}

void
clientCheckNoCacheDone(int answer, void *data)
{
    clientHttpRequest *http = data;
    http->request->flags.cachable = answer;
    http->acl_checklist = NULL;
    clientProcessRequest(http);
}

static void
clientHandleETagMiss(clientHttpRequest * http)
{
    StoreEntry *entry = http->entry;
    request_t *request = http->request;

    request->done_etag = 1;
    if (request->vary) {
	storeLocateVaryDone(request->vary);
	request->vary = NULL;
	request->etags = NULL;	/* pointed into request->vary */
    }
    safe_free(request->etag);
    safe_free(request->vary_headers);
    safe_free(request->vary_hdr);
    storeClientUnregister(http->sc, entry, http);
    storeUnlockObject(entry);
    http->entry = NULL;
    clientProcessRequest(http);
}

static void
clientHandleETagReply(void *data, HttpReply * rep)
{
    clientHttpRequest *http = data;
    StoreEntry *entry = http->entry;
    const char *url = storeLookupUrl(entry);
    if (entry == NULL) {
	/* client aborted */
	return;
    }
    if (!rep) {
	debug(33, 3) ("clientHandleETagReply: FAILED '%s'\n", url);
	clientHandleETagMiss(http);
	return;
    }
    if (EBIT_TEST(entry->flags, ENTRY_ABORTED)) {
	debug(33, 3) ("clientHandleETagReply: ABORTED '%s'\n", url);
	clientHandleETagMiss(http);
	return;
    }
    debug(33, 3) ("clientHandleETagReply: %s = %d\n", url, (int) rep->sline.status);
    if (HTTP_NOT_MODIFIED == rep->sline.status) {
	/* Remember the ETag and restart */
	if (rep) {
	    request_t *request = http->request;
	    const char *etag = httpHeaderGetStr(&rep->header, HDR_ETAG);
	    const char *vary = request->vary_headers;
	    int has_vary = httpHeaderHas(&rep->header, HDR_VARY);
#if X_ACCELERATOR_VARY
	    has_vary |= httpHeaderHas(&rep->header, HDR_X_ACCELERATOR_VARY);
#endif
	    if (has_vary)
		vary = httpMakeVaryMark(request, rep);

	    if (etag && vary) {
		storeAddVary(entry->mem_obj->store_url, entry->mem_obj->url, entry->mem_obj->method, NULL, httpHeaderGetStr(&rep->header, HDR_ETAG), request->vary_hdr, request->vary_headers, strBuf(request->vary_encoding));
	    }
	}
	clientHandleETagMiss(http);
	return;
    }
    /* Send the new object to the client */
    clientSendHeaders(data, rep);
    return;
}

static void
clientProcessETag(clientHttpRequest * http)
{
    char *url = http->uri;
    StoreEntry *entry = NULL;
    debug(33, 3) ("clientProcessETag: '%s'\n", http->uri);
    entry = storeCreateEntry(url, http->request->flags, http->request->method);
    if (http->request->store_url)
	storeEntrySetStoreUrl(entry, http->request->store_url);
    http->sc = storeClientRegister(entry, http);
#if DELAY_POOLS
    /* delay_id is already set on original store client */
    delaySetStoreClient(http->sc, delayClient(http));
#endif
    http->entry = entry;
    http->out.offset = 0;
    fwdStart(http->conn->fd, http->entry, http->request);
    /* Register with storage manager to receive updates when data comes in. */
    if (EBIT_TEST(entry->flags, ENTRY_ABORTED))
	debug(33, 0) ("clientProcessETag: found ENTRY_ABORTED object\n");
    storeClientCopyHeaders(http->sc, entry,
	clientHandleETagReply,
	http);
}

static void
clientRefreshCheck(clientHttpRequest * http)
{
    refreshCheckSubmit(http->entry, clientRefreshCheckDone, http);
}

static void
clientRefreshCheckDone(void *data, int fresh, const char *log)
{
    clientHttpRequest *http = data;
    if (log) {
	safe_free(http->al.ext_refresh);
	http->al.ext_refresh = xstrdup(log);
    }
    if (fresh)
	clientProcessHit(http);
    else
	clientProcessExpired(http);
}

static void
clientProcessExpired(clientHttpRequest * http)
{
    char *url = http->uri;
    StoreEntry *entry = NULL;
    int hit = 0;
    const char *etag;
    const int can_revalidate = http->entry->mem_obj->reply->sline.status == HTTP_OK;
    debug(33, 3) ("clientProcessExpired: '%s'\n", http->uri);
    /*
     * check if we are allowed to contact other servers
     * @?@: Instead of a 504 (Gateway Timeout) reply, we may want to return 
     *      a stale entry *if* it matches client requirements
     */
    if (clientOnlyIfCached(http)) {
	clientProcessOnlyIfCachedMiss(http);
	return;
    }
    http->request->flags.refresh = 1;
    http->old_entry = http->entry;
    http->old_sc = http->sc;
    if (http->entry->mem_obj && http->entry->mem_obj->ims_entry) {
	entry = http->entry->mem_obj->ims_entry;
	debug(33, 5) ("clientProcessExpired: collapsed request\n");
	if (EBIT_TEST(entry->flags, ENTRY_ABORTED)) {
	    debug(33, 1) ("clientProcessExpired: collapsed request ABORTED!\n");
	    entry = NULL;
	} else if (http->entry->mem_obj->refresh_timestamp + 30 < squid_curtime) {
	    debug(33, 1) ("clientProcessExpired: collapsed request STALE!\n");
	    entry = NULL;
	}
	if (entry) {
	    http->request->flags.collapsed = 1;		/* Don't trust the store entry */
	    storeLockObject(entry);
	    hit = 1;
	} else {
	    storeUnlockObject(http->entry->mem_obj->ims_entry);
	    http->entry->mem_obj->ims_entry = NULL;
	}
    }
    if (!entry) {
	entry = storeCreateEntry(url, http->request->flags, http->request->method);
	if (http->request->store_url)
	    storeEntrySetStoreUrl(entry, http->request->store_url);
	if (http->entry->mem_obj) {
	    http->entry->mem_obj->refresh_timestamp = squid_curtime;
	    if (Config.onoff.collapsed_forwarding) {
		http->entry->mem_obj->ims_entry = entry;
		storeLockObject(http->entry->mem_obj->ims_entry);
	    }
	}
    }
    if (entry->mem_obj->old_entry) {
	storeUnlockObject(entry->mem_obj->old_entry);
	entry->mem_obj->old_entry = NULL;
    }
    entry->mem_obj->old_entry = http->old_entry;
    storeLockObject(entry->mem_obj->old_entry);
    http->sc = storeClientRegister(entry, http);
#if DELAY_POOLS
    /* delay_id is already set on original store client */
    delaySetStoreClient(http->sc, delayClient(http));
#endif
    if (can_revalidate && http->old_entry->lastmod > 0) {
	http->request->lastmod = http->old_entry->lastmod;
	http->request->flags.cache_validation = 1;
    } else
	http->request->lastmod = -1;
    debug(33, 5) ("clientProcessExpired: lastmod %ld\n", (long int) entry->lastmod);
    /* NOTE, don't call storeLockObject(), storeCreateEntry() does it */
    http->entry = entry;
    http->out.offset = 0;
    if (can_revalidate) {
	etag = httpHeaderGetStr(&http->old_entry->mem_obj->reply->header, HDR_ETAG);
	if (etag) {
	    http->request->etag = xstrdup(etag);
	    http->request->flags.cache_validation = 1;
	}
    }
    if (!hit)
	fwdStart(http->conn->fd, http->entry, http->request);
    /* Register with storage manager to receive updates when data comes in. */
    if (EBIT_TEST(entry->flags, ENTRY_ABORTED))
	debug(33, 0) ("clientProcessExpired: found ENTRY_ABORTED object\n");
    storeClientCopyHeaders(http->sc, entry,
	clientHandleIMSReply,
	http);
}

static int
clientGetsOldEntry(StoreEntry * new_entry, StoreEntry * old_entry, request_t * request)
{
    const http_status status = new_entry->mem_obj->reply->sline.status;
    if (0 == status) {
	debug(33, 5) ("clientGetsOldEntry: YES, broken HTTP reply\n");
	return 1;
    }
    /* If the reply is a failure then send the old object as a last
     * resort */
    if (status >= 500 && status < 600) {
	if (EBIT_TEST(new_entry->flags, ENTRY_NEGCACHED)) {
	    debug(33, 3) ("clientGetsOldEntry: NO, negatively cached failure reply=%d\n", status);
	    return 0;
	}
	if (refreshCheckStaleOK(old_entry, request)) {
	    debug(33, 3) ("clientGetsOldEntry: YES, failure reply=%d and old acceptable to send\n", status);
	    return 1;
	}
	debug(33, 3) ("clientGetsOldEntry: NO, failure reply=%d and old NOT acceptable to send\n", status);
	return 0;
    }
    /* If the reply is not to a cache validation conditional then
     * we should forward it to the client */
    if (!request->flags.cache_validation) {
	debug(33, 5) ("clientGetsOldEntry: NO, not a cache validation\n");
	return 0;
    }
    /* If the reply is anything but "Not Modified" then
     * we must forward it to the client */
    if (HTTP_NOT_MODIFIED != status) {
	debug(33, 5) ("clientGetsOldEntry: NO, reply=%d\n", status);
	return 0;
    }
    /* If the ETag matches the clients If-None-Match, then return
     * the servers 304 reply
     */
    if (httpHeaderHas(&new_entry->mem_obj->reply->header, HDR_ETAG) &&
	httpHeaderHas(&request->header, HDR_IF_NONE_MATCH)) {
	const char *etag = httpHeaderGetStr(&new_entry->mem_obj->reply->header, HDR_ETAG);
	String etags = httpHeaderGetList(&request->header, HDR_IF_NONE_MATCH);
	int etag_match = strListIsMember(&etags, etag, ',');
	stringClean(&etags);
	if (etag_match) {
	    debug(33, 5) ("clientGetsOldEntry: NO, client If-None-Match\n");
	    return 0;
	}
    }
    /* If the client did not send IMS in the request, then it
     * must get the old object, not this "Not Modified" reply */
    if (!request->flags.ims) {
	debug(33, 5) ("clientGetsOldEntry: YES, no client IMS\n");
	return 1;
    }
    /* If the client IMS time is prior to the entry LASTMOD time we
     * need to send the old object */
    if (modifiedSince(old_entry, request)) {
	debug(33, 5) ("clientGetsOldEntry: YES, modified since %ld\n",
	    (long int) request->ims);
	return 1;
    }
    debug(33, 5) ("clientGetsOldEntry: NO, new one is fine\n");
    return 0;
}


static void
clientHandleIMSReply(void *data, HttpReply * rep)
{
    clientHttpRequest *http = data;
    StoreEntry *entry = http->entry;
    MemObject *mem;
    const char *url = storeUrl(entry);
    int unlink_request = 0;
    StoreEntry *oldentry;
    int recopy = 1;
    debug(33, 3) ("clientHandleIMSReply: %s\n", url);
    if (http->old_entry && http->old_entry->mem_obj && http->old_entry->mem_obj->ims_entry) {
	storeUnlockObject(http->old_entry->mem_obj->ims_entry);
	http->old_entry->mem_obj->ims_entry = NULL;
    }
    if (entry == NULL) {
	return;
    }
    if (entry->mem_obj->old_entry) {
	storeUnlockObject(entry->mem_obj->old_entry);
	entry->mem_obj->old_entry = NULL;
    }
    mem = entry->mem_obj;
    if (!rep) {
	debug(33, 3) ("clientHandleIMSReply: ABORTED '%s'\n", url);
	/* We have an existing entry, but failed to validate it */
	/* Its okay to send the old one anyway */
	http->log_type = LOG_TCP_REFRESH_FAIL_HIT;
	storeClientUnregister(http->sc, entry, http);
	storeUnlockObject(entry);
	entry = http->entry = http->old_entry;
	http->sc = http->old_sc;
    } else if (clientGetsOldEntry(entry, http->old_entry, http->request)) {
	/* We initiated the IMS request, the client is not expecting
	 * 304, so put the good one back.  First, make sure the old entry
	 * headers have been loaded from disk. */
	oldentry = http->old_entry;
	if (oldentry->mem_obj->request == NULL) {
	    oldentry->mem_obj->request = requestLink(mem->request);
	    unlink_request = 1;
	}
	if (rep->sline.status == HTTP_NOT_MODIFIED) {
	    /* Don't memcpy() the whole reply structure here.  For example,
	     * www.thegist.com (Netscape/1.13) returns a content-length for
	     * 304's which seems to be the length of the 304 HEADERS!!! and
	     * not the body they refer to.  */
	    httpReplyUpdateOnNotModified(oldentry->mem_obj->reply, rep);
	    storeTimestampsSet(oldentry);
	    storeUpdate(oldentry, http->request);
	    http->log_type = LOG_TCP_REFRESH_HIT;
	} else {
	    http->log_type = LOG_TCP_REFRESH_FAIL_HIT;
	}
	storeClientUnregister(http->sc, entry, http);
	http->sc = http->old_sc;
	storeUnlockObject(entry);
	entry = http->entry = oldentry;
	if (unlink_request) {
	    requestUnlink(entry->mem_obj->request);
	    entry->mem_obj->request = NULL;
	}
    } else {
	/* the client can handle this reply, whatever it is */
	http->flags.hit = 0;
	http->log_type = LOG_TCP_REFRESH_MISS;
	if (HTTP_NOT_MODIFIED == rep->sline.status && http->request->flags.cache_validation) {
	    httpReplyUpdateOnNotModified(http->old_entry->mem_obj->reply,
		rep);
	    storeTimestampsSet(http->old_entry);
	    storeUpdate(http->old_entry, http->request);
	    if (!EBIT_TEST(http->old_entry->flags, REFRESH_FAILURE))
		http->log_type = LOG_TCP_REFRESH_HIT;
	    else
		http->log_type = LOG_TCP_REFRESH_FAIL_HIT;
	}
	/* Get rid of the old entry if not a cache validation */
	if (!http->request->flags.cache_validation)
	    storeRelease(http->old_entry);
	storeClientUnregister(http->old_sc, http->old_entry, http);
	storeUnlockObject(http->old_entry);
	recopy = 0;
    }
    http->old_entry = NULL;	/* done with old_entry */
    http->old_sc = NULL;
    if (http->request->flags.collapsed && !http->flags.hit && EBIT_TEST(entry->flags, RELEASE_REQUEST)) {
	/* Collapsed request, but the entry is not good to be sent */
	clientProcessMiss(http);
	return;
    }
    if (EBIT_TEST(entry->flags, ENTRY_ABORTED)) {
	/* Old object got aborted, not good */
	clientProcessMiss(http);
	return;
    }
    if (recopy) {
	storeClientCopyHeaders(http->sc, entry,
	    clientSendHeaders,
	    http);
    } else {
	clientSendHeaders(data, rep);
    }
}

static int
modifiedSince(StoreEntry * entry, request_t * request)
{
    squid_off_t object_length;
    MemObject *mem = entry->mem_obj;
    time_t mod_time = entry->lastmod;
    debug(33, 3) ("modifiedSince: '%s'\n", storeLookupUrl(entry));
    debug(33, 3) ("modifiedSince: mod_time = %ld\n", (long int) mod_time);
    if (mod_time < 0)
	return 1;
    /* Find size of the object */
    object_length = mem->reply->content_length;
    if (object_length < 0)
	object_length = contentLen(entry);
    if (mod_time > request->ims) {
	debug(33, 3) ("--> YES: entry newer than client\n");
	return 1;
    } else if (mod_time < request->ims) {
	debug(33, 3) ("-->  NO: entry older than client\n");
	return 0;
    } else if (request->imslen < 0) {
	debug(33, 3) ("-->  NO: same LMT, no client length\n");
	return 0;
    } else if (request->imslen == object_length) {
	debug(33, 3) ("-->  NO: same LMT, same length\n");
	return 0;
    } else {
	debug(33, 3) ("--> YES: same LMT, different length\n");
	return 1;
    }
}

static void
clientPurgeRequest(clientHttpRequest * http)
{
    StoreEntry *entry;
    ErrorState *err = NULL;
    HttpReply *r;
    http_status status = HTTP_NOT_FOUND;
    debug(33, 3) ("Config2.onoff.enable_purge = %d\n", Config2.onoff.enable_purge);
    if (!Config2.onoff.enable_purge) {
	http->log_type = LOG_TCP_DENIED;
	err = errorCon(ERR_ACCESS_DENIED, HTTP_FORBIDDEN, http->orig_request);
	http->entry = clientCreateStoreEntry(http, http->request->method, null_request_flags);
	errorAppendEntry(http->entry, err);
	return;
    }
    /* Release both IP cache */
    ipcacheInvalidate(http->request->host);

    if (!http->flags.purging) {
	/* Try to find a base entry */
	http->flags.purging = 1;
	entry = storeGetPublicByRequestMethod(http->request, METHOD_GET);
	if (!entry)
	    entry = storeGetPublicByRequestMethod(http->request, METHOD_HEAD);
	if (entry) {
	    if (EBIT_TEST(entry->flags, ENTRY_SPECIAL)) {
		http->log_type = LOG_TCP_DENIED;
		err = errorCon(ERR_ACCESS_DENIED, HTTP_FORBIDDEN, http->request);
		http->entry = clientCreateStoreEntry(http, http->request->method, null_request_flags);
		errorAppendEntry(http->entry, err);
		return;
	    }
	    /* Swap in the metadata */
	    http->entry = entry;
	    storeLockObject(http->entry);
	    storeCreateMemObject(http->entry, http->uri);
	    http->entry->mem_obj->method = http->request->method;
	    http->sc = storeClientRegister(http->entry, http);
	    http->log_type = LOG_TCP_HIT;
	    storeClientCopyHeaders(http->sc, http->entry,
		clientCacheHit,
		http);
	    return;
	}
    }
    http->log_type = LOG_TCP_MISS;
    /* Release the cached URI */
    entry = storeGetPublicByRequestMethod(http->request, METHOD_GET);
    if (entry) {
	debug(33, 4) ("clientPurgeRequest: GET '%s'\n",
	    storeUrl(entry));
	storeRelease(entry);
	status = HTTP_OK;
    }
    entry = storeGetPublicByRequestMethod(http->request, METHOD_HEAD);
    if (entry) {
	debug(33, 4) ("clientPurgeRequest: HEAD '%s'\n",
	    storeUrl(entry));
	storeRelease(entry);
	status = HTTP_OK;
    }
    /* And for Vary, release the base URI if none of the headers was included in the request */
    if (http->request->vary_headers && !strstr(http->request->vary_headers, "=")) {
	entry = storeGetPublic(urlCanonical(http->request), METHOD_GET);
	if (entry) {
	    debug(33, 4) ("clientPurgeRequest: Vary GET '%s'\n",
		storeUrl(entry));
	    storeRelease(entry);
	    status = HTTP_OK;
	}
	entry = storeGetPublic(urlCanonical(http->request), METHOD_HEAD);
	if (entry) {
	    debug(33, 4) ("clientPurgeRequest: Vary HEAD '%s'\n",
		storeUrl(entry));
	    storeRelease(entry);
	    status = HTTP_OK;
	}
    }
    /*
     * Make a new entry to hold the reply to be written
     * to the client.
     */
    http->entry = clientCreateStoreEntry(http, http->request->method, null_request_flags);
    httpReplyReset(r = http->entry->mem_obj->reply);
    httpReplySetHeaders(r, status, NULL, NULL, 0, -1, squid_curtime);
    httpReplySwapOut(r, http->entry);
    storeComplete(http->entry);
}

int
checkNegativeHit(StoreEntry * e)
{
    if (!EBIT_TEST(e->flags, ENTRY_NEGCACHED))
	return 0;
    if (e->expires <= squid_curtime)
	return 0;
    if (e->store_status != STORE_OK)
	return 0;
    return 1;
}

static void
clientUpdateCounters(clientHttpRequest * http)
{
    int svc_time = tvSubMsec(http->start, current_time);
    ping_data *i;
    HierarchyLogEntry *H;
    statCounter.client_http.requests++;
    if (isTcpHit(http->log_type))
	statCounter.client_http.hits++;
    if (http->log_type == LOG_TCP_HIT)
	statCounter.client_http.disk_hits++;
    else if (http->log_type == LOG_TCP_MEM_HIT)
	statCounter.client_http.mem_hits++;
    if (http->request->err_type != ERR_NONE)
	statCounter.client_http.errors++;
    statHistCount(&statCounter.client_http.all_svc_time, svc_time);
    /*
     * The idea here is not to be complete, but to get service times
     * for only well-defined types.  For example, we don't include
     * LOG_TCP_REFRESH_FAIL_HIT because its not really a cache hit
     * (we *tried* to validate it, but failed).
     */
    switch (http->log_type) {
    case LOG_TCP_REFRESH_HIT:
	statHistCount(&statCounter.client_http.nh_svc_time, svc_time);
	break;
    case LOG_TCP_IMS_HIT:
	statHistCount(&statCounter.client_http.nm_svc_time, svc_time);
	break;
    case LOG_TCP_HIT:
    case LOG_TCP_MEM_HIT:
    case LOG_TCP_OFFLINE_HIT:
	statHistCount(&statCounter.client_http.hit_svc_time, svc_time);
	break;
    case LOG_TCP_MISS:
    case LOG_TCP_CLIENT_REFRESH_MISS:
	statHistCount(&statCounter.client_http.miss_svc_time, svc_time);
	break;
    default:
	/* make compiler warnings go away */
	break;
    }
    H = &http->request->hier;
    switch (H->code) {
#if USE_CACHE_DIGESTS
    case CD_PARENT_HIT:
    case CD_SIBLING_HIT:
	statCounter.cd.times_used++;
	break;
#endif
    case SIBLING_HIT:
    case PARENT_HIT:
    case FIRST_PARENT_MISS:
    case CLOSEST_PARENT_MISS:
	statCounter.icp.times_used++;
	i = &H->ping;
	if (0 != i->stop.tv_sec && 0 != i->start.tv_sec)
	    statHistCount(&statCounter.icp.query_svc_time,
		tvSubUsec(i->start, i->stop));
	if (i->timeout)
	    statCounter.icp.query_timeouts++;
	break;
    case CLOSEST_PARENT:
    case CLOSEST_DIRECT:
	statCounter.netdb.times_used++;
	break;
    default:
	break;
    }
}

static void
httpRequestFree(void *data)
{
    clientHttpRequest *http = data;
    ConnStateData *conn = http->conn;
    StoreEntry *e;
    request_t *request = http->request;
    MemObject *mem = NULL;
    debug(33, 3) ("httpRequestFree: %s\n", storeUrl(http->entry));
    if (!clientCheckTransferDone(http)) {
	requestAbortBody(request);	/* abort request body transter */
	/* HN: This looks a bit odd.. why should client_side care about
	 * the ICP selection status?
	 */
	if (http->entry && http->entry->ping_status == PING_WAITING)
	    storeReleaseRequest(http->entry);
    }
    assert(http->log_type < LOG_TYPE_MAX);
    if (http->entry)
	mem = http->entry->mem_obj;
    if (http->out.size || http->log_type) {
	http->al.icp.opcode = ICP_INVALID;
	http->al.url = http->log_uri;
	if (!http->al.url)
	    http->al.url = urlCanonicalClean(request);
	debug(33, 9) ("httpRequestFree: al.url='%s'\n", http->al.url);
	http->al.cache.out_ip = request->out_ip;
	if (http->reply && http->log_type != LOG_TCP_DENIED) {
	    http->al.http.code = http->reply->sline.status;
	    http->al.http.content_type = strBuf(http->reply->content_type);
	} else if (mem) {
	    http->al.http.code = mem->reply->sline.status;
	    http->al.http.content_type = strBuf(mem->reply->content_type);
	}
	http->al.cache.caddr = conn->log_addr;
	http->al.cache.size = http->out.size;
	http->al.cache.code = http->log_type;
	http->al.cache.msec = tvSubMsec(http->start, current_time);
	http->al.cache.rq_size = http->req_sz;
	if (request) {
	    http->al.cache.rq_size += request->content_length;
	    if (Config.onoff.log_mime_hdrs) {
		Packer p;
		MemBuf mb;
		memBufDefInit(&mb);
		packerToMemInit(&p, &mb);
		httpHeaderPackInto(&request->header, &p);
		http->al.headers.request = xstrdup(mb.buf);
		packerClean(&p);
		memBufClean(&mb);
	    }
	    http->al.http.method = request->method;
	    http->al.http.version = request->http_ver;
	    http->al.hier = request->hier;
	    if (request->auth_user_request) {
		if (authenticateUserRequestUsername(request->auth_user_request))
		    http->al.cache.authuser = xstrdup(authenticateUserRequestUsername(request->auth_user_request));
		authenticateAuthUserRequestUnlock(request->auth_user_request);
		request->auth_user_request = NULL;
	    } else if (request->extacl_user) {
		http->al.cache.authuser = xstrdup(request->extacl_user);
	    }
	    if (conn->rfc931[0])
		http->al.cache.rfc931 = conn->rfc931;
	}
#if USE_SSL
	http->al.cache.ssluser = sslGetUserEmail(fd_table[conn->fd].ssl);
#endif
	http->al.request = request;
	if (!http->acl_checklist)
	    http->acl_checklist = clientAclChecklistCreate(Config.accessList.http, http);
	http->acl_checklist->reply = http->reply;
	if (!Config.accessList.log || aclCheckFast(Config.accessList.log, http->acl_checklist)) {
	    http->al.reply = http->reply;
	    accessLogLog(&http->al, http->acl_checklist);
	    clientUpdateCounters(http);
	    clientdbUpdate(conn->peer.sin_addr, http->log_type, PROTO_HTTP, http->out.size);
	}
    }
    if (http->acl_checklist)
	aclChecklistFree(http->acl_checklist);
    if (request)
	checkFailureRatio(request->err_type, http->al.hier.code);
    safe_free(http->uri);
    safe_free(http->log_uri);
    safe_free(http->al.headers.request);
    safe_free(http->al.headers.reply);
    safe_free(http->al.cache.authuser);
    http->al.request = NULL;
    safe_free(http->redirect.location);
    stringClean(&http->range_iter.boundary);
    if (http->old_entry && http->old_entry->mem_obj && http->old_entry->mem_obj->ims_entry && http->old_entry->mem_obj->ims_entry == http->entry) {
	storeUnlockObject(http->old_entry->mem_obj->ims_entry);
	http->old_entry->mem_obj->ims_entry = NULL;
    }
    if ((e = http->entry)) {
	http->entry = NULL;
	storeClientUnregister(http->sc, e, http);
	http->sc = NULL;
	storeUnlockObject(e);
    }
    /* old_entry might still be set if we didn't yet get the reply
     * code in clientHandleIMSReply() */
    if ((e = http->old_entry)) {
	http->old_entry = NULL;
	storeClientUnregister(http->old_sc, e, http);
	http->old_sc = NULL;
	storeUnlockObject(e);
    }
    requestUnlink(http->request);
    http->request = NULL;
    requestUnlink(http->orig_request);
    http->orig_request = NULL;
    if (http->reply)
	httpReplyDestroy(http->reply);
    http->reply = NULL;
    assert(DLINK_HEAD(http->conn->reqs) != NULL);
    /* Unlink us from the clients request list */
    dlinkDelete(&http->node, &http->conn->reqs);
    dlinkDelete(&http->active, &ClientActiveRequests);
    cbdataFree(http);
}

/* This is a handler normally called by comm_close() */
static void
connStateFree(int fd, void *data)
{
    ConnStateData *connState = data;
    dlink_node *n;
    clientHttpRequest *http;
    debug(33, 3) ("connStateFree: FD %d\n", fd);
    assert(connState != NULL);
    clientdbEstablished(connState->peer.sin_addr, -1);	/* decrement */
    n = connState->reqs.head;
    while (n != NULL) {
	http = n->data;
	n = n->next;
	assert(http->conn == connState);
	httpRequestFree(http);
    }
    if (connState->auth_user_request)
	authenticateAuthUserRequestUnlock(connState->auth_user_request);
    connState->auth_user_request = NULL;
    authenticateOnCloseConnection(connState);
    memFreeBuf(connState->in.size, connState->in.buf);
    pconnHistCount(0, connState->nrequests);
    if (connState->pinning.fd >= 0)
	comm_close(connState->pinning.fd);
    cbdataFree(connState);
#ifdef _SQUID_LINUX_
    /* prevent those nasty RST packets */
    {
	char buf[SQUID_TCP_SO_RCVBUF];
	while (FD_READ_METHOD(fd, buf, SQUID_TCP_SO_RCVBUF) > 0);
    }
#endif
}

void
clientInterpretRequestHeaders(clientHttpRequest * http)
{
    request_t *request = http->request;
    HttpHeader *req_hdr = &request->header;
    int no_cache = 0;
    const char *str;
    request->imslen = -1;
    request->ims = httpHeaderGetTime(req_hdr, HDR_IF_MODIFIED_SINCE);
    if (request->ims > 0)
	request->flags.ims = 1;
    if (httpHeaderHas(req_hdr, HDR_PRAGMA)) {
	String s = httpHeaderGetList(req_hdr, HDR_PRAGMA);
	if (strListIsMember(&s, "no-cache", ','))
	    no_cache++;
	stringClean(&s);
    }
    assert(request->cache_control == NULL);
    request->cache_control = httpHeaderGetCc(req_hdr);
    if (request->cache_control)
	if (EBIT_TEST(request->cache_control->mask, CC_NO_CACHE))
	    no_cache++;
    /* Work around for supporting the Reload button in IE browsers
     * when Squid is used as an accelerator or transparent proxy,
     * by turning accelerated IMS request to no-cache requests.
     * Now knows about IE 5.5 fix (is actually only fixed in SP1, 
     * but we can't tell whether we are talking to SP1 or not so 
     * all 5.5 versions are treated 'normally').
     */
    if (Config.onoff.ie_refresh) {
	if (http->flags.accel && request->flags.ims) {
	    if ((str = httpHeaderGetStr(req_hdr, HDR_USER_AGENT))) {
		if (strstr(str, "MSIE 5.01") != NULL)
		    no_cache++;
		else if (strstr(str, "MSIE 5.0") != NULL)
		    no_cache++;
		else if (strstr(str, "MSIE 4.") != NULL)
		    no_cache++;
		else if (strstr(str, "MSIE 3.") != NULL)
		    no_cache++;
	    }
	}
    }
    if (no_cache) {
#if HTTP_VIOLATIONS
	if (Config.onoff.reload_into_ims)
	    request->flags.nocache_hack = 1;
	else if (refresh_nocache_hack)
	    request->flags.nocache_hack = 1;
	else
#endif
	    request->flags.nocache = 1;
    }
    if (http->conn->port->no_connection_auth)
	request->flags.no_connection_auth = 1;
    if (Config.onoff.pipeline_prefetch)
	request->flags.no_connection_auth = 1;

    /* ignore range header in non-GETs */
    if (request->method == METHOD_GET) {
	request->range = httpHeaderGetRange(req_hdr);
	if (request->range)
	    request->flags.range = 1;
    }
    if (httpHeaderHas(req_hdr, HDR_AUTHORIZATION))
	request->flags.auth = 1;
    else if (request->login[0] != '\0')
	request->flags.auth = 1;
    if (request->flags.no_connection_auth) {
	/* nothing special to do here.. */
    } else if (http->conn->pinning.fd != -1) {
	if (http->conn->pinning.auth) {
	    request->flags.connection_auth = 1;
	    request->flags.auth = 1;
	} else {
	    request->flags.connection_proxy_auth = 1;
	}
	request->pinned_connection = http->conn;
	cbdataLock(request->pinned_connection);
    }
    /* check if connection auth is used, and flag as candidate for pinning
     * in such case.
     * Note: we may need to set flags.connection_auth even if the connection
     * is already pinned if it was pinned earlier due to proxy auth
     */
    if (request->flags.connection_auth) {
	/* already taken care of above */
    } else if (httpHeaderHas(req_hdr, HDR_AUTHORIZATION) || httpHeaderHas(req_hdr, HDR_PROXY_AUTHORIZATION)) {
	HttpHeaderPos pos = HttpHeaderInitPos;
	HttpHeaderEntry *e;
	int may_pin = 0;
	while ((e = httpHeaderGetEntry(req_hdr, &pos))) {
	    if (e->id == HDR_AUTHORIZATION || e->id == HDR_PROXY_AUTHORIZATION) {
		const char *value = strBuf(e->value);
		if (strncasecmp(value, "NTLM ", 5) == 0
		    ||
		    strncasecmp(value, "Negotiate ", 10) == 0
		    ||
		    strncasecmp(value, "Kerberos ", 9) == 0) {
		    if (e->id == HDR_AUTHORIZATION) {
			request->flags.connection_auth = 1;
			may_pin = 1;
		    } else {
			request->flags.connection_proxy_auth = 1;
			may_pin = 1;
		    }
		}
	    }
	}
	if (may_pin && !request->pinned_connection) {
	    request->pinned_connection = http->conn;
	    cbdataLock(request->pinned_connection);
	}
    }
    if (httpHeaderHas(req_hdr, HDR_VIA)) {
	/*
	 * ThisCache cannot be a member of Via header, "1.0 ThisCache" can.
	 * Note ThisCache2 has a space prepended to the hostname so we don't
	 * accidentally match super-domains.
	 */
	String s = httpHeaderGetList(req_hdr, HDR_VIA);
	int n = strIsSubstr(&s, ThisCache2);
	if (n) {
	    debugObj(33, 1, "WARNING: Forwarding loop detected for:\n",
		request, (ObjPackMethod) & httpRequestPackDebug);
	    request->flags.loopdetect = 1;
	    if (n > 1)
		request->flags.loopdetect_twice = 1;
	}
#if FORW_VIA_DB
	fvdbCountVia(strBuf(s));
#endif
	stringClean(&s);
    }
#if USE_USERAGENT_LOG
    if ((str = httpHeaderGetStr(req_hdr, HDR_USER_AGENT)))
	logUserAgent(fqdnFromAddr(http->conn->log_addr), str);
#endif
#if USE_REFERER_LOG
    if ((str = httpHeaderGetStr(req_hdr, HDR_REFERER)))
	logReferer(fqdnFromAddr(http->conn->log_addr), str, rfc1738_escape_unescaped(http->uri));
#endif
#if FORW_VIA_DB
    if (httpHeaderHas(req_hdr, HDR_X_FORWARDED_FOR)) {
	String s = httpHeaderGetList(req_hdr, HDR_X_FORWARDED_FOR);
	fvdbCountForw(strBuf(s));
	stringClean(&s);
    }
#endif
    if (request->method == METHOD_TRACE) {
	request->max_forwards = httpHeaderGetInt(req_hdr, HDR_MAX_FORWARDS);
    }
    if (clientCachable(http))
	request->flags.cachable = 1;
    if (clientHierarchical(http))
	request->flags.hierarchical = 1;
    debug(33, 5) ("clientInterpretRequestHeaders: REQ_NOCACHE = %s\n",
	request->flags.nocache ? "SET" : "NOT SET");
    debug(33, 5) ("clientInterpretRequestHeaders: REQ_CACHABLE = %s\n",
	request->flags.cachable ? "SET" : "NOT SET");
    debug(33, 5) ("clientInterpretRequestHeaders: REQ_HIERARCHICAL = %s\n",
	request->flags.hierarchical ? "SET" : "NOT SET");
}

/*
 * clientSetKeepaliveFlag() sets request->flags.proxy_keepalive.
 * This is the client-side persistent connection flag.  We need
 * to set this relatively early in the request processing
 * to handle hacks for broken servers and clients.
 */
static void
clientSetKeepaliveFlag(clientHttpRequest * http)
{
    request_t *request = http->request;
    const HttpHeader *req_hdr = &request->header;

    debug(33, 3) ("clientSetKeepaliveFlag: http_ver = %d.%d\n",
	request->http_ver.major, request->http_ver.minor);
    debug(33, 3) ("clientSetKeepaliveFlag: method = %s\n",
	RequestMethods[request->method].str);
    {
	http_version_t http_ver;
	if (http->conn->port->http11)
	    http_ver = request->http_ver;
	else
	    httpBuildVersion(&http_ver, 1, 0);	/* we are HTTP/1.0, no matter what the client requests... */
	if (httpMsgIsPersistent(http_ver, req_hdr))
	    request->flags.proxy_keepalive = 1;
    }
}

static int
clientCheckContentLength(request_t * r)
{
    switch (r->method) {
    case METHOD_GET:
    case METHOD_HEAD:
	/* We do not want to see a request entity on GET/HEAD requests */
	return (r->content_length <= 0 || Config.onoff.request_entities);
    default:
	/* For other types of requests we don't care */
	return 1;
    }
    /* NOT REACHED */
}

static int
clientCachable(clientHttpRequest * http)
{
    request_t *req = http->request;
    method_t method = req->method;
    if (req->flags.loopdetect)
	return 0;
    if (req->protocol == PROTO_HTTP)
	return httpCachable(method);
    /* FTP is always cachable */
    if (method == METHOD_CONNECT)
	return 0;
    if (method == METHOD_TRACE)
	return 0;
    if (method == METHOD_PUT)
	return 0;
    if (method == METHOD_POST)
	return 0;		/* XXX POST may be cached sometimes.. ignored for now */
    if (req->protocol == PROTO_GOPHER)
	return gopherCachable(req);
    if (req->protocol == PROTO_CACHEOBJ)
	return 0;
    return 1;
}

/* Return true if we can query our neighbors for this object */
static int
clientHierarchical(clientHttpRequest * http)
{
    const char *url = http->uri;
    request_t *request = http->request;
    method_t method = request->method;
    const wordlist *p = NULL;

    /* IMS needs a private key, so we can use the hierarchy for IMS only
     * if our neighbors support private keys */
    if (request->flags.ims && !neighbors_do_private_keys)
	return 0;
    if (request->flags.auth)
	return 0;
    if (method == METHOD_TRACE)
	return 1;
    if (method != METHOD_GET)
	return 0;
    /* scan hierarchy_stoplist */
    for (p = Config.hierarchy_stoplist; p; p = p->next)
	if (strstr(url, p->key))
	    return 0;
    if (request->flags.loopdetect)
	return 0;
    if (request->protocol == PROTO_HTTP)
	return httpCachable(method);
    if (request->protocol == PROTO_GOPHER)
	return gopherCachable(request);
    if (request->protocol == PROTO_CACHEOBJ)
	return 0;
    return 1;
}

int
isTcpHit(log_type code)
{
    /* this should be a bitmap for better optimization */
    if (code == LOG_TCP_HIT)
	return 1;
    if (code == LOG_TCP_STALE_HIT)
	return 1;
    if (code == LOG_TCP_ASYNC_HIT)
	return 1;
    if (code == LOG_TCP_IMS_HIT)
	return 1;
    if (code == LOG_TCP_REFRESH_FAIL_HIT)
	return 1;
    if (code == LOG_TCP_REFRESH_HIT)
	return 1;
    if (code == LOG_TCP_NEGATIVE_HIT)
	return 1;
    if (code == LOG_TCP_MEM_HIT)
	return 1;
    if (code == LOG_TCP_OFFLINE_HIT)
	return 1;
    return 0;
}

/*
 * returns true if If-Range specs match reply, false otherwise
 */
static int
clientIfRangeMatch(clientHttpRequest * http, HttpReply * rep)
{
    const TimeOrTag spec = httpHeaderGetTimeOrTag(&http->request->header, HDR_IF_RANGE);
    /* check for parsing falure */
    if (!spec.valid)
	return 0;
    /* got an ETag? */
    if (spec.tag) {
	const char *rep_tag = httpHeaderGetStr(&rep->header, HDR_ETAG);
	debug(33, 3) ("clientIfRangeMatch: ETags: %s and %s\n",
	    spec.tag, rep_tag ? rep_tag : "<none>");
	if (!rep_tag)
	    return 0;		/* entity has no etag to compare with! */
	if (spec.tag[0] == 'W' || rep_tag[0] == 'W') {
	    debug(33, 1) ("clientIfRangeMatch: Weak ETags are not allowed in If-Range: %s ? %s\n",
		spec.tag, rep_tag);
	    return 0;		/* must use strong validator for sub-range requests */
	}
	return strcmp(rep_tag, spec.tag) == 0;
    }
    /* got modification time? */
    else if (spec.time >= 0) {
	return http->entry->lastmod == spec.time;
    }
    assert(0);			/* should not happen */
    return 0;
}

/* returns expected content length for multi-range replies
 * note: assumes that httpHdrRangeCanonize has already been called
 * warning: assumes that HTTP headers for individual ranges at the
 *          time of the actuall assembly will be exactly the same as
 *          the headers when clientMRangeCLen() is called */
static squid_off_t
clientMRangeCLen(clientHttpRequest * http)
{
    squid_off_t clen = 0;
    HttpHdrRangePos pos = HttpHdrRangeInitPos;
    const HttpHdrRangeSpec *spec;
    MemBuf mb;

    assert(http->entry->mem_obj);

    memBufDefInit(&mb);
    while ((spec = httpHdrRangeGetSpec(http->request->range, &pos))) {

	/* account for headers for this range */
	memBufReset(&mb);
	clientPackRangeHdr(http->entry->mem_obj->reply,
	    spec, http->range_iter.boundary, &mb);
	clen += mb.size;

	/* account for range content */
	clen += spec->length;

	debug(33, 6) ("clientMRangeCLen: (clen += %ld + %" PRINTF_OFF_T ") == %" PRINTF_OFF_T "\n",
	    (long int) mb.size, spec->length, clen);
    }
    /* account for the terminating boundary */
    memBufReset(&mb);
    clientPackTermBound(http->range_iter.boundary, &mb);
    clen += mb.size;

    memBufClean(&mb);
    return clen;
}

/* adds appropriate Range headers if needed */
static void
clientBuildRangeHeader(clientHttpRequest * http, HttpReply * rep)
{
    HttpHeader *hdr = rep ? &rep->header : 0;
    const char *range_err = NULL;
    request_t *request = http->request;
    assert(request->range);
    /* check if we still want to do ranges */
    if (!rep)
	range_err = "no [parse-able] reply";
    else if (rep->sline.status != HTTP_OK)
	range_err = "wrong status code";
    else if (httpHeaderHas(hdr, HDR_CONTENT_RANGE))
	range_err = "origin server does ranges";
    else if (rep->content_length < 0)
	range_err = "unknown length";
    else if (rep->content_length != http->entry->mem_obj->reply->content_length)
	range_err = "INCONSISTENT length";	/* a bug? */
    else if (httpHeaderHas(&http->request->header, HDR_IF_RANGE) && !clientIfRangeMatch(http, rep))
	range_err = "If-Range match failed";
    else if (!httpHdrRangeCanonize(http->request->range, rep->content_length))
	range_err = "canonization failed";
    else if (httpHdrRangeIsComplex(http->request->range))
	range_err = "too complex range header";
    else if (!request->flags.cachable)	/* from we_do_ranges in http.c */
	range_err = "non-cachable request";
    else if (!http->flags.hit && httpHdrRangeOffsetLimit(http->request->range))
	range_err = "range outside range_offset_limit";
    /* get rid of our range specs on error */
    if (range_err) {
	debug(33, 3) ("clientBuildRangeHeader: will not do ranges: %s.\n", range_err);
	httpHdrRangeDestroy(http->request->range);
	http->request->range = NULL;
    } else {
	const int spec_count = http->request->range->specs.count;
	squid_off_t actual_clen = -1;

	debug(33, 3) ("clientBuildRangeHeader: range spec count: %d virgin clen: %" PRINTF_OFF_T "\n",
	    spec_count, rep->content_length);
	assert(spec_count > 0);
	/* append appropriate header(s) */
	if (spec_count == 1) {
	    HttpHdrRangePos pos = HttpHdrRangeInitPos;
	    const HttpHdrRangeSpec *spec = httpHdrRangeGetSpec(http->request->range, &pos);
	    assert(spec);
	    /* append Content-Range */
	    httpHeaderAddContRange(hdr, *spec, rep->content_length);
	    /* set new Content-Length to the actual number of bytes
	     * transmitted in the message-body */
	    actual_clen = spec->length;
	} else {
	    /* multipart! */
	    /* generate boundary string */
	    http->range_iter.boundary = httpHdrRangeBoundaryStr(http);
	    /* delete old Content-Type, add ours */
	    httpHeaderDelById(hdr, HDR_CONTENT_TYPE);
	    httpHeaderPutStrf(hdr, HDR_CONTENT_TYPE,
		"multipart/byteranges; boundary=\"%s\"",
		strBuf(http->range_iter.boundary));
	    /* Content-Length is not required in multipart responses
	     * but it is always nice to have one */
	    actual_clen = clientMRangeCLen(http);
	}

	/* replace Content-Length header */
	assert(actual_clen >= 0);
	httpHeaderDelById(hdr, HDR_CONTENT_LENGTH);
	httpHeaderPutSize(hdr, HDR_CONTENT_LENGTH, actual_clen);
	rep->content_length = actual_clen;
	debug(33, 3) ("clientBuildRangeHeader: actual content length: %" PRINTF_OFF_T "\n", actual_clen);
    }
}

/*
 * filters out unwanted entries from original reply header
 * adds extra entries if we have more info than origin server
 * adds Squid specific entries
 */
static void
clientBuildReplyHeader(clientHttpRequest * http, HttpReply * rep)
{
    HttpHeader *hdr = &rep->header;
    request_t *request = http->request;
    httpHeaderDelById(hdr, HDR_PROXY_CONNECTION);
    /* here: Keep-Alive is a field-name, not a connection directive! */
    httpHeaderDelById(hdr, HDR_KEEP_ALIVE);
    /* remove Set-Cookie if a hit */
    if (http->flags.hit)
	httpHeaderDelById(hdr, HDR_SET_COOKIE);
    httpHeaderDelById(hdr, HDR_TRAILER);
    httpHeaderDelById(hdr, HDR_TRANSFER_ENCODING);
    httpHeaderDelById(hdr, HDR_UPGRADE);
    /* handle Connection header */
    if (httpHeaderHas(hdr, HDR_CONNECTION)) {
	/* anything that matches Connection list member will be deleted */
	String strConnection = httpHeaderGetList(hdr, HDR_CONNECTION);
	const HttpHeaderEntry *e;
	HttpHeaderPos pos = HttpHeaderInitPos;
	int headers_deleted = 0;
	/*
	 * think: on-average-best nesting of the two loops (hdrEntry
	 * and strListItem) @?@
	 */
	while ((e = httpHeaderGetEntry(hdr, &pos))) {
	    if (e->id == HDR_KEEP_ALIVE)
		continue;	/* Common, and already taken care of above */
	    if (strListIsMember(&strConnection, strBuf(e->name), ',')) {
		httpHeaderDelAt(hdr, pos);
		headers_deleted++;
	    }
	}
	if (headers_deleted)
	    httpHeaderRefreshMask(hdr);
	httpHeaderDelById(hdr, HDR_CONNECTION);
	stringClean(&strConnection);
    }
    /* Handle Ranges */
    if (request->range)
	clientBuildRangeHeader(http, rep);
    /*
     * Add a estimated Age header on cache hits.
     */
    if (http->flags.hit) {
	/*
	 * Remove any existing Age header sent by upstream caches
	 * (note that the existing header is passed along unmodified
	 * on cache misses)
	 */
	httpHeaderDelById(hdr, HDR_AGE);
	/*
	 * This adds the calculated object age. Note that the details of the
	 * age calculation is performed by adjusting the timestamp in
	 * storeTimestampsSet(), not here.
	 *
	 * BROWSER WORKAROUND: IE sometimes hangs when receiving a 0 Age
	 * header, so don't use it unless there is a age to report. Please
	 * note that Age is only used to make a conservative estimation of
	 * the objects age, so a Age: 0 header does not add any useful
	 * information to the reply in any case.
	 */
	if (http->entry) {
	    if (EBIT_TEST(http->entry->flags, ENTRY_SPECIAL)) {
		httpHeaderDelById(hdr, HDR_DATE);
		httpHeaderInsertTime(hdr, 0, HDR_DATE, squid_curtime);
	    } else if (http->entry->timestamp < 0) {
		(void) 0;
	    } else if (http->conn->port->act_as_origin) {
		HttpHeaderEntry *h = httpHeaderFindEntry(hdr, HDR_DATE);
		if (h)
		    httpHeaderPutExt(hdr, "X-Origin-Date", strBuf(h->value));
		httpHeaderDelById(hdr, HDR_DATE);
		httpHeaderInsertTime(hdr, 0, HDR_DATE, squid_curtime);
		h = httpHeaderFindEntry(hdr, HDR_EXPIRES);
		if (h && http->entry->expires >= 0) {
		    httpHeaderPutExt(hdr, "X-Origin-Expires", strBuf(h->value));
		    httpHeaderDelById(hdr, HDR_EXPIRES);
		    httpHeaderInsertTime(hdr, 1, HDR_EXPIRES, squid_curtime + http->entry->expires - http->entry->timestamp);
		} {
		    char age[64];
		    snprintf(age, sizeof(age), "%ld", (long int) squid_curtime - http->entry->timestamp);
		    httpHeaderPutExt(hdr, "X-Cache-Age", age);
		}
	    } else if (http->entry->timestamp < squid_curtime) {
		httpHeaderPutInt(hdr, HDR_AGE,
		    squid_curtime - http->entry->timestamp);
	    }
	    if (!httpHeaderHas(hdr, HDR_CONTENT_LENGTH) && http->entry->mem_obj && http->entry->store_status == STORE_OK) {
		rep->content_length = contentLen(http->entry);
		httpHeaderPutSize(hdr, HDR_CONTENT_LENGTH, rep->content_length);
	    }
	}
    }
    /* Filter unproxyable authentication types */
    if (http->log_type != LOG_TCP_DENIED &&
	(httpHeaderHas(hdr, HDR_WWW_AUTHENTICATE))) {
	HttpHeaderPos pos = HttpHeaderInitPos;
	HttpHeaderEntry *e;
	int connection_auth_blocked = 0;
	while ((e = httpHeaderGetEntry(hdr, &pos))) {
	    if (e->id == HDR_WWW_AUTHENTICATE) {
		const char *value = strBuf(e->value);
		if ((strncasecmp(value, "NTLM", 4) == 0 &&
			(value[4] == '\0' || value[4] == ' '))
		    ||
		    (strncasecmp(value, "Negotiate", 9) == 0 &&
			(value[9] == '\0' || value[9] == ' '))
		    ||
		    (strncasecmp(value, "Kerberos", 8) == 0 &&
			(value[8] == '\0' || value[8] == ' '))) {
		    if (request->flags.no_connection_auth) {
			httpHeaderDelAt(hdr, pos);
			connection_auth_blocked = 1;
			continue;
		    }
		    request->flags.must_keepalive = 1;
		    if (!request->flags.accelerated && !request->flags.transparent) {
			httpHeaderPutStr(hdr, HDR_PROXY_SUPPORT, "Session-Based-Authentication");
			httpHeaderPutStr(hdr, HDR_CONNECTION, "Proxy-support");
		    }
		    break;
		}
	    }
	}
	if (connection_auth_blocked)
	    httpHeaderRefreshMask(hdr);
    }
    /* Handle authentication headers */
    if (request->auth_user_request)
	authenticateFixHeader(rep, request->auth_user_request, request, http->flags.accel, 0);
    /* Append X-Cache */
    httpHeaderPutStrf(hdr, HDR_X_CACHE, "%s from %s",
	http->flags.hit ? "HIT" : "MISS", getMyHostname());
#if USE_CACHE_DIGESTS
    /* Append X-Cache-Lookup: -- temporary hack, to be removed @?@ @?@ */
    httpHeaderPutStrf(hdr, HDR_X_CACHE_LOOKUP, "%s from %s:%d",
	http->lookup_type ? http->lookup_type : "NONE",
	getMyHostname(), getMyPort());
#endif
    if (httpReplyBodySize(request->method, rep) < 0) {
	if (http->conn->port->http11 && (request->http_ver.major > 1 || (request->http_ver.major == 1 && request->http_ver.minor >= 1))) {
	    debug(33, 2) ("clientBuildReplyHeader: send chunked response, unknown body size\n");
	    request->flags.chunked_response = 1;
	} else {
	    debug(33, 3) ("clientBuildReplyHeader: can't keep-alive, unknown body size\n");
	    request->flags.proxy_keepalive = 0;
	}
    }
    if (fdUsageHigh() && !request->flags.must_keepalive) {
	debug(33, 3) ("clientBuildReplyHeader: Not many unused FDs, can't keep-alive\n");
	request->flags.proxy_keepalive = 0;
    }
    if (!Config.onoff.error_pconns && rep->sline.status >= 400 && !request->flags.must_keepalive) {
	debug(33, 3) ("clientBuildReplyHeader: Error, don't keep-alive\n");
	request->flags.proxy_keepalive = 0;
    }
    if (!Config.onoff.client_pconns && !request->flags.must_keepalive)
	request->flags.proxy_keepalive = 0;
    if (request->flags.connection_auth && !rep->keep_alive) {
	debug(33, 2) ("clientBuildReplyHeader: Connection oriented auth but server side non-persistent\n");
	request->flags.proxy_keepalive = 0;
    }
    /* Append Transfer-Encoding */
    if (request->flags.chunked_response) {
	httpHeaderPutStr(hdr, HDR_TRANSFER_ENCODING, "chunked");
    }
    /* Append Via */
    if (Config.onoff.via && http->entry) {
	LOCAL_ARRAY(char, bbuf, MAX_URL + 32);
	String strVia = httpHeaderGetList(hdr, HDR_VIA);
	snprintf(bbuf, MAX_URL + 32, "%d.%d %s",
	    rep->sline.version.major,
	    rep->sline.version.minor, ThisCache);
	strListAdd(&strVia, bbuf, ',');
	httpHeaderDelById(hdr, HDR_VIA);
	httpHeaderPutStr(hdr, HDR_VIA, strBuf(strVia));
	stringClean(&strVia);
    }
    /* Signal keep-alive if needed */
    if (!request->flags.proxy_keepalive)
	httpHeaderPutStr(hdr, HDR_CONNECTION, "close");
    else if ((request->http_ver.major == 1 && request->http_ver.minor == 0) || !http->conn->port->http11) {
	httpHeaderPutStr(hdr, HDR_CONNECTION, "keep-alive");
	if (!(http->flags.accel || http->flags.transparent))
	    httpHeaderPutStr(hdr, HDR_PROXY_CONNECTION, "keep-alive");
    }
#if ADD_X_REQUEST_URI
    /*
     * Knowing the URI of the request is useful when debugging persistent
     * connections in a client; we cannot guarantee the order of http headers,
     * but X-Request-URI is likely to be the very last header to ease use from a
     * debugger [hdr->entries.count-1].
     */
    httpHeaderPutStr(hdr, HDR_X_REQUEST_URI,
	http->entry->mem_obj->url ? http->entry->mem_obj->url : http->uri);
#endif
    httpHdrMangleList(hdr, request);
}

/* Used exclusively by clientCloneReply() during failure cases only */
static void
clientUnwindReply(clientHttpRequest * http, HttpReply * rep)
{
    if (rep != NULL) {
	httpReplyDestroy(rep);
	rep = NULL;
    }
    /* This destroys the range request */
    if (http->request->range)
	clientBuildRangeHeader(http, rep);
}

/*
 * This routine was historically called when we think we've got enough header
 * data - ie, after the first read. The store would not be allowed to release
 * data to be read until after all the headers were appended.
 *
 * So we, for now, just assume all the headers are here or they won't ever
 * be.
 */
static HttpReply *
clientCloneReply(clientHttpRequest * http, HttpReply * orig_rep)
{
    HttpReply *rep = NULL;
    /* If we don't have a memobj / reply by now then we're stuffed */
    if (http->sc->entry->mem_obj == NULL || http->sc->entry->mem_obj->reply == NULL) {
	clientUnwindReply(http, NULL);
	return NULL;
    }
    /* try to grab the already-parsed header */
    rep = httpReplyClone(orig_rep);
    if (rep->pstate == psParsed) {
	/* do header conversions */
	clientBuildReplyHeader(http, rep);
	/* if we do ranges, change status to "Partial Content" */
	if (http->request->range)
	    httpStatusLineSet(&rep->sline, rep->sline.version,
		HTTP_PARTIAL_CONTENT, NULL);
    } else {
	/* parsing failure, get rid of the invalid reply */
	clientUnwindReply(http, rep);
	return NULL;
    }
    return rep;
}

/*
 * clientProcessVary is called when it is detected that a object
 * varies and we need to get the correct variant
 */
static void
clientProcessVary(VaryData * vary, void *data)
{
    clientHttpRequest *http = data;
    if (!vary) {
	clientProcessRequest(http);
	return;
    }
    if (vary->key) {
	debug(33, 2) ("clientProcessVary: HIT key=%s etag=%s\n",
	    vary->key, vary->etag ? vary->etag : "NONE");
    } else {
	int i;
	debug(33, 2) ("clientProcessVary MISS\n");
	for (i = 0; i < vary->etags.count; i++) {
	    debug(33, 3) ("ETag: %s\n", (char *) vary->etags.items[i]);
	}
    }
    http->request->vary = vary;
    clientProcessRequest(http);
}

/*
 * Perform an async refresh of an object
 */
typedef struct _clientAsyncRefreshRequest {
    request_t *request;
    StoreEntry *entry;
    StoreEntry *old_entry;
    store_client *sc;
    squid_off_t offset;
    size_t buf_in_use;
    char readbuf[STORE_CLIENT_BUF_SZ];
    struct timeval start;
} clientAsyncRefreshRequest;

CBDATA_TYPE(clientAsyncRefreshRequest);

static void
clientAsyncDone(clientAsyncRefreshRequest * async)
{
    AccessLogEntry al;
    static aclCheck_t *ch;
    MemObject *mem = async->entry->mem_obj;
    request_t *request = async->request;
    memset(&al, 0, sizeof(al));
    al.icp.opcode = ICP_INVALID;
    al.url = mem->url;
    debug(33, 9) ("clientAsyncDone: url='%s'\n", al.url);
    al.http.code = mem->reply->sline.status;
    al.http.content_type = strBuf(mem->reply->content_type);
    al.cache.size = async->offset;
    if (async->old_entry->mem_obj)
	async->old_entry->mem_obj->refresh_timestamp = 0;
    if (mem->reply->sline.status == 304) {
	/* Don't memcpy() the whole reply structure here.  For example,
	 * www.thegist.com (Netscape/1.13) returns a content-length for
	 * 304's which seems to be the length of the 304 HEADERS!!! and
	 * not the body they refer to.  */
	httpReplyUpdateOnNotModified(async->old_entry->mem_obj->reply, async->entry->mem_obj->reply);
	storeTimestampsSet(async->old_entry);
	storeUpdate(async->old_entry, async->request);
	al.cache.code = LOG_TCP_ASYNC_HIT;
    } else
	al.cache.code = LOG_TCP_ASYNC_MISS;
    al.cache.msec = tvSubMsec(async->start, current_time);
    if (Config.onoff.log_mime_hdrs) {
	Packer p;
	MemBuf mb;
	memBufDefInit(&mb);
	packerToMemInit(&p, &mb);
	httpHeaderPackInto(&request->header, &p);
	al.headers.request = xstrdup(mb.buf);
	packerClean(&p);
	memBufClean(&mb);
    }
    al.http.method = request->method;
    al.http.version = request->http_ver;
    al.hier = request->hier;
    if (request->auth_user_request) {
	if (authenticateUserRequestUsername(request->auth_user_request))
	    al.cache.authuser = xstrdup(authenticateUserRequestUsername(request->auth_user_request));
	authenticateAuthUserRequestUnlock(request->auth_user_request);
	request->auth_user_request = NULL;
    } else if (request->extacl_user) {
	al.cache.authuser = xstrdup(request->extacl_user);
    }
    al.request = request;
    al.reply = mem->reply;
    ch = aclChecklistCreate(Config.accessList.http, request, NULL);
    ch->reply = mem->reply;
    if (!Config.accessList.log || aclCheckFast(Config.accessList.log, ch))
	accessLogLog(&al, ch);
    aclChecklistFree(ch);
    storeClientUnregister(async->sc, async->entry, async);
    storeUnlockObject(async->entry);
    storeUnlockObject(async->old_entry);
    requestUnlink(async->request);
    safe_free(al.headers.request);
    safe_free(al.headers.reply);
    safe_free(al.cache.authuser);
    cbdataFree(async);
}

static void
clientHandleAsyncReply(void *data, char *buf, ssize_t size)
{
    clientAsyncRefreshRequest *async = data;
    StoreEntry *e = async->entry;
    if (EBIT_TEST(e->flags, ENTRY_ABORTED)) {
	clientAsyncDone(async);
	return;
    }
    if (size <= 0) {
	clientAsyncDone(async);
	return;
    }
    async->offset += size;
    if (e->mem_obj->reply->sline.status == 304) {
	clientAsyncDone(async);
	return;
    }
    storeClientCopy(async->sc, async->entry,
	async->offset,
	async->offset,
	STORE_CLIENT_BUF_SZ, async->readbuf,
	clientHandleAsyncReply,
	async);
}

static void
clientAsyncRefresh(clientHttpRequest * http)
{
    char *url = http->uri;
    clientAsyncRefreshRequest *async;
    request_t *request = http->request;
    debug(33, 3) ("clientAsyncRefresh: '%s'\n", http->uri);
    CBDATA_INIT_TYPE(clientAsyncRefreshRequest);
    http->entry->mem_obj->refresh_timestamp = squid_curtime;
    async = cbdataAlloc(clientAsyncRefreshRequest);
    async->start = current_time;
    async->request = requestLink(request);
    async->old_entry = http->entry;
    storeLockObject(async->old_entry);
    async->entry = storeCreateEntry(url,
	request->flags,
	request->method);
    async->sc = storeClientRegister(async->entry, async);
    request->etags = NULL;	/* Should always be null as this was a cache hit, but just in case.. */
    httpHeaderDelById(&request->header, HDR_RANGE);
    httpHeaderDelById(&request->header, HDR_IF_RANGE);
    httpHeaderDelById(&request->header, HDR_IF_NONE_MATCH);
    httpHeaderDelById(&request->header, HDR_IF_MATCH);
    if (async->old_entry->lastmod > 0)
	request->lastmod = async->old_entry->lastmod;
    else if (async->old_entry->mem_obj && async->old_entry->mem_obj->reply)
	request->lastmod = async->old_entry->mem_obj->reply->date;
    else
	request->lastmod = -1;
    if (!request->etag) {
	const char *etag = httpHeaderGetStr(&async->old_entry->mem_obj->reply->header, HDR_ETAG);
	if (etag)
	    async->request->etag = xstrdup(etag);
    }
#if DELAY_POOLS
    /* delay_id is already set on original store client */
    delaySetStoreClient(async->sc, delayClient(http));
#endif
    fwdStart(-1, async->entry, async->request);
    storeClientCopy(async->sc, async->entry,
	async->offset,
	async->offset,
	STORE_CLIENT_BUF_SZ, async->readbuf,
	clientHandleAsyncReply,
	async);
}

/*
 * clientCacheHit should only be called until the HTTP reply headers
 * have been parsed.  Normally this should be a single call, but
 * it might take more than one.  As soon as we have the headers,
 * we hand off to clientSendMoreData, clientProcessExpired, or
 * clientProcessMiss.
 */
static void
clientCacheHit(void *data, HttpReply * rep)
{
    clientHttpRequest *http = data;
    StoreEntry *e = http->entry;
    MemObject *mem;
    request_t *r = http->request;
    int is_modified = -1;
    int stale;
    http->flags.hit = 0;
    if (http->entry == NULL) {
	debug(33, 3) ("clientCacheHit: request aborted\n");
	return;
    } else if (!rep) {
	/* swap in failure */
	debug(33, 3) ("clientCacheHit: swapin failure for %s\n", http->uri);
	http->log_type = LOG_TCP_SWAPFAIL_MISS;
	clientProcessMiss(http);
	return;
    } else if (EBIT_TEST(e->flags, ENTRY_ABORTED)) {
	/* aborted object */
	debug(33, 3) ("clientCacheHit: hit an aborted object %s\n", http->uri);
	http->log_type = LOG_TCP_SWAPFAIL_MISS;
	clientProcessMiss(http);
	return;
    }
    mem = e->mem_obj;
    debug(33, 3) ("clientCacheHit: %s = %d\n", http->uri, rep->sline.status);

    /*
     * This particular logic is a bit hairy.
     *
     * + If we have a store URL then we need to make sure the mem store url OR the mem url
     *   match the request store url.
     * + If we have no store URL then we need to make sure the mem url match the request url
     *   regardless of the store url (so objects which have store urls that match their urls
     *   can still be HIT fine.)
     */
    if (r->store_url) {
	if (mem->store_url == NULL && mem->url == NULL) {
	    debug(33, 1) ("clientCacheHit: request has store_url '%s'; mem has no url or store_url!\n", r->store_url);
	    clientProcessMiss(http);
	    return;
	}
	if (mem->store_url && strcmp(r->store_url, mem->store_url) != 0) {
	    debug(33, 1) ("clientCacheHit: request has store_url '%s'; mem object in hit has mis-matched store_url '%s'!\n", r->store_url, mem->store_url);
	    clientProcessMiss(http);
	    return;
	}
	if (mem->store_url == NULL && mem->url && strcmp(r->store_url, mem->url) != 0) {
	    debug(33, 1) ("clientCacheHit: request has store_url '%s'; mem object in hit has mis-matched url '%s'!\n", r->store_url, mem->url);
	    clientProcessMiss(http);
	    return;
	}
    } else {			/* no store URL in request */
	if (mem->store_url == NULL && mem->url == NULL) {
	    debug(33, 1) ("clientCacheHit: request has url '%s'; mem has no url or store_url!\n", urlCanonical(r));
	    clientProcessMiss(http);
	    return;
	}
	/* We currently don't enforce that memObjects with storeurl's -require- a request with a storeurl */
	if (strcmp(mem->url, urlCanonical(r)) != 0) {
	    debug(33, 1) ("clientCacheHit: (store url '%s'); URL mismatch '%s' != '%s'?\n", r->store_url, e->mem_obj->url, urlCanonical(r));
	    clientProcessMiss(http);
	    return;
	}
    }
    if (r->flags.collapsed && EBIT_TEST(e->flags, RELEASE_REQUEST)) {
	/* collapsed_forwarding, but the joined request is not good
	 * to be cached..
	 */
	clientProcessMiss(http);
	return;
    }
    /*
     * Got the headers, now grok them
     */
    assert(http->log_type == LOG_TCP_HIT);
    switch (varyEvaluateMatch(e, r)) {
    case VARY_NONE:
	/* No variance detected. Continue as normal */
	break;
    case VARY_MATCH:
	/* This is the correct entity for this request. Continue */
	debug(33, 2) ("clientCacheHit: Vary MATCH!\n");
	break;
    case VARY_OTHER:
	{
	    /* This is not the correct entity for this request. We need
	     * to requery the cache.
	     */
	    store_client *sc = http->sc;
	    http->entry = NULL;	/* saved in e */
	    /* Warning: storeClientUnregister may abort the object so we must
	     * call storeLocateVary before unregistering, and
	     * storeLocateVary may complete immediately so we cannot
	     * rely on the http structure for this...
	     */
	    http->sc = NULL;
	    storeLocateVary(e, e->mem_obj->reply->hdr_sz, r->vary_headers, r->vary_encoding, clientProcessVary, http);
	    storeClientUnregister(sc, e, http);
	    storeUnlockObject(e);
	    /* Note: varyEvalyateMatch updates the request with vary information
	     * so we only get here once. (it also takes care of cancelling loops)
	     */
	    debug(33, 2) ("clientCacheHit: Vary detected!\n");
	    return;
	}
    case VARY_RESTART:
	/* Used on collapsed requests when the main request wasn't
	 * compatible. Resart processing from the beginning.
	 */
	safe_free(r->vary_hdr);
	safe_free(r->vary_headers);
	clientProcessRequest(http);
	return;
    case VARY_CANCEL:
	/* varyEvaluateMatch found a object loop. Process as miss */
	debug(33, 1) ("clientCacheHit: Vary object loop!\n");
	storeClientUnregister(http->sc, e, http);
	http->sc = NULL;
	clientProcessMiss(http);
	return;
    }
    if (r->method == METHOD_PURGE) {
	http->entry = NULL;
	storeClientUnregister(http->sc, e, http);
	http->sc = NULL;
	storeUnlockObject(e);
	clientPurgeRequest(http);
	return;
    }
    http->flags.hit = 1;
    if (EBIT_TEST(e->flags, ENTRY_NEGCACHED)) {
	if (checkNegativeHit(e)
#if HTTP_VIOLATIONS
	    && !r->flags.nocache_hack
#endif
	    ) {
	    http->log_type = LOG_TCP_NEGATIVE_HIT;
	    clientSendHeaders(data, rep);
	} else {
	    http->log_type = LOG_TCP_MISS;
	    clientProcessMiss(http);
	}
	return;
    }
    if (httpHeaderHas(&r->header, HDR_IF_MATCH)) {
	const char *rep_etag = httpHeaderGetStr(&e->mem_obj->reply->header, HDR_ETAG);
	int has_etag = 0;
	if (rep_etag) {
	    String req_etags = httpHeaderGetList(&http->request->header, HDR_IF_MATCH);
	    has_etag = strListIsMember(&req_etags, rep_etag, ',');
	    stringClean(&req_etags);
	}
	if (!has_etag) {
	    /* The entity tags does not match. This cannot be a hit for this object.
	     * Query the origin to see what should be done.
	     */
	    http->log_type = LOG_TCP_MISS;
	    clientProcessMiss(http);
	    return;
	}
    }
    if (httpHeaderHas(&r->header, HDR_IF_NONE_MATCH)) {
	String req_etags;
	const char *rep_etag = httpHeaderGetStr(&e->mem_obj->reply->header, HDR_ETAG);
	int has_etag;
	if (mem->reply->sline.status != HTTP_OK) {
	    debug(33, 4) ("clientCacheHit: Reply code %d != 200\n",
		mem->reply->sline.status);
	    http->log_type = LOG_TCP_MISS;
	    clientProcessMiss(http);
	    return;
	}
	if (rep_etag) {
	    req_etags = httpHeaderGetList(&http->request->header, HDR_IF_NONE_MATCH);
	    has_etag = strListIsMember(&req_etags, rep_etag, ',');
	    stringClean(&req_etags);
	    if (has_etag) {
		debug(33, 4) ("clientCacheHit: If-None-Match matches\n");
		if (is_modified == -1)
		    is_modified = 0;
	    } else {
		debug(33, 4) ("clientCacheHit: If-None-Match mismatch\n");
		is_modified = 1;
	    }
	}
    }
    if (r->flags.ims && mem->reply->sline.status == HTTP_OK) {
	if (modifiedSince(e, http->request)) {
	    debug(33, 4) ("clientCacheHit: If-Modified-Since modified\n");
	    is_modified = 1;
	} else {
	    debug(33, 4) ("clientCacheHit: If-Modified-Since not modified\n");
	    if (is_modified == -1)
		is_modified = 0;
	}
    }
    stale = refreshCheckHTTPStale(e, r);
    debug(33, 2) ("clientCacheHit: refreshCheckHTTPStale returned %d\n", stale);
    if (stale == 0) {
	debug(33, 2) ("clientCacheHit: HIT\n");
    } else if (stale == -1 && Config.refresh_stale_window > 0 && e->mem_obj->refresh_timestamp + Config.refresh_stale_window > squid_curtime) {
	debug(33, 2) ("clientCacheHit: refresh_stale HIT\n");
	http->log_type = LOG_TCP_STALE_HIT;
	stale = 0;
    } else if (stale == -2 && e->mem_obj->refresh_timestamp + e->mem_obj->stale_while_revalidate >= squid_curtime) {
	debug(33, 2) ("clientCacheHit: stale-while-revalidate HIT\n");
	http->log_type = LOG_TCP_STALE_HIT;
	stale = 0;
    } else if (stale && http->flags.internal) {
	debug(33, 2) ("clientCacheHit: internal HIT\n");
	stale = 0;
    } else if (stale && Config.onoff.offline) {
	debug(33, 2) ("clientCacheHit: offline HIT\n");
	http->log_type = LOG_TCP_OFFLINE_HIT;
	stale = 0;
    } else if (stale == -2 && !clientOnlyIfCached(http)) {
	debug(33, 2) ("clientCacheHit: stale-while-revalidate needs revalidation\n");
	clientAsyncRefresh(http);
	http->log_type = LOG_TCP_STALE_HIT;
	stale = 0;
    }
    http->is_modified = is_modified;
    if (stale) {
	debug(33, 5) ("clientCacheHit: in refreshCheck() block\n");
	/*
	 * We hold a stale copy; it needs to be validated
	 */
	/*
	 * The 'need_validation' flag is used to prevent forwarding
	 * loops between siblings.  If our copy of the object is stale,
	 * then we should probably only use parents for the validation
	 * request.  Otherwise two siblings could generate a loop if
	 * both have a stale version of the object.
	 */
	r->flags.need_validation = 1;
	if (r->flags.nocache) {
	    /*
	     * This did not match a refresh pattern that overrides no-cache
	     * we should honour the client no-cache header.
	     */
	    http->log_type = LOG_TCP_CLIENT_REFRESH_MISS;
	    clientProcessMiss(http);
	    return;
	}
	clientRefreshCheck(http);
	return;
    }
    clientProcessHit(http);
}

static void
clientProcessHit(clientHttpRequest * http)
{
    int is_modified = http->is_modified;
    StoreEntry *e = http->entry;

    if (is_modified == 0) {
	time_t timestamp = e->timestamp;
	MemBuf mb = httpPacked304Reply(e->mem_obj->reply, http->conn->port->http11);
	http->log_type = LOG_TCP_IMS_HIT;
	storeClientUnregister(http->sc, e, http);
	http->sc = NULL;
	storeUnlockObject(e);
	e = clientCreateStoreEntry(http, http->request->method, null_request_flags);
	/*
	 * Copy timestamp from the original entry so the 304
	 * reply has a meaningful Age: header.
	 */
	http->entry = e;
	httpReplyParse(e->mem_obj->reply, mb.buf, mb.size);
	storeTimestampsSet(e);
	e->timestamp = timestamp;
	storeAppend(e, mb.buf, mb.size);
	memBufClean(&mb);
	storeComplete(e);
	return;
    }
    /*
     * plain ol' cache hit
     */
    if (EBIT_TEST(e->flags, REFRESH_FAILURE))
	http->log_type = LOG_TCP_NEGATIVE_HIT;
    if (e->store_status != STORE_OK)
	http->log_type = LOG_TCP_MISS;
    else if (http->log_type == LOG_TCP_HIT && e->mem_status == IN_MEMORY)
	http->log_type = LOG_TCP_MEM_HIT;
    clientSendHeaders(http, e->mem_obj->reply);
}

/* put terminating boundary for multiparts */
static void
clientPackTermBound(String boundary, MemBuf * mb)
{
    memBufPrintf(mb, "\r\n--%s--\r\n", strBuf(boundary));
    debug(33, 6) ("clientPackTermBound: buf offset: %ld\n", (long int) mb->size);
}

/* appends a "part" HTTP header (as in a multi-part/range reply) to the buffer */
static void
clientPackRangeHdr(const HttpReply * rep, const HttpHdrRangeSpec * spec, String boundary, MemBuf * mb)
{
    HttpHeader hdr;
    Packer p;
    assert(rep);
    assert(spec);

    /* put boundary */
    debug(33, 5) ("clientPackRangeHdr: appending boundary: %s\n", strBuf(boundary));
    /* rfc2046 requires to _prepend_ boundary with <crlf>! */
    memBufPrintf(mb, "\r\n--%s\r\n", strBuf(boundary));

    /* stuff the header with required entries and pack it */
    httpHeaderInit(&hdr, hoReply);
    if (httpHeaderHas(&rep->header, HDR_CONTENT_TYPE))
	httpHeaderPutStr(&hdr, HDR_CONTENT_TYPE, httpHeaderGetStr(&rep->header, HDR_CONTENT_TYPE));
    httpHeaderAddContRange(&hdr, *spec, rep->content_length);
    packerToMemInit(&p, mb);
    httpHeaderPackInto(&hdr, &p);
    packerClean(&p);
    httpHeaderClean(&hdr);

    /* append <crlf> (we packed a header, not a reply) */
    memBufPrintf(mb, crlf);
}

/*
 * extracts a "range" from *buf and appends them to mb, updating
 * all offsets and such.
 */
static void
clientPackRange(clientHttpRequest * http,
    HttpHdrRangeIter * i,
    const char **buf,
    size_t * size,
    MemBuf * mb)
{
    const size_t copy_sz = i->debt_size <= *size ? i->debt_size : *size;
    squid_off_t body_off = http->out.offset - i->prefix_size;
    assert(*size > 0);
    assert(i->spec);
    /*
     * intersection of "have" and "need" ranges must not be empty
     */
    assert(body_off < i->spec->offset + i->spec->length);
    assert(body_off + *size > i->spec->offset);
    /*
     * put boundary and headers at the beginning of a range in a
     * multi-range
     */
    if (http->request->range->specs.count > 1 && i->debt_size == i->spec->length) {
	assert(http->entry->mem_obj);
	clientPackRangeHdr(
	    http->entry->mem_obj->reply,	/* original reply */
	    i->spec,		/* current range */
	    i->boundary,	/* boundary, the same for all */
	    mb
	    );
    }
    /*
     * append content
     */
    debug(33, 3) ("clientPackRange: appending %ld bytes\n", (long int) copy_sz);
    memBufAppend(mb, *buf, copy_sz);
    /*
     * update offsets
     */
    *size -= copy_sz;
    i->debt_size -= copy_sz;
    body_off += copy_sz;
    *buf += copy_sz;
    http->out.offset = body_off + i->prefix_size;	/* sync */
    /*
     * paranoid check
     */
    assert(i->debt_size >= 0);
}

/* returns true if there is still data available to pack more ranges
 * increments iterator "i"
 * used by clientPackMoreRanges */
static int
clientCanPackMoreRanges(const clientHttpRequest * http, HttpHdrRangeIter * i, size_t size)
{
    /* first update "i" if needed */
    if (!i->debt_size) {
	if ((i->spec = httpHdrRangeGetSpec(http->request->range, &i->pos)))
	    i->debt_size = i->spec->length;
    }
    assert(!i->debt_size == !i->spec);	/* paranoid sync condition */
    /* continue condition: need_more_data && have_more_data */
    return i->spec && size > 0;
}

/* extracts "ranges" from buf and appends them to mb, updating all offsets and such */
/* returns true if we need more data */
static int
clientPackMoreRanges(clientHttpRequest * http, const char *buf, size_t size, MemBuf * mb)
{
    HttpHdrRangeIter *i = &http->range_iter;
    /* offset in range specs does not count the prefix of an http msg */
    squid_off_t body_off = http->out.offset - i->prefix_size;

    /* check: reply was parsed and range iterator was initialized */
    assert(i->prefix_size > 0);
    /* filter out data according to range specs */
    while (clientCanPackMoreRanges(http, i, size)) {
	squid_off_t start;	/* offset of still missing data */
	assert(i->spec);
	start = i->spec->offset + i->spec->length - i->debt_size;
	debug(33, 3) ("clientPackMoreRanges: in:  offset: %ld size: %ld\n",
	    (long int) body_off, (long int) size);
	debug(33, 3) ("clientPackMoreRanges: out: start: %ld spec[%ld]: [%ld, %ld), len: %ld debt: %ld\n",
	    (long int) start, (long int) i->pos, (long int) i->spec->offset, (long int) (i->spec->offset + i->spec->length), (long int) i->spec->length, (long int) i->debt_size);
	assert(body_off <= start);	/* we did not miss it */
	/* skip up to start */
	if (body_off + size > start) {
	    const size_t skip_size = start - body_off;
	    body_off = start;
	    size -= skip_size;
	    buf += skip_size;
	} else {
	    /* has not reached start yet */
	    body_off += size;
	    size = 0;
	    buf = NULL;
	}
	/* put next chunk if any */
	if (size) {
	    http->out.offset = body_off + i->prefix_size;	/* sync */
	    clientPackRange(http, i, &buf, &size, mb);
	    body_off = http->out.offset - i->prefix_size;	/* sync */
	}
    }
    assert(!i->debt_size == !i->spec);	/* paranoid sync condition */
    debug(33, 3) ("clientPackMoreRanges: buf exhausted: in:  offset: %ld size: %ld need_more: %ld\n",
	(long int) body_off, (long int) size, (long int) i->debt_size);
    if (i->debt_size) {
	debug(33, 3) ("clientPackMoreRanges: need more: spec[%ld]: [%ld, %ld), len: %ld\n",
	    (long int) i->pos, (long int) i->spec->offset, (long int) (i->spec->offset + i->spec->length), (long int) i->spec->length);
	/* skip the data we do not need if possible */
	if (i->debt_size == i->spec->length)	/* at the start of the cur. spec */
	    body_off = i->spec->offset;
	else
	    assert(body_off == i->spec->offset + i->spec->length - i->debt_size);
    } else if (http->request->range->specs.count > 1) {
	/* put terminating boundary for multiparts */
	clientPackTermBound(i->boundary, mb);
    }
    http->out.offset = body_off + i->prefix_size;	/* sync */
    return i->debt_size > 0;
}

/*
 * Calculates the maximum size allowed for an HTTP response
 */
static void
clientMaxBodySize(request_t * request, clientHttpRequest * http, HttpReply * reply)
{
    body_size *bs;
    aclCheck_t *checklist;
    if (http->log_type == LOG_TCP_DENIED)
	return;
    bs = (body_size *) Config.ReplyBodySize.head;
    while (bs) {
	checklist = clientAclChecklistCreate(bs->access_list, http);
	checklist->reply = reply;
	if (aclCheckFast(bs->access_list, checklist)) {
	    /* deny - skip this entry */
	    bs = (body_size *) bs->node.next;
	} else {
	    /* Allow - use this entry */
	    http->maxBodySize = bs->maxsize;
	    bs = NULL;
	    debug(58, 3) ("clientMaxBodySize: Setting maxBodySize to %ld\n", (long int) http->maxBodySize);
	}
	aclChecklistFree(checklist);
    }
}

static int
clientReplyBodyTooLarge(clientHttpRequest * http, squid_off_t clen)
{
    if (0 == http->maxBodySize)
	return 0;		/* disabled */
    if (clen < 0)
	return 0;		/* unknown */
    if (clen > http->maxBodySize)
	return 1;		/* too large */
    return 0;
}

static int
clientRequestBodyTooLarge(squid_off_t clen)
{
    if (0 == Config.maxRequestBodySize)
	return 0;		/* disabled */
    if (clen < 0)
	return 0;		/* unknown, bug? */
    if (clen > Config.maxRequestBodySize)
	return 1;		/* too large */
    return 0;
}


/* Responses with no body will not have a content-type header, 
 * which breaks the rep_mime_type acl, which
 * coincidentally, is the most common acl for reply access lists.
 * A better long term fix for this is to allow acl matchs on the various
 * status codes, and then supply a default ruleset that puts these 
 * codes before any user defines access entries. That way the user 
 * can choose to block these responses where appropriate, but won't get
 * mysterious breakages.
 */
static int
clientAlwaysAllowResponse(http_status sline)
{
    switch (sline) {
    case HTTP_CONTINUE:
    case HTTP_SWITCHING_PROTOCOLS:
    case HTTP_PROCESSING:
    case HTTP_NO_CONTENT:
    case HTTP_NOT_MODIFIED:
	return 1;
	/* unreached */
	break;
    default:
	return 0;
    }
}

static void clientHttpLocationRewriteCheck(clientHttpRequest * http);
static void clientHttpLocationRewriteCheckDone(int answer, void *data);
static void clientHttpLocationRewrite(clientHttpRequest * http);
static void clientHttpLocationRewriteDone(void *data, char *reply);
static void clientHttpReplyAccessCheck(clientHttpRequest * http);
static void clientHttpReplyAccessCheckDone(int answer, void *data);
static void clientCheckErrorMap(clientHttpRequest * http);
static void clientCheckHeaderDone(clientHttpRequest * http);

/*
 * accepts chunk of a http message in buf, parses prefix, filters headers and
 * such, writes processed message to the client's socket
 */
static void
clientSendHeaders(void *data, HttpReply * rep)
{
    clientHttpRequest *http = data;
    StoreEntry *entry = http->entry;
    ConnStateData *conn = http->conn;
    int fd = conn->fd;
    assert(http->request != NULL);
    dlinkDelete(&http->active, &ClientActiveRequests);
    dlinkAdd(http, &http->active, &ClientActiveRequests);
    debug(33, 5) ("clientSendHeaders: FD %d '%s'\n", fd, storeUrl(entry));
    assert(conn->reqs.head != NULL);
    if (DLINK_HEAD(conn->reqs) != http) {
	/* there is another object in progress, defer this one */
	debug(33, 2) ("clientSendHeaders: Deferring %s\n", storeUrl(entry));
	return;
    } else if (http->request->flags.reset_tcp) {
	comm_reset_close(fd);
	return;
    } else if (!rep) {
	/* call clientWriteComplete so the client socket gets closed */
	clientWriteComplete(fd, NULL, 0, COMM_OK, http);
	return;
    }
    assert(http->out.offset == 0);

    if (Config.zph_mode != ZPH_OFF) {
	int tos = 0;

	if (!isTcpHit(http->log_type))
	    tos = 0;
	else if (Config.zph_local)
	    tos = Config.zph_local;
	else if (Config.zph_sibling && http->request->hier.code == SIBLING_HIT)		/* sibling hit */
	    tos = Config.zph_sibling;
	else if (Config.zph_parent && http->request->hier.code == PARENT_HIT)	/* parent hit */
	    tos = Config.zph_parent;
	if (conn->tos_priority != tos) {
	    conn->tos_priority = tos;
	    switch (Config.zph_mode) {
	    case ZPH_OFF:
		break;
	    case ZPH_TOS:
		commSetTos(fd, tos);
		break;
	    case ZPH_PRIORITY:
		commSetSocketPriority(fd, tos);
		break;
	    case ZPH_OPTION:
		{
		    uint16_t value = tos;
		    commSetIPOption(fd, Config.zph_option, &value, sizeof(value));
		}
		break;
	    }
	}
    }
    rep = http->reply = clientCloneReply(http, rep);
    if (!rep) {
	ErrorState *err = errorCon(ERR_INVALID_RESP, HTTP_BAD_GATEWAY, http->orig_request);
	storeClientUnregister(http->sc, http->entry, http);
	http->sc = NULL;
	storeUnlockObject(http->entry);
	http->entry = clientCreateStoreEntry(http, http->request->method,
	    null_request_flags);
	errorAppendEntry(http->entry, err);
	return;
    }
    clientMaxBodySize(http->request, http, rep);
    if (http->log_type != LOG_TCP_DENIED && clientReplyBodyTooLarge(http, rep->content_length)) {
	ErrorState *err = errorCon(ERR_TOO_BIG, HTTP_FORBIDDEN, http->orig_request);
	storeClientUnregister(http->sc, http->entry, http);
	http->sc = NULL;
	storeUnlockObject(http->entry);
	http->log_type = LOG_TCP_DENIED;
	http->entry = clientCreateStoreEntry(http, http->request->method,
	    null_request_flags);
	errorAppendEntry(http->entry, err);
	return;
    }
    /* 
     * At this point we might have more data in the headers than this silly 4k read.
     * So lets just ignore there being any body data in this particular read
     * (as eventually we won't be issuing a read just to get header data) and issue
     * our next read at the point just after the reply length in rep->hdr_sz.
     * Hopefully this horrible hackery will go away once the store API has changed to
     * seperate entity-data and reply-data. We'll then reinstance the "grab header data
     * and body data, writing them out in one swift hit" logic which I've just disabled.
     * - [ahc]
     */
    http->range_iter.prefix_size = rep->hdr_sz;
    debug(33, 3) ("clientSendHeaders: %d bytes of headers\n", rep->hdr_sz);
    clientHttpLocationRewriteCheck(http);
}

static void
clientHttpLocationRewriteCheck(clientHttpRequest * http)
{
    HttpReply *rep = http->reply;
    aclCheck_t *ch;
    if (!Config.Program.location_rewrite.command || !httpHeaderHas(&rep->header, HDR_LOCATION)) {
	clientHttpLocationRewriteDone(http, NULL);
	return;
    }
    if (Config.accessList.location_rewrite) {
	ch = clientAclChecklistCreate(Config.accessList.location_rewrite, http);
	ch->reply = http->reply;
	aclNBCheck(ch, clientHttpLocationRewriteCheckDone, http);
    } else {
	clientHttpLocationRewriteCheckDone(ACCESS_ALLOWED, http);
    }
}

static void
clientHttpLocationRewriteCheckDone(int answer, void *data)
{
    clientHttpRequest *http = data;
    if (answer == ACCESS_ALLOWED) {
	clientHttpLocationRewrite(http);
    } else {
	clientHttpLocationRewriteDone(http, NULL);
    }
}

static void
clientHttpLocationRewrite(clientHttpRequest * http)
{
    HttpReply *rep = http->reply;
    if (!httpHeaderHas(&rep->header, HDR_LOCATION))
	clientHttpLocationRewriteDone(http, NULL);
    else
	locationRewriteStart(rep, http, clientHttpLocationRewriteDone, http);
}

static void
clientHttpLocationRewriteDone(void *data, char *reply)
{
    clientHttpRequest *http = data;
    HttpReply *rep = http->reply;
    ConnStateData *conn = http->conn;
    if (reply && *reply) {
	httpHeaderDelById(&rep->header, HDR_LOCATION);
	if (*reply == '/') {
	    /* We have to restore the URL as sent by the client */
	    request_t *req = http->orig_request;
	    const char *proto = conn->port->protocol;
	    const char *host = httpHeaderGetStr(&req->header, HDR_HOST);
	    if (!host)
		host = req->host;
	    httpHeaderPutStrf(&rep->header, HDR_LOCATION, "%s://%s%s", proto, host, reply);
	} else {
	    httpHeaderPutStr(&rep->header, HDR_LOCATION, reply);
	}
    }
    clientHttpReplyAccessCheck(http);
}

static void
clientHttpReplyAccessCheck(clientHttpRequest * http)
{
    aclCheck_t *ch;
    if (Config.accessList.reply && http->log_type != LOG_TCP_DENIED && !clientAlwaysAllowResponse(http->reply->sline.status)) {
	ch = clientAclChecklistCreate(Config.accessList.reply, http);
	ch->reply = http->reply;
	aclNBCheck(ch, clientHttpReplyAccessCheckDone, http);
    } else {
	clientHttpReplyAccessCheckDone(ACCESS_ALLOWED, http);
    }
}

/* Handle error mapping.
 * 
 *   1. Look up if there is a error map for the request
 *   2. Start requesting the error URL
 *   3. When headers are received, create a new reply structure and copy
 *      over the relevant headers (start with the headers from the original
 *      reply, and copy over Content-Length)
 *   4. Make the new reply the current one
 *   5. Detatch from the previous reply
 *   6. Go to clientCheckHeaderDone, as if nothing had happened, but now
 *      fetching from the new reply.
 */
static void
clientHttpReplyAccessCheckDone(int answer, void *data)
{
    clientHttpRequest *http = data;
    debug(33, 2) ("The reply for %s %s is %s, because it matched '%s'\n",
	RequestMethods[http->request->method].str, http->uri,
	answer ? "ALLOWED" : "DENIED",
	AclMatchedName ? AclMatchedName : "NO ACL's");
    if (answer != ACCESS_ALLOWED) {
	ErrorState *err;
	err_type page_id;
	page_id = aclGetDenyInfoPage(&Config.denyInfoList, AclMatchedName, 1);
	if (page_id == ERR_NONE)
	    page_id = ERR_ACCESS_DENIED;
	err = errorCon(page_id, HTTP_FORBIDDEN, http->orig_request);
	storeClientUnregister(http->sc, http->entry, http);
	http->sc = NULL;
	if (http->reply)
	    httpReplyDestroy(http->reply);
	http->reply = NULL;
	storeUnlockObject(http->entry);
	http->log_type = LOG_TCP_DENIED;
	http->entry = clientCreateStoreEntry(http, http->request->method,
	    null_request_flags);
	errorAppendEntry(http->entry, err);
	return;
    }
    clientCheckErrorMap(http);
}

static void
clientCheckErrorMapDone(StoreEntry * e, int body_offset, squid_off_t content_length, void *data)
{
    clientHttpRequest *http = data;
    if (e) {
	/* Get rid of the old request entry */
	storeClientUnregister(http->sc, http->entry, http);
	storeUnlockObject(http->entry);
	/* Attach ourselves to the new request entry */
	http->entry = e;
	storeLockObject(e);
	http->sc = storeClientRegister(http->entry, http);
	/* Adjust the header size */
	http->reply->hdr_sz = body_offset;
	/* Clean up any old body content */
	httpBodyClean(&http->reply->body);
	/* And finally, adjust content-length to the new value */
	httpHeaderDelById(&http->reply->header, HDR_CONTENT_LENGTH);
	if (content_length >= 0) {
	    httpHeaderPutSize(&http->reply->header, HDR_CONTENT_LENGTH, content_length);
	}
	http->reply->content_length = content_length;
    }
    clientCheckHeaderDone(http);
}

static void
clientCheckErrorMap(clientHttpRequest * http)
{
    HttpReply *rep = http->reply;
    if (rep->sline.status < 100 || rep->sline.status >= 400) {
	request_t *request = http->orig_request;
	/* XXX The NULL is meant to pass ACL name, but the ACL name is not
	 * known here (AclMatchedName is no longer valid)
	 */
	if (errorMapStart(Config.errorMapList, request, rep, NULL, clientCheckErrorMapDone, http))
	    return;
    }
    clientCheckHeaderDone(http);
}

static void
clientCheckHeaderDone(clientHttpRequest * http)
{
    HttpReply *rep = http->reply;
    MemBuf mb;
    int send_header = 1;
    /* reset range iterator */
    http->range_iter.pos = HttpHdrRangeInitPos;
    if (http->request->method == METHOD_HEAD) {
	/* do not forward body for HEAD replies */
	http->flags.done_copying = 1;
    }
    if (http->http_ver.major < 1)
	send_header = 0;
    if (rep->sline.version.major < 1) {
	if (send_header && Config.accessList.upgrade_http09) {
	    aclCheck_t *checklist = clientAclChecklistCreate(Config.accessList.upgrade_http09, http);
	    checklist->reply = rep;
	    if (aclCheckFast(Config.accessList.upgrade_http09, checklist) != 1)
		send_header = 0;
	    aclChecklistFree(checklist);
	}
	httpHeaderDelById(&rep->header, HDR_X_HTTP09_FIRST_LINE);
    }
    /* init mb; put status line and headers  */
    if (send_header) {
	if (http->conn->port->http11) {
	    /* enforce 1.1 reply version */
	    httpBuildVersion(&rep->sline.version, 1, 1);
	} else {
	    /* enforce 1.0 reply version */
	    httpBuildVersion(&rep->sline.version, 1, 0);
	}
	mb = httpReplyPack(rep);
    } else {
	debug(33, 2) ("HTTP/0.9 response, disable everything\n");
	http->request->flags.chunked_response = 0;
	http->request->flags.proxy_keepalive = 0;
	memBufDefInit(&mb);
    }
    if (Config.onoff.log_mime_hdrs) {
	http->al.headers.reply = xmalloc(mb.size + 1);
	xstrncpy(http->al.headers.reply, mb.buf, mb.size);
	http->al.headers.reply[mb.size] = '\0';
    }
    http->out.offset += rep->hdr_sz;
#if HEADERS_LOG
    headersLog(0, 0, http->request->method, rep);
#endif
    /* append body if any */
    if (http->request->range) {
	/* Only GET requests should have ranges */
	assert(http->request->method == METHOD_GET);
	/* clientPackMoreRanges() updates http->out.offset */
	/* force the end of the transfer if we are done */
	if (!clientPackMoreRanges(http, "", 0, &mb))
	    http->flags.done_copying = 1;
    }
    /* write headers and initial body */
    if (mb.size > 0) {
	comm_write_mbuf(http->conn->fd, mb, clientWriteComplete, http);
    } else {
	storeClientCopy(http->sc, http->entry,
	    http->out.offset,
	    http->out.offset,
	    STORE_CLIENT_BUF_SZ, memAllocate(MEM_STORE_CLIENT_BUF),
	    clientSendMoreData,
	    http);
    }
}


/*
 * accepts chunk of a http message in buf, parses prefix, filters headers and
 * such, writes processed message to the client's socket
 */
static void
clientSendMoreData(void *data, char *buf, ssize_t size)
{
    clientHttpRequest *http = data;
    StoreEntry *entry = http->entry;
    ConnStateData *conn = http->conn;
    int fd = conn->fd;
    MemBuf mb;
    debug(33, 5) ("clientSendMoreData: %s, %d bytes\n", http->uri, (int) size);
    assert(size <= STORE_CLIENT_BUF_SZ);
    assert(http->request != NULL);
    dlinkDelete(&http->active, &ClientActiveRequests);
    dlinkAdd(http, &http->active, &ClientActiveRequests);
    debug(33, 5) ("clientSendMoreData: FD %d '%s', out.offset=%d \n",
	fd, storeUrl(entry), (int) http->out.offset);
    assert(conn->reqs.head != NULL);
    if (DLINK_HEAD(conn->reqs) != http) {
	/* there is another object in progress, defer this one */
	debug(33, 1) ("clientSendMoreData: Deferring %s\n", storeUrl(entry));
	memFree(buf, MEM_STORE_CLIENT_BUF);
	return;
    } else if (size < 0) {
	/* call clientWriteComplete so the client socket gets closed */
	clientWriteComplete(fd, NULL, 0, COMM_OK, http);
	memFree(buf, MEM_STORE_CLIENT_BUF);
	return;
    } else if (size == 0) {
	/* call clientWriteComplete so the client socket gets closed */
	clientWriteComplete(fd, NULL, 0, COMM_OK, http);
	memFree(buf, MEM_STORE_CLIENT_BUF);
	return;
    }
    if (!http->request->range && !http->request->flags.chunked_response) {
	/* Avoid copying to MemBuf for non-range requests */
	http->out.offset += size;
	comm_write(fd, buf, size, clientWriteBodyComplete, http, NULL);
	/* NULL because clientWriteBodyComplete frees it */
	return;
    }
    if (http->request->method == METHOD_HEAD) {
	/*
	 * If we are here, then store_status == STORE_OK and it
	 * seems we have a HEAD repsponse which is missing the
	 * empty end-of-headers line (home.mira.net, phttpd/0.99.72
	 * does this).  Because clientCloneReply() fails we just
	 * call this reply a body, set the done_copying flag and
	 * continue...
	 */
	http->flags.done_copying = 1;
	/*
	 * And as this is a malformed HTTP reply we cannot keep
	 * the connection persistent
	 */
	http->request->flags.proxy_keepalive = 0;
    }
    /* init mb; put status line and headers if any */
    memBufDefInit(&mb);
    if (http->request->range) {
	/* Only GET requests should have ranges */
	assert(http->request->method == METHOD_GET);
	/* clientPackMoreRanges() updates http->out.offset */
	/* force the end of the transfer if we are done */
	if (!clientPackMoreRanges(http, buf, size, &mb))
	    http->flags.done_copying = 1;
    } else {
	http->out.offset += size;
	memBufAppend(&mb, buf, size);
    }
    /* write body */
    if (http->request->flags.chunked_response) {
	char header[32];
	size_t header_size;
	header_size = snprintf(header, sizeof(header), "%x\r\n", mb.size);
	memBufAppend(&mb, "\r\n", 2);
	comm_write_mbuf_header(fd, mb, header, header_size, clientWriteComplete, http);
    } else {
	comm_write_mbuf(fd, mb, clientWriteComplete, http);
    }
    memFree(buf, MEM_STORE_CLIENT_BUF);
}

/*
 * clientWriteBodyComplete is called for MEM_STORE_CLIENT_BUF's
 * written directly to the client socket, versus copying to a MemBuf
 * and going through comm_write_mbuf.  Most non-range responses after
 * the headers probably go through here.
 */
static void
clientWriteBodyComplete(int fd, char *buf, size_t size, int errflag, void *data)
{
    /*
     * NOTE: clientWriteComplete doesn't currently use its "buf"
     * (second) argument, so we pass in NULL.
     */
    memFree(buf, MEM_STORE_CLIENT_BUF);
    clientWriteComplete(fd, NULL, size, errflag, data);
}

static void
clientKeepaliveNextRequest(clientHttpRequest * http)
{
    ConnStateData *conn = http->conn;
    StoreEntry *entry;
    debug(33, 3) ("clientKeepaliveNextRequest: FD %d\n", conn->fd);
    conn->defer.until = 0;	/* Kick it to read a new request */
    httpRequestFree(http);
    if (conn->pinning.pinned && conn->pinning.fd == -1) {
	debug(33, 2) ("clientKeepaliveNextRequest: FD %d Connection was pinned but server side gone. Terminating client connection\n", conn->fd);
	comm_close(conn->fd);
	return;
    }
    http = NULL;
    if (conn->reqs.head != NULL) {
	http = DLINK_HEAD(conn->reqs);
    }
    if (http == NULL) {
	debug(33, 5) ("clientKeepaliveNextRequest: FD %d reading next req\n",
	    conn->fd);
	fd_note_static(conn->fd, "Waiting for next request");
	/*
	 * Set the timeout BEFORE calling clientReadRequest().
	 */
	commSetTimeout(conn->fd, Config.Timeout.persistent_request, requestTimeout, conn);
	clientReadRequest(conn->fd, conn);	/* Read next request */
	/*
	 * Note, the FD may be closed at this point.
	 */
    } else if ((entry = http->entry) == NULL) {
	/*
	 * this request is in progress, maybe doing an ACL or a redirect,
	 * execution will resume after the operation completes.
	 */
	/* if it was a pipelined CONNECT kick it alive here */
	if (http->request->method == METHOD_CONNECT)
	    clientCheckFollowXForwardedFor(http);
    } else {
	debug(33, 2) ("clientKeepaliveNextRequest: FD %d Sending next\n",
	    conn->fd);
	assert(entry);
	if (0 == storeClientCopyPending(http->sc, entry, http)) {
	    if (EBIT_TEST(entry->flags, ENTRY_ABORTED))
		debug(33, 0) ("clientKeepaliveNextRequest: ENTRY_ABORTED\n");
	    storeClientCopyHeaders(http->sc, entry,
		clientSendHeaders,
		http);
	}
    }
}

static void
clientWriteComplete(int fd, char *bufnotused, size_t size, int errflag, void *data)
{
    clientHttpRequest *http = data;
    StoreEntry *entry = http->entry;
    int done;
    http->out.size += size;
    debug(33, 5) ("clientWriteComplete: FD %d, sz %d, err %d, off %" PRINTF_OFF_T ", len %" PRINTF_OFF_T "\n",
	fd, (int) size, errflag, http->out.offset, entry ? objectLen(entry) : (squid_off_t) 0);
    if (size > 0) {
	kb_incr(&statCounter.client_http.kbytes_out, size);
	if (isTcpHit(http->log_type))
	    kb_incr(&statCounter.client_http.hit_kbytes_out, size);
    }
#if SIZEOF_SQUID_OFF_T <= 4
    if (http->out.size > 0x7FFF0000) {
	debug(33, 1) ("WARNING: closing FD %d to prevent counter overflow\n", fd);
	debug(33, 1) ("\tclient %s\n", inet_ntoa(http->conn->peer.sin_addr));
	debug(33, 1) ("\treceived %d bytes\n", (int) http->out.size);
	debug(33, 1) ("\tURI %s\n", http->uri);
	comm_close(fd);
    } else
#endif
#if SIZEOF_SQUID_OFF_T <= 4
    if (http->out.offset > 0x7FFF0000) {
	debug(33, 1) ("WARNING: closing FD %d to prevent counter overflow\n", fd);
	debug(33, 1) ("\tclient %s\n", inet_ntoa(http->conn->peer.sin_addr));
	debug(33, 1) ("\treceived %d bytes (offset %d)\n", (int) http->out.size,
	    (int) http->out.offset);
	debug(33, 1) ("\tURI %s\n", http->uri);
	comm_close(fd);
    } else
#endif
    if (errflag) {
	/*
	 * just close the socket, httpRequestFree will abort if needed
	 */
	comm_close(fd);
    } else if (NULL == entry) {
	comm_close(fd);		/* yuk */
    } else if ((done = clientCheckTransferDone(http)) != 0 || size == 0) {
	debug(33, 5) ("clientWriteComplete: FD %d transfer is DONE\n", fd);
	/* We're finished case */
	if (!done) {
	    debug(33, 5) ("clientWriteComplete: closing, !done\n");
	    comm_close(fd);
	} else if (EBIT_TEST(entry->flags, ENTRY_ABORTED)) {
	    debug(33, 2) ("clientWriteComplete: Object aborted\n");
	    comm_close(fd);
	} else if (clientGotNotEnough(http)) {
	    debug(33, 5) ("clientWriteComplete: client didn't get all it expected\n");
	    comm_close(fd);
	} else if (EBIT_TEST(http->entry->flags, ENTRY_ABORTED)) {
	    debug(33, 5) ("clientWriteComplete: aborted object\n");
	    comm_close(fd);
	} else if (http->request->flags.chunked_response) {
	    /* Finish chunked transfer encoding */
	    http->request->flags.chunked_response = 0;	/* no longer chunking */
	    comm_write(http->conn->fd, "0\r\n\r\n", 5, clientWriteComplete, http, NULL);
	} else if (http->request->body_reader == clientReadBody) {
	    debug(33, 5) ("clientWriteComplete: closing, but first we need to read the rest of the request\n");
	    /* XXX We assumes the reply does fit in the TCP transmit window.
	     * If not the connection may stall while sending the reply
	     * (before reaching here) if the client does not try to read the
	     * response while sending the request body. As of yet we have
	     * not received any complaints indicating this may be an issue.
	     */
	    clientEatRequestBody(http);
	} else if (http->request->flags.proxy_keepalive) {
	    debug(33, 5) ("clientWriteComplete: FD %d Keeping Alive\n", fd);
	    clientKeepaliveNextRequest(http);
	} else {
	    comm_close(fd);
	}
    } else if (clientReplyBodyTooLarge(http, http->out.offset - 4096)) {
	/* 4096 is a margin for the HTTP headers included in out.offset */
	comm_close(fd);
    } else {
	/* More data will be coming from primary server; register with 
	 * storage manager. */
	storeClientCopy(http->sc, entry,
	    http->out.offset,
	    http->out.offset,
	    STORE_CLIENT_BUF_SZ, memAllocate(MEM_STORE_CLIENT_BUF),
	    clientSendMoreData,
	    http);
    }
}

/*
 * client issued a request with an only-if-cached cache-control directive;
 * we did not find a cached object that can be returned without
 *     contacting other servers;
 * respond with a 504 (Gateway Timeout) as suggested in [RFC 2068]
 */
static void
clientProcessOnlyIfCachedMiss(clientHttpRequest * http)
{
    char *url = http->uri;
    request_t *r = http->request;
    ErrorState *err = NULL;
    http->flags.hit = 0;
    debug(33, 4) ("clientProcessOnlyIfCachedMiss: '%s %s'\n",
	RequestMethods[r->method].str, url);
    http->al.http.code = HTTP_GATEWAY_TIMEOUT;
    err = errorCon(ERR_ONLY_IF_CACHED_MISS, HTTP_GATEWAY_TIMEOUT, http->orig_request);
    if (http->entry) {
	storeClientUnregister(http->sc, http->entry, http);
	http->sc = NULL;
	storeUnlockObject(http->entry);
    }
    http->entry = clientCreateStoreEntry(http, r->method, null_request_flags);
    errorAppendEntry(http->entry, err);
}

/* 
 * Return true if we should force a cache miss on this range request.
 * entry must be non-NULL.
 */
static int
clientCheckRangeForceMiss(StoreEntry * entry, HttpHdrRange * range)
{
    /*
     * If the range_offset_limit is NOT in effect, there
     * is no reason to force a miss.
     */
    if (0 == httpHdrRangeOffsetLimit(range))
	return 0;
    /*
     * Here, we know it's possibly a hit.  If we already have the
     * whole object cached, we won't force a miss.
     */
    if (STORE_OK == entry->store_status)
	return 0;		/* we have the whole object */
    /*
     * Now we have a hit on a PENDING object.  We need to see
     * if the part we want is already cached.  If so, we don't
     * force a miss.
     */
    assert(NULL != entry->mem_obj);
    if (httpHdrRangeFirstOffset(range) <= entry->mem_obj->inmem_hi)
	return 0;
    /*
     * Even though we have a PENDING copy of the object, we
     * don't want to wait to reach the first range offset,
     * so we force a miss for a new range request to the
     * origin.
     */
    return 1;
}

static log_type
clientProcessRequest2(clientHttpRequest * http)
{
    request_t *r = http->request;
    StoreEntry *e;
    if (r->flags.cachable || r->flags.internal)
	e = http->entry = storeGetPublicByRequest(r);
    else
	e = http->entry = NULL;
    /* Release IP-cache entries on reload */
    if (r->flags.nocache) {
#if USE_DNSSERVERS
	ipcacheInvalidate(r->host);
#else
	ipcacheInvalidateNegative(r->host);
#endif /* USE_DNSSERVERS */
    }
#if HTTP_VIOLATIONS
    else if (r->flags.nocache_hack) {
#if USE_DNSSERVERS
	ipcacheInvalidate(r->host);
#else
	ipcacheInvalidateNegative(r->host);
#endif /* USE_DNSSERVERS */
    }
#endif /* HTTP_VIOLATIONS */
#if USE_CACHE_DIGESTS
    http->lookup_type = e ? "HIT" : "MISS";
#endif
    if (NULL == e) {
	/* this object isn't in the cache */
	debug(33, 3) ("clientProcessRequest2: storeGet() MISS\n");
	if (r->vary) {
	    if (r->done_etag) {
		debug(33, 2) ("clientProcessRequest2: ETag loop\n");
	    } else if (r->etags) {
		debug(33, 2) ("clientProcessRequest2: ETag miss\n");
		r->etags = NULL;
	    } else if (r->vary->etags.count > 0) {
		r->etags = &r->vary->etags;
	    }
	}
	return LOG_TCP_MISS;
    }
    if (Config.onoff.offline) {
	debug(33, 3) ("clientProcessRequest2: offline HIT\n");
	http->entry = e;
	return LOG_TCP_HIT;
    }
    if (http->redirect.status) {
	/* force this to be a miss */
	http->entry = NULL;
	return LOG_TCP_MISS;
    }
    if (!storeEntryValidToSend(e)) {
	debug(33, 3) ("clientProcessRequest2: !storeEntryValidToSend MISS\n");
	http->entry = NULL;
	return LOG_TCP_MISS;
    }
    if (EBIT_TEST(e->flags, KEY_EARLY_PUBLIC)) {
	if (clientOnlyIfCached(http)) {
	    debug(33, 3) ("clientProcessRequest2: collapsed only-if-cached MISS\n");
	    http->entry = NULL;
	    return LOG_TCP_MISS;
	}
	r->flags.collapsed = 1;	/* Don't trust the store entry */
    }
    if (EBIT_TEST(e->flags, ENTRY_SPECIAL)) {
	/* Special entries are always hits, no matter what the client says */
	debug(33, 3) ("clientProcessRequest2: ENTRY_SPECIAL HIT\n");
	http->entry = e;
	return LOG_TCP_HIT;
    }
    if (r->flags.nocache) {
	debug(33, 3) ("clientProcessRequest2: no-cache REFRESH MISS\n");
	http->entry = NULL;
	return LOG_TCP_CLIENT_REFRESH_MISS;
    }
    if (NULL == r->range) {
	(void) 0;
    } else if (httpHdrRangeWillBeComplex(r->range)) {
	/*
	 * Some clients break if we return "200 OK" for a Range
	 * request.  We would have to return "200 OK" for a _complex_
	 * Range request that is also a HIT. Thus, let's prevent HITs
	 * on complex Range requests
	 */
	debug(33, 3) ("clientProcessRequest2: complex range MISS\n");
	http->entry = NULL;
	return LOG_TCP_MISS;
    } else if (clientCheckRangeForceMiss(e, r->range)) {
	debug(33, 3) ("clientProcessRequest2: forcing miss due to range_offset_limit\n");
	http->entry = NULL;
	return LOG_TCP_MISS;
    }
    debug(33, 3) ("clientProcessRequest2: default HIT\n");
    http->entry = e;
    return LOG_TCP_HIT;
}

static void
clientProcessRequest(clientHttpRequest * http)
{
    char *url = http->uri;
    request_t *r = http->request;
    HttpReply *rep;
    debug(33, 4) ("clientProcessRequest: %s '%s'\n",
	RequestMethods[r->method].str,
	url);
    r->flags.collapsed = 0;
    if (httpHeaderHas(&r->header, HDR_EXPECT)) {
	int ignore = 0;
	if (Config.onoff.ignore_expect_100) {
	    String expect = httpHeaderGetList(&r->header, HDR_EXPECT);
	    if (strCaseCmp(expect, "100-continue") == 0)
		ignore = 1;
	    stringClean(&expect);
	}
	if (!ignore) {
	    ErrorState *err = errorCon(ERR_INVALID_REQ, HTTP_EXPECTATION_FAILED, r);
	    http->log_type = LOG_TCP_MISS;
	    http->entry = clientCreateStoreEntry(http, http->request->method, null_request_flags);
	    errorAppendEntry(http->entry, err);
	    return;
	}
    }
    if (r->method == METHOD_CONNECT && !http->redirect.status) {
	http->log_type = LOG_TCP_MISS;
#if USE_SSL && SSL_CONNECT_INTERCEPT
	if (Config.Sockaddr.https) {
	    static const char ok[] = "HTTP/1.0 200 Established\r\n\r\n";
	    write(http->conn->fd, ok, strlen(ok));
	    httpsAcceptSSL(http->conn, Config.Sockaddr.https->sslContext);
	    httpRequestFree(http);
	} else
#endif
	    sslStart(http, &http->out.size, &http->al.http.code);
	return;
    } else if (r->method == METHOD_PURGE) {
	clientPurgeRequest(http);
	return;
    } else if (r->method == METHOD_TRACE) {
	if (r->max_forwards == 0) {
	    http->log_type = LOG_TCP_HIT;
	    http->entry = clientCreateStoreEntry(http, r->method, null_request_flags);
	    storeReleaseRequest(http->entry);
	    storeBuffer(http->entry);
	    rep = httpReplyCreate();
	    httpReplySetHeaders(rep, HTTP_OK, NULL, "text/plain", httpRequestPrefixLen(r), -1, squid_curtime);
	    httpReplySwapOut(rep, http->entry);
	    httpRequestSwapOut(r, http->entry);
	    storeComplete(http->entry);
	    return;
	}
	/* yes, continue */
	http->log_type = LOG_TCP_MISS;
    } else {
	http->log_type = clientProcessRequest2(http);
    }
    debug(33, 4) ("clientProcessRequest: %s for '%s'\n",
	log_tags[http->log_type],
	http->uri);
    http->out.offset = 0;
    if (NULL != http->entry) {
	storeLockObject(http->entry);
	if (http->entry->store_status == STORE_PENDING && http->entry->mem_obj) {
	    if (http->entry->mem_obj->request)
		r->hier = http->entry->mem_obj->request->hier;
	}
	storeCreateMemObject(http->entry, http->uri);
	http->entry->mem_obj->method = r->method;
	http->sc = storeClientRegister(http->entry, http);
#if DELAY_POOLS
	delaySetStoreClient(http->sc, delayClient(http));
#endif
	storeClientCopyHeaders(http->sc, http->entry,
	    clientCacheHit,
	    http);
    } else {
	/* MISS CASE, http->log_type is already set! */
	clientProcessMiss(http);
    }
}

/*
 * Prepare to fetch the object as it's a cache miss of some kind.
 */
static void
clientProcessMiss(clientHttpRequest * http)
{
    char *url = http->uri;
    request_t *r = http->request;
    ErrorState *err = NULL;
    debug(33, 4) ("clientProcessMiss: '%s %s'\n",
	RequestMethods[r->method].str, url);
    http->flags.hit = 0;
    r->flags.collapsed = 0;
    /*
     * We might have a left-over StoreEntry from a failed cache hit
     * or IMS request.
     */
    if (http->entry) {
	if (EBIT_TEST(http->entry->flags, ENTRY_SPECIAL)) {
	    debug(33, 0) ("clientProcessMiss: miss on a special object (%s).\n", url);
	    debug(33, 0) ("\tlog_type = %s\n", log_tags[http->log_type]);
	    storeEntryDump(http->entry, 1);
	}
	/* touch timestamp for refresh_stale_hit */
	if (http->entry->mem_obj)
	    http->entry->mem_obj->refresh_timestamp = squid_curtime;
	storeClientUnregister(http->sc, http->entry, http);
	http->sc = NULL;
	storeUnlockObject(http->entry);
	http->entry = NULL;
    }
    if (r->method == METHOD_PURGE) {
	clientPurgeRequest(http);
	return;
    }
    if (clientOnlyIfCached(http)) {
	clientProcessOnlyIfCachedMiss(http);
	return;
    }
    /*
     * Deny double loops
     */
    if (r->flags.loopdetect_twice) {
	http->al.http.code = HTTP_GATEWAY_TIMEOUT;
	err = errorCon(ERR_CANNOT_FORWARD, HTTP_GATEWAY_TIMEOUT, http->orig_request);
	http->log_type = LOG_TCP_DENIED;
	http->entry = clientCreateStoreEntry(http, r->method, null_request_flags);
	errorAppendEntry(http->entry, err);
	return;
    }
    assert(http->out.offset == 0);
    if (http->redirect.status) {
	HttpReply *rep = httpReplyCreate();
	http->entry = clientCreateStoreEntry(http, r->method, r->flags);
#if LOG_TCP_REDIRECTS
	http->log_type = LOG_TCP_REDIRECT;
#endif
	storeReleaseRequest(http->entry);
	httpRedirectReply(rep, http->redirect.status, http->redirect.location);
	httpReplySwapOut(rep, http->entry);
	storeComplete(http->entry);
	return;
    }
    if (r->etags) {
	clientProcessETag(http);
	return;
    }
    http->entry = clientCreateStoreEntry(http, r->method, r->flags);
    if (Config.onoff.collapsed_forwarding && r->flags.cachable && !r->flags.need_validation && (r->method == METHOD_GET || r->method == METHOD_HEAD)) {
	http->entry->mem_obj->refresh_timestamp = squid_curtime;
	/* Set the vary object state */
	safe_free(http->entry->mem_obj->vary_headers);
	if (r->vary_headers)
	    http->entry->mem_obj->vary_headers = xstrdup(r->vary_headers);
	safe_free(http->entry->mem_obj->vary_encoding);
	if (strBuf(r->vary_encoding))
	    http->entry->mem_obj->vary_encoding = xstrdup(strBuf(r->vary_encoding));
	http->entry->mem_obj->request = requestLink(r);
	EBIT_SET(http->entry->flags, KEY_EARLY_PUBLIC);
	storeSetPublicKey(http->entry);
    }
    fwdStart(http->conn->fd, http->entry, r);
}

static clientHttpRequest *
parseHttpRequestAbort(ConnStateData * conn, const char *uri)
{
    clientHttpRequest *http;
    http = cbdataAlloc(clientHttpRequest);
    http->conn = conn;
    http->start = current_time;
    http->req_sz = conn->in.offset;
    http->uri = xstrdup(uri);
    http->range_iter.boundary = StringNull;
    httpBuildVersion(&http->http_ver, 1, 0);
    dlinkAdd(http, &http->active, &ClientActiveRequests);
    return http;
}

/*
 *  parseHttpRequest()
 * 
 *  Returns
 *   NULL on error or incomplete request
 *    a clientHttpRequest structure on success
 */
static clientHttpRequest *
parseHttpRequest(ConnStateData * conn, HttpMsgBuf * hmsg, method_t * method_p, int *status)
{
    LOCAL_ARRAY(char, urlbuf, MAX_URL);
    char *url = urlbuf;
    const char *req_hdr = NULL;
    http_version_t http_ver;
    size_t header_sz;		/* size of headers, not including first line */
    size_t prefix_sz;		/* size of whole request (req-line + headers) */
    size_t req_sz;
    method_t method;
    clientHttpRequest *http = NULL;
    char *t;
    int ret;

    /* pre-set these values to make aborting simpler */
    *method_p = METHOD_NONE;
    *status = -1;

    /* Parse the request line */
    ret = httpMsgParseRequestLine(hmsg);
    if (ret == -1)
	return parseHttpRequestAbort(conn, "error:invalid-request");
    if (ret == 0) {
	debug(33, 5) ("Incomplete request, waiting for end of request line\n");
	*status = 0;
	return NULL;
    }
    /* If HTTP/0.9 then there's no headers */
    if (hmsg->v_maj == 0 && hmsg->v_min == 9) {
	req_sz = hmsg->r_len;
    } else {
	req_sz = httpMsgFindHeadersEnd(hmsg);
	if (req_sz == 0) {
	    debug(33, 5) ("Incomplete request, waiting for end of headers\n");
	    *status = 0;
	    return NULL;
	}
    }
    /* Set version */
    httpBuildVersion(&http_ver, hmsg->v_maj, hmsg->v_min);

    /* Enforce max_request_size */
    if (req_sz >= Config.maxRequestHeaderSize) {
	debug(33, 5) ("parseHttpRequest: Too large request\n");
	return parseHttpRequestAbort(conn, "error:request-too-large");
    }
    /* Look for request method */
    method = urlParseMethod(hmsg->buf + hmsg->m_start, hmsg->m_len);

    if (method == METHOD_NONE) {
	debug(33, 1) ("parseHttpRequest: Unsupported method '%.*s'\n", hmsg->m_len, hmsg->buf + hmsg->m_start);
	return parseHttpRequestAbort(conn, "error:unsupported-request-method");
    }
    debug(33, 5) ("parseHttpRequest: Method is '%s'\n", RequestMethods[method].str);
    *method_p = method;

    /* Make sure URL fits inside MAX_URL */
    if (hmsg->u_len >= MAX_URL) {
	debug(33, 1) ("parseHttpRequest: URL too big (%d) chars: %s\n", hmsg->u_len, hmsg->buf + hmsg->u_start);
	return parseHttpRequestAbort(conn, "error:request-too-large");
    }
    xmemcpy(urlbuf, hmsg->buf + hmsg->u_start, hmsg->u_len);
    /* XXX off-by-one termination error? */
    urlbuf[hmsg->u_len] = '\0';
    debug(33, 5) ("parseHttpRequest: URI is '%s'\n", urlbuf);

    /*
     * Process headers after request line
     * XXX at this point we really should just parse the damned headers rather than doing
     * it later, allowing us to then do the URL acceleration stuff withuot too much hackery.
     */
    /* XXX re-evaluate all of these values and use whats in hmsg instead! */
    req_hdr = hmsg->buf + hmsg->r_len;
    header_sz = hmsg->h_len;
    debug(33, 3) ("parseHttpRequest: req_hdr = {%s}\n", req_hdr);

    prefix_sz = req_sz;
    debug(33, 3) ("parseHttpRequest: prefix_sz = %d, req_line_sz = %d\n",
	(int) prefix_sz, (int) hmsg->r_len);
    assert(prefix_sz <= conn->in.offset);

    /* Ok, all headers are received */
    http = cbdataAlloc(clientHttpRequest);
    http->http_ver = http_ver;
    http->conn = conn;
    http->start = current_time;
    http->req_sz = prefix_sz;
    http->range_iter.boundary = StringNull;
    dlinkAdd(http, &http->active, &ClientActiveRequests);

    debug(33, 5) ("parseHttpRequest: Request Header is\n%s\n", hmsg->buf + hmsg->req_end);

#if THIS_VIOLATES_HTTP_SPECS_ON_URL_TRANSFORMATION
    if ((t = strchr(url, '#')))	/* remove HTML anchors */
	*t = '\0';
#endif

    /* handle "accelerated" objects (and internal) */
    if (method == METHOD_CONNECT) {
	if (http_ver.major < 1) {
	    debug(33, 1) ("parseHttpRequest: Invalid HTTP version\n");
	    goto invalid_request;
	}
	if (conn->port->accel) {
	    debug(33, 1) ("parseHttpRequest: CONNECT not valid in accelerator mode\n");
	    goto invalid_request;
	}
    } else if (*url == '/' && Config.onoff.global_internal_static && internalCheck(url)) {
      internal:
	/* prepend our name & port */
	http->uri = xstrdup(internalStoreUri("", url));
	http->flags.internal = 1;
	http->flags.accel = 1;
	debug(33, 5) ("INTERNAL REWRITE: '%s'\n", http->uri);
    } else if (*url == '/' && conn->port->transparent) {
	int port = 0;
	const char *host = mime_get_header(req_hdr, "Host");
	char *portstr;
	if (host && (portstr = strchr(host, ':')) != NULL) {
	    *portstr++ = '\0';
	    port = atoi(portstr);
	}
	http->flags.transparent = 1;
	if (Config.onoff.accel_no_pmtu_disc) {
#if defined(IP_MTU_DISCOVER) && defined(IP_PMTUDISC_DONT)
	    int i = IP_PMTUDISC_DONT;
	    setsockopt(conn->fd, SOL_IP, IP_MTU_DISCOVER, &i, sizeof i);
#else
	    static int reported = 0;
	    if (!reported) {
		debug(33, 1) ("Notice: httpd_accel_no_pmtu_disc not supported on your platform\n");
		reported = 1;
	    }
#endif
	}
	if (!host && !conn->transparent && clientNatLookup(conn) == 0)
	    conn->transparent = 1;
	if (!host && conn->transparent) {
	    port = ntohs(conn->me.sin_port);
	    if (!host)
		host = inet_ntoa(conn->me.sin_addr);
	}
	if (host) {
	    size_t url_sz = 10 + strlen(host) + 6 + strlen(url) + 32 + Config.appendDomainLen;
	    http->uri = xcalloc(url_sz, 1);
	    if (port) {
		snprintf(http->uri, url_sz, "%s://%s:%d%s",
		    conn->port->protocol, host, port, url);
	    } else {
		snprintf(http->uri, url_sz, "%s://%s%s",
		    conn->port->protocol, host, url);
	    }
	} else if (internalCheck(url)) {
	    goto internal;
	} else {
	    goto invalid_request;
	}
    } else if (*url == '/' || conn->port->accel) {
	int vhost = conn->port->vhost;
	int vport = conn->port->vport;
	http->flags.accel = 1;
	if (*url != '/' && !vhost && strncasecmp(url, "cache_object://", 15) != 0) {
	    url = strstr(url, "//");
	    if (!url)
		goto invalid_request;
	    url = strchr(url + 2, '/');
	    if (!url)
		url = (char *) "/";
	}
	if (*url != '/') {
	    /* Fully qualified URL. Nothing special to do */
	} else if (conn->port->accel) {
	    const char *host = NULL;
	    int port;
	    size_t url_sz;
	    if (vport > 0)
		port = vport;
	    else
		port = htons(http->conn->me.sin_port);
	    if (vhost && (t = mime_get_header(req_hdr, "Host")))
		host = t;
	    else if (conn->port->defaultsite)
		host = conn->port->defaultsite;
	    else if (vport == -1)
		host = inet_ntoa(http->conn->me.sin_addr);
	    else
		host = getMyHostname();
	    url_sz = strlen(url) + 32 + Config.appendDomainLen + strlen(host);
	    http->uri = xcalloc(url_sz, 1);
	    if (strchr(host, ':'))
		snprintf(http->uri, url_sz, "%s://%s%s",
		    conn->port->protocol, host, url);
	    else
		snprintf(http->uri, url_sz, "%s://%s:%d%s",
		    conn->port->protocol, host, port, url);
	    debug(33, 5) ("VHOST REWRITE: '%s'\n", http->uri);
	} else if (internalCheck(url)) {
	    goto internal;
	} else {
	    goto invalid_request;
	}
    }
    if (!http->uri) {
	/* No special rewrites have been applied above, use the
	 * requested url. may be rewritten later, so make extra room */
	size_t url_sz = strlen(url) + Config.appendDomainLen + 5;
	http->uri = xcalloc(url_sz, 1);
	strcpy(http->uri, url);
    }
    debug(33, 5) ("parseHttpRequest: Complete request received\n");
    *status = 1;
    return http;

  invalid_request:
    /* This tries to back out what is done above */
    dlinkDelete(&http->active, &ClientActiveRequests);
    safe_free(http->uri);
    cbdataFree(http);
    return parseHttpRequestAbort(conn, "error:invalid-request");
}

static int
clientReadDefer(int fd, void *data)
{
    fde *F = &fd_table[fd];
    ConnStateData *conn = data;
    if (conn->body.size_left && !F->flags.socket_eof) {
	if (conn->in.offset >= conn->in.size - 1) {
	    commDeferFD(fd);
	    return 1;
	} else {
	    return 0;
	}
    } else {
	if (conn->defer.until > squid_curtime) {
	    /* This is a second resolution timer, so commEpollBackon will 
	     * handle the resume for this defer call */
	    commDeferFD(fd);
	    return 1;
	} else {
	    return 0;
	}
    }
}

/*
 * Attempt to parse a request in the conn buffer
 *
 * Return the number of bytes to consume from the buffer.
 * >0 : consume X bytes and try parsing next request
 * =0 : couldn't consume anything this trip (partial request); stop parsing & read more data
 * <0 : error; stop parsing
 */
static int
clientTryParseRequest(ConnStateData * conn)
{
    int fd = conn->fd;
    int nrequests;
    dlink_node *n;
    clientHttpRequest *http = NULL;
    method_t method;
    ErrorState *err = NULL;
    int parser_return_code = 0;
    request_t *request = NULL;
    HttpMsgBuf msg;


    /* Skip leading (and trailing) whitespace */
    while (conn->in.offset > 0 && xisspace(conn->in.buf[0])) {
	xmemmove(conn->in.buf, conn->in.buf + 1, conn->in.offset - 1);
	conn->in.offset--;
    }
    conn->in.buf[conn->in.offset] = '\0';	/* Terminate the string */
    if (conn->in.offset == 0)
	return 0;

    HttpMsgBufInit(&msg, conn->in.buf, conn->in.offset);	/* XXX for now there's no deallocation function needed but this may change */
    /* Limit the number of concurrent requests to 2 */
    for (n = conn->reqs.head, nrequests = 0; n; n = n->next, nrequests++);
    if (nrequests >= (Config.onoff.pipeline_prefetch ? 2 : 1)) {
	debug(33, 3) ("clientTryParseRequest: FD %d max concurrent requests reached\n", fd);
	debug(33, 5) ("clientTryParseRequest: FD %d defering new request until one is done\n", fd);
	conn->defer.until = squid_curtime + 100;	/* Reset when a request is complete */
	return 0;
    }
    conn->in.buf[conn->in.offset] = '\0';	/* Terminate the string */
    if (nrequests == 0)
	fd_note_static(conn->fd, "Reading next request");
    /* Process request */
    http = parseHttpRequest(conn, &msg, &method, &parser_return_code);
    if (!http) {
	/* falls through here to the "if parser_return_code == 0"; not sure what will
	 * happen if http == NULL and parser_return_code != 0 .. */
    }
    if (http) {
	/* add to the client request queue */
	dlinkAddTail(http, &http->node, &conn->reqs);
	conn->nrequests++;
	commSetTimeout(fd, Config.Timeout.lifetime, clientLifetimeTimeout, http);
	if (parser_return_code < 0) {
	    debug(33, 1) ("clientTryParseRequest: FD %d (%s:%d) Invalid Request\n", fd, fd_table[fd].ipaddr, fd_table[fd].remote_port);
	    err = errorCon(ERR_INVALID_REQ, HTTP_BAD_REQUEST, NULL);
	    err->src_addr = conn->peer.sin_addr;
	    err->request_hdrs = xstrdup(conn->in.buf);
	    http->log_type = LOG_TCP_DENIED;
	    http->entry = clientCreateStoreEntry(http, method, null_request_flags);
	    errorAppendEntry(http->entry, err);
	    return -1;
	}
	if ((request = urlParse(method, http->uri)) == NULL) {
	    debug(33, 5) ("Invalid URL: %s\n", http->uri);
	    err = errorCon(ERR_INVALID_URL, HTTP_BAD_REQUEST, NULL);
	    err->src_addr = conn->peer.sin_addr;
	    err->url = xstrdup(http->uri);
	    http->al.http.code = err->http_status;
	    http->log_type = LOG_TCP_DENIED;
	    http->entry = clientCreateStoreEntry(http, method, null_request_flags);
	    errorAppendEntry(http->entry, err);
	    return -1;
	}
	/* compile headers */
	/* we should skip request line! */
	if ((http->http_ver.major >= 1) && !httpMsgParseRequestHeader(request, &msg)) {
	    debug(33, 1) ("Failed to parse request headers: %s\n%s\n",
		http->uri, msg.buf + msg.req_end);
	    err = errorCon(ERR_INVALID_URL, HTTP_BAD_REQUEST, request);
	    err->url = xstrdup(http->uri);
	    http->al.http.code = err->http_status;
	    http->log_type = LOG_TCP_DENIED;
	    http->entry = clientCreateStoreEntry(http, method, null_request_flags);
	    errorAppendEntry(http->entry, err);
	    return -1;
	}
	/*
	 * If we read past the end of this request, move the remaining
	 * data to the beginning
	 */
	assert(conn->in.offset >= http->req_sz);
	conn->in.offset -= http->req_sz;
	debug(33, 5) ("removing %d bytes; conn->in.offset = %d\n", (int) http->req_sz, (int) conn->in.offset);
	if (conn->in.offset > 0)
	    xmemmove(conn->in.buf, conn->in.buf + http->req_sz, conn->in.offset);

	if (!http->flags.internal && internalCheck(strBuf(request->urlpath))) {
	    if (internalHostnameIs(request->host))
		http->flags.internal = 1;
	    else if (Config.onoff.global_internal_static && internalStaticCheck(strBuf(request->urlpath)))
		http->flags.internal = 1;
	    if (http->flags.internal) {
		request_t *old_request = requestLink(request);
		request = urlParse(method, internalStoreUri("", strBuf(request->urlpath)));
		httpHeaderAppend(&request->header, &old_request->header);
		requestUnlink(old_request);
	    }
	}
	if (conn->port->urlgroup)
	    request->urlgroup = xstrdup(conn->port->urlgroup);
#if LINUX_TPROXY
	request->flags.tproxy = conn->port->tproxy && need_linux_tproxy;
#endif
	request->flags.accelerated = http->flags.accel;
	request->flags.no_direct = request->flags.accelerated ? !conn->port->allow_direct : 0;
	request->flags.transparent = http->flags.transparent;
	/*
	 * cache the Content-length value in request_t.
	 */
	request->content_length = httpHeaderGetSize(&request->header,
	    HDR_CONTENT_LENGTH);
	request->flags.internal = http->flags.internal;
	request->client_addr = conn->peer.sin_addr;
	request->client_port = ntohs(conn->peer.sin_port);
#if FOLLOW_X_FORWARDED_FOR
	request->indirect_client_addr = request->client_addr;
#endif /* FOLLOW_X_FORWARDED_FOR */
	request->my_addr = conn->me.sin_addr;
	request->my_port = ntohs(conn->me.sin_port);
	request->http_ver = http->http_ver;
	if (!urlCheckRequest(request)) {
	    err = errorCon(ERR_UNSUP_REQ, HTTP_NOT_IMPLEMENTED, request);
	    request->flags.proxy_keepalive = 0;
	    http->al.http.code = err->http_status;
	    http->log_type = LOG_TCP_DENIED;
	    http->entry = clientCreateStoreEntry(http, request->method, null_request_flags);
	    errorAppendEntry(http->entry, err);
	    return -1;
	}
	if (!clientCheckContentLength(request) || httpHeaderHas(&request->header, HDR_TRANSFER_ENCODING)) {
	    err = errorCon(ERR_INVALID_REQ, HTTP_LENGTH_REQUIRED, request);
	    http->al.http.code = err->http_status;
	    http->log_type = LOG_TCP_DENIED;
	    http->entry = clientCreateStoreEntry(http, request->method, null_request_flags);
	    errorAppendEntry(http->entry, err);
	    return -1;
	}
	http->request = requestLink(request);
	http->orig_request = requestLink(request);
	clientSetKeepaliveFlag(http);
	/* Do we expect a request-body? */
	if (request->content_length > 0) {
	    conn->body.size_left = request->content_length;
	    requestRegisterBody(request, clientReadBody, conn);
	    /* Is it too large? */
	    if (clientRequestBodyTooLarge(request->content_length)) {
		err = errorCon(ERR_TOO_BIG, HTTP_REQUEST_ENTITY_TOO_LARGE, request);
		http->log_type = LOG_TCP_DENIED;
		http->entry = clientCreateStoreEntry(http,
		    METHOD_NONE, null_request_flags);
		errorAppendEntry(http->entry, err);
		return -1;
	    }
	}
	if (request->method == METHOD_CONNECT) {
	    /* Stop reading requests... */
	    commSetSelect(fd, COMM_SELECT_READ, NULL, NULL, 0);
	    if (!DLINK_ISEMPTY(conn->reqs) && DLINK_HEAD(conn->reqs) == http)
		clientCheckFollowXForwardedFor(http);
	    else {
		debug(33, 1) ("WARNING: pipelined CONNECT request seen from %s\n", inet_ntoa(http->conn->peer.sin_addr));
		debugObj(33, 1, "Previous request:\n", ((clientHttpRequest *) DLINK_HEAD(conn->reqs))->request,
		    (ObjPackMethod) & httpRequestPackDebug);
		debugObj(33, 1, "This request:\n", request, (ObjPackMethod) & httpRequestPackDebug);
	    }
	    return -2;
	} else {
	    clientCheckFollowXForwardedFor(http);
	}
    } else if (parser_return_code == 0) {
	/*
	 *    Partial request received; reschedule until parseHttpRequest()
	 *    is happy with the input
	 */
	if (conn->in.offset >= Config.maxRequestHeaderSize) {
	    /* The request is too large to handle */
	    debug(33, 1) ("Request header is too large (%d bytes)\n",
		(int) conn->in.offset);
	    debug(33, 1) ("Config 'request_header_max_size'= %ld bytes.\n",
		(long int) Config.maxRequestHeaderSize);
	    err = errorCon(ERR_TOO_BIG, HTTP_REQUEST_URI_TOO_LONG, NULL);
	    err->src_addr = conn->peer.sin_addr;
	    http = parseHttpRequestAbort(conn, "error:request-too-large");
	    /* add to the client request queue */
	    dlinkAddTail(http, &http->node, &conn->reqs);
	    http->log_type = LOG_TCP_DENIED;
	    http->entry = clientCreateStoreEntry(http, METHOD_NONE, null_request_flags);
	    errorAppendEntry(http->entry, err);
	    return -1;
	}
	return 0;
    }
    if (!cbdataValid(conn))
	return -1;

    /* 
     * For now we assume "here" means "we parsed a valid request. This might not be the case
     * as I might've broken up clientReadRequest() wrong. Quite a bit more work should be
     * done to simplify this code anyway so the first step is identifying the cases where
     * this isn't true.
     */
    assert(http != NULL);
    assert(http->req_sz > 0);

    return http->req_sz;
}

static void
clientReadRequest(int fd, void *data)
{
    ConnStateData *conn = data;
    int size;
    fde *F = &fd_table[fd];
    int len = conn->in.size - conn->in.offset - 1;
    int ret;
    debug(33, 4) ("clientReadRequest: FD %d: reading request...\n", fd);
    if (len == 0) {
	/* Grow the request memory area to accomodate for a large request */
	conn->in.buf = memReallocBuf(conn->in.buf, conn->in.size * 2, &conn->in.size);
	debug(33, 2) ("growing request buffer: offset=%ld size=%ld\n",
	    (long) conn->in.offset, (long) conn->in.size);
	len = conn->in.size - conn->in.offset - 1;
    }
    statCounter.syscalls.sock.reads++;
    size = FD_READ_METHOD(fd, conn->in.buf + conn->in.offset, len);
    if (size > 0) {
	fd_bytes(fd, size, FD_READ);
	kb_incr(&statCounter.client_http.kbytes_in, size);
    }
    /*
     * Don't reset the timeout value here.  The timeout value will be
     * set to Config.Timeout.request by httpAccept() and
     * clientWriteComplete(), and should apply to the request as a
     * whole, not individual read() calls.  Plus, it breaks our
     * lame half-close detection
     */
    if (size > 0) {
	conn->in.offset += size;
	conn->in.buf[conn->in.offset] = '\0';	/* Terminate the string */
    } else if (size == 0) {
	if (DLINK_ISEMPTY(conn->reqs) && conn->in.offset == 0) {
	    /* no current or pending requests */
	    debug(33, 4) ("clientReadRequest: FD %d closed\n", fd);
	    comm_close(fd);
	    return;
	} else if (!Config.onoff.half_closed_clients) {
	    /* admin doesn't want to support half-closed client sockets */
	    debug(33, 3) ("clientReadRequest: FD %d aborted (half_closed_clients disabled)\n", fd);
	    comm_close(fd);
	    return;
	}
	/* It might be half-closed, we can't tell */
	debug(33, 5) ("clientReadRequest: FD %d closed?\n", fd);
	F->flags.socket_eof = 1;
	conn->defer.until = squid_curtime + 1;
	conn->defer.n++;
	fd_note_static(fd, "half-closed");
	/* There is one more close check at the end, to detect aborted
	 * (partial) requests. At this point we can't tell if the request
	 * is partial.
	 */
	/* Continue to process previously read data */
    } else if (size < 0) {
	if (!ignoreErrno(errno)) {
	    debug(50, 2) ("clientReadRequest: FD %d: %s\n", fd, xstrerror());
	    comm_close(fd);
	    return;
	} else if (conn->in.offset == 0) {
	    debug(50, 2) ("clientReadRequest: FD %d: no data to process (%s)\n", fd, xstrerror());
	}
	/* Continue to process previously read data */
    }
    cbdataLock(conn);		/* clientProcessBody might pull the connection under our feets */
    /* Process request body if any */
    if (conn->in.offset > 0 && conn->body.callback != NULL) {
	clientProcessBody(conn);
	if (!cbdataValid(conn)) {
	    cbdataUnlock(conn);
	    return;
	}
    }
    /* Process next request */
    ret = 0;
    while (cbdataValid(conn) && conn->in.offset > 0 && conn->body.size_left == 0) {
	/* Ret tells us how many bytes was consumed - 0 == didn't consume request, > 0 == consumed, -1 == error, -2 == CONNECT request stole the connection */
	ret = clientTryParseRequest(conn);
	if (ret <= 0)
	    break;
    }				/* while offset > 0 && conn->body.size_left == 0 */
    if (!cbdataValid(conn)) {
	cbdataUnlock(conn);
	return;
    }
    cbdataUnlock(conn);
    /* Check if a half-closed connection was aborted in the middle */
    if (F->flags.socket_eof) {
	if (conn->in.offset != conn->body.size_left) {	/* != 0 when no request body */
	    /* Partial request received. Abort client connection! */
	    debug(33, 3) ("clientReadRequest: FD %d aborted, partial request\n", fd);
	    comm_close(fd);
	    return;
	}
    }
    if (ret >= 0)
	commSetSelect(fd, COMM_SELECT_READ, clientReadRequest, conn, 0);
}

/* file_read like function, for reading body content */
static void
clientReadBody(void *data, request_t * request, char *buf, size_t size, CBCB * callback, void *cbdata)
{
    ConnStateData *conn = data;
    if (!callback) {
	clientAbortBody(conn, request);
	return;
    }
    if (!conn) {
	debug(33, 5) ("clientReadBody: no body to read, request=%p\n", request);
	callback(buf, 0, cbdata);	/* Signal end of body */
	return;
    }
    assert(cbdataValid(conn));
    debug(33, 2) ("clientReadBody: start fd=%d body_size=%lu in.offset=%ld cb=%p req=%p\n", conn->fd, (unsigned long int) conn->body.size_left, (long int) conn->in.offset, callback, request);
    conn->body.callback = callback;
    conn->body.cbdata = cbdata;
    cbdataLock(conn->body.cbdata);
    conn->body.buf = buf;
    conn->body.bufsize = size;
    conn->body.request = requestLink(request);
    clientProcessBody(conn);
}

static void
clientEatRequestBodyHandler(char *buf, ssize_t size, void *data)
{
    clientHttpRequest *http = data;
    ConnStateData *conn = http->conn;
    if (buf && size < 0) {
	return;			/* Aborted, don't care */
    }
    if (conn->body.size_left > 0) {
	conn->body.callback = clientEatRequestBodyHandler;
	conn->body.cbdata = http;
	cbdataLock(conn->body.cbdata);
	conn->body.buf = NULL;
	conn->body.bufsize = SQUID_TCP_SO_RCVBUF;
	clientProcessBody(conn);
    } else {
	if (http->request->flags.proxy_keepalive) {
	    debug(33, 5) ("clientEatRequestBodyHandler: FD %d Keeping Alive\n", conn->fd);
	    clientKeepaliveNextRequest(http);
	} else {
	    comm_close(conn->fd);
	}
    }
}

static void
clientEatRequestBody(clientHttpRequest * http)
{
    ConnStateData *conn = http->conn;
    cbdataLock(conn);
    if (conn->body.request)
	requestAbortBody(conn->body.request);
    if (cbdataValid(conn))
	clientEatRequestBodyHandler(NULL, -1, http);
    cbdataUnlock(conn);
}

/* Called by clientReadRequest to process body content */
static void
clientProcessBody(ConnStateData * conn)
{
    int size;
    char *buf = conn->body.buf;
    void *cbdata = conn->body.cbdata;
    CBCB *callback = conn->body.callback;
    request_t *request = conn->body.request;
    /* Note: request is null while eating "aborted" transfers */
    debug(33, 2) ("clientProcessBody: start fd=%d body_size=%lu in.offset=%ld cb=%p req=%p\n", conn->fd, (unsigned long int) conn->body.size_left, (long int) conn->in.offset, callback, request);
    if (conn->in.offset) {
	int valid = cbdataValid(conn->body.cbdata);
	if (!valid) {
	    comm_close(conn->fd);
	    return;
	}
	/* Some sanity checks... */
	assert(conn->body.size_left > 0);
	assert(conn->in.offset > 0);
	assert(callback != NULL);
	/* How much do we have to process? */
	size = conn->in.offset;
	if (size > conn->body.size_left)	/* only process the body part */
	    size = conn->body.size_left;
	if (size > conn->body.bufsize)	/* don't copy more than requested */
	    size = conn->body.bufsize;
	if (valid && buf)
	    xmemcpy(buf, conn->in.buf, size);
	conn->body.size_left -= size;
	/* Move any remaining data */
	conn->in.offset -= size;
	/* Resume the fd if necessary */
	if (conn->in.offset < conn->in.size - 1)
	    commResumeFD(conn->fd);
	if (conn->in.offset > 0)
	    xmemmove(conn->in.buf, conn->in.buf + size, conn->in.offset);
	/* Remove request link if this is the last part of the body, as
	 * clientReadRequest automatically continues to process next request */
	if (conn->body.size_left <= 0 && request != NULL)
	    requestUnregisterBody(request, clientReadBody, conn);
	/* Remove clientReadBody arguments (the call is completed) */
	conn->body.request = NULL;
	conn->body.callback = NULL;
	cbdataUnlock(conn->body.cbdata);
	conn->body.cbdata = NULL;
	conn->body.buf = NULL;
	conn->body.bufsize = 0;
	/* Remember that we have touched the body, not restartable */
	if (request != NULL)
	    request->flags.body_sent = 1;
	/* Invoke callback function */
	if (valid)
	    callback(buf, size, cbdata);
	if (request != NULL)
	    requestUnlink(request);	/* Linked in clientReadBody */
	debug(33, 2) ("clientProcessBody: end fd=%d size=%d body_size=%lu in.offset=%ld cb=%p req=%p\n", conn->fd, size, (unsigned long int) conn->body.size_left, (long int) conn->in.offset, callback, request);
    }
}

/* Abort a body request */
static void
clientAbortBody(ConnStateData * conn, request_t * request)
{
    char *buf;
    CBCB *callback;
    void *cbdata;
    int valid;
    if (!conn || !cbdataValid(conn))
	return;
    if (!conn->body.callback || conn->body.request != request)
	return;
    buf = conn->body.buf;
    callback = conn->body.callback;
    cbdata = conn->body.cbdata;
    valid = cbdataValid(cbdata);
    assert(request == conn->body.request);
    conn->body.buf = NULL;
    conn->body.callback = NULL;
    cbdataUnlock(conn->body.cbdata);
    conn->body.cbdata = NULL;
    conn->body.request = NULL;
    if (valid)
	callback(buf, -1, cbdata);	/* Signal abort to clientReadBody caller to allow them to clean up */
    else
	debug(33, 1) ("NOTICE: A request body was aborted with cancelled callback: %p, possible memory leak\n", callback);
    requestUnlink(request);	/* Linked in clientReadBody */
}

/* general lifetime handler for HTTP requests */
static void
requestTimeout(int fd, void *data)
{
    /*
     * Just close the connection to not confuse browsers
     * using persistent connections. Some browsers opens
     * an connection and then does not use it until much
     * later (presumeably because the request triggering
     * the open has already been completed on another
     * connection)
     */
    debug(33, 3) ("requestTimeout: FD %d: lifetime is expired.\n", fd);
    comm_close(fd);
}

static void
clientLifetimeTimeout(int fd, void *data)
{
    clientHttpRequest *http = data;
    ConnStateData *conn = http->conn;
    debug(33, 1) ("WARNING: Closing client %s connection due to lifetime timeout\n",
	inet_ntoa(conn->peer.sin_addr));
    debug(33, 1) ("\t%s\n", http->uri);
    comm_close(fd);
}

static int
httpAcceptDefer(int fd, void *dataunused)
{
    static time_t last_warn = 0;
    if (fdNFree() >= RESERVED_FD)
	return 0;
    if (last_warn + 15 < squid_curtime) {
	debug(33, 0) ("WARNING! Your cache is running out of filedescriptors\n");
	last_warn = squid_curtime;
    }
    commDeferFD(fd);
    return 1;
}

#if IPF_TRANSPARENT
static int
clientNatLookup(ConnStateData * conn)
{
    struct natlookup natLookup;
    static int natfd = -1;
    int x;
#if defined(IPFILTER_VERSION) && (IPFILTER_VERSION >= 4000027)
    struct ipfobj obj;
#else
    static int siocgnatl_cmd = SIOCGNATL & 0xff;
#endif
    static time_t last_reported = 0;

#if defined(IPFILTER_VERSION) && (IPFILTER_VERSION >= 4000027)
    obj.ipfo_rev = IPFILTER_VERSION;
    obj.ipfo_size = sizeof(natLookup);
    obj.ipfo_ptr = &natLookup;
    obj.ipfo_type = IPFOBJ_NATLOOKUP;
    obj.ipfo_offset = 0;
#endif

    natLookup.nl_inport = conn->me.sin_port;
    natLookup.nl_outport = conn->peer.sin_port;
    natLookup.nl_inip = conn->me.sin_addr;
    natLookup.nl_outip = conn->peer.sin_addr;
    natLookup.nl_flags = IPN_TCP;
    if (natfd < 0) {
	int save_errno;
	enter_suid();
#ifdef IPNAT_NAME
	natfd = open(IPNAT_NAME, O_RDONLY, 0);
#else
	natfd = open(IPL_NAT, O_RDONLY, 0);
#endif
	save_errno = errno;
	leave_suid();
	if (natfd >= 0)
	    commSetCloseOnExec(natfd);
	errno = save_errno;
    }
    if (natfd < 0) {
	if (squid_curtime - last_reported > 60) {
	    debug(50, 1) ("clientNatLookup: NAT open failed: %s\n",
		xstrerror());
	    last_reported = squid_curtime;
	}
	return -1;
    }
#if defined(IPFILTER_VERSION) && (IPFILTER_VERSION >= 4000027)
    x = ioctl(natfd, SIOCGNATL, &obj);
#else
    /*
     * IP-Filter changed the type for SIOCGNATL between
     * 3.3 and 3.4.  It also changed the cmd value for
     * SIOCGNATL, so at least we can detect it.  We could
     * put something in configure and use ifdefs here, but
     * this seems simpler.
     */
    if (63 == siocgnatl_cmd) {
	struct natlookup *nlp = &natLookup;
	x = ioctl(natfd, SIOCGNATL, &nlp);
    } else {
	x = ioctl(natfd, SIOCGNATL, &natLookup);
    }
#endif
    if (x < 0) {
	if (errno != ESRCH) {
	    if (squid_curtime - last_reported > 60) {
		debug(50, 1) ("clientNatLookup: NAT lookup failed: ioctl(SIOCGNATL)\n");
		last_reported = squid_curtime;
	    }
	    close(natfd);
	    natfd = -1;
	}
	return -1;
    } else {
	int natted = conn->me.sin_addr.s_addr != natLookup.nl_realip.s_addr;
	conn->me.sin_port = natLookup.nl_realport;
	conn->me.sin_addr = natLookup.nl_realip;
	if (natted)
	    return 0;
	else
	    return -1;
    }
}
#elif LINUX_NETFILTER
static int
clientNatLookup(ConnStateData * conn)
{
    socklen_t sock_sz = sizeof(conn->me);
    struct in_addr orig_addr = conn->me.sin_addr;
    static time_t last_reported = 0;
    /* If the call fails the address structure will be unchanged */
    if (getsockopt(conn->fd, SOL_IP, SO_ORIGINAL_DST, &conn->me, &sock_sz) != 0) {
	if (squid_curtime - last_reported > 60) {
	    debug(50, 1) ("clientNatLookup: NF getsockopt(SO_ORIGINAL_DST) failed: %s\n", xstrerror());
	    last_reported = squid_curtime;
	}
	return -1;
    }
    debug(33, 5) ("clientNatLookup: addr = %s", inet_ntoa(conn->me.sin_addr));
    if (orig_addr.s_addr != conn->me.sin_addr.s_addr)
	return 0;
    else
	return -1;
}
#elif PF_TRANSPARENT
static int
clientNatLookup(ConnStateData * conn)
{
    struct pfioc_natlook nl;
    static int pffd = -1;
    static time_t last_reported = 0;
    if (pffd < 0) {
	pffd = open("/dev/pf", O_RDONLY);
	if (pffd >= 0)
	    commSetCloseOnExec(pffd);
    }
    if (pffd < 0) {
	debug(50, 1) ("clientNatLookup: PF open failed: %s\n",
	    xstrerror());
	return -1;
    }
    memset(&nl, 0, sizeof(struct pfioc_natlook));
    nl.saddr.v4.s_addr = conn->peer.sin_addr.s_addr;
    nl.sport = conn->peer.sin_port;
    nl.daddr.v4.s_addr = conn->me.sin_addr.s_addr;
    nl.dport = conn->me.sin_port;
    nl.af = AF_INET;
    nl.proto = IPPROTO_TCP;
    nl.direction = PF_OUT;
    if (ioctl(pffd, DIOCNATLOOK, &nl)) {
	if (errno != ENOENT) {
	    if (squid_curtime - last_reported > 60) {
		debug(50, 1) ("clientNatLookup: PF lookup failed: ioctl(DIOCNATLOOK)\n");
		last_reported = squid_curtime;
	    }
	    close(pffd);
	    pffd = -1;
	}
	return -1;
    } else {
	int natted = conn->me.sin_addr.s_addr != nl.rdaddr.v4.s_addr;
	conn->me.sin_port = nl.rdport;
	conn->me.sin_addr = nl.rdaddr.v4;
	if (natted)
	    return 0;
	else
	    return -1;
    }
}
#else
static int
clientNatLookup(ConnStateData * conn)
{
    static int reported = 0;
    if (!reported) {
	debug(33, 1) ("NOTICE: no explicit transparent proxy support enabled. Assuming getsockname() works on intercepted connections\n");
	reported = 1;
    }
    return 0;
}
#endif

/* Handle a new connection on HTTP socket. */
void
httpAccept(int sock, void *data)
{
    http_port_list *s = data;
    int fd = -1;
    fde *F;
    ConnStateData *connState = NULL;
    struct sockaddr_in peer;
    struct sockaddr_in me;
    int max = INCOMING_HTTP_MAX;
#if USE_IDENT
    static aclCheck_t identChecklist;
#endif
    commSetSelect(sock, COMM_SELECT_READ, httpAccept, data, 0);
    while (max-- && !httpAcceptDefer(sock, NULL)) {
	memset(&peer, '\0', sizeof(struct sockaddr_in));
	memset(&me, '\0', sizeof(struct sockaddr_in));
	if ((fd = comm_accept(sock, &peer, &me)) < 0) {
	    if (!ignoreErrno(errno))
		debug(50, 1) ("httpAccept: FD %d: accept failure: %s\n",
		    sock, xstrerror());
	    break;
	}
	F = &fd_table[fd];
	debug(33, 4) ("httpAccept: FD %d: accepted port %d client %s:%d\n", fd, F->local_port, F->ipaddr, F->remote_port);
	fd_note_static(fd, "client http connect");
	connState = cbdataAlloc(ConnStateData);
	connState->port = s;
	cbdataLock(connState->port);
	connState->peer = peer;
	connState->log_addr = peer.sin_addr;
	connState->log_addr.s_addr &= Config.Addrs.client_netmask.s_addr;
	connState->me = me;
	connState->fd = fd;
	connState->pinning.fd = -1;
	connState->in.buf = memAllocBuf(CLIENT_REQ_BUF_SZ, &connState->in.size);
	comm_add_close_handler(fd, connStateFree, connState);
	if (Config.onoff.log_fqdn)
	    fqdncache_gethostbyaddr(peer.sin_addr, FQDN_LOOKUP_IF_MISS);
	commSetTimeout(fd, Config.Timeout.request, requestTimeout, connState);
#if USE_IDENT
	identChecklist.src_addr = peer.sin_addr;
	identChecklist.my_addr = me.sin_addr;
	identChecklist.my_port = ntohs(me.sin_port);
	if (aclCheckFast(Config.accessList.identLookup, &identChecklist))
	    identStart(&me, &peer, clientIdentDone, connState);
#endif
	commSetSelect(fd, COMM_SELECT_READ, clientReadRequest, connState, 0);
	commSetDefer(fd, clientReadDefer, connState);
	if (s->tcp_keepalive.enabled) {
	    commSetTcpKeepalive(fd, s->tcp_keepalive.idle, s->tcp_keepalive.interval, s->tcp_keepalive.timeout);
	}
	clientdbEstablished(peer.sin_addr, 1);
	incoming_sockets_accepted++;
    }
}

#if USE_SSL

/* negotiate an SSL connection */
static void
clientNegotiateSSL(int fd, void *data)
{
    ConnStateData *conn = data;
    X509 *client_cert;
    SSL *ssl = fd_table[fd].ssl;
    int ret;

    if ((ret = SSL_accept(ssl)) <= 0) {
	int ssl_error = SSL_get_error(ssl, ret);
	switch (ssl_error) {
	case SSL_ERROR_WANT_READ:
	    commSetSelect(fd, COMM_SELECT_READ, clientNegotiateSSL, conn, 0);
	    return;
	case SSL_ERROR_WANT_WRITE:
	    commSetSelect(fd, COMM_SELECT_WRITE, clientNegotiateSSL, conn, 0);
	    return;
	case SSL_ERROR_SYSCALL:
	    if (ret == 0) {
		debug(83, 2) ("clientNegotiateSSL: Error negotiating SSL connection on FD %d: Aborted by client\n", fd);
		comm_close(fd);
		return;
	    } else {
		int hard = 1;
		if (errno == ECONNRESET)
		    hard = 0;
		debug(83, hard ? 1 : 2) ("clientNegotiateSSL: Error negotiating SSL connection on FD %d: %s (%d)\n",
		    fd, strerror(errno), errno);
		comm_close(fd);
		return;
	    }
	case SSL_ERROR_ZERO_RETURN:
	    debug(83, 1) ("clientNegotiateSSL: Error negotiating SSL connection on FD %d: Closed by client\n", fd);
	    comm_close(fd);
	    return;
	default:
	    debug(83, 1) ("clientNegotiateSSL: Error negotiating SSL connection on FD %d: %s (%d/%d)\n",
		fd, ERR_error_string(ERR_get_error(), NULL), ssl_error, ret);
	    comm_close(fd);
	    return;
	}
	/* NOTREACHED */
    }
    fd_table[fd].read_pending = COMM_PENDING_NOW;
    if (SSL_session_reused(ssl)) {
	debug(83, 2) ("clientNegotiateSSL: Session %p reused on FD %d (%s:%d)\n", SSL_get_session(ssl), fd, fd_table[fd].ipaddr, (int) fd_table[fd].remote_port);
    } else {
	if (do_debug(83, 4)) {
	    /* Write out the SSL session details.. actually the call below, but
	     * OpenSSL headers do strange typecasts confusing GCC.. */
	    /* PEM_write_SSL_SESSION(debug_log, SSL_get_session(ssl)); */
#if defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x00908000L
	    PEM_ASN1_write((i2d_of_void *) i2d_SSL_SESSION, PEM_STRING_SSL_SESSION, debug_log, (char *) SSL_get_session(ssl), NULL, NULL, 0, NULL, NULL);
#else
	    PEM_ASN1_write(i2d_SSL_SESSION, PEM_STRING_SSL_SESSION, debug_log, (char *) SSL_get_session(ssl), NULL, NULL, 0, NULL, NULL);
#endif
	    /* Note: This does not automatically fflush the log file.. */
	}
	debug(83, 2) ("clientNegotiateSSL: New session %p on FD %d (%s:%d)\n", SSL_get_session(ssl), fd, fd_table[fd].ipaddr, (int) fd_table[fd].remote_port);
    }
    debug(83, 3) ("clientNegotiateSSL: FD %d negotiated cipher %s\n", fd,
	SSL_get_cipher(ssl));

    client_cert = SSL_get_peer_certificate(ssl);
    if (client_cert != NULL) {
	debug(83, 3) ("clientNegotiateSSL: FD %d client certificate: subject: %s\n", fd,
	    X509_NAME_oneline(X509_get_subject_name(client_cert), 0, 0));

	debug(83, 3) ("clientNegotiateSSL: FD %d client certificate: issuer: %s\n", fd,
	    X509_NAME_oneline(X509_get_issuer_name(client_cert), 0, 0));

	X509_free(client_cert);
    } else {
	debug(83, 5) ("clientNegotiateSSL: FD %d has no certificate.\n", fd);
    }
    clientReadRequest(fd, conn);
}

static void
httpsAcceptSSL(ConnStateData * connState, SSL_CTX * sslContext)
{
    SSL *ssl;
    fde *F;
    int fd = connState->fd;
    if ((ssl = SSL_new(sslContext)) == NULL) {
	int ssl_error = ERR_get_error();
	debug(83, 1) ("httpsAcceptSSL: Error allocating handle: %s\n",
	    ERR_error_string(ssl_error, NULL));
	comm_close(fd);
	return;
    }
    SSL_set_fd(ssl, fd);
    F = &fd_table[fd];
    F->ssl = ssl;
    F->read_method = &ssl_read_method;
    F->write_method = &ssl_write_method;
    debug(50, 5) ("httpsAcceptSSL: FD %d: starting SSL negotiation.\n", fd);
    fd_note_static(fd, "client https connect");

    commSetSelect(fd, COMM_SELECT_READ, clientNegotiateSSL, connState, 0);
    commSetDefer(fd, clientReadDefer, connState);
}

/* handle a new HTTPS connection */
static void
httpsAccept(int sock, void *data)
{
    https_port_list *s = data;
    int fd = -1;
    ConnStateData *connState = NULL;
    struct sockaddr_in peer;
    struct sockaddr_in me;
    int max = INCOMING_HTTP_MAX;
#if USE_IDENT
    static aclCheck_t identChecklist;
#endif
    commSetSelect(sock, COMM_SELECT_READ, httpsAccept, s, 0);
    while (max-- && !httpAcceptDefer(sock, NULL)) {
	fde *F;
	memset(&peer, '\0', sizeof(struct sockaddr_in));
	memset(&me, '\0', sizeof(struct sockaddr_in));
	if ((fd = comm_accept(sock, &peer, &me)) < 0) {
	    if (!ignoreErrno(errno))
		debug(50, 1) ("httpsAccept: FD %d: accept failure: %s\n",
		    sock, xstrerror());
	    break;
	}
	F = &fd_table[fd];
	debug(33, 4) ("httpsAccept: FD %d: accepted port %d client %s:%d\n", fd, F->local_port, F->ipaddr, F->remote_port);
	connState = cbdataAlloc(ConnStateData);
	connState->port = (http_port_list *) s;
	cbdataLock(connState->port);
	connState->peer = peer;
	connState->log_addr = peer.sin_addr;
	connState->log_addr.s_addr &= Config.Addrs.client_netmask.s_addr;
	connState->me = me;
	connState->fd = fd;
	connState->pinning.fd = -1;
	connState->in.buf = memAllocBuf(CLIENT_REQ_BUF_SZ, &connState->in.size);
	comm_add_close_handler(fd, connStateFree, connState);
	if (Config.onoff.log_fqdn)
	    fqdncache_gethostbyaddr(peer.sin_addr, FQDN_LOOKUP_IF_MISS);
	commSetTimeout(fd, Config.Timeout.request, requestTimeout, connState);
#if USE_IDENT
	identChecklist.src_addr = peer.sin_addr;
	identChecklist.my_addr = me.sin_addr;
	identChecklist.my_port = ntohs(me.sin_port);
	if (aclCheckFast(Config.accessList.identLookup, &identChecklist))
	    identStart(&me, &peer, clientIdentDone, connState);
#endif
	if (s->http.tcp_keepalive.enabled) {
	    commSetTcpKeepalive(fd, s->http.tcp_keepalive.idle, s->http.tcp_keepalive.interval, s->http.tcp_keepalive.timeout);
	}
	clientdbEstablished(peer.sin_addr, 1);
	incoming_sockets_accepted++;
	httpsAcceptSSL(connState, s->sslContext);
    }
}

#endif /* USE_SSL */

#define SENDING_BODY 0
#define SENDING_HDRSONLY 1
static int
clientCheckTransferDone(clientHttpRequest * http)
{
    int sending = SENDING_BODY;
    StoreEntry *entry = http->entry;
    MemObject *mem;
    http_reply *reply;
    squid_off_t sendlen;
    if (entry == NULL)
	return 0;
    /*
     * For now, 'done_copying' is used for special cases like
     * Range and HEAD requests.
     */
    if (http->flags.done_copying)
	return 1;
    /*
     * Handle STORE_OK objects.
     * objectLen(entry) will be set proprely.
     */
    if (entry->store_status == STORE_OK) {
	if (http->out.offset >= objectLen(entry))
	    return 1;
	else
	    return 0;
    }
    /*
     * Now, handle STORE_PENDING objects
     */
    mem = entry->mem_obj;
    assert(mem != NULL);
    assert(http->request != NULL);
    reply = mem->reply;
    if (reply->hdr_sz == 0)
	return 0;		/* haven't found end of headers yet */
    else if (reply->sline.status == HTTP_OK)
	sending = SENDING_BODY;
    else if (reply->sline.status == HTTP_NO_CONTENT)
	sending = SENDING_HDRSONLY;
    else if (reply->sline.status == HTTP_NOT_MODIFIED)
	sending = SENDING_HDRSONLY;
    else if (reply->sline.status < HTTP_OK)
	sending = SENDING_HDRSONLY;
    else if (http->request->method == METHOD_HEAD)
	sending = SENDING_HDRSONLY;
    else
	sending = SENDING_BODY;
    /*
     * Figure out how much data we are supposed to send.
     * If we are sending a body and we don't have a content-length,
     * then we must wait for the object to become STORE_OK.
     */
    if (sending == SENDING_HDRSONLY)
	sendlen = reply->hdr_sz;
    else if (reply->content_length < 0)
	return 0;
    else
	sendlen = reply->content_length + reply->hdr_sz;
    /*
     * Now that we have the expected length, did we send it all?
     */
    if (http->out.offset < sendlen)
	return 0;
    else
	return 1;
}

static int
clientGotNotEnough(clientHttpRequest * http)
{
    squid_off_t cl = httpReplyBodySize(http->request->method, http->entry->mem_obj->reply);
    int hs = http->entry->mem_obj->reply->hdr_sz;
    if (cl < 0)
	return 0;
    if (http->out.offset != cl + hs)
	return 1;
    return 0;
}

/*
 * This function is designed to serve a fairly specific purpose.
 * Occasionally our vBNS-connected caches can talk to each other, but not
 * the rest of the world.  Here we try to detect frequent failures which
 * make the cache unusable (e.g. DNS lookup and connect() failures).  If
 * the failure:success ratio goes above 1.0 then we go into "hit only"
 * mode where we only return UDP_HIT or UDP_MISS_NOFETCH.  Neighbors
 * will only fetch HITs from us if they are using the ICP protocol.  We
 * stay in this mode for 5 minutes.
 * 
 * Duane W., Sept 16, 1996
 */

static void
checkFailureRatio(err_type etype, hier_code hcode)
{
    static double magic_factor = 100.0;
    double n_good;
    double n_bad;
    if (hcode == HIER_NONE)
	return;
    n_good = magic_factor / (1.0 + request_failure_ratio);
    n_bad = magic_factor - n_good;
    switch (etype) {
    case ERR_DNS_FAIL:
    case ERR_CONNECT_FAIL:
    case ERR_READ_ERROR:
	n_bad++;
	break;
    default:
	n_good++;
    }
    request_failure_ratio = n_bad / n_good;
    if (hit_only_mode_until > squid_curtime)
	return;
    if (request_failure_ratio < 1.0)
	return;
    debug(33, 0) ("Failure Ratio at %4.2f\n", request_failure_ratio);
    debug(33, 0) ("Going into hit-only-mode for %d minutes...\n",
	FAILURE_MODE_TIME / 60);
    hit_only_mode_until = squid_curtime + FAILURE_MODE_TIME;
    request_failure_ratio = 0.8;	/* reset to something less than 1.0 */
}

static void
clientHttpConnectionsOpen(void)
{
    http_port_list *s;
    int fd;
    for (s = Config.Sockaddr.http; s; s = s->next) {
	if (MAXHTTPPORTS == NHttpSockets) {
	    debug(1, 1) ("WARNING: You have too many 'http_port' lines.\n");
	    debug(1, 1) ("         The limit is %d\n", MAXHTTPPORTS);
	    continue;
	}
	if ((NHttpSockets == 0) && opt_stdin_overrides_http_port) {
	    fd = 0;
	    if (reconfiguring) {
		/* this one did not get closed, just reuse it */
		HttpSockets[NHttpSockets++] = fd;
		continue;
	    }
	    comm_fdopen(fd,
		SOCK_STREAM,
		no_addr,
		ntohs(s->s.sin_port),
		COMM_NONBLOCKING,
		"HTTP Socket");
	} else {
	    enter_suid();
	    fd = comm_open(SOCK_STREAM,
		IPPROTO_TCP,
		s->s.sin_addr,
		ntohs(s->s.sin_port),
		COMM_NONBLOCKING,
		"HTTP Socket");
	    leave_suid();
	}
	if (fd < 0)
	    continue;
	comm_listen(fd);
	commSetSelect(fd, COMM_SELECT_READ, httpAccept, s, 0);
	/*
	 * We need to set a defer handler here so that we don't
	 * peg the CPU with select() when we hit the FD limit.
	 */
	commSetDefer(fd, httpAcceptDefer, NULL);
	debug(1, 1) ("Accepting %s HTTP connections at %s, port %d, FD %d.\n",
	    s->transparent ? "transparently proxied" :
	    s->accel ? "accelerated" :
	    "proxy",
	    inet_ntoa(s->s.sin_addr),
	    (int) ntohs(s->s.sin_port),
	    fd);
	HttpSockets[NHttpSockets++] = fd;
    }
}

#if USE_SSL
static void
clientHttpsConnectionsOpen(void)
{
    https_port_list *s;
    int fd;
    for (s = Config.Sockaddr.https; s; s = (https_port_list *) s->http.next) {
	if (MAXHTTPPORTS == NHttpSockets) {
	    debug(1, 1) ("WARNING: You have too many 'https_port' lines.\n");
	    debug(1, 1) ("         The limit is %d\n", MAXHTTPPORTS);
	    continue;
	}
	if (!s->sslContext)
	    continue;
	enter_suid();
	fd = comm_open(SOCK_STREAM,
	    IPPROTO_TCP,
	    s->http.s.sin_addr,
	    ntohs(s->http.s.sin_port),
	    COMM_NONBLOCKING,
	    "HTTPS Socket");
	leave_suid();
	if (fd < 0)
	    continue;
	comm_listen(fd);
	commSetSelect(fd, COMM_SELECT_READ, httpsAccept, s, 0);
	commSetDefer(fd, httpAcceptDefer, NULL);
	debug(1, 1) ("Accepting HTTPS connections at %s, port %d, FD %d.\n",
	    inet_ntoa(s->http.s.sin_addr),
	    (int) ntohs(s->http.s.sin_port),
	    fd);
	HttpSockets[NHttpSockets++] = fd;
    }
}

#endif

void
clientOpenListenSockets(void)
{
    clientHttpConnectionsOpen();
#if USE_SSL
    clientHttpsConnectionsOpen();
#endif
    if (NHttpSockets < 1)
	fatal("Cannot open HTTP Port");
}
void
clientHttpConnectionsClose(void)
{
    int i = 0;
    if (opt_stdin_overrides_http_port && reconfiguring)
	i++;			/* skip closing & reopening first port because it is overridden */
    for (; i < NHttpSockets; i++) {
	if (HttpSockets[i] >= 0) {
	    debug(1, 1) ("FD %d Closing HTTP connection\n", HttpSockets[i]);
	    comm_close(HttpSockets[i]);
	    HttpSockets[i] = -1;
	}
    }
    NHttpSockets = 0;
}

static int
varyEvaluateMatch(StoreEntry * entry, request_t * request)
{
    const char *vary = request->vary_headers;
    int has_vary = httpHeaderHas(&entry->mem_obj->reply->header, HDR_VARY);
#if X_ACCELERATOR_VARY
    has_vary |= httpHeaderHas(&entry->mem_obj->reply->header, HDR_X_ACCELERATOR_VARY);
#endif
    if (!has_vary || !entry->mem_obj->vary_headers) {
	if (vary) {
	    /* Oops... something odd is going on here.. */
	    debug(33, 1) ("varyEvaluateMatch: Oops. Not a Vary object on second attempt, '%s' '%s'\n",
		entry->mem_obj->url, vary);
	    safe_free(request->vary_headers);
	    return VARY_CANCEL;
	}
	if (!has_vary) {
	    /* This is not a varying object */
	    return VARY_NONE;
	}
	/* virtual "vary" object found. Calculate the vary key and
	 * continue the search
	 */
	vary = httpMakeVaryMark(request, entry->mem_obj->reply);
	if (vary) {
	    return VARY_OTHER;
	} else {
	    /* Ouch.. we cannot handle this kind of variance */
	    /* XXX This cannot really happen, but just to be complete */
	    return VARY_CANCEL;
	}
    } else {
	if (!vary)
	    vary = httpMakeVaryMark(request, entry->mem_obj->reply);
	if (!vary) {
	    /* Ouch.. we cannot handle this kind of variance */
	    /* XXX This cannot really happen, but just to be complete */
	    return VARY_CANCEL;
	} else if (request->flags.collapsed) {
	    /* This request was merged before we knew the outcome. Don't trust the response */
	    /* restart vary processing from the beginning */
	    return VARY_RESTART;
	} else {
	    return VARY_MATCH;
	}
    }
}

/* This is a handler normally called by comm_close() */
static void
clientPinnedConnectionClosed(int fd, void *data)
{
    ConnStateData *conn = data;
    conn->pinning.fd = -1;
    if (conn->pinning.peer) {
	cbdataUnlock(conn->pinning.peer);
	conn->pinning.peer = NULL;
    }
    safe_free(conn->pinning.host);
    /* NOTE: pinning.pinned should be kept. This combined with fd == -1 at the end of a request indicates that the host
     * connection has gone away */
}

void
clientPinConnection(ConnStateData * conn, int fd, const request_t * request, peer * peer, int auth)
{
    fde *f;
    LOCAL_ARRAY(char, desc, FD_DESC_SZ);
    const char *host = request->host;
    const int port = request->port;
    if (!cbdataValid(conn))
	comm_close(fd);
    if (conn->pinning.fd == fd)
	return;
    else if (conn->pinning.fd != -1)
	comm_close(conn->pinning.fd);
    conn->pinning.fd = fd;
    safe_free(conn->pinning.host);
    conn->pinning.host = xstrdup(host);
    conn->pinning.port = port;
    conn->pinning.pinned = 1;
    if (conn->pinning.peer)
	cbdataUnlock(conn->pinning.peer);
    conn->pinning.peer = peer;
    if (peer)
	cbdataLock(conn->pinning.peer);
    conn->pinning.auth = auth;
    f = &fd_table[conn->fd];
    snprintf(desc, FD_DESC_SZ, "%s pinned connection for %s:%d (%d)",
	(auth || !peer) ? host : peer->name, f->ipaddr, (int) f->remote_port, conn->fd);
    fd_note(fd, desc);
    comm_add_close_handler(fd, clientPinnedConnectionClosed, conn);
}

int
clientGetPinnedInfo(const ConnStateData * conn, const request_t * request, peer ** peer)
{
    int fd = conn->pinning.fd;

    if (fd < 0)
	return -1;

    if (conn->pinning.auth && request && strcasecmp(conn->pinning.host, request->host) != 0) {
      err:
	comm_close(fd);
	return -1;
    }
    if (request && conn->pinning.port != request->port)
	goto err;
    if (conn->pinning.peer && !cbdataValid(conn->pinning.peer))
	goto err;
    *peer = conn->pinning.peer;
    return fd;
}

int
clientGetPinnedConnection(ConnStateData * conn, const request_t * request, const peer * peer, int *auth)
{
    int fd = conn->pinning.fd;

    if (fd < 0)
	return -1;

    if (conn->pinning.auth && request && strcasecmp(conn->pinning.host, request->host) != 0) {
      err:
	comm_close(fd);
	return -1;
    }
    *auth = conn->pinning.auth;
    if (peer != conn->pinning.peer)
	goto err;
    cbdataUnlock(conn->pinning.peer);
    conn->pinning.peer = NULL;
    conn->pinning.fd = -1;
    comm_remove_close_handler(fd, clientPinnedConnectionClosed, conn);
    return fd;
}

#if DELAY_POOLS
void
clientReassignDelaypools(void)
{
    dlink_node *i;
    for (i = ClientActiveRequests.head; i; i = i->next) {
	clientHttpRequest *http = i->data;
	assert(http);
	if (http->sc && http->log_type != LOG_TCP_DENIED && http->log_type != LOG_TAG_NONE)
	    delaySetStoreClient(http->sc, delayClient(http));
    }
}
#endif
