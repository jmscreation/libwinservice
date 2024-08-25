#include "libwinservice.h"
#include <strsafe.h>

// Initialize the singleton service instance.
CServiceBase *CServiceBase::s_service = NULL;


//
//   FUNCTION: CServiceBase::Run(CServiceBase &)
//
//   PURPOSE: Register the executable for a service with the Service Control
//   Manager (SCM). After you call Run(ServiceBase), the SCM issues a Start
//   command, which results in a call to the OnStart method in the service.
//   This method blocks until the service has stopped.
//
//   PARAMETERS:
//   * service - the reference to a CServiceBase object. It will become the
//     singleton service instance of this service application.
//
//   RETURN VALUE: If the function succeeds, the return value is TRUE. If the
//   function fails, the return value is FALSE. To get extended error
//   information, call GetLastError.
//
BOOL CServiceBase::Run(CServiceBase &service, DWORD &errorCode)
{
    s_service = &service;

    SERVICE_TABLE_ENTRY serviceTable[] =
    {
        { (LPSTR)service.m_name, &ServiceMain },
        { NULL, NULL }
    };

    service.WriteEventLogEntry("Service Attempting To Start", EVENTLOG_INFORMATION_TYPE);

    // Connects the main thread of a service process to the service control
    // manager, which causes the thread to be the service control dispatcher
    // thread for the calling process. This call returns when the service has
    // stopped. The process should simply terminate when the call returns.
    auto r = StartServiceCtrlDispatcher(serviceTable);
    if(!r) errorCode = service.WriteErrorLogEntry("Service Failed To Start");
    return r;
}


//
//   FUNCTION: CServiceBase::ServiceMain(DWORD, PWSTR *)
//
//   PURPOSE: Entry point for the service. It registers the handler function
//   for the service and starts the service.
//
//   PARAMETERS:
//   * dwArgc   - number of command line arguments
//   * lpszArgv - array of command line arguments
//
void WINAPI CServiceBase::ServiceMain(DWORD dwArgc, char **pszArgv)
{
    assert(s_service != NULL);

    // Register the handler function for the service
    s_service->m_statusHandle = RegisterServiceCtrlHandler((LPCSTR)s_service->m_name, &ServiceCtrlHandler);
    if (s_service->m_statusHandle == NULL)
    {
        throw s_service->WriteErrorLogEntry("RegisterServiceCtrlHandler");
    }

    // Start the service.
    s_service->Start(dwArgc, (PWSTR*)pszArgv);
}


//
//   FUNCTION: CServiceBase::ServiceCtrlHandler(DWORD)
//
//   PURPOSE: The function is called by the SCM whenever a control code is
//   sent to the service.
//
//   PARAMETERS:
//   * dwCtrlCode - the control code. This parameter can be one of the
//   following values:
//
//     SERVICE_CONTROL_CONTINUE
//     SERVICE_CONTROL_INTERROGATE
//     SERVICE_CONTROL_NETBINDADD
//     SERVICE_CONTROL_NETBINDDISABLE
//     SERVICE_CONTROL_NETBINDREMOVE
//     SERVICE_CONTROL_PARAMCHANGE
//     SERVICE_CONTROL_PAUSE
//     SERVICE_CONTROL_SHUTDOWN
//     SERVICE_CONTROL_STOP
//
//   This parameter can also be a user-defined control code ranges from 128
//   to 255.
//
void WINAPI CServiceBase::ServiceCtrlHandler(DWORD dwCtrl)
{
    switch (dwCtrl)
    {
    case SERVICE_CONTROL_STOP:
        if(s_service->service_log) s_service->WriteEventLogEntry("Attempting To Stop Service", EVENTLOG_INFORMATION_TYPE);
        s_service->Stop(); break;
    case SERVICE_CONTROL_PAUSE:
        if(s_service->service_log) s_service->WriteEventLogEntry("Attempting To Pause Service", EVENTLOG_INFORMATION_TYPE);
        s_service->Pause(); break;
    case SERVICE_CONTROL_CONTINUE:
        if(s_service->service_log) s_service->WriteEventLogEntry("Attempting To Continue Service", EVENTLOG_INFORMATION_TYPE);
        s_service->Continue(); break;
    case SERVICE_CONTROL_SHUTDOWN:
        if(s_service->service_log) s_service->WriteEventLogEntry("Attempting To Shutdown Service", EVENTLOG_INFORMATION_TYPE);
        s_service->Shutdown(); break;
    case SERVICE_CONTROL_INTERROGATE:
        if(s_service->service_log) s_service->WriteEventLogEntry("Cannot Interrogate Service Because There Is Not Control For This", EVENTLOG_WARNING_TYPE);
        break;
    default: break;
    }
}

