#pragma once
#include "libwinservice.h"

#include <functional>
#include <map>
#include <string>

using ServiceCallback = std::function<void()>;
using ServiceCallbackList = std::map<std::string, ServiceCallback>;

class ServiceControlWrapper : public CServiceBase
{
public:

    ServiceControlWrapper(const char* pszServiceName, const ServiceCallbackList& callbacks = {}, DWORD dwControl = SERVICE_ACCEPT_PAUSE_CONTINUE | SERVICE_ACCEPT_STOP);
    virtual ~ServiceControlWrapper(void);

protected:

    virtual void OnStart(DWORD dwArgc, PWSTR* pszArgv);
    virtual void OnStop();
    virtual void OnShutdown();
    virtual void OnPause();
    virtual void OnContinue();

    void ServiceWorkerThread(void);

    DWORD pauseTimeout, updateTimeout; // time to wait each pause cycle / update cycle

private:

    void CheckForPause();
    std::atomic<bool> m_fStopping, m_fPaused, m_fSignal;
    HANDLE m_hStoppedEvent;

    ServiceCallback callback_update, callback_stopped,
                    callback_paused, callback_continue,
                    callback_shutdown, callback_start;

    bool RegisterCallback(const ServiceCallbackList& list, const std::string& name, ServiceCallback& dest_function);
};
