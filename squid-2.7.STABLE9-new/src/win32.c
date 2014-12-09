
/*
 * $Id: win32.c,v 1.18.2.8 2009/06/25 22:56:47 hno Exp $
 *
 * Windows support
 * AUTHOR: Guido Serassio <serassio@squid-cache.org>
 * inspired by previous work by Romeo Anghelache & Eric Stern.
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

#define WIN32_C

#include "squid.h"

/* This code compiles only Cygwin & native Windows Port */
#ifdef _SQUID_WIN32_
#include <windows.h>
#ifdef _SQUID_MSWIN_
#if HAVE_WIN32_PSAPI
#include <psapi.h>
#endif
#ifndef _MSWSOCK_
#include <mswsock.h>
#endif
#include <process.h>
#endif

static unsigned int GetOSVersion();
void WIN32_svcstatusupdate(DWORD, DWORD);
void WINAPI WIN32_svcHandler(DWORD);
#if USE_WIN32_SERVICE
static int WIN32_StoreKey(const char *, DWORD, unsigned char *, int);
static int WIN32_create_key(void);
static void WIN32_build_argv(char *);
#endif /* USE_WIN32_SERVICE */
void WINAPI SquidWinSvcMain(DWORD, char **);

#if defined(_SQUID_MSWIN_)
static int Win32SockInit(void);
static void Win32SockCleanup(void);
extern LPCRITICAL_SECTION dbg_mutex;
void WIN32_ExceptionHandlerCleanup(void);
static LPTOP_LEVEL_EXCEPTION_FILTER Win32_Old_ExceptionHandler = NULL;
static HANDLE NotifyAddrChange_thread = INVALID_HANDLE_VALUE;
#endif /* _SQUID_MSWIN_ */

#if USE_WIN32_SERVICE
static SERVICE_STATUS svcStatus;
static SERVICE_STATUS_HANDLE svcHandle;

static int WIN32_argc;
static char **WIN32_argv;
static char *WIN32_module_name;
#endif /* USE_WIN32_SERVICE */
static int Squid_Aborting = 0;

#define VENDOR   "GNU"
#define SOFTWARENAME "Squid"
#define WIN32_VERSION  "2.6"
#define COMMANDLINE "CommandLine"
#define CONFIGFILE  "ConfigFile"
#undef ChangeServiceConfig2
typedef BOOL(WINAPI * PFChangeServiceConfig2) (SC_HANDLE, DWORD, LPVOID);
#ifdef UNICODE
#define CHANGESERVICECONFIG2 "ChangeServiceConfig2W"
#else
#define CHANGESERVICECONFIG2 "ChangeServiceConfig2A"
#endif
#undef NotifyAddrChange
typedef DWORD(WINAPI * PFNotifyAddrChange) (OUT PHANDLE, IN LPOVERLAPPED);
#define NOTIFYADDRCHANGE "NotifyAddrChange"
#if USE_WIN32_SERVICE
static SC_ACTION Squid_SCAction[] =
{
    {SC_ACTION_RESTART, 60000}};
static SERVICE_DESCRIPTION Squid_ServiceDescription =
{SOFTWARENAME " " VERSION " " PACKAGE_NAME};
static SERVICE_FAILURE_ACTIONS Squid_ServiceFailureActions =
{INFINITE, NULL, NULL, 1, Squid_SCAction};

static char REGKEY[256] = "SOFTWARE\\" VENDOR "\\" SOFTWARENAME "\\" WIN32_VERSION "\\";
static char *keys[] =
{
    "SOFTWARE",			/* key[0] */
    VENDOR,			/* key[1] */
    SOFTWARENAME,		/* key[2] */
    WIN32_VERSION,		/* key[3] */
    NULL,			/* key[4] */
    NULL			/* key[5] */
};
#endif /* USE_WIN32_SERVICE */

/* ====================================================================== */
/* LOCAL FUNCTIONS */
/* ====================================================================== */

#if USE_WIN32_SERVICE
static int
WIN32_create_key(void)
{
    int index;
    HKEY hKey;
    HKEY hKeyNext;
    int retval;
    LONG rv;

    hKey = HKEY_LOCAL_MACHINE;
    index = 0;
    retval = 0;

    /* Walk the tree, creating at each stage if necessary */
    while (keys[index]) {
	unsigned long result;
	rv = RegCreateKeyEx(hKey, keys[index],	/* subkey */
	    0,			/* reserved */
	    NULL,		/* class */
	    REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKeyNext, &result);
	if (rv != ERROR_SUCCESS) {
	    fprintf(stderr, "RegCreateKeyEx(%s),%d\n", keys[index], (int) rv);
	    retval = -4;
	}
	/* Close the old key */
	rv = RegCloseKey(hKey);
	if (rv != ERROR_SUCCESS) {
	    fprintf(stderr, "RegCloseKey %d\n", (int) rv);
	    if (retval == 0) {
		/* Keep error status from RegCreateKeyEx, if any */
		retval = -4;
	    }
	}
	if (retval) {
	    break;
	}
	hKey = hKeyNext;
	index++;
    }
    if (keys[index] == NULL) {
	/* Close the final key we opened, if we walked the entire
	 * tree
	 */
	rv = RegCloseKey(hKey);
	if (rv != ERROR_SUCCESS) {
	    fprintf(stderr, "RegCloseKey %d\n", (int) rv);
	    if (retval == 0) {
		/* Keep error status from RegCreateKeyEx, if any */
		retval = -4;
	    }
	}
    }
    return retval;
}

