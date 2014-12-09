
/*
 * $Id: pinger.c,v 1.50.6.4 2008/06/04 20:32:48 hno Exp $
 *
 * DEBUG: section 42    ICMP Pinger program
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

#if USE_ICMP

/* Native Windows port doesn't have netinet support, so we emulate it.
 * At this time, Cygwin lacks icmp support in its include files, so we need
 * to use the native Windows port definitions.
 */

#if !defined(_SQUID_WIN32_)

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#define PINGER_TIMEOUT 10

static int socket_from_squid = 0;
static int socket_to_squid = 1;

#else /* _SQUID_WIN32_ */

#ifdef _SQUID_MSWIN_

#if HAVE_WINSOCK2_H
#include <winsock2.h>
#endif
#include <process.h>

#define PINGER_TIMEOUT 5

static SOCKET socket_to_squid = -1;
#define socket_from_squid socket_to_squid

#else /* _SQUID_CYGWIN_ */

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#define PINGER_TIMEOUT 10

static int socket_from_squid = 0;
static int socket_to_squid = 1;

#endif

#define ICMP_ECHO 8
#define ICMP_ECHOREPLY 0

typedef struct iphdr {
    u_int8_t ip_vhl:4;		/* Length of the header in dwords */
    u_int8_t version:4;		/* Version of IP                  */
    u_int8_t tos;		/* Type of service                */
    u_int16_t total_len;	/* Length of the packet in dwords */
    u_int16_t ident;		/* unique identifier              */
    u_int16_t flags;		/* Flags                          */
    u_int8_t ip_ttl;		/* Time to live                   */
    u_int8_t proto;		/* Protocol number (TCP, UDP etc) */
    u_int16_t checksum;		/* IP checksum                    */
    u_int32_t source_ip;
    u_int32_t dest_ip;
} iphdr;

/* ICMP header */
typedef struct icmphdr {
    u_int8_t icmp_type;		/* ICMP packet type                 */
    u_int8_t icmp_code;		/* Type sub code                    */
    u_int16_t icmp_cksum;
    u_int16_t icmp_id;
    u_int16_t icmp_seq;
    u_int32_t timestamp;	/* not part of ICMP, but we need it */
} icmphdr;

#endif /* _SQUID_MSWIN_ */

#ifndef _SQUID_LINUX_
#ifndef _SQUID_WIN32_
#define icmphdr icmp
#define iphdr ip
#endif
#endif

#if defined (_SQUID_LINUX_)
#ifdef icmp_id
#undef icmp_id
#endif
#ifdef icmp_seq
#undef icmp_seq
#endif
#define icmp_type type
#define icmp_code code
#define icmp_cksum checksum
#define icmp_id un.echo.id
#define icmp_seq un.echo.sequence
#define ip_hl ihl
#define ip_v version
#define ip_tos tos
#define ip_len tot_len
#define ip_id id
#define ip_off frag_off
#define ip_ttl ttl
#define ip_p protocol
#define ip_sum check
#define ip_src saddr
#define ip_dst daddr
#endif

#if ALLOW_SOURCE_PING
#define MAX_PKT_SZ 8192
#define MAX_PAYLOAD (MAX_PKT_SZ - sizeof(struct icmphdr) - sizeof (char) - sizeof(struct timeval) - 1)
#else
#define MAX_PAYLOAD SQUIDHOSTNAMELEN
#define MAX_PKT_SZ (MAX_PAYLOAD + sizeof(struct timeval) + sizeof (char) + sizeof(struct icmphdr) + 1)
#endif

typedef struct {
    struct timeval tv;
    unsigned char opcode;
    char payload[MAX_PAYLOAD];
} icmpEchoData;

int icmp_ident = -1;
int icmp_pkts_sent = 0;

static const char *icmpPktStr[] =
{
    "Echo Reply",
    "ICMP 1",
    "ICMP 2",
    "Destination Unreachable",
    "Source Quench",
    "Redirect",
    "ICMP 6",
    "ICMP 7",
    "Echo",
    "ICMP 9",
    "ICMP 10",
    "Time Exceeded",
    "Parameter Problem",
    "Timestamp",
    "Timestamp Reply",
    "Info Request",
    "Info Reply",
    "Out of Range Type"
};

