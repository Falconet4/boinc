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

#include "gtest/gtest.h"

#include "proc_control.h"

#ifndef _WIN32
#include <cerrno>
#include <unistd.h>
#include <sys/wait.h>
#endif

// proc_start_time() is what lets kill_descendants() tell its own processes
// from unrelated ones that took over a pid, so it has to be stable while a
// process lives and unavailable once it is gone.

namespace test_proc_control {

    class test_proc_control : public ::testing::Test {
    protected:
        static int self_pid() {
#ifdef _WIN32
            return (int)GetCurrentProcessId();
#else
            return (int)getpid();
#endif
        }
    };

    TEST_F(test_proc_control, start_time_is_stable_for_a_live_process) {
        int pid = self_pid();
        PROC_START_TIME first = 0, second = 0;
        ASSERT_TRUE(proc_start_time(pid, first));
        EXPECT_NE(0u, first);
        ASSERT_TRUE(proc_start_time(pid, second));
        EXPECT_EQ(first, second);
    }

    TEST_F(test_proc_control, no_start_time_for_a_pid_nobody_has) {
        // 0x7FFFFFF0: not a pid any supported system hands out, and one
        // OpenProcess() and /proc both reject
        PROC_START_TIME start = 12345;
        EXPECT_FALSE(proc_start_time(0x7FFFFFF0, start));
    }

#ifndef _WIN32
    // The grace period in kill_descendants() relies on this: once a process
    // exits, its pid no longer reports the start time it had while running.
    //
    TEST_F(test_proc_control, start_time_is_gone_after_the_process_exits) {
        int fds[2];
        ASSERT_EQ(0, pipe(fds));
        int child = (int)fork();
        ASSERT_NE(-1, child);
        if (child == 0) {
            close(fds[1]);
            // block until the parent has read the start time: read the read
            // end, which is fds[0] (pipe() hands back read then write)
            char c;
            while (read(fds[0], &c, 1) < 0 && errno == EINTR) {
                // retry
            }
            _exit(0);
        }
        close(fds[0]);

        PROC_START_TIME start = 0;
        EXPECT_TRUE(proc_start_time(child, start));
        EXPECT_NE(0u, start);

        // let the child exit, then reap it: write the write end, fds[1]
        ASSERT_EQ(1, (int)write(fds[1], "x", 1));
        close(fds[1]);
        int status = 0;
        ASSERT_EQ(child, (int)waitpid(child, &status, 0));

        PROC_START_TIME after = 0;
        EXPECT_FALSE(proc_start_time(child, after));
    }
#endif

}   // namespace test_proc_control