static int
WIN32_StoreKey(const char *key, DWORD type, unsigned char *value,
    int value_size)
{
    LONG rv;
    HKEY hKey;
    int retval;

    rv = RegOpenKeyEx(HKEY_LOCAL_MACHINE, REGKEY, 0, KEY_WRITE, &hKey);
    if (rv == ERROR_FILE_NOT_FOUND) {
	/* Key could not be opened -- try to create it 
	 */
	if (WIN32_create_key() < 0) {
	    /* Creation failed (error already reported) */
	    return -4;
	}
	/* Now it has been created we should be able to open it
	 */
	rv = RegOpenKeyEx(HKEY_LOCAL_MACHINE, REGKEY, 0, KEY_WRITE, &hKey);
	if (rv == ERROR_FILE_NOT_FOUND) {
	    fprintf(stderr, "Registry does not contain key %s after creation\n",
		REGKEY);
	    return -1;
	}
    }
    if (rv != ERROR_SUCCESS) {
	fprintf(stderr, "RegOpenKeyEx HKLM\\%s, %d\n", REGKEY, (int) rv);
	return -4;
    }
    /* Now set the value and data */
    rv = RegSetValueEx(hKey, key,	/* value key name */
	0,			/* reserved */
	type,			/* type */
	value,			/* value data */
	(DWORD) value_size);	/* for size of "value" */
    retval = 0;			/* Return value */
    if (rv != ERROR_SUCCESS) {
	fprintf(stderr, "RegQueryValueEx(key %s),%d\n", key, (int) rv);
	retval = -4;
    } else {
	fprintf(stderr, "Registry stored HKLM\\%s\\%s value %s\n",
	    REGKEY,
	    key,
	    type == REG_SZ ? value : (unsigned char *) "(not displayable)");
    }
    /* Make sure we close the key even if there was an error storing
     * the data
     */
    rv = RegCloseKey(hKey);
    if (rv != ERROR_SUCCESS) {
	fprintf(stderr, "RegCloseKey HKLM\\%s, %d\n", REGKEY, (int) rv);
	if (retval == 0) {
	    /* Keep error status from RegQueryValueEx, if any */
	    retval = -4;
	}
    }
    return retval;
}
#endif /* USE_WIN32_SERVICE */

static unsigned int
GetOSVersion()
{
    OSVERSIONINFOEX osvi;
    BOOL bOsVersionInfoEx;

    safe_free(WIN32_OS_string);
    memset(&osvi, '\0', sizeof(OSVERSIONINFOEX));
    /* Try calling GetVersionEx using the OSVERSIONINFOEX structure.
     * If that fails, try using the OSVERSIONINFO structure.
     */

    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);

    if (!(bOsVersionInfoEx = GetVersionEx((OSVERSIONINFO *) & osvi))) {
	osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	if (!GetVersionEx((OSVERSIONINFO *) & osvi))
	    goto GetVerError;
    }
    switch (osvi.dwPlatformId) {
    case VER_PLATFORM_WIN32_NT:
	if (osvi.dwMajorVersion <= 4) {
	    WIN32_OS_string = xstrdup("Windows NT");
	    return _WIN_OS_WINNT;
	}
	if ((osvi.dwMajorVersion == 5) && (osvi.dwMinorVersion == 0)) {
	    WIN32_OS_string = xstrdup("Windows 2000");
	    return _WIN_OS_WIN2K;
	}
	if ((osvi.dwMajorVersion == 5) && (osvi.dwMinorVersion == 1)) {
	    WIN32_OS_string = xstrdup("Windows XP");
	    return _WIN_OS_WINXP;
	}
	if ((osvi.dwMajorVersion == 5) && (osvi.dwMinorVersion == 2)) {
	    WIN32_OS_string = xstrdup("Windows Server 2003");
	    return _WIN_OS_WINNET;
	}
	if ((osvi.dwMajorVersion == 6) && (osvi.dwMinorVersion == 0)) {
	    if (osvi.wProductType == VER_NT_WORKSTATION)
		WIN32_OS_string = xstrdup("Windows Vista");
	    else
		WIN32_OS_string = xstrdup("Windows Server 2008");
	    return _WIN_OS_WINLON;
	}
	if ((osvi.dwMajorVersion == 6) && (osvi.dwMinorVersion == 1)) {
	    if (osvi.wProductType == VER_NT_WORKSTATION)
		WIN32_OS_string = xstrdup("Windows 7");
	    else
		WIN32_OS_string = xstrdup("Windows Server 2008 R2");
	    return _WIN_OS_WIN7;
	}
	if (((osvi.dwMajorVersion > 6)) || ((osvi.dwMajorVersion == 6) && (osvi.dwMinorVersion > 1))) {
	    if (osvi.wProductType == VER_NT_WORKSTATION)
		WIN32_OS_string = xstrdup("Unknown Windows version, assuming Windows 7 capabilities");
	    else
		WIN32_OS_string = xstrdup("Unknown Windows version, assuming Windows Server 2008 R2 capabilities");
	    return _WIN_OS_WIN7;
	}
	break;
    case VER_PLATFORM_WIN32_WINDOWS:
	if ((osvi.dwMajorVersion == 4) && (osvi.dwMinorVersion == 0)) {
	    WIN32_OS_string = xstrdup("Windows 95");
	    return _WIN_OS_WIN95;
	}
	if ((osvi.dwMajorVersion == 4) && (osvi.dwMinorVersion == 10)) {
	    WIN32_OS_string = xstrdup("Windows 98");
	    return _WIN_OS_WIN98;
	}
	if ((osvi.dwMajorVersion == 4) && (osvi.dwMinorVersion == 90)) {
	    WIN32_OS_string = xstrdup("Windows Me");
	    return _WIN_OS_WINME;
	}
	break;
    case VER_PLATFORM_WIN32s:
	WIN32_OS_string = xstrdup("Windows 3.1 with WIN32S");
	return _WIN_OS_WIN32S;
	break;
    default:
	break;
    }
  GetVerError:
    WIN32_OS_string = xstrdup("Unknown Windows system");
    return _WIN_OS_UNKNOWN;
}

