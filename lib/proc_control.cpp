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

#include <vector>
#ifdef _WIN32
#include "diagnostics.h"
#include "boinc_win.h"
#include "win_util.h"
#else
#include "config.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/user.h>
#endif

#if HAVE_CSIGNAL
#include <csignal>
#elif HAVE_SYS_SIGNAL_H
#include <sys/signal.h>
#elif HAVE_SIGNAL_H
#include <signal.h>
#endif
#endif

#include "common_defs.h"
#include "procinfo.h"
#include "str_util.h"
#include "util.h"

#include "proc_control.h"

using std::vector;

//#define DEBUG_PROC_CONTROL

#ifdef DEBUG_PROC_CONTROL
#include <stdio.h>
#endif

static void get_descendants_aux(PROC_MAP& pm, int pid, vector<int>& pids) {
    PROC_MAP::iterator i = pm.find(pid);
    if (i == pm.end()) return;
    PROCINFO& p = i->second;
    if (p.scanned) return;  // avoid infinite recursion
    p.scanned = true;
    for (int child_pid: p.children) {
        pids.push_back(child_pid);
        get_descendants_aux(pm, child_pid, pids);
    }
}

// return a list of all descendants of the given process
//
void get_descendants(int pid, vector<int>& pids) {
    int retval;
    PROC_MAP pm;
    pids.clear();
    retval = procinfo_setup(pm);
    if (retval) return;
    get_descendants_aux(pm, pid, pids);
#ifdef DEBUG_PROC_CONTROL
    fprintf(stderr, "descendants of %d:\n", pid);
    for (int dpid: pids) {
        fprintf(stderr, "   %d\n", dpid);
    }
#endif
}

// A pid identifies a process only until that process exits: the OS may then
// give the same pid to an unrelated process, and a pid list captured earlier
// then points at processes that are not ours (often running as the same user,
// so signalling them succeeds).  A start time, read while the process is known
// to be ours, tells the two apart.
//
bool proc_start_time(int pid, PROC_START_TIME& start) {
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    FILETIME creation_time, exit_time, kernel_time, user_time;
    BOOL ok = GetProcessTimes(
        h, &creation_time, &exit_time, &kernel_time, &user_time
    );
    CloseHandle(h);
    if (!ok) return false;
    start = ((PROC_START_TIME)creation_time.dwHighDateTime << 32)
        | (PROC_START_TIME)creation_time.dwLowDateTime;
    return true;
#elif defined(__linux__)
    // /proc/<pid>/stat, field 22: start time, in clock ticks after boot
    //
    char path[64], buf[512];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE* f = fopen(path, "r");
    if (!f) return false;
    bool found = false;
    if (fgets(buf, sizeof(buf), f)) {
        // Field 2 is the command name, in parentheses, and may itself contain
        // spaces and parentheses, so start after the last ')'.  Fields 3
        // (state) to 21 come before the start time: skip 19 of them.
        //
        const char* p = strrchr(buf, ')');
        if (p) {
            p++;
            for (int field=3; field<22 && *p; field++) {
                while (*p == ' ') p++;
                while (*p && *p != ' ') p++;
            }
            while (*p == ' ') p++;
            if (*p >= '0' && *p <= '9') {
                PROC_START_TIME val = 0;
                while (*p >= '0' && *p <= '9') {
                    val = val*10 + (PROC_START_TIME)(*p - '0');
                    p++;
                }
                start = val;
                found = true;
            }
        }
    }
    fclose(f);
    return found;
#elif defined(__APPLE__) || defined(__FreeBSD__)
    struct kinfo_proc info = {};
    size_t size = sizeof(info);
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
    if (sysctl(mib, 4, &info, &size, nullptr, 0) != 0 || size == 0) {
        return false;
    }
#if defined(__APPLE__)
    const struct timeval& tv = info.kp_proc.p_starttime;
#else
    const struct timeval& tv = info.ki_start;
#endif
    if (tv.tv_sec == 0) return false;
    start = (PROC_START_TIME)tv.tv_sec * 1000000ULL
        + (PROC_START_TIME)tv.tv_usec;
    return true;
#else
    (void)pid;
    (void)start;
    return false;
#endif
}

#ifdef _WIN32