static int in_cksum(unsigned short *ptr, int size);
static void pingerRecv(void);
static void pingerLog(struct icmphdr *, struct in_addr, int, int);
static int ipHops(int ttl);
static void pingerSendtoSquid(pingerReplyData * preply);
static void pingerOpen(void);
static void pingerClose(void);

#ifdef _SQUID_MSWIN_
void
Win32SockCleanup(void)
{
    WSACleanup();
    return;
}
#endif /* ifdef _SQUID_MSWIN_ */

void
pingerOpen(void)
{
    struct protoent *proto = NULL;
#ifdef _SQUID_MSWIN_
    WSADATA wsaData;
    WSAPROTOCOL_INFO wpi;
    char buf[sizeof(wpi)];
    int x;
    struct sockaddr_in PS;

    WSAStartup(2, &wsaData);
    atexit(Win32SockCleanup);

    getCurrentTime();
    _db_init(NULL, "ALL,1");
    setmode(0, O_BINARY);
    setmode(1, O_BINARY);
    x = read(0, buf, sizeof(wpi));
    if (x < sizeof(wpi)) {
	getCurrentTime();
	debug(42, 0) ("pingerOpen: read: FD 0: %s\n", xstrerror());
	write(1, "ERR\n", 4);
	exit(1);
    }
    xmemcpy(&wpi, buf, sizeof(wpi));

    write(1, "OK\n", 3);
    x = read(0, buf, sizeof(PS));
    if (x < sizeof(PS)) {
	getCurrentTime();
	debug(42, 0) ("pingerOpen: read: FD 0: %s\n", xstrerror());
	write(1, "ERR\n", 4);
	exit(1);
    }
    xmemcpy(&PS, buf, sizeof(PS));
#endif
    if ((proto = getprotobyname("icmp")) == 0) {
	debug(42, 0) ("pingerOpen: unknown protocol: icmp\n");
	exit(1);
    }
    icmp_sock = socket(PF_INET, SOCK_RAW, proto->p_proto);
    if (icmp_sock < 0) {
	debug(42, 0) ("pingerOpen: icmp_sock: %s\n", xstrerror());
	exit(1);
    }
    icmp_ident = getpid() & 0xffff;
    debug(42, 0) ("pingerOpen: ICMP socket opened\n");
#ifdef _SQUID_MSWIN_
    socket_to_squid =
	WSASocket(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO,
	&wpi, 0, 0);
    if (socket_to_squid == -1) {
	getCurrentTime();
	debug(42, 0) ("pingerOpen: WSASocket: %s\n", xstrerror());
	write(1, "ERR\n", 4);
	exit(1);
    }
    x = connect(socket_to_squid, (struct sockaddr *) &PS, sizeof(PS));
    if (SOCKET_ERROR == x) {
	getCurrentTime();
	debug(42, 0) ("pingerOpen: connect: %s\n", xstrerror());
	write(1, "ERR\n", 4);
	exit(1);
    }
    write(1, "OK\n", 3);
    memset(buf, 0, sizeof(buf));
    x = recv(socket_to_squid, buf, sizeof(buf), 0);
    if (x < 3) {
	debug(42, 0) ("pingerOpen: recv: %s\n", xstrerror());
	exit(1);
    }
    x = send(socket_to_squid, buf, strlen(buf), 0);
    if (x < 3 || strncmp("OK\n", buf, 3)) {
	debug(42, 0) ("pingerOpen: recv: %s\n", xstrerror());
	exit(1);
    }
    getCurrentTime();
    debug(42, 0) ("pingerOpen: Squid socket opened\n");
#endif
}

void
pingerClose(void)
{
    close(icmp_sock);
#ifdef _SQUID_MSWIN_
    shutdown(socket_to_squid, SD_BOTH);
    close(socket_to_squid);
    socket_to_squid = -1;
#endif
    icmp_sock = -1;
    icmp_ident = 0;
}