#if USE_WIN32_SERVICE
/* Build argv, argc from string passed from Windows.  */
static void
WIN32_build_argv(char *cmd)
{
    int argvlen = 0;
    char *word;

    WIN32_argc = 1;
    WIN32_argv = (char **) xmalloc((WIN32_argc + 1) * sizeof(char *));
    WIN32_argv[0] = xstrdup(WIN32_module_name);
/* Scan command line until there is nothing left. */
    while (*cmd) {
	/* Ignore spaces */
	if (xisspace(*cmd)) {
	    cmd++;
	    continue;
	}
	/* Found the beginning of an argument. */
	word = cmd;
	while (*cmd) {
	    cmd++;		/* Skip over this character */
	    if (xisspace(*cmd))	/* End of argument if space */
		break;
	}
	if (*cmd)
	    *cmd++ = '\0';	/* Terminate `word' */
	/* See if we need to allocate more space for argv */
	if (WIN32_argc >= argvlen) {
	    argvlen = WIN32_argc + 1;
	    WIN32_argv = (char **) xrealloc(WIN32_argv, (1 + argvlen) * sizeof(char *));
	}
	/* Add word to argv file. */
	WIN32_argv[WIN32_argc++] = word;
    }
    WIN32_argv[WIN32_argc] = NULL;
}

#endif

/* ====================================================================== */
/* PUBLIC FUNCTIONS */
/* ====================================================================== */

void
WIN32_Abort(int sig)
{
#if USE_WIN32_SERVICE
    svcStatus.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
    svcStatus.dwServiceSpecificExitCode = 1;
#endif
    Squid_Aborting = 1;
    WIN32_Exit();
}

#ifdef _SQUID_MSWIN_
void
WIN32_IpAddrChangeMonitorExit()
{
    DWORD status = ERROR_SUCCESS;

    if (NotifyAddrChange_thread != INVALID_HANDLE_VALUE) {
	TerminateThread(NotifyAddrChange_thread, status);
	CloseHandle(NotifyAddrChange_thread);
    }
}
#endif

void
WIN32_Exit()
{
#ifdef _SQUID_MSWIN_
    Win32SockCleanup();
#endif
#if USE_WIN32_SERVICE
    if (WIN32_run_mode == _WIN_SQUID_RUN_MODE_SERVICE) {
	if (!Squid_Aborting) {
	    svcStatus.dwCurrentState = SERVICE_STOPPED;
	    SetServiceStatus(svcHandle, &svcStatus);
	}
    }
#endif
#ifdef _SQUID_MSWIN_
    if (dbg_mutex)
	DeleteCriticalSection(dbg_mutex);
    WIN32_ExceptionHandlerCleanup();
    WIN32_IpAddrChangeMonitorExit();
#endif
    _exit(0);
}

