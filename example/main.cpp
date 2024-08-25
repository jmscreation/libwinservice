#include "service_control_wrapper.h"
#include <map>
#include <string>
#include <vector>
#include <functional>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <queue>
#include <mutex>

#include "commctrl.h"
#include <tlhelp32.h>

#include "libwinservice_csd.h"
#include "clock.h"

std::string service_name = "libwinservice_example";
std::string service_displayname = "Example Service";
std::string service_description = "This is an example service registered via the example from the libwinservice library.";

std::string service_mailbox = service_name + "_service";
std::string process_mailbox = service_name + "_process";

bool debug_service = false;

void PrintTime() {
    SYSTEMTIME tm;

    GetSystemTime(&tm);

    std::cout << tm.wMonth << "/" << tm.wDay << "/" << tm.wYear << " " <<
        tm.wHour << ":" << tm.wMinute << ":" << tm.wSecond << "\n";
}

// find process ID by process name
int GetProcessID(const char *procname) {

  HANDLE hSnapshot;
  PROCESSENTRY32 pe;
  int pid = 0;
  BOOL hResult;

  // snapshot of all processes in the system
  hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (INVALID_HANDLE_VALUE == hSnapshot) return 0;

  // initializing size: needed for using Process32First
  pe.dwSize = sizeof(PROCESSENTRY32);

  // info about first process encountered in a system snapshot
  hResult = Process32First(hSnapshot, &pe);

  // retrieve information about the processes
  // and exit if unsuccessful
  while (hResult) {
    // if we find the process: return process ID
    if (strcmp(procname, pe.szExeFile) == 0) {
      pid = pe.th32ProcessID;
      break;
    }
    hResult = Process32Next(hSnapshot, &pe);
  }

  // closes an open handle (CreateToolhelp32Snapshot)
  CloseHandle(hSnapshot);
  return pid;
}

HANDLE GetProcessHandle(DWORD pid) {
    return OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
}


std::mutex mtx_message;
std::atomic_bool showingMessage = false;

const char* CLASS_NAME = "__CUSTOM_WNDCLASS";
HFONT hFont;
HINSTANCE hinstance = NULL;

LRESULT CALLBACK _WindowProcedure(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam); // forward decl

void _InitWindow() {
    hinstance = GetModuleHandle(NULL);
    WNDCLASS winClass;
    ZeroMemory(&winClass, sizeof(winClass));

    winClass.lpfnWndProc   = &_WindowProcedure;
    winClass.hInstance     = hinstance;
    winClass.lpszClassName = CLASS_NAME;

    RegisterClass(&winClass);

    hFont = CreateFont(64, 0, 0, 0, FW_DONTCARE, 0, 0, 0,
                       ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, NULL);
}

void _FreeWindow() {
    showingMessage = false;

    UnregisterClass(CLASS_NAME, hinstance);
    DeleteObject(hFont);
    hFont = NULL;

}

