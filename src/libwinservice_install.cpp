#include "libwinservice.h"
//   FUNCTION: InstallService
//
//   PURPOSE: Install the current application as a service to the local
//   service control manager database.
//
//   PARAMETERS:
//   * pszServiceName - the name of the service to be installed
//   * pszDisplayName - the display name of the service
//   * dwStartType - the service start option. This parameter can be one of
//     the following values: SERVICE_AUTO_START, SERVICE_BOOT_START,
//     SERVICE_DEMAND_START, SERVICE_DISABLED, SERVICE_SYSTEM_START.
//   * dwErrorControl - the service error control type. This parameter can be one
//     of the following values: SERVICE_ERROR_IGNORE, SERVICE_ERROR_NORMAL, SERVICE_ERROR_CRITICAL, SERVICE_ERROR_SEVERE
//   * pszDependencies - a pointer to a double null-terminated array of null-
//     separated names of services or load ordering groups that the system
//     must start before this service.
//   * pszAccount - the name of the account under which the service runs.
//   * pszPassword - the password to the account name.
//
//   RETURN:
//   bool - Success status on service installation
//
//   NOTE: If the function fails to install the service, it prints the error
//   in the standard output stream for users to diagnose the problem.
//
bool InstallService(const char* pszServiceName,
                    const char* pszDisplayName,
                    const char* pszDescription,
                    DWORD dwStartType,
                    DWORD dwErrorControl,
                    const char* pszDependencies,
                    const char* pszAccount,
                    const char* pszPassword)
{
    bool success = false;
    TCHAR szPath[MAX_PATH];

    SC_HANDLE schSCManager = NULL;
    SC_HANDLE schService = NULL;
    SERVICE_DESCRIPTION description = {LPSTR("")};
    SERVICE_FAILURE_ACTIONS recoveryOptions;
    SC_ACTION actions[3];

    if (GetModuleFileName(NULL, (LPSTR)szPath, ARRAYSIZE(szPath)) == 0)
    {
        std::cout << "GetModuleFileName failed w/err 0x" << GetLastError() << "\n";
        goto Cleanup;
    }

    // Open the local default service control manager database
    schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    if (schSCManager == NULL)
    {
        std::cout << "OpenSCManager failed w/err 0x" << GetLastError() << "\n";
        goto Cleanup;
    }

    // Install the service into SCM by calling CreateService
    schService = CreateService(
        schSCManager,                   // SCManager database
        TEXT(pszServiceName),           // Name of service
        TEXT(pszDisplayName),           // Name to display
        SERVICE_ALL_ACCESS,             // Desired access
        SERVICE_WIN32_OWN_PROCESS,      // Service type
        dwStartType,                    // Service start type
        dwErrorControl,                 // Error control type
        szPath,                         // Service's binary
        NULL,                           // No load ordering group
        NULL,                           // No tag identifier
        TEXT(pszDependencies),          // Dependencies
        TEXT(pszAccount),               // Service running account
        TEXT(pszPassword)               // Password of the account
        );
    if (schService == NULL)
    {
        std::cout << "CreateService failed w/err 0x" << GetLastError() << "\n";
        goto Cleanup;
    }

    //Set description of the service
    description = {
        LPSTR(pszDescription)
    };
    ChangeServiceConfig2(schService, SERVICE_CONFIG_DESCRIPTION, &description);

    //Set the failure/recovery options for the service
    for(int i=0; i<3; i++){
        actions[i].Type =   SC_ACTION_RESTART;  //Restart service on fail
        actions[i].Delay = 5000;    //5 second delay
    }
    recoveryOptions = {
        3000,           //Reset the failure count after this number of seconds. Use INFINITE to never reset.
        LPSTR(""),      //Command path/file to execute on failure
        LPSTR(""),      //Reboot message displayed to user when system reboots
        3,              //Number of actions
        actions         //Actions to perform
    };
    ChangeServiceConfig2(schService, SERVICE_CONFIG_FAILURE_ACTIONS, &recoveryOptions);

    std::cout << pszServiceName << " is installed\n";
    success = true;

Cleanup:
    // Centralized cleanup for all allocated resources.
    if (schSCManager)
    {
        CloseServiceHandle(schSCManager);
        schSCManager = NULL;
    }
    if (schService)
    {
        CloseServiceHandle(schService);
        schService = NULL;
    }
    return success;
}

//
//   FUNCTION: ServiceInstalled
//
//   PURPOSE: Check if the service exists on the local service control
//   manager database.
//
//   PARAMETERS:
//   * pszServiceName - the name of the service to start.
//
//   RETURN:
//   bool - Existence of the service
//
//   NOTE: If the function fails to start the service, it prints the
//   error in the standard output stream for users to diagnose the problem.
//
bool ServiceInstalled(const char* pszServiceName)
{
    SC_HANDLE schSCManager = NULL;
    SC_HANDLE schService = NULL;
    SERVICE_STATUS ssSvcStatus = {};
    bool serviceInstalled = FALSE;

    // Open the local default service control manager database
    schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (schSCManager == NULL)
    {
        std::cout << "OpenSCManager failed w/err 0x" << GetLastError() << "\n";
        goto Cleanup;
    }

    // Open the service with start and query status permissions
    schService = OpenService(schSCManager, (LPCSTR)pszServiceName, SERVICE_QUERY_STATUS);
    if (schService == NULL)
    {
        goto Cleanup;
    }

    if(QueryServiceStatus(schService, &ssSvcStatus)){
        serviceInstalled = TRUE;
    }

Cleanup:
    // Centralized cleanup for all allocated resources.
    if (schSCManager)
    {
        CloseServiceHandle(schSCManager);
        schSCManager = NULL;
    }
    if (schService)
    {
        CloseServiceHandle(schService);
        schService = NULL;
    }
    return serviceInstalled;
}