// Suspend or resume the threads in a set of processes,
// but don't suspend 'calling_thread'.
//
// Called from:
//  API (processes = self)
//      This handles a) throttling; b) suspend/resume by user
//  wrapper (via suspend_or_resume_process()); process = child
//  wrapper, MP case (via suspend_or_resume_decendants());
//      processes = descendants
//
// The only way to do this on Windows is to enumerate
// all the threads in the entire system,
// and identify those belonging to one of the processes (ugh!!)
//
int suspend_or_resume_threads(
    vector<int>pids, DWORD calling_thread_id, bool resume, bool check_exempt
) {
    HANDLE threads, thread;
    THREADENTRY32 te = {0};
    int retval = 0;
    DWORD n;
    static vector<DWORD> suspended_threads;

#ifdef DEBUG_PROC_CONTROL
    fprintf(stderr, "start: check_exempt %d %s\n", check_exempt, precision_time_to_string(dtime()));
    fprintf(stderr, "%s processes", resume?"resume":"suspend");
    for (int pid: pids) {
        fprintf(stderr, " %d", pid);
    }
    fprintf(stderr, "\n");
#endif

    threads = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (threads == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "CreateToolhelp32Snapshot failed\n");
        return -1;
    }

    te.dwSize = sizeof(THREADENTRY32);
    if (!Thread32First(threads, &te)) {
        fprintf(stderr, "Thread32First failed\n");
        CloseHandle(threads);
        return -1;
    }

    if (!resume) {
        suspended_threads.clear();
    }

    do {
        if (check_exempt && !diagnostics_is_thread_exempt_suspend(te.th32ThreadID)) {
#ifdef DEBUG_PROC_CONTROL
            fprintf(stderr, "thread is exempt\n");
#endif
            continue;
        }
#ifdef DEBUG_PROC_CONTROL
        fprintf(stderr, "thread %d PID %d %s\n",
            te.th32ThreadID, te.th32OwnerProcessID,
            precision_time_to_string(dtime())
        );
#endif
        if (te.th32ThreadID == calling_thread_id) continue;
        if (!in_vector(te.th32OwnerProcessID, pids)) continue;
        thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (resume) {
            // check whether we suspended this thread earlier
            //
            if (std::find(
                suspended_threads.begin(), suspended_threads.end(),
                te.th32ThreadID
            ) != suspended_threads.end()) {
                n = ResumeThread(thread);
#ifdef DEBUG_PROC_CONTROL
                fprintf(stderr, "ResumeThread returns %d\n", n);
#endif
            } else {
                n = 0;
            }
        } else {
            n = SuspendThread(thread);
            suspended_threads.push_back(te.th32ThreadID);
#ifdef DEBUG_PROC_CONTROL
            fprintf(stderr, "SuspendThread returns %d\n", n);
#endif
        }
        if (n == -1) retval = -1;
        CloseHandle(thread);
    } while (Thread32Next(threads, &te));

    CloseHandle (threads);
#ifdef DEBUG_PROC_CONTROL
    fprintf(stderr, "end: %s\n", precision_time_to_string(dtime()));
#endif
    return retval;
}

#else

// True if the process exists but has already exited and is waiting to be
// reaped.  Linux reports the state in /proc/<pid>/stat; macOS (and other
// BSDs) expose it through kinfo_proc, since there is no /proc.
//
static bool process_is_zombie(int pid) {
#if defined(__linux__)
    char path[64], buf[256];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE* f = fopen(path, "r");
    if (!f) return false;
    bool zombie = false;
    if (fgets(buf, sizeof(buf), f)) {
        const char* p = strrchr(buf, ')');
        if (p && p[1] == ' ' && p[2] == 'Z') zombie = true;
    }
    fclose(f);
    return zombie;
#elif defined(__APPLE__)
    struct kinfo_proc info = {};
    size_t size = sizeof(info);
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
    if (sysctl(mib, 4, &info, &size, nullptr, 0) != 0 || size == 0) {
        return false;
    }
    return info.kp_proc.p_stat == SZOMB;
#elif defined(__FreeBSD__)
    struct kinfo_proc info = {};
    size_t size = sizeof(info);
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
    if (sysctl(mib, 4, &info, &size, nullptr, 0) != 0 || size == 0) {
        return false;
    }
    return info.ki_stat == SZOMB;
#else
    (void)pid;
    return false;
#endif
}

// True if this pid still refers to the process whose start time was recorded.
// A recorded start time of 0 means "unknown" (the platform could not read it),
// and then the pid alone is used, as this code did before start times existed.
//
static bool same_process(int pid, PROC_START_TIME recorded) {
    if (!recorded) return true;
    PROC_START_TIME now;
    if (!proc_start_time(pid, now)) return false;   // exited
    return now == recorded;
}

// True if any of these processes still exists, and is still the process whose
// start time was recorded for it.
//
static bool any_process_exists(
    const vector<int>& pids, const vector<PROC_START_TIME>& start_times
) {
    for (size_t i=0; i<pids.size(); i++) {
        int pid = pids[i];
        PROC_START_TIME recorded = (i<start_times.size())?start_times[i]:0;
        if (!same_process(pid, recorded)) continue;
        // kill(pid, 0) tests for existence without reaping, but it also
        // succeeds for a zombie (an exited child that has not been
        // reaped).  Treat those as gone, or kill_descendants() waits the
        // full grace period for a process that has already exited.
        //
        if (process_is_zombie(pid)) continue;
        errno = 0;
        if (kill(pid, 0) == 0 || errno == EPERM) {
            return true;
        }
    }
    return false;
}

bool any_process_exists(vector<int>& pids) {
    vector<PROC_START_TIME> start_times;
    return any_process_exists(pids, start_times);
}

// Record the start time of each pid, so that a check or a kill made later can
// tell our processes from unrelated ones that took over a pid.
//
static void record_start_times(
    const vector<int>& pids, vector<PROC_START_TIME>& start_times
) {
    start_times.clear();
    start_times.reserve(pids.size());
    for (int pid: pids) {
        PROC_START_TIME start = 0;
        if (!proc_start_time(pid, start)) {
            start = 0;
        }
        start_times.push_back(start);
    }
}

