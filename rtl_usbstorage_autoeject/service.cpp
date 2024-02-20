/* REF: https://learn.microsoft.com/en-us/windows/win32/services/svc-cpp */
#include <windows.h>
#include <dbt.h>
#include <tchar.h>
#include <strsafe.h>
#include "mc/sample.h"
#include "realtek_usbstorage.h"
#include "notification_message_window.h"

#pragma comment(lib, "advapi32.lib")

#define SVCNAME     TEXT("RTL88X1CU_Enabler")
#define SVCDISPNAME TEXT("Realtek NIC Enabler (RTL8811CU RTL8821CU)")
#define SVCDESC     TEXT("Auto eject the CDROM drive for Realtek RTL8811CU/RTL8821CU chips to enable the NIC function ASAP.")

SERVICE_STATUS          gSvcStatus;
SERVICE_STATUS_HANDLE   gSvcStatusHandle;
HANDLE                  ghSvcStopEvent = NULL;
HANDLE                  hDevNotify = NULL;

VOID SvcInstall(void);
VOID SvcDelete(void);
VOID SvcStart(void);
DWORD WINAPI SvcCtrlHandler(DWORD, DWORD, LPVOID, LPVOID);
VOID WINAPI SvcMain(DWORD, LPTSTR*);

VOID ReportSvcStatus(DWORD, DWORD, DWORD);
VOID SvcInit(DWORD, LPTSTR*);
VOID SvcReportEvent(LPTSTR);

DWORD WINAPI SvcMainWindow(LPVOID);
VOID StandaloneMode(void);
VOID HelpText(TCHAR*);

//
// Purpose: 
//   Entry point for the process
//
// Parameters:
//   None
// 
// Return value:
//   None, defaults to 0 (zero)
//
int __cdecl _tmain(int argc, TCHAR* argv[])
{
    // If command-line parameter is "install", install the service. 
    // Otherwise, the service is probably being started by the SCM.

    if (!lstrcmpi(argv[1], TEXT("install")))
    {
        SvcInstall();
        return 0;
    }
    else if (!lstrcmpi(argv[1], TEXT("uninstall")))
    {
        // Add custom uninstall thiny here
        SvcDelete();
        return 0;
    }
    else if (!lstrcmpi(argv[1], TEXT("service")))
    {
        SvcStart();
        return 0;
    }
    else if (!lstrcmpi(argv[1], TEXT("standalone")))
    {
        StandaloneMode();
        return 0;
    }
    else if (!lstrcmpi(argv[1], TEXT("once")))
    {
        check_realtek_cdrom_disk();
        return 0;
    }

    HelpText(argv[0]);

    return 0;
}

VOID SvcStart(void) {
    // TO_DO: Add any additional services for the process to this table.
    SERVICE_TABLE_ENTRY DispatchTable[] =
    {
        { (LPTSTR)SVCNAME, (LPSERVICE_MAIN_FUNCTION)SvcMain },
        { NULL, NULL }
    };

    // This call returns when the service has stopped. 
    // The process should simply terminate when the call returns.

    if (!StartServiceCtrlDispatcher(DispatchTable))
    {
        SvcReportEvent((LPTSTR)TEXT("StartServiceCtrlDispatcher"));
    }
}

//
// Purpose: 
//   Installs a service in the SCM database
//
// Parameters:
//   None
// 
// Return value:
//   None
//
VOID SvcInstall()
{
    SC_HANDLE schSCManager;
    SC_HANDLE schService;
    TCHAR szUnquotedPath[MAX_PATH];

    if (!GetModuleFileName(NULL, szUnquotedPath, MAX_PATH))
    {
        printf("Cannot install service (%d)\n", GetLastError());
        return;
    }

    // In case the path contains a space, it must be quoted so that
    // it is correctly interpreted. For example,
    // "d:\my share\myservice.exe" should be specified as
    // ""d:\my share\myservice.exe"".
    TCHAR szPath[MAX_PATH];
    StringCbPrintf(szPath, MAX_PATH, TEXT("\"%s\" \"service\""), szUnquotedPath);

    // Get a handle to the SCM database. 

    schSCManager = OpenSCManager(
        NULL,                    // local computer
        NULL,                    // ServicesActive database 
        SC_MANAGER_ALL_ACCESS);  // full access rights 

    if (NULL == schSCManager)
    {
        printf("OpenSCManager failed (%d)\n", GetLastError());
        return;
    }

    // Create the service

    schService = CreateService(
        schSCManager,              // SCM database 
        SVCNAME,                   // name of service 
        SVCDISPNAME,               // service name to display 
        SERVICE_ALL_ACCESS,        // desired access 
        SERVICE_WIN32_OWN_PROCESS, // service type 
        SERVICE_AUTO_START,        // start type 
        SERVICE_ERROR_NORMAL,      // error control type 
        szPath,                    // path to service's binary 
        NULL,                      // no load ordering group 
        NULL,                      // no tag identifier 
        NULL,                      // no dependencies 
        NULL,                      // LocalSystem account 
        NULL);                     // no password 

    if (schService == NULL)
    {
        printf("CreateService failed (%d)\n", GetLastError());
        CloseServiceHandle(schSCManager);
        return;
    }
    else {
        printf("Service installed successfully\n");

        // Change description
        SERVICE_DESCRIPTION description = { (LPWSTR)SVCDESC };
        ChangeServiceConfig2(schService, SERVICE_CONFIG_DESCRIPTION, &description);

        // Start service
        StartService(schService, 0, NULL);
    }

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}