//
//   FUNCTION: CServiceBase::CServiceBase(PWSTR, BOOL, BOOL, BOOL)
//
//   PURPOSE: The constructor of CServiceBase. It initializes a new instance
//   of the CServiceBase class. The optional flags can be passed via the
//   dwControlsAccepted parameter to allow you to specify whether the
//   service can be stopped, paused and continued, or be notified when system
//   shutdown occurs.
//
//   PARAMETERS:
//   * pszServiceName - the name of the service
//   * dwControlsAccepted - the control flags the service accepts: can use any of the following flags
//   SERVICE_ACCEPT_STOP, SERVICE_ACCEPT_PAUSE_CONTINUE, SERVICE_ACCEPT_SHUTDOWN
//
CServiceBase::CServiceBase(const char* pszServiceName, DWORD dwControlsAccepted)
{
    // Service name must be a valid string and cannot be NULL.
    m_name = (pszServiceName == NULL) ? "" : pszServiceName;

    service_log = false; // logging disabled by default

    m_statusHandle = NULL;

    // The service runs in its own process.
    m_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;

    // The service is starting.
    m_status.dwCurrentState = SERVICE_START_PENDING;

    // The accepted commands of the service.
    m_status.dwControlsAccepted = dwControlsAccepted;

    m_status.dwWin32ExitCode = NO_ERROR;
    m_status.dwServiceSpecificExitCode = 0;
    m_status.dwCheckPoint = 0;
    m_status.dwWaitHint = 0;
}


//
//   FUNCTION: CServiceBase::~CServiceBase()
//
//   PURPOSE: The virtual destructor of CServiceBase.
//
CServiceBase::~CServiceBase(void)
{
}

//
//   FUNCTION: CServiceBase::EnableLogging(BOOL)
//
//   PURPOSE: The function enables or disables the service event log
//
//   PARAMETERS:
//   * enabled   - enabled or disabled logging
//
void CServiceBase::EnableLogging(bool enabled)
{
    service_log = enabled;
}

//
//   FUNCTION: CServiceBase::Start(DWORD, PWSTR *)
//
//   PURPOSE: The function starts the service. It calls the OnStart virtual
//   function in which you can specify the actions to take when the service
//   starts. If an error occurs during the startup, the error will be logged
//   in the Application event log, and the service will be stopped.
//
//   PARAMETERS:
//   * dwArgc   - number of command line arguments
//   * lpszArgv - array of command line arguments
//
void CServiceBase::Start(DWORD dwArgc, PWSTR *pszArgv)
{
    try
    {
        // Tell SCM that the service is starting and should take no more than 4 seconds.
        SetServiceStatus(SERVICE_START_PENDING, NOERROR, 4000);

        // Perform service-specific initialization.
        OnStart(dwArgc, pszArgv);

        // Tell SCM that the service is started.
        SetServiceStatus(SERVICE_RUNNING);
    }
    catch (DWORD dwError)
    {
        // Log the error.
        WriteErrorLogEntry("Service Start", dwError);

        // Set the service status to be stopped.
        SetServiceStatus(SERVICE_STOPPED, dwError);
    }
    catch (...)
    {
        // Log the error.
        WriteEventLogEntry("Service failed to start", EVENTLOG_ERROR_TYPE);

        // Set the service status to be stopped.
        SetServiceStatus(SERVICE_STOPPED);
    }
}


//
//   FUNCTION: CServiceBase::OnStart(DWORD, PWSTR *)
//
//   PURPOSE: When implemented in a derived class, executes when a Start
//   command is sent to the service by the SCM or when the operating system
//   starts (for a service that starts automatically). Specifies actions to
//   take when the service starts. Be sure to periodically call
//   CServiceBase::SetServiceStatus() with SERVICE_START_PENDING if the
//   procedure is going to take long time. You may also consider spawning a
//   new thread in OnStart to perform time-consuming initialization tasks.
//
//   PARAMETERS:
//   * dwArgc   - number of command line arguments
//   * lpszArgv - array of command line arguments
//
void CServiceBase::OnStart(DWORD dwArgc, PWSTR *pszArgv)
{
}


