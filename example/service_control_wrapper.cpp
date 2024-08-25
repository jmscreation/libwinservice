#include "service_control_wrapper.h"


ServiceControlWrapper::ServiceControlWrapper(const char* pszServiceName, const ServiceCallbackList& callbacks, DWORD dwControl) : CServiceBase(pszServiceName, dwControl)
{
    m_fStopping = false;
    m_fPaused = false;
    m_fSignal = false;

    // Link the callback functions to their respective titles from the past callback list
    //  This will automatically default initialize missing callbacks to valid empty functions
    RegisterCallback(callbacks, "start", callback_start);
    RegisterCallback(callbacks, "update", callback_update);
    RegisterCallback(callbacks, "stopped", callback_stopped);
    RegisterCallback(callbacks, "paused", callback_paused);
    RegisterCallback(callbacks, "continue", callback_continue);
    RegisterCallback(callbacks, "shutdown", callback_shutdown);

    pauseTimeout = 2000; // 2000ms paused delay
    updateTimeout = 5; // 5ms service update delay

    // Create a manual-reset event that is not signaled at first to indicate
    // the stopped signal of the service.
    m_hStoppedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (m_hStoppedEvent == NULL)
    {
        throw GetLastError();
    }
}


ServiceControlWrapper::~ServiceControlWrapper(void)
{
    if (m_hStoppedEvent)
    {
        CloseHandle(m_hStoppedEvent);
        m_hStoppedEvent = NULL;
    }
}

// Service starts up
void ServiceControlWrapper::OnStart(DWORD dwArgc, PWSTR* lpszArgv)
{
    // Log a service start message to the Application log.
    WriteEventLogEntry("Example Service Starting...", EVENTLOG_INFORMATION_TYPE);

    // Queue the main service function for execution in a worker thread.
    CThreadPool::QueueUserWorkItem(&ServiceControlWrapper::ServiceWorkerThread, this);
}


// This is the service thread - continue looping until the service is stopped
void ServiceControlWrapper::ServiceWorkerThread(void)
{
    callback_start();
    while (!m_fStopping){ // Periodically check if the service is stopping.
        Sleep(updateTimeout); // Sleep for the update idle interval
        callback_update(); // Execute the service callback
        CheckForPause();   // Pause the worker thread if the system requested a pause
    }
    callback_stopped();

    // Signal the stopped event.
    SetEvent(m_hStoppedEvent);
}

// Attempt to stop the service by sending a stop signal and waiting for the service to terminate
void ServiceControlWrapper::OnStop()
{
    // Log a service stop message to the Application log.
    WriteEventLogEntry("Example Service Stopping...", EVENTLOG_INFORMATION_TYPE);

    // Indicate that the service is stopping and wait for the finish of the
    // main service function (ServiceWorkerThread).
    m_fStopping = true;
    if (WaitForSingleObject(m_hStoppedEvent, INFINITE) != WAIT_OBJECT_0)
    {
        throw GetLastError();
    }
}


// Attempt to pause the running service by sending a pause signal
void ServiceControlWrapper::OnPause()
{
    std::clock_t timeout = std::clock();
    m_fPaused = TRUE;
    while(!m_fSignal){
        if((std::clock() - timeout) / CLOCKS_PER_SEC > 4){
            m_fPaused = false;
            throw DWORD(ERROR_TIMEOUT);
        }
    }
    m_fSignal = false;
}


// Attempt to continue the paused service by sending a continue signal
void ServiceControlWrapper::OnContinue()
{
    std::clock_t timeout = std::clock();
    m_fPaused = false;
    while(!m_fSignal){
        if((std::clock() - timeout) / CLOCKS_PER_SEC > 4){
            m_fPaused = true;
            throw DWORD(ERROR_TIMEOUT);
        }
    }
    m_fSignal = false;
}


// OS initiated a shutdown - perform actions here for safe shutdown
void ServiceControlWrapper::OnShutdown()
{
    callback_shutdown();
    Stop(); // stop the running service
}


// Check the service for a pause signal - hold and block while service is paused
void ServiceControlWrapper::CheckForPause()
{
    //When service is paused
    if(m_fPaused){
        if(!m_fSignal){
            m_fSignal = true;   // tell service control manager that the thread has been paused
            callback_paused();
        }
        while(m_fPaused){   // wait until the service is un-paused
            Sleep(pauseTimeout);
            if(m_fStopping) return;  //if we are stopping when paused, return from the pause state
        }
        if(!m_fSignal){
            callback_continue();
            m_fSignal = true;   //tell service control manager that the thread has continued
        }
    }
}

// Internal method for registering the callbacks for the service on initialization
bool ServiceControlWrapper::RegisterCallback(const ServiceCallbackList& list, const std::string& name, ServiceCallback& dest_function) {
    if(!list.contains(name)){
        dest_function = [](){};
        return false;
    }

    dest_function = list.at(name);

    return true;
}