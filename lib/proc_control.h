// This file is part of BOINC.
// http://boinc.berkeley.edu
// Copyright (C) 2023 University of California
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

// functions for building the system-wide process tree,
// and querying/controlling it

#ifndef BOINC_PROC_CONTROL_H
#define BOINC_PROC_CONTROL_H

#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

extern void get_descendants(int pid, std::vector<int>& pids);

// A pid identifies a process only until that process exits: the OS may then
// give the same pid to an unrelated process.  A start time, read while the
// process is known to be ours, tells the two apart.  The unit is whatever the
// platform reports, so values are only ever compared with another value read
// for the same pid.  0 means "unknown".
//
typedef unsigned long long PROC_START_TIME;
extern bool proc_start_time(int pid, PROC_START_TIME& start);

extern bool any_process_exists(std::vector<int>& pids);
extern void kill_all(std::vector<int>& pids);
#ifdef _WIN32
extern void kill_descendants();
extern int suspend_or_resume_threads(
    std::vector<int> pids, DWORD threadid, bool resume, bool check_exempt
);
extern void suspend_or_resume_descendants(bool resume);
extern void suspend_or_resume_process(int pid, bool resume);
#else
extern void kill_descendants(int child_pid=0);
extern void suspend_or_resume_descendants(bool resume, bool use_tstp=false);
extern void suspend_or_resume_process(int pid, bool resume, bool use_tstp=false);
#endif

extern int process_priority_value(int);

#endif
