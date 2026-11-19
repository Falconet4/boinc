// This file is part of BOINC.
// https://boinc.berkeley.edu
// Copyright (C) 2026 University of California
//
// BOINC is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// BOINC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with BOINC.  If not, see <http://www.gnu.org/licenses/>.

#ifndef BOINC_ASYNCRPC_H
#define BOINC_ASYNCRPC_H

#include <string>

#ifndef __WXMAC__

#define BOINC_Condition wxCondition
#define BOINC_Mutex wxMutex

#else

// Adapted from wxMac-2.8.10
#include <pthread.h>


#if 1
#define BOINC_Mutex wxMutex
#define BOINC_Condition wxCondition
#else
class BOINC_Mutex
{
public:
    BOINC_Mutex( wxMutexType mutexType = wxMUTEX_DEFAULT );
    ~BOINC_Mutex();

    wxMutexError Lock();
    wxMutexError TryLock();
    wxMutexError Unlock();

    bool IsOk() const
    { return m_isOk; }

private:
    pthread_mutex_t m_mutex;
    bool m_isOk;

    // BOINC_Condition uses our m_mutex
    friend class BOINC_Condition;
};


// Adapted from wxMac-2.8.10 but using native pthread_cond_*()
class BOINC_Condition
{
public:
    BOINC_Condition(BOINC_Mutex& mutex);
    ~BOINC_Condition();
    bool IsOk() const { return (m_BOINC_Mutex.IsOk() && mb_initOK); }
    wxCondError Wait();
    wxCondError WaitTimeout(unsigned long milliseconds);
    void Signal();
    void Broadcast();

private:
    BOINC_Mutex&                m_BOINC_Mutex;
    pthread_cond_t              m_cond;
    bool                        mb_initOK;

    DECLARE_NO_COPY_CLASS(BOINC_Condition)
};

#endif
#endif

class CMainDocument;    // Forward declaration

enum RPC_SELECTOR {
    RPC_AUTHORIZE = 1,
    RPC_EXCHANGE_VERSIONS,
    RPC_GET_STATE,
    RPC_GET_RESULTS,
    RPC_GET_FILE_TRANSFERS,
    RPC_GET_SIMPLE_GUI_INFO1,
    RPC_GET_SIMPLE_GUI_INFO2,
    RPC_GET_PROJECT_STATUS1,
    RPC_GET_PROJECT_STATUS2,
    RPC_GET_ALL_PROJECTS_LIST,              // 10
    RPC_GET_DISK_USAGE,
    RPC_PROJECT_OP,
    RPC_SET_RUN_MODE,
    RPC_SET_GPU_MODE,
    RPC_SET_NETWORK_MODE,
    RPC_GET_SCREENSAVER_TASKS,
    RPC_RUN_BENCHMARKS,
    RPC_SET_PROXY_SETTINGS,
    RPC_GET_PROXY_SETTINGS,
    RPC_GET_MESSAGES,                       // 20
    RPC_FILE_TRANSFER_OP,
    RPC_RESULT_OP,
    RPC_GET_HOST_INFO,
    RPC_QUIT,
    RPC_ACCT_MGR_INFO,
    RPC_GET_STATISTICS,
    RPC_NETWORK_AVAILABLE,
    RPC_GET_PROJECT_INIT_STATUS,
    RPC_GET_PROJECT_CONFIG,
    RPC_GET_PROJECT_CONFIG_POLL,            // 30
    RPC_LOOKUP_ACCOUNT,
    RPC_LOOKUP_ACCOUNT_POLL,
    RPC_CREATE_ACCOUNT,
    RPC_CREATE_ACCOUNT_POLL,
    RPC_PROJECT_ATTACH,
    RPC_PROJECT_ATTACH_FROM_FILE,
    RPC_PROJECT_ATTACH_POLL,
    RPC_ACCT_MGR_RPC,
    RPC_ACCT_MGR_RPC_POLL,
    RPC_GET_NEWER_VERSION,                  // 40
    RPC_READ_GLOBAL_PREFS_OVERRIDE,
    RPC_READ_CC_CONFIG,
    RPC_GET_CC_STATUS,
    RPC_GET_GLOBAL_PREFS_FILE,
    RPC_GET_GLOBAL_PREFS_WORKING,
    RPC_GET_GLOBAL_PREFS_WORKING_STRUCT,
    RPC_GET_GLOBAL_PREFS_OVERRIDE,
    RPC_SET_GLOBAL_PREFS_OVERRIDE,
    RPC_GET_GLOBAL_PREFS_OVERRIDE_STRUCT,
    RPC_SET_GLOBAL_PREFS_OVERRIDE_STRUCT,   // 50
    RPC_GET_NOTICES,
    RPC_GET_CC_CONFIG,
    RPC_SET_CC_CONFIG,
	RPC_SET_LANGUAGE,
    NUM_RPC_SELECTORS
};