/* REF: https://learn.microsoft.com/en-us/windows/win32/services/deleting-a-service */
//
// Purpose: 
//   Deletes a service from the SCM database
//
// Parameters:
//   None
// 
// Return value:
//   None
//
VOID SvcDelete(void)
{
    SC_HANDLE schSCManager;
    SC_HANDLE schService;

    // Get a handle to the SCM database. 

    schSCManager = OpenSCManager(
        NULL,                    // local computer
        NULL,                    // ServicesActive database 
        SC_MANAGER_ALL_ACCESS);  // full access rights 

    if (NULL == schSCManager)
    {
        printf("OpenSCManager failed (%d)\n", GetLastError());
        return;
    }

    // Get a handle to the service.

    schService = OpenService(
        schSCManager,          // SCM database 
        SVCNAME,               // name of service 
        DELETE |               // need delete access 
        SERVICE_STOP |
        SERVICE_QUERY_STATUS);

    if (schService == NULL)
    {
        printf("OpenService failed (%d)\n", GetLastError());
        CloseServiceHandle(schSCManager);
        return;
    }

    // Stop the service
    // REF: https://learn.microsoft.com/en-us/windows/win32/services/svccontrol-cpp

    // Make sure the service is not already stopped.
    SERVICE_STATUS_PROCESS ssp;
    DWORD dwStartTime = GetTickCount();
    DWORD dwBytesNeeded;
    DWORD dwTimeout = 30000; // 30-second time-out
    DWORD dwWaitTime;

    if (!QueryServiceStatusEx(
        schService,
        SC_STATUS_PROCESS_INFO,
        (LPBYTE)&ssp,
        sizeof(SERVICE_STATUS_PROCESS),
        &dwBytesNeeded))
    {
        printf("QueryServiceStatusEx failed (%d)\n", GetLastError());
        goto stop_cleanup;
    }

    if (ssp.dwCurrentState == SERVICE_STOPPED)
    {
        printf("Service is already stopped.\n");
        goto do_deletion;
    }

    // If a stop is pending, wait for it.

    while (ssp.dwCurrentState == SERVICE_STOP_PENDING)
    {
        printf("Service stop pending...\n");

        // Do not wait longer than the wait hint. A good interval is 
        // one-tenth of the wait hint but not less than 1 second  
        // and not more than 10 seconds. 

        dwWaitTime = ssp.dwWaitHint / 10;

        if (dwWaitTime < 1000)
            dwWaitTime = 1000;
        else if (dwWaitTime > 10000)
            dwWaitTime = 10000;

        Sleep(dwWaitTime);

        if (!QueryServiceStatusEx(
            schService,
            SC_STATUS_PROCESS_INFO,
            (LPBYTE)&ssp,
            sizeof(SERVICE_STATUS_PROCESS),
            &dwBytesNeeded))
        {
            printf("QueryServiceStatusEx failed (%d)\n", GetLastError());
            goto stop_cleanup;
        }

        if (ssp.dwCurrentState == SERVICE_STOPPED)
        {
            printf("Service stopped successfully.\n");
            goto do_deletion;
        }

        if (GetTickCount() - dwStartTime > dwTimeout)
        {
            printf("Service stop timed out.\n");
            goto stop_cleanup;
        }
    }

    // Send a stop code to the service.

    if (!ControlService(
        schService,
        SERVICE_CONTROL_STOP,
        (LPSERVICE_STATUS)&ssp))
    {
        printf("ControlService failed (%d)\n", GetLastError());
        goto stop_cleanup;
    }

    // Wait for the service to stop.

    while (ssp.dwCurrentState != SERVICE_STOPPED)
    {
        Sleep(ssp.dwWaitHint);
        if (!QueryServiceStatusEx(
            schService,
            SC_STATUS_PROCESS_INFO,
            (LPBYTE)&ssp,
            sizeof(SERVICE_STATUS_PROCESS),
            &dwBytesNeeded))
        {
            printf("QueryServiceStatusEx failed (%d)\n", GetLastError());
            goto stop_cleanup;
        }

        if (ssp.dwCurrentState == SERVICE_STOPPED)
            break;

        if (GetTickCount() - dwStartTime > dwTimeout)
        {
            printf("Wait timed out\n");
            goto stop_cleanup;
        }
    }
    printf("Service stopped successfully\n");

do_deletion:
    // Delete the service.

    if (!DeleteService(schService))
    {
        printf("DeleteService failed (%d)\n", GetLastError());
    }
    else printf("Service deleted successfully\n");

stop_cleanup:
    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}