static void
pingerSendEcho(struct in_addr to, int opcode, char *payload, int len)
{
    LOCAL_ARRAY(char, pkt, MAX_PKT_SZ);
    struct icmphdr *icmp = NULL;
    icmpEchoData *echo;
    int icmp_pktsize = sizeof(struct icmphdr);
    struct sockaddr_in S;
    memset(pkt, '\0', MAX_PKT_SZ);
    icmp = (struct icmphdr *) (void *) pkt;

    /*
     * cevans - beware signed/unsigned issues in untrusted data from
     * the network!!
     */
    if (len < 0) {
	len = 0;
    }
    icmp->icmp_type = ICMP_ECHO;
    icmp->icmp_code = 0;
    icmp->icmp_cksum = 0;
    icmp->icmp_id = icmp_ident;
    icmp->icmp_seq = (u_short) icmp_pkts_sent++;
    echo = (icmpEchoData *) (icmp + 1);
    echo->opcode = (unsigned char) opcode;
    memcpy(&echo->tv, &current_time, sizeof(current_time));
    icmp_pktsize += sizeof(struct timeval) + sizeof(char);
    if (payload) {
	if (len > MAX_PAYLOAD)
	    len = MAX_PAYLOAD;
	xmemcpy(echo->payload, payload, len);
	icmp_pktsize += len;
    }
    icmp->icmp_cksum = in_cksum((u_short *) icmp, icmp_pktsize);
    S.sin_family = AF_INET;
    /*
     * cevans: alert: trusting to-host, was supplied in network packet
     */
    S.sin_addr = to;
    S.sin_port = 0;
    assert(icmp_pktsize <= MAX_PKT_SZ);
    sendto(icmp_sock,
	pkt,
	icmp_pktsize,
	0,
	(struct sockaddr *) &S,
	sizeof(struct sockaddr_in));
    pingerLog(icmp, to, 0, 0);
}

static void
pingerRecv(void)
{
    int n;
    socklen_t fromlen;
    struct sockaddr_in from;
    int iphdrlen = 20;
    struct iphdr *ip = NULL;
    struct icmphdr *icmp = NULL;
    static char *pkt = NULL;
    struct timeval now;
    icmpEchoData *echo;
    static pingerReplyData preply;
    struct timeval tv;

    if (pkt == NULL)
	pkt = xmalloc(MAX_PKT_SZ);
    fromlen = sizeof(from);
    n = recvfrom(icmp_sock,
	pkt,
	MAX_PKT_SZ,
	0,
	(struct sockaddr *) &from,
	&fromlen);
#if GETTIMEOFDAY_NO_TZP
    gettimeofday(&now);
#else
    gettimeofday(&now, NULL);
#endif
    debug(42, 9) ("pingerRecv: %d bytes from %s\n", n, inet_ntoa(from.sin_addr));
    ip = (struct iphdr *) (void *) pkt;
#if HAVE_IP_HL
    iphdrlen = ip->ip_hl << 2;
#else /* HAVE_IP_HL */
#if WORDS_BIGENDIAN
    iphdrlen = (ip->ip_vhl >> 4) << 2;
#else
    iphdrlen = (ip->ip_vhl & 0xF) << 2;
#endif
#endif /* HAVE_IP_HL */
    icmp = (struct icmphdr *) (void *) (pkt + iphdrlen);
    if (icmp->icmp_type != ICMP_ECHOREPLY)
	return;
    if (icmp->icmp_id != icmp_ident)
	return;
    echo = (icmpEchoData *) (void *) (icmp + 1);
    preply.from = from.sin_addr;
    preply.opcode = echo->opcode;
    preply.hops = ipHops(ip->ip_ttl);
    memcpy(&tv, &echo->tv, sizeof(tv));
    preply.rtt = tvSubMsec(tv, now);
    preply.psize = n - iphdrlen - (sizeof(icmpEchoData) - MAX_PKT_SZ);
    pingerSendtoSquid(&preply);
    pingerLog(icmp, from.sin_addr, preply.rtt, preply.hops);
}


static int
in_cksum(unsigned short *ptr, int size)
{
    long sum;
    unsigned short oddbyte;
    unsigned short answer;
    sum = 0;
    while (size > 1) {
	sum += *ptr++;
	size -= 2;
    }
    if (size == 1) {
	oddbyte = 0;
	*((unsigned char *) &oddbyte) = *(unsigned char *) ptr;
	sum += oddbyte;
    }
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    answer = (unsigned short) ~sum;
    return (answer);
}

