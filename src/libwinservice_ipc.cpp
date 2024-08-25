#include "libwinservice.h"
#include "libwinservice_csd.h"

IPCController::IPCController(const std::string& id_inbox, const std::string& id_outbox):
    ipc_valid(false), ipc_valid_inbox(false), ipc_valid_outbox(false), ipc_running(false),
    ipc_sa(CreateSecurityAttribute()),
    last_error(0), error_count(0), ipc_inbox_enabled(false), ipc_outbox_enabled(false)
{
    InitializeInbox(id_inbox);
    InitializeOutbox(id_outbox);

    ipc_thread = std::thread(&IPCHandle, this);
}

IPCController::IPCController():
    mailslot_in(INVALID_HANDLE_VALUE), mailslot_out(INVALID_HANDLE_VALUE),
    ipc_sa(CreateSecurityAttribute()),
    ipc_valid(false), ipc_valid_inbox(false), ipc_valid_outbox(false), ipc_running(false),
    last_error(0), error_count(0), ipc_inbox_enabled(false), ipc_outbox_enabled(false)
{
    ipc_thread = std::thread(&IPCHandle, this);
}

IPCController::~IPCController() {
    ipc_valid = false;

    if(ipc_thread.joinable()){
        ipc_running = false;
        ipc_thread.join(); // wait for IPC thread to terminate
    }

    ipc_valid_inbox = false;
    ipc_valid_outbox = false;

    if(mailslot_in != INVALID_HANDLE_VALUE) CloseHandle(mailslot_in);
    if(mailslot_out != INVALID_HANDLE_VALUE) CloseHandle(mailslot_out);

    FreeSecurityAttribute(&ipc_sa);
}

bool IPCController::Initialize(const std::string& id_inbox, const std::string& id_outbox) {
    return InitializeInbox(id_inbox) && InitializeOutbox(id_outbox);
}

bool IPCController::InitializeInbox(const std::string& id_inbox) {
    if(ipc_valid_inbox){
        CloseHandle(mailslot_in);
        mailslot_in = INVALID_HANDLE_VALUE;
        ipc_valid_inbox = false;
    }

    if(!id_inbox.empty()){
        mailslot_in_str = IPC_MAILSLOT_HEADER + id_inbox;
    }

    ipc_inbox_enabled = true;

    mailslot_in = CreateMailslot(mailslot_in_str.c_str(), 0, MAILSLOT_WAIT_FOREVER, &ipc_sa);
    if(mailslot_in == INVALID_HANDLE_VALUE){
        IPCReportError();
        return false;
    }

    ipc_valid_inbox = true;
    ipc_valid = true;

    return true;
}