#ifdef _SQUID_MSWIN_
static DWORD WINAPI
WIN32_IpAddrChangeMonitor(LPVOID lpParam)
{
    DWORD Result;
    HMODULE IPHLPAPIHandle;
    PFNotifyAddrChange NotifyAddrChange;

    if ((IPHLPAPIHandle = GetModuleHandle("IPHLPAPI")) == NULL)
	IPHLPAPIHandle = LoadLibrary("IPHLPAPI");
    NotifyAddrChange = (PFNotifyAddrChange) GetProcAddress(IPHLPAPIHandle, NOTIFYADDRCHANGE);

    while (1) {
	Result = NotifyAddrChange(NULL, NULL);
	if (Result != NO_ERROR) {
	    debug(1, 1) ("NotifyAddrChange error %ld\n", Result);
	    return 1;
	}
	debug(1, 1) ("Notification of IP address change received, requesting Squid reconfiguration ...\n");
	reconfigure(SIGHUP);
    }
    return 0;
}

DWORD
WIN32_IpAddrChangeMonitorInit()
{
    DWORD status = ERROR_SUCCESS;
    DWORD threadID = 0, ThrdParam = 0;

    if ((WIN32_run_mode == _WIN_SQUID_RUN_MODE_SERVICE) && (Config.onoff.WIN32_IpAddrChangeMonitor)) {
	NotifyAddrChange_thread = CreateThread(NULL, 0, WIN32_IpAddrChangeMonitor,
	    &ThrdParam, 0, &threadID);
	if (NotifyAddrChange_thread == NULL) {
	    status = GetLastError();
	    NotifyAddrChange_thread = INVALID_HANDLE_VALUE;
	    debug(1, 1) ("Failed to start IP monitor thread.\n");
	} else
	    debug(1, 2) ("Starting IP monitor thread [%li] ...\n", threadID);
    }
    return status;
}
#endif

int
WIN32_Subsystem_Init(int *argc, char ***argv)
{
    WIN32_OS_version = GetOSVersion();
    if ((WIN32_OS_version == _WIN_OS_UNKNOWN) || (WIN32_OS_version == _WIN_OS_WIN32S))
	return 1;
    if (atexit(WIN32_Exit) != 0)
	return 1;
#if USE_WIN32_SERVICE
    if (WIN32_run_mode == _WIN_SQUID_RUN_MODE_SERVICE) {
	char path[512];
	HKEY hndKey;
	if (signal(SIGABRT, WIN32_Abort) == SIG_ERR)
	    return 1;
	/* Register the service Handler function */
	svcHandle = RegisterServiceCtrlHandler(WIN32_Service_name, WIN32_svcHandler);
	if (svcHandle == 0)
	    return 1;
	/* Set Process work dir to directory cointaining squid.exe */
	GetModuleFileName(NULL, path, 512);
	WIN32_module_name = xstrdup(path);
	path[strlen(path) - 10] = '\0';
	if (SetCurrentDirectory(path) == 0)
	    return 1;
	safe_free(ConfigFile);
	/* get config file from Windows Registry */
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, REGKEY, 0, KEY_QUERY_VALUE, &hndKey) == ERROR_SUCCESS) {
	    DWORD Type = 0;
	    DWORD Size = 0;
	    LONG Result;
	    Result = RegQueryValueEx(hndKey, CONFIGFILE, NULL, &Type, NULL, &Size);
	    if (Result == ERROR_SUCCESS && Size) {
		ConfigFile = xmalloc(Size);
		RegQueryValueEx(hndKey, CONFIGFILE, NULL, &Type, ConfigFile, &Size);
	    } else
		ConfigFile = xstrdup(DefaultConfigFile);
	    Size = 0;
	    Type = 0;
	    Result = RegQueryValueEx(hndKey, COMMANDLINE, NULL, &Type, NULL, &Size);
	    if (Result == ERROR_SUCCESS && Size) {
		WIN32_Service_Command_Line = xmalloc(Size);
		RegQueryValueEx(hndKey, COMMANDLINE, NULL, &Type, WIN32_Service_Command_Line, &Size);
	    } else
		WIN32_Service_Command_Line = xstrdup("");
	    RegCloseKey(hndKey);
	} else {
	    ConfigFile = xstrdup(DefaultConfigFile);
	    WIN32_Service_Command_Line = xstrdup("");
	}
	WIN32_build_argv(WIN32_Service_Command_Line);
	*argc = WIN32_argc;
	*argv = WIN32_argv;
	/* Set Service Status to SERVICE_START_PENDING */
	svcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	svcStatus.dwCurrentState = SERVICE_START_PENDING;
	svcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
	svcStatus.dwWin32ExitCode = 0;
	svcStatus.dwServiceSpecificExitCode = 0;
	svcStatus.dwCheckPoint = 0;
	svcStatus.dwWaitHint = 10000;
	SetServiceStatus(svcHandle, &svcStatus);
#ifdef _SQUID_MSWIN_
	_setmaxstdio(Squid_MaxFD);
#endif
    }
#endif
#ifdef _SQUID_MSWIN_
    if (Win32SockInit() < 0)
	return 1;