//
// Purpose: 
//   Entry point for the service
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
// 
// Return value:
//   None.
//
VOID WINAPI SvcMain(DWORD dwArgc, LPTSTR* lpszArgv)
{
    // Register the handler function for the service

    gSvcStatusHandle = RegisterServiceCtrlHandlerEx(
        SVCNAME,
        SvcCtrlHandler,
        NULL);

    if (!gSvcStatusHandle)
    {
        SvcReportEvent((LPTSTR)TEXT("RegisterServiceCtrlHandlerEx"));
        return;
    }

    DEV_BROADCAST_DEVICEINTERFACE notofy_filter = { 0 };
    notofy_filter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    notofy_filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    memcpy(&notofy_filter.dbcc_classguid, &GUID_DEVINTERFACE_CDROM, sizeof(GUID));

    hDevNotify = RegisterDeviceNotification(gSvcStatusHandle, &notofy_filter, DEVICE_NOTIFY_SERVICE_HANDLE);

    if (!hDevNotify)
    {
        SvcReportEvent((LPTSTR)TEXT("RegisterDeviceNotification"));
        return;
    }

    // These SERVICE_STATUS members remain as set here

    gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    gSvcStatus.dwServiceSpecificExitCode = 0;

    // Report initial status to the SCM

    ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    // Perform service-specific initialization and work.

    SvcInit(dwArgc, lpszArgv);
}

//
// Purpose: 
//   The service code
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
// 
// Return value:
//   None
//
VOID SvcInit(DWORD dwArgc, LPTSTR* lpszArgv)
{
    // TO_DO: Declare and set any required variables.
    //   Be sure to periodically call ReportSvcStatus() with 
    //   SERVICE_START_PENDING. If initialization fails, call
    //   ReportSvcStatus with SERVICE_STOPPED.

    // Create an event. The control handler function, SvcCtrlHandler,
    // signals this event when it receives the stop control code.

    ghSvcStopEvent = CreateEvent(
        NULL,    // default security attributes
        TRUE,    // manual reset event
        FALSE,   // not signaled
        NULL);   // no name

    if (ghSvcStopEvent == NULL)
    {
        ReportSvcStatus(SERVICE_STOPPED, GetLastError(), 0);
        return;
    }

    // Report running status when initialization is complete.

    ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);

    // TO_DO: Perform work until service stops.

    // Run once if the NIC has already been plugged before svc run
    check_realtek_cdrom_disk();

    while (1)
    {
        // Check whether to stop the service.

        WaitForSingleObject(ghSvcStopEvent, INFINITE);

        if (hDevNotify) {
            UnregisterDeviceNotification(hDevNotify);
            hDevNotify = NULL;
        }

        ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
        return;
    }
}