// Window procedure for dialog
LRESULT CALLBACK _WindowProcedure(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static HBRUSH wincolorbrush = NULL;
    switch(uMsg){
            case WM_CREATE:{
                wincolorbrush = CreateSolidBrush(RGB(40,40,40));
                return true;
            }
            case WM_INITDIALOG:{
                SetForegroundWindow(hwnd);
                return true;
            }
            case WM_PAINT:{
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                FillRect(hdc, &ps.rcPaint, wincolorbrush);
                EndPaint(hwnd, &ps);

                return (LRESULT)FALSE;
            }
            case WM_CTLCOLORSTATIC:{
                SetTextColor((HDC)wParam, RGB(210, 190, 200));
                SetBkMode((HDC)wParam, TRANSPARENT);
                return (LRESULT)wincolorbrush;
            }
            case WM_CLOSE:{
                DeleteObject(wincolorbrush);
                wincolorbrush = NULL;
                break;
            }
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void CustomWindowHandle(std::string msg, std::string caption) {
    std::scoped_lock lock(mtx_message);
    showingMessage = true;

    int x = 64, y = 64;
    int width = 512, height = 356;
    int xborder = 16, yborder = 16;

    HWND hWnd = CreateWindowEx( WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                            CLASS_NAME, caption.c_str(),
                            WS_POPUPWINDOW | WS_VISIBLE | WS_DLGFRAME,
                            x, y, width, height,
                            GetDesktopWindow(), NULL,
                            hinstance, NULL);
    if(hWnd == NULL){
        std::cout << GetLastError() << "\n";
        return;
    }

    HWND txtWnd = CreateWindowEx(0,
                    WC_STATIC, msg.c_str(),
                    WS_CHILD | WS_VISIBLE | SS_CENTER,
                    xborder, yborder, width - xborder * 2, height - yborder * 2,
                    hWnd, NULL,
                    hinstance, NULL);
    SendMessage (txtWnd, WM_SETFONT, WPARAM(hFont), TRUE);

    Clock timeout;

    MSG evMsg {};
    do { // message handler
        if(PeekMessage(&evMsg, NULL, 0, 0, PM_REMOVE) != 0){
            TranslateMessage(&evMsg);
            DispatchMessage(&evMsg);
        } else {
            Sleep(100); // deep sleep
        }
    } while(showingMessage && IsWindow(hWnd) && timeout.getSeconds() < 8);

    showingMessage = false;
    CloseWindow(hWnd);
    DestroyWindow(hWnd); // free memory
}

void CustomMessage(std::string msg, std::string caption = "") {
    if(showingMessage){
        showingMessage = false;
        std::scoped_lock lock(mtx_message);
    }

    std::thread t(CustomWindowHandle, msg, caption);
    t.detach();
}

HANDLE childProcess = NULL;
DWORD childPID = 0;

void SpawnProcess() {
    if(childProcess != NULL) return; // do not spawn another process

    std::string cmd;
    {
        TCHAR szPath[MAX_PATH];
        if (GetModuleFileName(NULL, (LPSTR)szPath, ARRAYSIZE(szPath)) == 0) return;
        cmd.assign(szPath);
    }
    cmd += " child " + std::to_string(GetCurrentProcessId());

    if(debug_service) cmd += " debug";

    HANDLE token {};

    { // hijack account login token
        HANDLE proc = GetProcessHandle(GetProcessID("winlogon.exe"));
        if(proc == NULL) {
            std::cout << "Failed to open winlogon.exe: " << GetLastError() << "\n";
            return;
        }
        if(!OpenProcessToken(proc, TOKEN_READ | TOKEN_IMPERSONATE | TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE, &token)){
            std::cout << "Failed to get process token: " << GetLastError() << "\n";
            return;
        }
        CloseHandle(proc);
    }

    Retry:

    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    std::cout << "CreateProcessAsUser with token: "<< token << "\n"
              << " Path: " << cmd << "\n";

    if( !CreateProcessAsUser(
        token,
        NULL,   // No module name (use command line)
        cmd.data(),    // Command line
        NULL,           // Process handle inheritable
        NULL,           // Thread handle not inheritable
        FALSE,          // Set handle inheritance to FALSE
        debug_service ? CREATE_NEW_CONSOLE : 0,  // Flags
        NULL,           // Use parent's environment block
        NULL,           // Use parent's starting directory 
        &si,            // Pointer to STARTUPINFO structure
        &pi)           // Pointer to PROCESS_INFORMATION structure
    ){
        std::cout << "CreateProcess failed (" << GetLastError() << ").\n";
        if(token == NULL) return;
        token = NULL;
        goto Retry;
    }

    childProcess = pi.hProcess;
    childPID = pi.dwProcessId;

    PrintTime();
    std::cout << "Child process " << childPID << " started\n";
}

void CloseProcess() {
    if(childProcess == NULL) return;

    TerminateProcess(childProcess, 0);
    childProcess = NULL;
}

bool CheckProcess() {
    DWORD code;
    if(!GetExitCodeProcess(childProcess, &code)){
        return false;
    }
    return code == STILL_ACTIVE;
}

// This is the child process runtime
void ChildProcess(std::vector<std::string>& args) {
    if(args.size() < 2) return;
    
    std::stringstream nil;
    if(args.size() < 3 || args.at(2) != "debug"){
        FreeConsole();
        std::cout.rdbuf(nil.rdbuf());
    }

    DWORD pid = 0;
    try {
        std::cout << "Parent PID: " << args.at(1) << "\n";
        pid = std::stoul(args.at(1));
    } catch(...) {
        std::cout << "Invalid Parent Process ID\n";
        return;
    }

    process_mailbox += std::to_string(GetCurrentProcessId()); // my pid
    service_mailbox += args.at(1); // service pid passed into program

    IPCController ipc; // incoming process / outgoing service direction
    ipc.InitializeInbox(process_mailbox);
    Sleep(200); // wait for IPC to register
    ipc.InitializeOutbox(service_mailbox);
    Sleep(1000); // wait for IPC to register

    if(!ipc.IsValid() || !ipc.IsValidOutbox()){
        std::cout << "IPC failed to initialize: " << ipc.LastError() << "\n";
        return;
    } else {
        std::cout << "IPC registered\n";
    }

    // { // authorize IPC
    //     std::string auth {};
    //     int retry = 10;
    //     while(!ipc.Receive(auth) && --retry){
    //         Sleep(200);
    //     }

    //     if(auth != service_mailbox){
    //         std::cout << "IPC failed authorization\n";
    //         std::cout << auth << " != " << service_mailbox << "\n";
    //         // return; // if the IPC authentication doesn't match, then fail immediately
    //     }
    // }

    bool running = true;
    while(running){
        if(GetAsyncKeyState(VK_F7) & 0x8000){
            CustomMessage("F7 Key Pressed");
            Sleep(100);
        }
        std::vector<std::string> parts;
        std::string msg;
        if(ipc.Receive(msg)){
            std::string* next = &parts.emplace_back();
            for(char c : msg){
                if(c == ';'){
                    next = &parts.emplace_back();
                    continue;
                }
                (*next) += c;
            }

            std::cout << "parts: " << parts.size() << "\n";

            if(!ipc.Send("echo test message")){
                std::cout << " -- failed to send message\n";
            }

            if(parts[0] == "exit"){
                running = false;
            } else {
                CustomMessage(parts[0]);
            }
            for(int i=1; i < parts.size(); ++i){
                std::cout << "Data[" << i << "]: " << parts[i] << "\n";
            }
        }
        Sleep(3);
    }
}

int main(int argc, char *argv[]) {
    std::vector<std::string> args; for(int i=1; i < argc; ++i) args.emplace_back(argv[i]);

    std::map<std::string, std::function<void()>> commands {
        { "install", [&](){
                std::cout << "Installing Service..." << std::endl;
                if(InstallService(
                    service_name.c_str(),               // Name of service
                    service_displayname.c_str(),        // Name to display
                    service_description.c_str(),        // Description
                    SERVICE_AUTO_START, SERVICE_ERROR_NORMAL
                )){
                    std::cout << "Starting Service" << std::endl;
                    StartService(service_name.c_str());
                }
            }
        },
        { "remove", [&](){
                UninstallService(service_name.c_str());
            }
        },
        { "start", [&](){    
                if(!ServiceInstalled(service_name.c_str())){
                    std::cout << "Service Not Installed" << std::endl;
                } else {
                    std::cout << "Starting Service" << std::endl;
                    StartService(service_name.c_str());
                }
            }
        },
        { "debug_ipc", [&](){
                service_mailbox += std::to_string(GetCurrentProcessId());

                std::cout << "Debugging IPC... Spawn process...\n";
                SpawnProcess();

                process_mailbox += std::to_string(childPID);

                std::cout << "Setting up IPC as service...\n";
                IPCController ipc(service_mailbox, process_mailbox);

                std::cout << "IPC Details:\n"
                          << " inbox: " << service_mailbox << "\n"
                          << " outbox: " << process_mailbox << "\n";

                if(!ipc.IsValid()){
                    std::cout << "IPC failed to initialize:" << ipc.LastError() << "\n";
                } else {
                    std::cout << "Registered Mailslot\n";
                }

                std::cout << "Wait for process launch...\n";
                Sleep(1000);

                std::cout << "Sending Messages...\n";
                for(int i=0; i < 3; ++i){
                    if(!ipc.Send("Debug Message " + std::to_string(i) + "\n" + std::string(20, '.') + "\n-----")){
                        std::cout << "- failed: " << ipc.LastError() << "\n";
                    }
                    Sleep(2000);
                }

                Sleep(1000);
                std::cout << "Reading Messages As Service...\n";
                
                for(int i=0; i < 3; ++i){
                    std::string msg;
                    bool success = false;
                    while(ipc.Receive(msg)){
                        std::cout << msg << "\n";
                        success = true;
                    }
                    if(success) break;

                    std::cout << "- no messages or failed\n";
                    Sleep(2000);
                }

                Sleep(1000);
                std::cout << "Send Safe Exit Message...\n";
                ipc.Send("exit");

                Sleep(1000);
                if(CheckProcess()){
                    std::cout << "Force closing child process...\n";
                    CloseProcess();
                }

                std::cout << "IPC encountered " << ipc.ErrorCount() << " errors\n";
                std::cout << "Exiting...\n";
            }
        },
        { "debug_samem", [&](){
                std::cout << "Debug Security Attributes memory leak...\n";
                Sleep(3000);
                size_t repeat = 999999;
                std::cout << "Generating " << repeat << " Security Descriptors...\n";
                for(int i=0; i < repeat; ++i){
                    SECURITY_ATTRIBUTES x = CreateSecurityAttribute();
                    if(x.lpSecurityDescriptor == NULL){
                        std::cout << GetLastError() << " Error\n";
                        break;
                    }
                    FreeSecurityAttribute(&x);
                }
                std::cout << "memory leak detection...\n";
                Sleep(90000);
            }
        },
        
        { "child", [&](){
                ChildProcess(args);
            }
        },
    };

    if(args.size()){
        // Non-service executable
        _InitWindow();

        for(auto& [command, func] : commands){
            if(std::find(args.begin(), args.end(), command) != args.end()){
                func();
                return 0;
            }
        }

        _FreeWindow();

        std::cout << "Invalid Operation\n";
    } else {

        IPCController ipc;

        ServiceControlWrapper service(service_name.c_str(),
            {
                  { "start", [&](){
                        PrintTime();
                        std::cout << "Service started\n";
                        service_mailbox += std::to_string(GetCurrentProcessId());
                        ipc.InitializeInbox(service_mailbox);

                        SpawnProcess();
                        Sleep(500);
                        if(CheckProcess()){
                            ipc.InitializeOutbox(process_mailbox + std::to_string(childPID));
                            Sleep(300);
                        }
                        ipc.Send("Service Started");
                    }
                },{ "update", [&](){
                        if(!CheckProcess()){
                            childProcess = NULL;
                            childPID = 0;
                            Sleep(2000); // deep sleep

                            SpawnProcess();
                            Sleep(1000);
                            if(CheckProcess()){
                                ipc.InitializeOutbox(process_mailbox + std::to_string(childPID));
                            }
                        }
                        std::string msg;
                        if(ipc.Receive(msg)){
                            std::cout << "message: " << msg << "\n";
                        }

                        std::cout.flush(); // flush data
                    }
                },{ "stopped", [&](){
                        ipc.Send("Service Stopped");
                        Sleep(1500);

                        PrintTime();
                        std::cout << "Service stopping...\n";

                        ipc.Reset();
                        CloseProcess();
                    }
                },{ "paused", [&](){
                        ipc.Send("Service Paused;" + std::string(256, 'X'));
                    }
                },{ "continue", [&](){
                        ipc.Send("Service Resumed");
                    }
                },{ "shutdown", [&](){
                        ipc.Send("System Shutdown Detected");
                    }
                },
            },
            SERVICE_ACCEPT_PAUSE_CONTINUE | SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN
        );

        std::streambuf* orig_cout = std::cout.rdbuf();
        std::string path(getenv("SystemDrive"));
        path += "\\example_service.log";

        std::ofstream log(path, std::ios::app | std::ios::out);
        std::cout.rdbuf(log.rdbuf());

        DWORD errorCode;
        if (!CServiceBase::Run(service, errorCode)){
            std::cout.rdbuf(orig_cout); // put cout back

            std::cout << "Parameters:\n"
                      << " install  to install the service.\n"
                      << " remove   to remove the service.\n"
                      << " start   to start the service.\n";
        }
    }

    return 0;
}