enum ASYNC_RPC_TYPE {
    // Demand RPC: wait for completion before returning (usually
    // a user-initiated request.)
    RPC_TYPE_WAIT_FOR_COMPLETION = 1,
    // Periodic RPC: post request on queue and return immediately
    // (requested due to a timer interrupt.)
    RPC_TYPE_ASYNC_NO_REFRESH,
    // Periodic RPC as above, but on completion also process a
    // wxEVT_FRAME_REFRESHVIEW event to refresh the display.
    RPC_TYPE_ASYNC_WITH_REFRESH_AFTER,
    // Periodic RPC as above, but on completion also process a
    // wxEVT_FRAME_REFRESHVIEW event to refresh the display.
    RPC_TYPE_ASYNC_WITH_REFRESH_EVENT_LOG_AFTER,
    // Periodic RPC as above, but on completion also process a
    // wxEVT_TASKBAR_REFRESH event to refresh the taskbar icon.
    RPC_TYPE_ASYNC_WITH_UPDATE_TASKBAR_ICON_AFTER,
    NUM_RPC_TYPES
};

// Pass the following structure to CMainDocument::RequestRPC()
// The members are as follows:
//
//   arg1 is usually the buffer to read into
//
//   exchangeBuf is the (optional) buffer to exchange with after
//     completing the RPC, the buffer used by the Manager code.
//     Pass NULL if you don't want the buffer exchanged.
//
//  arg2, arg3, arg4 are additional arguments when needed by the
//      RPC call; their usage varies for different RPC requests.
//
//  rpcType is as described above
//
//  completionTime is a pointer to a wxDateTime variable into which
//      to write the completion time of the RPC.  It may be NULL.
//
//  resultPtr is a pointer to an int into which to write the result
//      returned by the RPC call.  It may be NULL.
//
//  retval is for internal use by the async RPC logic; do not use.
//
//  isActive is for internal use by the async RPC logic; do not use.
//

struct ASYNC_RPC_REQUEST {
    RPC_SELECTOR which_rpc;
    void *arg1;
    void *exchangeBuf;
    void *arg2;
    void *arg3;
    void *arg4;
    ASYNC_RPC_TYPE rpcType;
    wxDateTime *completionTime;
    double *RPCExecutionTime;
    int *resultPtr;
    int retval;
    bool isActive;

    // Owned copies of the arguments the RPC thread reads.  A request is
    // copied into the queue and again into CMainDocument::current_rpc_request,
    // so a raw pointer to the caller's stack frame - or, as before, to a
    // shared function-local static - would be read after the call returned or
    // be overwritten by the next call of the same type.  These members are
    // copied with the request and repoint() re-derives arg1..arg4 from them
    // after every copy, so each queued request owns its own arguments.
    //
    std::string owned_str1, owned_str2, owned_str3, owned_str4;
    int         owned_int1;
    double      owned_double1;
    bool        owned_bool1;

    ASYNC_RPC_REQUEST();
    ASYNC_RPC_REQUEST(const ASYNC_RPC_REQUEST& other);
    ASYNC_RPC_REQUEST& operator=(const ASYNC_RPC_REQUEST& other);
    ~ASYNC_RPC_REQUEST();

    void                        clear();
    void                        repoint();
    bool                        isSameAs(ASYNC_RPC_REQUEST& otherRequest);
};


class AsyncRPC
{
public:
    AsyncRPC(CMainDocument *pDoc);
    ~AsyncRPC();