//
//   FUNCTION: CServiceBase::Stop()
//
//   PURPOSE: The function stops the service. It calls the OnStop virtual
//   function in which you can specify the actions to take when the service
//   stops. If an error occurs, the error will be logged in the Application
//   event log, and the service will be restored to the original state.
//
void CServiceBase::Stop()
{
    DWORD dwOriginalState = m_status.dwCurrentState;
    try
    {
        // Tell SCM that the service is stopping.
        SetServiceStatus(SERVICE_STOP_PENDING);

        // Perform service-specific stop operations.
        OnStop();

        // Tell SCM that the service is stopped.
        SetServiceStatus(SERVICE_STOPPED);
    }
    catch (DWORD dwError)
    {
        // Log the error.
        WriteErrorLogEntry("Service Stop", dwError);

        // Set the orginal service status.
        SetServiceStatus(dwOriginalState);
    }
    catch (...)
    {
        // Log the error.
        WriteEventLogEntry("Service failed to stop", EVENTLOG_ERROR_TYPE);

        // Set the orginal service status.
        SetServiceStatus(dwOriginalState);
    }
}


//
//   FUNCTION: CServiceBase::OnStop()
//
//   PURPOSE: When implemented in a derived class, executes when a Stop
//   command is sent to the service by the SCM. Specifies actions to take
//   when a service stops running. Be sure to periodically call
//   CServiceBase::SetServiceStatus() with SERVICE_STOP_PENDING if the
//   procedure is going to take long time.
//
void CServiceBase::OnStop()
{
}


//
//   FUNCTION: CServiceBase::Pause()
//
//   PURPOSE: The function pauses the service if the service supports pause
//   and continue. It calls the OnPause virtual function in which you can
//   specify the actions to take when the service pauses. If an error occurs,
//   the error will be logged in the Application event log, and the service
//   will become running.
//
void CServiceBase::Pause()
{
    try
    {
        // Tell SCM that the service is pausing.
        SetServiceStatus(SERVICE_PAUSE_PENDING);

        // Perform service-specific pause operations.
        OnPause();

        // Tell SCM that the service is paused.
        SetServiceStatus(SERVICE_PAUSED);
    }
    catch (DWORD dwError)
    {
        // Log the error.
        WriteErrorLogEntry("Service Pause", dwError);

        // Tell SCM that the service is still running.
        SetServiceStatus(SERVICE_RUNNING);
    }
    catch (...)
    {
        // Log the error.
        WriteEventLogEntry("Service failed to pause", EVENTLOG_ERROR_TYPE);

        // Tell SCM that the service is still running.
        SetServiceStatus(SERVICE_RUNNING);
    }
}


//
//   FUNCTION: CServiceBase::OnPause()
//
//   PURPOSE: When implemented in a derived class, executes when a Pause
//   command is sent to the service by the SCM. Specifies actions to take
//   when a service pauses.
//
void CServiceBase::OnPause()
{
}


//
//   FUNCTION: CServiceBase::Continue()
//
//   PURPOSE: The function resumes normal functioning after being paused if
//   the service supports pause and continue. It calls the OnContinue virtual
//   function in which you can specify the actions to take when the service
//   continues. If an error occurs, the error will be logged in the
//   Application event log, and the service will still be paused.
//
void CServiceBase::Continue()
{
    try
    {
        // Tell SCM that the service is resuming.
        SetServiceStatus(SERVICE_CONTINUE_PENDING);

        // Perform service-specific continue operations.
        OnContinue();

        // Tell SCM that the service is running.
        SetServiceStatus(SERVICE_RUNNING);
    }
    catch (DWORD dwError)
    {
        // Log the error.
        WriteErrorLogEntry("Service Continue", dwError);

        // Tell SCM that the service is still paused.
        SetServiceStatus(SERVICE_PAUSED);
    }
    catch (...)
    {
        // Log the error.
        WriteEventLogEntry("Service failed to resume", EVENTLOG_ERROR_TYPE);

        // Tell SCM that the service is still paused.
        SetServiceStatus(SERVICE_PAUSED);
    }
}


//
//   FUNCTION: CServiceBase::OnContinue()
//
//   PURPOSE: When implemented in a derived class, OnContinue runs when a
//   Continue command is sent to the service by the SCM. Specifies actions to
//   take when a service resumes normal functioning after being paused.
//
void CServiceBase::OnContinue()
{
}


