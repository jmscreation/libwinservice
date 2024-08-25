#pragma once
#include "libwinservice.h"

#include <string>
#include <queue>
#include <atomic>
#include <thread>
#include <mutex>
#include <sstream>

#define IPC_MAILSLOT_HEADER "\\\\.\\mailslot\\"
constexpr size_t BUFSIZE = 4096; // incoming cache size

class IPCController {
    std::string mailslot_in_str, mailslot_out_str;
    HANDLE mailslot_in, mailslot_out;

    SECURITY_ATTRIBUTES ipc_sa;

    std::atomic_bool ipc_valid, ipc_valid_inbox, ipc_valid_outbox, ipc_running,
                     ipc_inbox_enabled, ipc_outbox_enabled;
    int last_error;
    size_t error_count;
    
    std::thread ipc_thread;
    std::mutex mtx_outgoing_messages, mtx_incoming_messages;

    std::queue<std::string> outgoing_messages, incoming_messages;
public:
    IPCController();
    IPCController(const std::string& id_inbox, const std::string& id_outbox);
    virtual ~IPCController();

    bool Initialize(const std::string& id_inbox, const std::string& id_outbox);
    bool InitializeInbox(const std::string& id_inbox = "");
    bool InitializeOutbox(const std::string& id_outbox = "");

    bool IsValid() const { return ipc_valid; }
    bool IsValidInbox() const { return ipc_valid_inbox; }
    bool IsValidOutbox() const { return ipc_valid_outbox; }
    bool LastError() const { return last_error; }
    bool ErrorCount() const { return error_count; }

    bool Send(const std::string& data); // queue up a message to be sent
    bool Receive(std::string& data);    // read 1 message from incoming queue
    bool Peek(std::string& data);       // peek at next message without dequeing

    void DisableInbox();
    void DisableOutbox();
    void Reset();
    void ClearSend() { outgoing_messages = {}; }
    void ClearReceive() { incoming_messages = {}; }

private:
    void IPCReportError();
    void IPCHandle();
    bool IPCWriteData();
    bool IPCReadData();
};