bool IPCController::InitializeOutbox(const std::string& id_outbox) {
    if(ipc_valid_outbox){
        CloseHandle(mailslot_out);
        mailslot_out = INVALID_HANDLE_VALUE;
        ipc_valid_outbox = false;
    }

    if(!id_outbox.empty()){
        mailslot_out_str = IPC_MAILSLOT_HEADER + id_outbox;
    }

    ipc_outbox_enabled = true;

    mailslot_out = CreateFile(mailslot_out_str.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if(mailslot_out == INVALID_HANDLE_VALUE){
        IPCReportError();
        return false;
    }

    ipc_valid_outbox = true;
    ipc_valid = true;
    return true;
}

void IPCController::DisableInbox() {
    if(ipc_valid_inbox){
        CloseHandle(mailslot_in);
        mailslot_in = INVALID_HANDLE_VALUE;
        ipc_valid_inbox = false;
    }
    ipc_inbox_enabled = false;
}

void IPCController::DisableOutbox() {
    if(ipc_valid_outbox){
        CloseHandle(mailslot_out);
        mailslot_out = INVALID_HANDLE_VALUE;
        ipc_valid_outbox = false;
    }
    ipc_outbox_enabled = false;
}

void IPCController::Reset() {
    DisableInbox();
    DisableOutbox();
    ClearSend();
    ClearReceive();

    ipc_valid = false;
    ipc_valid_inbox = false;
    ipc_valid_outbox = false;    
    last_error = 0;
    error_count = 0;
    ipc_inbox_enabled = false;
    ipc_outbox_enabled = false;
}

bool IPCController::Send(const std::string& data) {
    if(!ipc_valid) return false;
    std::scoped_lock lock(mtx_outgoing_messages);

    outgoing_messages.emplace(data);
    return true;
}

bool IPCController::Receive(std::string& data) {
    if(!ipc_valid || incoming_messages.empty()) return false;
    std::scoped_lock lock(mtx_incoming_messages);

    data = incoming_messages.front();
    incoming_messages.pop();
    return true;
}

bool IPCController::Peek(std::string& data) {
    if(!ipc_valid || incoming_messages.empty()) return false;
    std::scoped_lock lock(mtx_incoming_messages);

    data = incoming_messages.front();
    return true;
}



// Internal Methods

void IPCController::IPCReportError() {
    last_error = GetLastError();
    error_count++;
}

// IPC Thread Handle
void IPCController::IPCHandle() {
    ipc_running = true;

    while(ipc_running){
        if(ipc_inbox_enabled){
            if(!ipc_valid_inbox){
                if(!InitializeInbox()) Sleep(100); // idle timeouts
            }
            IPCReadData(); // process incoming messages
        }

        if(ipc_outbox_enabled){
            if(!ipc_valid_outbox){
                if(!InitializeOutbox()) Sleep(100); // idle timeouts
            }
            IPCWriteData(); // process outgoing messages
        }
        Sleep(25); // idle sleep
    }
}


// Write Data to connected mailslot
bool IPCController::IPCWriteData() {
    std::scoped_lock lock(mtx_outgoing_messages);
    if(!ipc_valid_outbox || outgoing_messages.empty()) return false;

    for(const std::string& data = outgoing_messages.front(); !outgoing_messages.empty(); outgoing_messages.pop()){
        DWORD written;
        if(!WriteFile(mailslot_out, (LPCVOID)data.c_str(), (DWORD)data.size(), &written, NULL)){ 
            IPCReportError();
            ipc_valid_outbox = false;
            return false;
        }
    }
    return true;
}

// Read Data from connected mailslot
bool IPCController::IPCReadData() {
    std::scoped_lock lock(mtx_incoming_messages);

    if(!ipc_valid_inbox) return false;

    DWORD szNextMsg, szMsg;
    DWORD timeout = 2000;
    char cacheBuf[BUFSIZE] {};
    std::stringstream msgCache;

    szNextMsg = szMsg = 0;

    if(!GetMailslotInfo( mailslot_in, (LPDWORD) NULL, &szNextMsg, &szMsg, &timeout)){
        IPCReportError();
        return false;
    }

    if (szNextMsg == MAILSLOT_NO_MESSAGE) return false;

    while(szMsg != 0){
        DWORD totalBytes = 0;
        DWORD retryCount = 4;
        bool alloc = szNextMsg > BUFSIZE;

        char* pCache = alloc ? new char[szNextMsg] : cacheBuf;

        do {
            DWORD bytes;
            if(!ReadFile(mailslot_in, pCache, szNextMsg, &bytes, NULL)){
                IPCReportError();
                if(alloc) delete[] pCache;
                return false;
            }
            if(bytes == 0){
                if(--retryCount == 0){
                    IPCReportError();
                    if(alloc) delete[] pCache;
                    return false;
                }
                Sleep(100);
                continue;
            }

            msgCache.write(pCache, bytes);
            totalBytes += bytes;
        } while(totalBytes < szMsg); // keep reading until message comes through

        if(alloc) delete[] pCache;

        incoming_messages.emplace(msgCache.str());
        msgCache.str("");
 
        if(!GetMailslotInfo( mailslot_in, (LPDWORD) NULL, &szNextMsg, &szMsg, &timeout)){
            IPCReportError();
            ipc_valid_inbox = false;
            return false;
        }

        if (szNextMsg == MAILSLOT_NO_MESSAGE) break;
    }

    return true;
}