//
//   FUNCTION: CServiceBase::Shutdown()
//
//   PURPOSE: The function executes when the system is shutting down. It
//   calls the OnShutdown virtual function in which you can specify what
//   should occur immediately prior to the system shutting down. If an error
//   occurs, the error will be logged in the Application event log.
//
void CServiceBase::Shutdown()
{
    try
    {
        // Perform service-specific shutdown operations.
        OnShutdown();

        // Tell SCM that the service is stopped.
        SetServiceStatus(SERVICE_STOPPED);
    }
    catch (DWORD dwError)
    {
        // Log the error.
        WriteErrorLogEntry("Service Shutdown", dwError);
    }
    catch (...)
    {
        // Log the error.
        WriteEventLogEntry("Service failed to shut down", EVENTLOG_ERROR_TYPE);
    }
}


//
//   FUNCTION: CServiceBase::OnShutdown()
//
//   PURPOSE: When implemented in a derived class, executes when the system
//   is shutting down. Specifies what should occur immediately prior to the
//   system shutting down.
//
void CServiceBase::OnShutdown()
{
}

//
//   FUNCTION: CServiceBase::SetServiceStatus(DWORD, DWORD, DWORD)
//
//   PURPOSE: The function sets the service status and reports the status to
//   the SCM.
//
//   PARAMETERS:
//   * dwCurrentState - the state of the service
//   * dwWin32ExitCode - error code to report
//   * dwWaitHint - estimated time for pending operation, in milliseconds
//
void CServiceBase::SetServiceStatus(DWORD dwCurrentState,
                                    DWORD dwWin32ExitCode,
                                    DWORD dwWaitHint)
{
    static DWORD dwCheckPoint = 1;

    // Fill in the SERVICE_STATUS structure of the service.

    m_status.dwCurrentState = dwCurrentState;
    m_status.dwWin32ExitCode = dwWin32ExitCode;
    m_status.dwWaitHint = dwWaitHint;

    m_status.dwCheckPoint =
        ((dwCurrentState == SERVICE_RUNNING) || (dwCurrentState == SERVICE_STOPPED)) ?
            0 : dwCheckPoint++;

    // Report the status of the service to the SCM.
    ::SetServiceStatus(m_statusHandle, &m_status);
}


//
//   FUNCTION: CServiceBase::WriteEventLogEntry(PWSTR, WORD)
//
//   PURPOSE: Log a message to the Application event log.
//
//   PARAMETERS:
//   * pszMessage - string message to be logged.
//   * wType - the type of event to be logged. The parameter can be one of
//     the following values.
//
//     EVENTLOG_SUCCESS
//     EVENTLOG_AUDIT_FAILURE
//     EVENTLOG_AUDIT_SUCCESS
//     EVENTLOG_ERROR_TYPE
//     EVENTLOG_INFORMATION_TYPE
//     EVENTLOG_WARNING_TYPE
//
void CServiceBase::WriteEventLogEntry(const char* pszMessage, WORD wType)
{
    if(!service_log) return;

    HANDLE hEventSource = NULL;
    LPCWSTR lpszStrings[2] = { NULL, NULL };

    hEventSource = RegisterEventSource(NULL, (LPCSTR)m_name);
    if (hEventSource)
    {
        lpszStrings[0] = (LPCWSTR)m_name;
        lpszStrings[1] = (PWSTR)pszMessage;

        ReportEvent(hEventSource,  // Event log handle
            wType,                 // Event type
            0,                     // Event category
            0,                     // Event identifier
            NULL,                  // No security identifier
            2,                     // Size of lpszStrings array
            0,                     // No binary data
            (LPCSTR*)lpszStrings,           // Array of strings
            NULL                   // No binary data
            );

        DeregisterEventSource(hEventSource);
    } else {
        std::cout << "Error Writing To Event Log" << std::endl;
    }
}


//
//   FUNCTION: CServiceBase::WriteErrorLogEntry(PWSTR, DWORD)
//
//   PURPOSE: Log an error message to the Application event log.
//
//   PARAMETERS:
//   * pszFunction - the function that gives the error or a message
//   * dwError - the error code
//
DWORD CServiceBase::WriteErrorLogEntry(const char* message, DWORD dwError)
{
    /*wchar_t szMessage[260];
    StringCchPrintf((STRSAFE_LPSTR)szMessage, ARRAYSIZE(szMessage),
        (STRSAFE_LPSTR)L"%s failed w/err 0x%08lx", pszFunction, dwError);*/

    std::string msg = std::string(message) + " failed w/err 0x" + std::to_string(dwError);
    WriteEventLogEntry(msg.data(), EVENTLOG_ERROR_TYPE);
    return dwError;
}