// Drop the pids whose process has exited, or whose pid now belongs to another
// process.
//
static void keep_original_processes(
    vector<int>& pids, const vector<PROC_START_TIME>& start_times
) {
    size_t j = 0;
    for (size_t i=0; i<pids.size(); i++) {
        PROC_START_TIME recorded = (i<start_times.size())?start_times[i]:0;
        if (same_process(pids[i], recorded)) {
            pids[j++] = pids[i];
        }
    }
    pids.resize(j);
}

#endif

void kill_all(vector<int>& pids) {
    for (int pid: pids) {
#ifdef _WIN32
        HANDLE h = OpenProcess(READ_CONTROL | PROCESS_TERMINATE, false, pid);
        if (h == NULL) continue;
        TerminateProcess(h, 0);
        CloseHandle(h);
#else
        kill(pid, SIGTERM);
#endif
    }
}

// Kill the descendants of the calling process.
//
#ifdef _WIN32
void kill_descendants() {
    vector<int> descendants;
    // on Win, kill descendants directly
    //
    get_descendants(GetCurrentProcessId(), descendants);
    kill_all(descendants);
}
#else
// Same, but if child_pid is nonzero,
// give it a chance to exit gracefully on Unix
//
void kill_descendants(int child_pid) {
    vector<int> descendants;
    vector<PROC_START_TIME> start_times;
    // on Unix, ask main process nicely.
    // it descendants still exist after 10 sec, use the nuclear option
    //
    get_descendants(getpid(), descendants);
    // Remember which process each pid was.  During the grace period below
    // these processes can exit, and the OS can then give their pids to
    // unrelated processes, which must not be waited for or killed: both the
    // existence check and the kills are matched against these start times.
    //
    record_start_times(descendants, start_times);
    if (child_pid) {
        ::kill(child_pid, SIGTERM);
        for (int i=0; i<10; i++) {
            if (!any_process_exists(descendants, start_times)) {
                return;
            }
            boinc_sleep(1);
        }
        keep_original_processes(descendants, start_times);
        kill_all(descendants);
        // kill any processes that might have been created
        // in the last 10 secs
        get_descendants(getpid(), descendants);
    }
    kill_all(descendants);
}
#endif

// suspend/resume the descendants of the calling process.
// Unix version lets you choose the stop signal:
// SIGSTOP (the default) can't be caught.
// SIGTSTP can be caught, but it has no effect for processes without a TTY.
// So it's useful only for programs that are wrappers of some sort;
// they must catch and handle it.
//
#ifdef _WIN32
void suspend_or_resume_descendants(bool resume) {
    vector<int> descendants;
    int pid = GetCurrentProcessId();
    get_descendants(pid, descendants);
    suspend_or_resume_threads(descendants, 0, resume, false);
}
#else
void suspend_or_resume_descendants(bool resume, bool use_tstp) {
    vector<int> descendants;
    int pid = getpid();
    get_descendants(pid, descendants);
    for (int dpid: descendants) {
        kill(dpid, resume?SIGCONT:(use_tstp?SIGTSTP:SIGSTOP));
    }
}
#endif

// Suspend/resume the given process; used by the wrapper.
// See signal comment above.
//
#ifdef _WIN32
void suspend_or_resume_process(int pid, bool resume) {
    vector<int> pids;
    pids.push_back(pid);
    suspend_or_resume_threads(pids, 0, resume, false);
}
#else
void suspend_or_resume_process(int pid, bool resume, bool use_tstp) {
    ::kill(pid, resume?SIGCONT:(use_tstp?SIGTSTP:SIGSTOP));
}
#endif

// return OS-specific value associated with priority code
//
int process_priority_value(int priority) {
#ifdef _WIN32
    switch (priority) {
    case PROCESS_PRIORITY_LOWEST: return IDLE_PRIORITY_CLASS;
    case PROCESS_PRIORITY_LOW: return BELOW_NORMAL_PRIORITY_CLASS;
    case PROCESS_PRIORITY_NORMAL: return NORMAL_PRIORITY_CLASS;
    case PROCESS_PRIORITY_HIGH: return ABOVE_NORMAL_PRIORITY_CLASS;
    case PROCESS_PRIORITY_HIGHEST: return HIGH_PRIORITY_CLASS;
    }
    return 0;
#else
    switch (priority) {
    case PROCESS_PRIORITY_LOWEST: return PROCESS_IDLE_PRIORITY;
    case PROCESS_PRIORITY_LOW: return PROCESS_MEDIUM_PRIORITY;
    case PROCESS_PRIORITY_NORMAL: return PROCESS_NORMAL_PRIORITY;
    case PROCESS_PRIORITY_HIGH: return PROCESS_ABOVE_NORMAL_PRIORITY;
    case PROCESS_PRIORITY_HIGHEST: return PROCESS_HIGH_PRIORITY;
    }
    return 0;
#endif
}