#endif
    return 0;
}

#if USE_WIN32_SERVICE
void
WIN32_svcstatusupdate(DWORD svcstate, DWORD WaitHint)
{
    if (WIN32_run_mode == _WIN_SQUID_RUN_MODE_SERVICE) {
	svcStatus.dwCheckPoint++;
	svcStatus.dwWaitHint = WaitHint;
	svcStatus.dwCurrentState = svcstate;
	SetServiceStatus(svcHandle, &svcStatus);
    }
}

VOID WINAPI
WIN32_svcHandler(DWORD Opcode)
{
    DWORD status;

    switch (Opcode) {
    case _WIN_SQUID_SERVICE_CONTROL_STOP:
    case _WIN_SQUID_SERVICE_CONTROL_SHUTDOWN:
/* Do whatever it takes to stop here. */
	svcStatus.dwWin32ExitCode = 0;
	svcStatus.dwCurrentState = SERVICE_STOP_PENDING;
	svcStatus.dwCheckPoint = 0;
	svcStatus.dwWaitHint = 10000;
	shut_down(SIGTERM);
	if (!SetServiceStatus(svcHandle, &svcStatus)) {
	    status = GetLastError();
	    debug(1, 1) ("SetServiceStatus error %ld\n", status);
	}
	debug(1, 1) ("Leaving Squid service\n");
	return;
    case _WIN_SQUID_SERVICE_CONTROL_INTERROGATE:
/* Fall through to send current status. */
	if (!SetServiceStatus(svcHandle, &svcStatus)) {
	    status = GetLastError();
	    debug(1, 1) ("SetServiceStatus error %ld\n", status);
	}
	break;
    case _WIN_SQUID_SERVICE_CONTROL_ROTATE:
	rotate_logs(SIGUSR1);
	break;
    case _WIN_SQUID_SERVICE_CONTROL_RECONFIGURE:
	reconfigure(SIGHUP);
	break;
    case _WIN_SQUID_SERVICE_CONTROL_DEBUG:
	sigusr2_handle(SIGUSR2);
	break;
    case _WIN_SQUID_SERVICE_CONTROL_INTERRUPT:
/* Do whatever it takes to stop here. */
	svcStatus.dwWin32ExitCode = 0;
	svcStatus.dwCurrentState = SERVICE_STOP_PENDING;
	svcStatus.dwCheckPoint = 0;
	svcStatus.dwWaitHint = 10000;
	shut_down(SIGINT);
	if (!SetServiceStatus(svcHandle, &svcStatus)) {
	    status = GetLastError();
	    debug(1, 1) ("SetServiceStatus error %ld\n", status);
	}
	debug(1, 1) ("Leaving Squid service\n");
	break;
    default:
	debug(1, 1) ("Unrecognized opcode %ld\n", Opcode);
    }
    return;
}

void
WIN32_RemoveService()
{
    SC_HANDLE schService;
    SC_HANDLE schSCManager;

    if (!WIN32_Service_name)
	WIN32_Service_name = xstrdup(_WIN_SQUID_DEFAULT_SERVICE_NAME);
    strcat(REGKEY, WIN32_Service_name);
    keys[4] = WIN32_Service_name;
    schSCManager = OpenSCManager(NULL,	/* machine (NULL == local)    */
	NULL,			/* database (NULL == default) */
	SC_MANAGER_ALL_ACCESS	/* access required            */
	);
    if (!schSCManager)
	fprintf(stderr, "OpenSCManager failed\n");
    else {
	schService = OpenService(schSCManager, WIN32_Service_name, SERVICE_ALL_ACCESS);
	if (schService == NULL)
	    fprintf(stderr, "OpenService failed\n");
	/* Could not open the service */
	else {
	    /* try to stop the service */
	    if (ControlService(schService, _WIN_SQUID_SERVICE_CONTROL_STOP,
		    &svcStatus)) {
		sleep(1);
		while (QueryServiceStatus(schService, &svcStatus)) {
		    if (svcStatus.dwCurrentState == SERVICE_STOP_PENDING)
			sleep(1);
		    else
			break;
		}
	    }
	    /* now remove the service */
	    if (DeleteService(schService) == 0)
		fprintf(stderr, "DeleteService failed.\n");
	    else
		printf("Service %s deleted successfully.\n",
		    WIN32_Service_name);
	    CloseServiceHandle(schService);
	}
	CloseServiceHandle(schSCManager);
    }
}

void
WIN32_SetServiceCommandLine()
{
    if (!WIN32_Service_name)
	WIN32_Service_name = xstrdup(_WIN_SQUID_DEFAULT_SERVICE_NAME);
    strcat(REGKEY, WIN32_Service_name);
    keys[4] = WIN32_Service_name;
    /* Now store the Service Command Line in the registry */
    WIN32_StoreKey(COMMANDLINE, REG_SZ, (unsigned char *) WIN32_Command_Line, strlen(WIN32_Command_Line) + 1);
}