    int RPC_Wait(
            RPC_SELECTOR which_rpc, void* arg1 = NULL, void*
            arg2 = NULL, void* arg3 = NULL, void* arg4 = NULL,
            bool hasPriority = false
    );

    // Queue a request that carries its own argument copies.  The caller fills
    // the owned_* fields and which_rpc; repoint() then derives arg1..arg4, and
    // every copy of the request re-derives them, so nothing points at the
    // caller's stack or at shared static storage.
    //
    int RPC_Wait_Owned(ASYNC_RPC_REQUEST& request);

    int set_mode(RPC_SELECTOR which_rpc, int mode, double duration) {
        ASYNC_RPC_REQUEST req;
        req.which_rpc = which_rpc;
        req.owned_int1 = mode;
        req.owned_double1 = duration;
        return RPC_Wait_Owned(req);
    }

    // Manager must do all RPC data transfers through AsyncRPC calls, so
    // this class must have methods corresponding to all RPC_CLIENT data
    // transfer operations, but NOT init(), init_async(), close(), etc.
    int authorize(const char* passwd) {
        ASYNC_RPC_REQUEST req;
        req.which_rpc = RPC_AUTHORIZE;
        req.owned_str1 = passwd ? passwd : "";
        return RPC_Wait_Owned(req);
    }
    int exchange_versions(std::string client_name, VERSION_INFO& server) {
        ASYNC_RPC_REQUEST req;
        req.which_rpc = RPC_EXCHANGE_VERSIONS;
        req.owned_str1 = client_name;
        req.arg2 = (void*)&server;      // output; the caller is waiting for it
        return RPC_Wait_Owned(req);
    }
    int get_state(CC_STATE& arg1)
            { return RPC_Wait(RPC_GET_STATE, (void*)&arg1); }
    int get_results(RESULTS& arg1, bool& arg2)
            { return RPC_Wait(RPC_GET_RESULTS, (void*)&arg1, (void*)&arg2); }
    int get_file_transfers(FILE_TRANSFERS& arg1)
            { return RPC_Wait(RPC_GET_FILE_TRANSFERS, (void*)&arg1); }
    int get_simple_gui_info(SIMPLE_GUI_INFO& arg1)
            { return RPC_Wait(RPC_GET_SIMPLE_GUI_INFO1, (void*)&arg1); }
    int get_simple_gui_info(PROJECTS& arg1, CC_STATE& ccbuf, RESULTS& rbuf, bool& activeTasksOnly)
            { return RPC_Wait(RPC_GET_SIMPLE_GUI_INFO2, (void*)&arg1, (void*)&ccbuf, (void*)&rbuf, (void*)&activeTasksOnly); }
    int get_project_status(PROJECTS& arg1, CC_STATE& arg2)
            { return RPC_Wait(RPC_GET_PROJECT_STATUS1, (void*)&arg1, (void*)&arg2); }
    int get_project_status(PROJECTS& arg1)
            { return RPC_Wait(RPC_GET_PROJECT_STATUS2, (void*)&arg1); }
    int get_all_projects_list(ALL_PROJECTS_LIST& arg1)
            { return RPC_Wait(RPC_GET_ALL_PROJECTS_LIST, (void*)&arg1); }
    int get_disk_usage(DISK_USAGE& arg1)
            { return RPC_Wait(RPC_GET_DISK_USAGE, (void*)&arg1); }
    int project_op(PROJECT& arg1, const char* op)
            { return RPC_Wait(RPC_PROJECT_OP, (void*)&arg1, (void*)op); }
    int set_run_mode(int mode, double duration) {
        return set_mode(RPC_SET_RUN_MODE, mode, duration);
    }
        // if duration is zero, change is permanent.
        // otherwise, after duration expires,
        // restore last permanent mode
    int set_gpu_mode(int mode, double duration) {
        return set_mode(RPC_SET_GPU_MODE, mode, duration);
    }
    int set_network_mode(int mode, double duration) {
        return set_mode(RPC_SET_NETWORK_MODE, mode, duration);
    }
    int get_screensaver_tasks(int& suspend_reason, RESULTS& rbuf) {
        return RPC_Wait(
            RPC_GET_SCREENSAVER_TASKS, (void*)&suspend_reason, (void*)&rbuf
        );
    }
    int run_benchmarks()
            { return RPC_Wait(RPC_RUN_BENCHMARKS); }
    int set_proxy_settings(GR_PROXY_INFO& arg1)
            { return RPC_Wait(RPC_SET_PROXY_SETTINGS, (void*)&arg1); }
    int get_proxy_settings(GR_PROXY_INFO& arg1)
            { return RPC_Wait(RPC_GET_PROXY_SETTINGS, (void*)&arg1); }
    int file_transfer_op(FILE_TRANSFER& arg1, const char* op)
            { return RPC_Wait(RPC_FILE_TRANSFER_OP, (void*)&arg1, (void*)op); }
    int result_op(RESULT& arg1, const char* op)
            { return RPC_Wait(RPC_RESULT_OP, (void*)&arg1, (void*)op); }
    int get_host_info(HOST_INFO& arg1)
            { return RPC_Wait(RPC_GET_HOST_INFO, (void*)&arg1); }
    int quit()
            { return RPC_Wait(RPC_QUIT); }
    int acct_mgr_info(ACCT_MGR_INFO& arg1)
            { return RPC_Wait(RPC_ACCT_MGR_INFO, (void*)&arg1); }
    int get_statistics(PROJECTS& arg1)
            { return RPC_Wait(RPC_GET_STATISTICS, (void*)&arg1); }
    int network_available()
            { return RPC_Wait(RPC_NETWORK_AVAILABLE); }
    int get_project_init_status(PROJECT_INIT_STATUS& pis)
            { return RPC_Wait(RPC_GET_PROJECT_INIT_STATUS, (void*)&pis); }