//
// Purpose: 
//   Sets the current service status and reports it to the SCM.
//
// Parameters:
//   dwCurrentState - The current state (see SERVICE_STATUS)
//   dwWin32ExitCode - The system error code
//   dwWaitHint - Estimated time for pending operation, 
//     in milliseconds
// 
// Return value:
//   None
//
VOID ReportSvcStatus(DWORD dwCurrentState,
    DWORD dwWin32ExitCode,
    DWORD dwWaitHint)
{
    static DWORD dwCheckPoint = 1;

    // Fill in the SERVICE_STATUS structure.

    gSvcStatus.dwCurrentState = dwCurrentState;
    gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
    gSvcStatus.dwWaitHint = dwWaitHint;

    if (dwCurrentState == SERVICE_START_PENDING)
        gSvcStatus.dwControlsAccepted = 0;
    else gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

    if ((dwCurrentState == SERVICE_RUNNING) ||
        (dwCurrentState == SERVICE_STOPPED))
        gSvcStatus.dwCheckPoint = 0;
    else gSvcStatus.dwCheckPoint = dwCheckPoint++;

    // Report the status of the service to the SCM.
    SetServiceStatus(gSvcStatusHandle, &gSvcStatus);
}

//
// Purpose: 
//   Called by SCM whenever a control code is sent to the service
//   using the ControlService function.
//
// Parameters:
//   dwCtrl    - control code
//   dwEvtType - event type
//   lpEvtData - event data
//   lpCtx     - context
// 
// Return value:
//   Error code
//
DWORD WINAPI SvcCtrlHandler(DWORD dwCtrl, DWORD dwEvtType, LPVOID lpEvtData, LPVOID lpCtx)
{
    // Handle the requested control code. 
    switch (dwCtrl)
    {
    case SERVICE_CONTROL_STOP:
        ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);

        // Signal the service to stop.

        SetEvent(ghSvcStopEvent);
        ReportSvcStatus(gSvcStatus.dwCurrentState, NO_ERROR, 0);

        return NO_ERROR;

    case SERVICE_CONTROL_INTERROGATE:
        break;

    case SERVICE_CONTROL_DEVICEEVENT:
        switch (dwEvtType)
        {
        case DBT_DEVICEARRIVAL:
            check_realtek_cdrom_disk();
            break;

        case DBT_DEVICEREMOVECOMPLETE:
            break;
        }
        break;

    default:
        break;
    }

    return NO_ERROR;
}

//
// Purpose: 
//   Logs messages to the event log
//
// Parameters:
//   szFunction - name of function that failed
// 
// Return value:
//   None
//
// Remarks:
//   The service must have an entry in the Application event log.
//
VOID SvcReportEvent(LPTSTR szFunction)
{
    HANDLE hEventSource;
    LPCTSTR lpszStrings[2];
    TCHAR Buffer[80];

    hEventSource = RegisterEventSource(NULL, SVCNAME);

    if (NULL != hEventSource)
    {
        StringCchPrintf(Buffer, 80, TEXT("%s failed with %d"), szFunction, GetLastError());

        lpszStrings[0] = SVCNAME;
        lpszStrings[1] = Buffer;

        ReportEvent(hEventSource,        // event log handle
            EVENTLOG_ERROR_TYPE, // event type
            0,                   // event category
            SVC_ERROR,           // event identifier
            NULL,                // no security identifier
            2,                   // size of lpszStrings array
            0,                   // no binary data
            lpszStrings,         // array of strings
            NULL);               // no binary data

        DeregisterEventSource(hEventSource);
    }
}

/************** MAIN LOOP **************/
DWORD WINAPI SvcMainWindow(LPVOID lpParam) {
    BOOL* is_running = (BOOL*)lpParam;
    if (!is_running) {
        return ERROR_INVALID_PARAMETER;
    }

    notify_window_start();

    return NO_ERROR;
}

VOID StandaloneMode(void) {
    BOOL main_loop_running = TRUE;
    HANDLE main_loop_thread = CreateThread(NULL, 0,
        SvcMainWindow,
        &main_loop_running, 0, NULL);

    if (main_loop_thread) {
        printf("Running as standalone mode\n");
        WaitForSingleObject(main_loop_thread, INFINITE);
        CloseHandle(main_loop_thread);
    }
}

VOID HelpText(TCHAR* text) {
    auto ptr = wcsrchr(text, '/');
    if (!ptr) {
        ptr = wcsrchr(text, '\\');
    }

    printf("Usage: %ws [install|uninstall|standalone|once|service]\n", ptr ? ptr + 1 : text);
    printf("\t  install : Install as service\n");
    printf("\tuninstall : Remove service\n");
    printf("\tstandalone: Run as standalone mode\n");
    printf("\tonce      : Run the checking once\n");
    printf("\tservice   : Run as service mode, should only called by Windows service\n");
}