static void
pingerLog(struct icmphdr *icmp, struct in_addr addr, int rtt, int hops)
{
    debug(42, 2) ("pingerLog: %9d.%06d %-16s %d %-15.15s %dms %d hops\n",
	(int) current_time.tv_sec,
	(int) current_time.tv_usec,
	inet_ntoa(addr),
	(int) icmp->icmp_type,
	icmpPktStr[icmp->icmp_type],
	rtt,
	hops);
}

static int
ipHops(int ttl)
{
    if (ttl < 33)
	return 33 - ttl;
    if (ttl < 63)
	return 63 - ttl;	/* 62 = (64+60)/2 */
    if (ttl < 65)
	return 65 - ttl;	/* 62 = (64+60)/2 */
    if (ttl < 129)
	return 129 - ttl;
    if (ttl < 193)
	return 193 - ttl;
    return 256 - ttl;
}

static int
pingerReadRequest(void)
{
    static pingerEchoData pecho;
    int n;
    int guess_size;
    memset(&pecho, '\0', sizeof(pecho));
    n = recv(socket_from_squid, (char *) &pecho, sizeof(pecho), 0);
    if (n < 0)
	return n;
    if (0 == n) {
	/* EOF indicator */
	fprintf(stderr, "EOF encountered\n");
	errno = 0;
	return -1;
    }
    guess_size = n - (sizeof(pingerEchoData) - PINGER_PAYLOAD_SZ);
    if (guess_size != pecho.psize) {
	fprintf(stderr, "size mismatch, guess=%d psize=%d\n",
	    guess_size, pecho.psize);
	/* don't process this message, but keep running */
	return 0;
    }
    pingerSendEcho(pecho.to,
	pecho.opcode,
	pecho.payload,
	pecho.psize);
    return n;
}

static void
pingerSendtoSquid(pingerReplyData * preply)
{
    int len = sizeof(pingerReplyData) - MAX_PKT_SZ + preply->psize;
    if (send(socket_to_squid, (char *) preply, len, 0) < 0) {
	debug(42, 0) ("pingerSendtoSquid: send: %s\n", xstrerror());
	pingerClose();
	exit(1);
    }
}

time_t
getCurrentTime(void)
{
#if GETTIMEOFDAY_NO_TZP
    gettimeofday(&current_time);
#else
    gettimeofday(&current_time, NULL);
#endif
    return squid_curtime = current_time.tv_sec;
}


int
main(int argc, char *argv[])
{
    fd_set R;
    int x;
    struct timeval tv;
    const char *debug_args = "ALL,1";
    char *t;
    time_t last_check_time = 0;

/*
 * cevans - do this first. It grabs a raw socket. After this we can
 * drop privs
 */
    pingerOpen();
    setgid(getgid());
    setuid(getuid());

    if ((t = getenv("SQUID_DEBUG")))
	debug_args = xstrdup(t);
    getCurrentTime();
    _db_init(NULL, debug_args);

    for (;;) {
	tv.tv_sec = PINGER_TIMEOUT;
	tv.tv_usec = 0;
	FD_ZERO(&R);
	FD_SET(socket_from_squid, &R);
	FD_SET(icmp_sock, &R);
	x = select(icmp_sock + 1, &R, NULL, NULL, &tv);
	getCurrentTime();
	if (x < 0) {
	    pingerClose();
	    exit(1);
	}
	if (FD_ISSET(socket_from_squid, &R))
	    if (pingerReadRequest() < 0) {
		debug(42, 0) ("Pinger exiting.\n");
		pingerClose();
		exit(1);
	    }
	if (FD_ISSET(icmp_sock, &R))
	    pingerRecv();
	if (PINGER_TIMEOUT + last_check_time < squid_curtime) {
	    debug(42, 5) ("pinger: timeout occured\n");
	    if (send(socket_to_squid, (char *) &tv, 0, 0) < 0) {
		debug(42, 5) ("Pinger: send socket_to_squid failed\n");
		pingerClose();
		exit(1);
	    } else {
		debug(42, 5) ("Pinger: send socket_to_squid OK\n");
	    }
	    last_check_time = squid_curtime;
	}
    }
    /* NOTREACHED */
}

#else
#include <stdio.h>
int
main(int argc, char *argv[])
{
    fprintf(stderr, "%s: ICMP support not compiled in.\n", argv[0]);
    return 1;
}
#endif /* USE_ICMP */