void
WIN32_InstallService()
{
    SC_HANDLE schService;
    SC_HANDLE schSCManager;
    char ServicePath[512];
    char szPath[512];
    int lenpath;

    if (!WIN32_Service_name)
	WIN32_Service_name = xstrdup(_WIN_SQUID_DEFAULT_SERVICE_NAME);
    strcat(REGKEY, WIN32_Service_name);
    keys[4] = WIN32_Service_name;
    if ((lenpath = GetModuleFileName(NULL, ServicePath, 512)) == 0) {
	fprintf(stderr, "Can't get executable path\n");
	exit(1);
    }
    snprintf(szPath, sizeof(szPath), "%s %s:%s", ServicePath, _WIN_SQUID_SERVICE_OPTION, WIN32_Service_name);
    schSCManager = OpenSCManager(NULL,	/* machine (NULL == local)    */
	NULL,			/* database (NULL == default) */
	SC_MANAGER_ALL_ACCESS	/* access required            */
	);
    if (!schSCManager) {
	fprintf(stderr, "OpenSCManager failed\n");
	exit(1);
    } else {
	schService = CreateService(schSCManager,	/* SCManager database     */
	    WIN32_Service_name,	/* name of service        */
	    WIN32_Service_name,	/* name to display        */
	    SERVICE_ALL_ACCESS,	/* desired access         */
	    SERVICE_WIN32_OWN_PROCESS,	/* service type           */
	    SERVICE_AUTO_START,	/* start type             */
	    SERVICE_ERROR_NORMAL,	/* error control type     */
	    (const char *) szPath,	/* service's binary       */
	    NULL,		/* no load ordering group */
	    NULL,		/* no tag identifier      */
	    "Tcpip\0AFD\0",	/* dependencies           */
	    NULL,		/* LocalSystem account    */
	    NULL);		/* no password            */
	if (schService) {
	    if (WIN32_OS_version > _WIN_OS_WINNT) {
		HMODULE ADVAPI32Handle;
		PFChangeServiceConfig2 ChangeServiceConfig2;
		DWORD dwInfoLevel = SERVICE_CONFIG_DESCRIPTION;

		ADVAPI32Handle = GetModuleHandle("advapi32");
		ChangeServiceConfig2 = (PFChangeServiceConfig2) GetProcAddress(ADVAPI32Handle, CHANGESERVICECONFIG2);
		ChangeServiceConfig2(schService, dwInfoLevel, &Squid_ServiceDescription);
		dwInfoLevel = SERVICE_CONFIG_FAILURE_ACTIONS;
		ChangeServiceConfig2(schService, dwInfoLevel, &Squid_ServiceFailureActions);
	    }
	    CloseServiceHandle(schService);
	    /* Now store the config file location in the registry */
	    if (!ConfigFile)
		ConfigFile = xstrdup(DefaultConfigFile);
	    WIN32_StoreKey(CONFIGFILE, REG_SZ, (unsigned char *) ConfigFile, strlen(ConfigFile) + 1);
	    printf("Squid Cache version %s for %s\n", version_string,
		CONFIG_HOST_TYPE);
	    printf("installed successfully as %s Windows System Service.\n",
		WIN32_Service_name);
	    printf
		("To run, start it from the Services Applet of Control Panel.\n");
	    printf("Don't forget to edit squid.conf before starting it.\n\n");
	} else {
	    fprintf(stderr, "CreateService failed\n");
	    exit(1);
	}
	CloseServiceHandle(schSCManager);
    }
}