    // the following are asynch operations.
    // Make the first call to start the op,
    // call the second one periodically until it returns zero.
    // TODO: do project update
    //
    int get_project_config(std::string url) {
        ASYNC_RPC_REQUEST req;
        req.which_rpc = RPC_GET_PROJECT_CONFIG;
        req.owned_str1 = url;
        return RPC_Wait_Owned(req);
    }
    int get_project_config_poll(PROJECT_CONFIG& arg1)
            { return RPC_Wait(RPC_GET_PROJECT_CONFIG_POLL, (void*)&arg1); }
    int lookup_account(ACCOUNT_IN& arg1)
            { return RPC_Wait(RPC_LOOKUP_ACCOUNT, (void*)&arg1); }
    int lookup_account_poll(ACCOUNT_OUT& arg1)
            { return RPC_Wait(RPC_LOOKUP_ACCOUNT_POLL, (void*)&arg1); }
    int create_account(ACCOUNT_IN& arg1)
            { return RPC_Wait(RPC_CREATE_ACCOUNT, (void*)&arg1); }
    int create_account_poll(ACCOUNT_OUT& arg1)
            { return RPC_Wait(RPC_CREATE_ACCOUNT_POLL, (void*)&arg1); }
    int project_attach(
        const char* url, const char* auth, const char* project_name, const char* email_addr
    ) {
        ASYNC_RPC_REQUEST req;
        req.which_rpc = RPC_PROJECT_ATTACH;
        req.owned_str1 = url ? url : "";
        req.owned_str2 = auth ? auth : "";
        req.owned_str3 = project_name ? project_name : "";
        req.owned_str4 = email_addr ? email_addr : "";
        return RPC_Wait_Owned(req);
    }
    int project_attach_from_file()
            { return RPC_Wait(RPC_PROJECT_ATTACH_FROM_FILE); }
    int project_attach_poll(PROJECT_ATTACH_REPLY& arg1)
            { return RPC_Wait(RPC_PROJECT_ATTACH_POLL, (void*)&arg1); }
    int acct_mgr_rpc(
        const char* url, const char* name, const char* passwd,
        bool use_config_file=false
    ) {
        ASYNC_RPC_REQUEST req;
        req.which_rpc = RPC_ACCT_MGR_RPC;
        req.owned_str1 = url ? url : "";
        req.owned_str2 = name ? name : "";
        req.owned_str3 = passwd ? passwd : "";
        req.owned_bool1 = use_config_file;
        return RPC_Wait_Owned(req);
    }
    int acct_mgr_rpc_poll(ACCT_MGR_RPC_REPLY& arg1)
            { return RPC_Wait(RPC_ACCT_MGR_RPC_POLL, (void*)&arg1); }

