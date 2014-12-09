#include "squid.h"
/*
 * $Id: globals.h,v 1.126 2007/09/24 13:28:48 hno Exp $
 *
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
FILE *debug_log = NULL;
SquidConfig Config;
SquidConfig2 Config2;
char *ConfigFile = NULL;
const char *dns_error_message = NULL;
char tmp_error_buf[ERROR_BUF_SZ];
char *volatile debug_options = NULL;
char ThisCache[SQUIDHOSTNAMELEN << 1];
char ThisCache2[SQUIDHOSTNAMELEN << 1];
char config_input_line[BUFSIZ];
const char *AclMatchedName = NULL;
const char *DefaultConfigFile = DEFAULT_CONFIG_FILE;
const char *cfg_filename = NULL;
const char *const appname = "squid";
const char *const dash_str = "-";
const char *const localhost = "127.0.0.1";
const char *const null_string = "";
const char *const version_string = VERSION;
const char *const full_appname_string = PACKAGE "/" VERSION;
const char *const appname_string = PACKAGE;
char *visible_appname_string;
const char *const w_space = " \t\n\r";
fde *fd_table = NULL;
int Biggest_FD = -1;
int Number_FD = 0;
int Opening_FD = 0;
int HttpSockets[MAXHTTPPORTS];
int NDnsServersAlloc = 0;
int NHttpSockets = 0;
int RESERVED_FD;
int Squid_MaxFD = SQUID_MAXFD;
int config_lineno = 0;
int debugLevels[MAX_DEBUG_SECTIONS];
int do_mallinfo = 0;
int opt_reuseaddr = 1;
int icmp_sock = -1;
int neighbors_do_private_keys = 1;
int opt_catch_signals = 1;
int opt_debug_stderr = -1;
int opt_dns_tests = 1;
int opt_foreground_rebuild = 0;
int opt_forwarded_for = 1;
int opt_reload_hit_only = 0;
int opt_stdin_overrides_http_port = 0;
#if HAVE_SYSLOG
int opt_syslog_enable = 0;
#endif
int opt_udp_hit_obj = 0;
int opt_create_swap_dirs = 0;
int opt_store_doublecheck = 0;
int syslog_enable = 0;
int theInIcpConnection = -1;
int theOutIcpConnection = -1;
int DnsSocket = -1;
#ifdef SQUID_SNMP
int theInSnmpConnection = -1;
int theOutSnmpConnection = -1;
char *snmp_agentinfo;
#endif
int n_disk_objects = 0;
iostats IOStats;
struct _acl_deny_info_list *DenyInfoList = NULL;
struct in_addr any_addr;
struct in_addr local_addr;
struct in_addr no_addr;
struct in_addr theOutICPAddr;
struct in_addr theOutSNMPAddr;
struct timeval current_time;
struct timeval squid_start;
time_t squid_curtime = 0;
int shutting_down = 0;
int reconfiguring = 0;
int store_dirs_rebuilding = 1;
int store_swap_size = 0;
unsigned long store_mem_size = 0;
time_t hit_only_mode_until = 0;
StatCounters statCounter;
double request_failure_ratio = 0.0;
double current_dtime;
int store_hash_buckets = 0;
hash_table *store_table = NULL;
dlink_list ClientActiveRequests;
const String StringNull = { 0, 0, NULL };
const MemBuf MemBufNull = MemBufNULL;
int hot_obj_count = 0;
int _db_level;
const int CacheDigestHashFuncCount = 4;
CacheDigest *store_digest = NULL;
const char *StoreDigestFileName = "store_digest";
const char *StoreDigestMimeStr = "application/cache-digest";
#if USE_CACHE_DIGESTS
const Version CacheDigestVer = { 5, 3 };
#endif
const char *MultipartMsgBoundaryStr = "Unique-Squid-Separator";
icpUdpData *IcpQueueHead = NULL;
#if HTTP_VIOLATIONS
int refresh_nocache_hack = 0;
#endif
request_flags null_request_flags;
int store_open_disk_fd = 0;
authscheme_entry_t *authscheme_list = NULL;
storefs_entry_t *storefs_list = NULL;
storerepl_entry_t *storerepl_list = NULL;
int store_swap_low = 0;
int store_swap_high = 0;
int store_pages_max = 0;
squid_off_t store_maxobjsize = -1;
RemovalPolicy *mem_policy;
hash_table *proxy_auth_username_cache = NULL;
int incoming_sockets_accepted;
#ifdef _SQUID_WIN32_
unsigned int WIN32_Socks_initialized = 0;
unsigned int WIN32_OS_version = 0;
char *WIN32_OS_string = NULL;
char *WIN32_Service_name = NULL;
char *WIN32_Command_Line = NULL;
char *WIN32_Service_Command_Line = NULL;
unsigned int WIN32_run_mode = _WIN_SQUID_RUN_MODE_INTERACTIVE;
#endif
const char *external_acl_message = NULL;
#if HAVE_SBRK
void *sbrk_start = 0;
#endif
int opt_send_signal = -1;
int opt_no_daemon = 0;
#if LINUX_TPROXY
int need_linux_tproxy = 0;
#endif
int opt_parse_cfg_only = 0;
int n_coss_dirs = 0;
#ifdef LOG_LOCAL4
int syslog_facility = LOG_LOCAL4;
#endif