void
WIN32_sendSignal(int WIN32_signal)
{
    SERVICE_STATUS ssStatus;
    DWORD fdwAccess, fdwControl;
    SC_HANDLE schService;
    SC_HANDLE schSCManager;

    if (!WIN32_Service_name)
	WIN32_Service_name = xstrdup(_WIN_SQUID_DEFAULT_SERVICE_NAME);
    schSCManager = OpenSCManager(NULL,	/* machine (NULL == local)    */
	NULL,			/* database (NULL == default) */
	SC_MANAGER_ALL_ACCESS	/* access required            */
	);
    if (!schSCManager) {
	fprintf(stderr, "OpenSCManager failed\n");
	exit(1);
    }
    /* The required service object access depends on the control. */
    switch (WIN32_signal) {
    case 0:			/* SIGNULL */
	fdwAccess = SERVICE_INTERROGATE;
	fdwControl = _WIN_SQUID_SERVICE_CONTROL_INTERROGATE;
	break;
    case SIGUSR1:
	fdwAccess = SERVICE_USER_DEFINED_CONTROL;
	fdwControl = _WIN_SQUID_SERVICE_CONTROL_ROTATE;
	break;
    case SIGUSR2:
	fdwAccess = SERVICE_USER_DEFINED_CONTROL;
	fdwControl = _WIN_SQUID_SERVICE_CONTROL_DEBUG;
	break;
    case SIGHUP:
	fdwAccess = SERVICE_USER_DEFINED_CONTROL;
	fdwControl = _WIN_SQUID_SERVICE_CONTROL_RECONFIGURE;
	break;
    case SIGTERM:
	fdwAccess = SERVICE_STOP;
	fdwControl = _WIN_SQUID_SERVICE_CONTROL_STOP;
	break;
    case SIGINT:
    case SIGKILL:
	fdwAccess = SERVICE_USER_DEFINED_CONTROL;
	fdwControl = _WIN_SQUID_SERVICE_CONTROL_INTERRUPT;
	break;
    default:
	exit(1);
    }
    /* Open a handle to the service. */
    schService = OpenService(schSCManager,	/* SCManager database */
	WIN32_Service_name,	/* name of service    */
	fdwAccess);		/* specify access     */
    if (schService == NULL) {
	fprintf(stderr, "%s: ERROR: Could not open Service %s\n", appname,
	    WIN32_Service_name);
	exit(1);
    } else {
	/* Send a control value to the service. */
	if (!ControlService(schService,		/* handle of service      */
		fdwControl,	/* control value to send  */
		&ssStatus)) {	/* address of status info */
	    fprintf(stderr, "%s: ERROR: Could not Control Service %s\n",
		appname, WIN32_Service_name);
	    exit(1);
	} else {
	    /* Print the service status. */
	    printf("\nStatus of %s Service:\n", WIN32_Service_name);
	    printf("  Service Type: 0x%lx\n", ssStatus.dwServiceType);
	    printf("  Current State: 0x%lx\n", ssStatus.dwCurrentState);
	    printf("  Controls Accepted: 0x%lx\n", ssStatus.dwControlsAccepted);
	    printf("  Exit Code: %ld\n", ssStatus.dwWin32ExitCode);
	    printf("  Service Specific Exit Code: %ld\n",
		ssStatus.dwServiceSpecificExitCode);
	    printf("  Check Point: %ld\n", ssStatus.dwCheckPoint);
	    printf("  Wait Hint: %ld\n", ssStatus.dwWaitHint);
	}
	CloseServiceHandle(schService);
    }
    CloseServiceHandle(schSCManager);
}

int
main(int argc, char **argv)
{
    SERVICE_TABLE_ENTRY DispatchTable[] =
    {
	{NULL, SquidWinSvcMain},
	{NULL, NULL}
    };
    char *c;
    char stderr_path[256];

    if ((argc == 2) && strstr(argv[1], _WIN_SQUID_SERVICE_OPTION)) {
	strcpy(stderr_path, argv[0]);
	strcat(stderr_path, ".log");
	freopen(stderr_path, "w", stderr);
	setmode(fileno(stderr), O_TEXT);
	WIN32_run_mode = _WIN_SQUID_RUN_MODE_SERVICE;
	if (!(c = strchr(argv[1], ':'))) {
	    fprintf(stderr, "Bad Service Parameter: %s\n", argv[1]);
	    return 1;
	}
	WIN32_Service_name = xstrdup(c + 1);
	DispatchTable[0].lpServiceName = WIN32_Service_name;
	strcat(REGKEY, WIN32_Service_name);
	keys[4] = WIN32_Service_name;
	if (!StartServiceCtrlDispatcher(DispatchTable)) {
	    fprintf(stderr, "StartServiceCtrlDispatcher error = %ld\n",
		GetLastError());
	    return 1;
	}
    } else {
	WIN32_run_mode = _WIN_SQUID_RUN_MODE_INTERACTIVE;
#ifdef _SQUID_MSWIN_
	opt_no_daemon = 1;
#endif
	return SquidMain(argc, argv);
    }
    return 0;
}
#endif

#if defined(_SQUID_MSWIN_)
static int s_iInitCount = 0;