    int get_newer_version(std::string& version, std::string& version_download_url)
            { return RPC_Wait(RPC_GET_NEWER_VERSION, (void*)&version, (void*)&version_download_url); }
    int read_global_prefs_override()
            { return RPC_Wait(RPC_READ_GLOBAL_PREFS_OVERRIDE); }
    int read_cc_config()
            { return RPC_Wait(RPC_READ_CC_CONFIG); }
    int get_cc_status(CC_STATUS& arg1)
            { return RPC_Wait(RPC_GET_CC_STATUS, (void*)&arg1); }
    int get_global_prefs_file(std::string& arg1)
            { return RPC_Wait(RPC_GET_GLOBAL_PREFS_FILE, (void*)&arg1); }
    int get_global_prefs_working(std::string& arg1)
            { return RPC_Wait(RPC_GET_GLOBAL_PREFS_WORKING, (void*)&arg1); }
    int get_global_prefs_working_struct(GLOBAL_PREFS& arg1, GLOBAL_PREFS_MASK& arg2)
            { return RPC_Wait(RPC_GET_GLOBAL_PREFS_WORKING_STRUCT, (void*)&arg1, (void*)&arg2); }
    int get_global_prefs_override(std::string& arg1)
            { return RPC_Wait(RPC_GET_GLOBAL_PREFS_OVERRIDE, (void*)&arg1); }
    int set_global_prefs_override(std::string& arg1)
            { return RPC_Wait(RPC_SET_GLOBAL_PREFS_OVERRIDE, (void*)&arg1); }
    int get_global_prefs_override_struct(GLOBAL_PREFS& arg1, GLOBAL_PREFS_MASK& arg2)
            { return RPC_Wait(RPC_GET_GLOBAL_PREFS_OVERRIDE_STRUCT, (void*)&arg1, (void*)&arg2); }
    int set_global_prefs_override_struct(GLOBAL_PREFS& arg1, GLOBAL_PREFS_MASK& arg2)
            { return RPC_Wait(RPC_SET_GLOBAL_PREFS_OVERRIDE_STRUCT, (void*)&arg1, (void*)&arg2); }
    int get_cc_config(CC_CONFIG& arg1, LOG_FLAGS& arg2)
            { return RPC_Wait(RPC_GET_CC_CONFIG, (void*)&arg1, (void*)&arg2); }
    int set_cc_config(CC_CONFIG& arg1, LOG_FLAGS& arg2)
            { return RPC_Wait(RPC_SET_CC_CONFIG, (void*)&arg1, (void*)&arg2); }
private:
    CMainDocument*              m_pDoc;
};


class RPCThread : public wxThread
{
public:
    RPCThread(CMainDocument *pDoc,
                BOINC_Mutex* pRPC_Thread_Mutex,
                BOINC_Condition* pRPC_Thread_Condition,
                BOINC_Mutex* pRPC_Request_Mutex,
                BOINC_Condition* RPC_Request_Condition
            );
    virtual void                *Entry();

private:
    int                         ProcessRPCRequest();
    CMainDocument*              m_pDoc;
    BOINC_Mutex*                m_pRPC_Thread_Mutex;
    BOINC_Condition*            m_pRPC_Thread_Condition;
    BOINC_Mutex*                m_pRPC_Request_Mutex;
    BOINC_Condition*            m_pRPC_Request_Condition;
};


class AsyncRPCDlg : public wxDialog
{
    DECLARE_DYNAMIC_CLASS( AsyncRPCDlg )

public:
    AsyncRPCDlg();
    void                        OnExit(wxCommandEvent& event);
};


class CRPCFinishedEvent : public wxEvent
{
public:
    CRPCFinishedEvent(wxEventType evtType)
        : wxEvent(-1, evtType)
        {
            SetEventObject(wxTheApp);
        }

    virtual wxEvent *Clone() const { return new CRPCFinishedEvent(*this); }
};

wxDECLARE_EVENT(wxEVT_RPC_FINISHED, CRPCFinishedEvent);

#endif