//
//   FUNCTION: StartService
//
//   PURPOSE: Start the service on the local service control
//   manager database.
//
//   PARAMETERS:
//   * pszServiceName - the name of the service to start.
//
//   NOTE: If the function fails to start the service, it prints the
//   error in the standard output stream for users to diagnose the problem.
//
void StartService(const char* pszServiceName)
{
    SC_HANDLE schSCManager = NULL;
    SC_HANDLE schService = NULL;
    SERVICE_STATUS ssSvcStatus = {};

    // Open the local default service control manager database
    schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (schSCManager == NULL)
    {
        std::cout << "OpenSCManager failed w/err 0x" << GetLastError() << "\n";
        goto Cleanup;
    }

    // Open the service with start and query status permissions
    schService = OpenService(schSCManager, (LPCSTR)pszServiceName, SERVICE_START | SERVICE_QUERY_STATUS);
    if (schService == NULL)
    {
        std::cout << "OpenService failed w/err 0x" << GetLastError() << "\n";
        goto Cleanup;
    }
    //Check if service is not already running
    QueryServiceStatus(schService, &ssSvcStatus);
    if(ssSvcStatus.dwCurrentState != SERVICE_STOPPED)
    {
        std::cout << "Service already running\n";
        goto Cleanup;
    }

    // Try to start the service
    if (StartService(schService, 0, NULL))
    {
        std::cout << "Starting " << pszServiceName << ".";
        Sleep(1000);

        while (QueryServiceStatus(schService, &ssSvcStatus))
        {
            if (ssSvcStatus.dwCurrentState == SERVICE_START_PENDING)
            {
                std::cout << ".";
                Sleep(1000);
            }
            else break;
        }

        if (ssSvcStatus.dwCurrentState == SERVICE_RUNNING)
        {
            std::cout << "\n" << pszServiceName << " has started\n";
        }
        else
        {
            std::cout << "\n" << pszServiceName << " failed to start. Check the event log\n";
        }
    } else {
        std::cout << "\n" << pszServiceName << " failed to start. Error: 0x" << GetLastError() << "\n";
    }

Cleanup:
    // Centralized cleanup for all allocated resources.
    if (schSCManager)
    {
        CloseServiceHandle(schSCManager);
        schSCManager = NULL;
    }
    if (schService)
    {
        CloseServiceHandle(schService);
        schService = NULL;
    }
}

//
//   FUNCTION: UninstallService
//
//   PURPOSE: Stop and remove the service from the local service control
//   manager database.
//
//   PARAMETERS:
//   * pszServiceName - the name of the service to be removed.
//
//   NOTE: If the function fails to uninstall the service, it prints the
//   error in the standard output stream for users to diagnose the problem.
//
void UninstallService(const char* pszServiceName)
{
    SC_HANDLE schSCManager = NULL;
    SC_HANDLE schService = NULL;
    SERVICE_STATUS ssSvcStatus = {};

    // Open the local default service control manager database
    schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (schSCManager == NULL)
    {
        std::cout << "OpenSCManager failed w/err 0x" << GetLastError() << "\n";
        goto Cleanup;
    }

    // Open the service with delete, stop, and query status permissions
    schService = OpenService(schSCManager, (LPCSTR)pszServiceName, SERVICE_STOP |
        SERVICE_QUERY_STATUS | DELETE);
    if (schService == NULL)
    {
        std::cout << "OpenService failed w/err 0x" << GetLastError() << "\n";
        goto Cleanup;
    }

    // Try to stop the service
    if (ControlService(schService, SERVICE_CONTROL_STOP, &ssSvcStatus))
    {
        std::cout << "Stopping " << pszServiceName << ".";
        Sleep(1000);

        while (QueryServiceStatus(schService, &ssSvcStatus))
        {
            if (ssSvcStatus.dwCurrentState == SERVICE_STOP_PENDING)
            {
                std::cout << ".";
                Sleep(1000);
            }
            else break;
        }

        if (ssSvcStatus.dwCurrentState == SERVICE_STOPPED)
        {
            std::cout << "\n" << pszServiceName << " is stopped\n";
        }
        else
        {
            std::cout << "\n" << pszServiceName << " failed to stop\n";
        }
    }

    // Now remove the service by calling DeleteService.
    if (!DeleteService(schService))
    {
        std::cout << "DeleteService failed w/err 0x" << GetLastError() << "\n";
        goto Cleanup;
    }

    std::cout << pszServiceName << " is removed\n";

Cleanup:
    // Centralized cleanup for all allocated resources.
    if (schSCManager)
    {
        CloseServiceHandle(schSCManager);
        schSCManager = NULL;
    }
    if (schService)
    {
        CloseServiceHandle(schService);
        schService = NULL;
    }
}