int
WIN32_pipe(int handles[2])
{
    int new_socket;
    fde *F = NULL;

    struct sockaddr_in serv_addr;
    int len = sizeof(serv_addr);
    u_short handle1_port;

    handles[0] = handles[1] = -1;

    statCounter.syscalls.sock.sockets++;
    if ((new_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	return -1;

    memset((void *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(0);
    serv_addr.sin_addr = local_addr;

    if (bind(new_socket, (SOCKADDR *) & serv_addr, len) < 0 ||
	listen(new_socket, 1) < 0 || getsockname(new_socket, (SOCKADDR *) & serv_addr, &len) < 0 ||
	(handles[1] = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
	closesocket(new_socket);
	return -1;
    }
    handle1_port = ntohs(serv_addr.sin_port);
    if (connect(handles[1], (SOCKADDR *) & serv_addr, len) < 0 ||
	(handles[0] = accept(new_socket, (SOCKADDR *) & serv_addr, &len)) < 0) {
	closesocket(handles[1]);
	handles[1] = -1;
	closesocket(new_socket);
	return -1;
    }
    closesocket(new_socket);

    F = &fd_table[handles[0]];
    F->local_addr = local_addr;
    F->local_port = ntohs(serv_addr.sin_port);

    F = &fd_table[handles[1]];
    F->local_addr = local_addr;
    xstrncpy(F->ipaddr, inet_ntoa(local_addr), 16);
    F->remote_port = handle1_port;

    return 0;
}

int
WIN32_getrusage(int who, struct rusage *usage)
{
#if HAVE_WIN32_PSAPI
    if (WIN32_OS_version >= _WIN_OS_WINNT) {
	/* On Windows NT and later call PSAPI.DLL for process Memory */
	/* informations -- Guido Serassio                       */
	HANDLE hProcess;
	PROCESS_MEMORY_COUNTERS pmc;
	hProcess = OpenProcess(PROCESS_QUERY_INFORMATION |
	    PROCESS_VM_READ,
	    FALSE, GetCurrentProcessId());
	{
	    /* Microsoft CRT doesn't have getrusage function,  */
	    /* so we get process CPU time information from PSAPI.DLL. */
	    FILETIME ftCreate, ftExit, ftKernel, ftUser;
	    if (GetProcessTimes(hProcess, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
		PFILETIME p_ftUser = &ftUser;
		PFILETIME p_ftKernel = &ftKernel;
		int64_t tUser64 = (*(int64_t *) p_ftUser / 10);
		int64_t tKernel64 = (*(int64_t *) p_ftKernel / 10);
		usage->ru_utime.tv_sec = (long) (tUser64 / 1000000);
		usage->ru_stime.tv_sec = (long) (tKernel64 / 1000000);
		usage->ru_utime.tv_usec = (long) (tUser64 % 1000000);
		usage->ru_stime.tv_usec = (long) (tKernel64 % 1000000);
	    } else {
		CloseHandle(hProcess);
		return -1;
	    }
	}
	if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
	    usage->ru_maxrss = (DWORD) (pmc.WorkingSetSize / getpagesize());
	    usage->ru_majflt = pmc.PageFaultCount;
	} else {
	    CloseHandle(hProcess);
	    return -1;
	}
	CloseHandle(hProcess);
    }
#endif
    return 0;
}

static int
Win32SockInit(void)
{
    int iVersionRequested;
    WSADATA wsaData;
    int err, opt;
    int optlen = sizeof(opt);


    if (s_iInitCount > 0) {
	s_iInitCount++;
	return (0);
    } else if (s_iInitCount < 0)
	return (s_iInitCount);
    /* s_iInitCount == 0. Do the initailization */
    iVersionRequested = MAKEWORD(2, 0);
    err = WSAStartup((WORD) iVersionRequested, &wsaData);
    if (err) {
	s_iInitCount = -1;
	return (s_iInitCount);
    }
    if (LOBYTE(wsaData.wVersion) != 2 ||
	HIBYTE(wsaData.wVersion) != 0) {
	s_iInitCount = -2;
	WSACleanup();
	return (s_iInitCount);
    }
    if (WIN32_OS_version != _WIN_OS_WINNT) {
	if (getsockopt(INVALID_SOCKET, SOL_SOCKET, SO_OPENTYPE, (char *) &opt, &optlen)) {
	    s_iInitCount = -3;
	    WSACleanup();
	    return (s_iInitCount);
	} else {
	    opt = opt | SO_SYNCHRONOUS_NONALERT;
	    if (setsockopt(INVALID_SOCKET, SOL_SOCKET, SO_OPENTYPE, (char *) &opt, optlen)) {
		s_iInitCount = -3;
		WSACleanup();
		return (s_iInitCount);
	    }
	}
    }
    WIN32_Socks_initialized = 1;
    s_iInitCount++;
    return (s_iInitCount);
}

static void
Win32SockCleanup(void)
{
    if (--s_iInitCount == 0)
	WSACleanup();
    return;
}

LONG CALLBACK
WIN32_ExceptionHandler(EXCEPTION_POINTERS * ep)
{
    EXCEPTION_RECORD *er;

    er = ep->ExceptionRecord;

    switch (er->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
	raise(SIGSEGV);
	break;
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_IN_PAGE_ERROR:
	death(SIGBUS);
	break;
    default:
	break;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}


void
WIN32_ExceptionHandlerInit()
{
#if !defined(_DEBUG)
    if (Win32_Old_ExceptionHandler == NULL)
	Win32_Old_ExceptionHandler = SetUnhandledExceptionFilter(WIN32_ExceptionHandler);
#endif
}

void
WIN32_ExceptionHandlerCleanup()
{
    if (Win32_Old_ExceptionHandler != NULL)
	SetUnhandledExceptionFilter(Win32_Old_ExceptionHandler);
}

#endif /* _SQUID_MSWIN_ */

#endif /* _SQUID_WIN32_ */
