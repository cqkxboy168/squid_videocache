/*
 * $Id: squid_types.h,v 1.8.6.1 2010/02/12 20:22:18 hno Exp $
 *
 * * * * * * * * Legal stuff * * * * * * *
 *
 * (C) 2000 Francesco Chemolli <kinkie@kame.usr.dsi.unimi.it>
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
 * * * * * * * * Declaration of intents * * * * * * *
 *
 * Here are defined several known-width types, obtained via autoconf
 * from system locations or various attempts. This is just a convenience
 * header to include which takes care of proper preprocessor stuff
 */

#ifndef SQUID_TYPES_H
#define SQUID_TYPES_H

#ifndef AUTOCONF_H
#define AUTOCONF_H 1
#include "autoconf.h"
#endif

/* This should be in synch with what we have in acinclude.m4 */
#if STDC_HEADERS
#include <stdlib.h>
#include <stddef.h>
#endif
#if HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if HAVE_SYS_BITYPES_H
#include <sys/bitypes.h>
#endif

#if SIZEOF_INT64_T > SIZEOF_LONG && HAVE_STRTOLL
typedef int64_t squid_off_t;
#define SIZEOF_SQUID_OFF_T SIZEOF_INT64_T
#define PRINTF_OFF_T PRId64
#define strto_off_t (int64_t)strtoll
#else
typedef long squid_off_t;
#define SIZEOF_SQUID_OFF_T SIZEOF_LONG
#define PRINTF_OFF_T "ld"
#define strto_off_t strtol
#endif

/* 
 * ISO C99 Standard printf() macros for 64 bit integers
 * On some 64 bit platform, HP Tru64 is one, for printf must be used
 * "%lx" instead of "%llx" 
 */
#ifndef PRId64
#ifdef _SQUID_MSWIN_		/* Windows native port using MSVCRT */
#define PRId64 "I64d"
#elif SIZEOF_INT64_T > SIZEOF_LONG
#define PRId64 "lld"
#else
#define PRId64 "ld"
#endif
#endif

#ifndef PRIu64
#ifdef _SQUID_MSWIN_		/* Windows native port using MSVCRT */
#define PRIu64 "I64u"
#elif SIZEOF_INT64_T > SIZEOF_LONG
#define PRIu64 "llu"
#else
#define PRIu64 "lu"
#endif
#endif

#endif /* SQUID_TYPES_H */
