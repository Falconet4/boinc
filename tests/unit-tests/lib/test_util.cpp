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

#ifndef _WIN32
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "gtest/gtest.h"
#endif
#include "util.h"
#include <math.h>

using namespace std;

namespace test_util {
    class test_util : public ::testing::Test {};

    TEST_F(test_util, dtime) {
        EXPECT_TRUE(dtime() >= 0);
    }

    TEST_F(test_util, dday) {
        EXPECT_TRUE(dday() >= 0);
    }

    TEST_F(test_util, boinc_sleep) {
        double startTime = dtime();
        boinc_sleep(2);
        double endTime = dtime();
        EXPECT_EQ(trunc(endTime - startTime) , 2);
    }

    TEST_F(test_util, push_unique) {
        vector<string> strings;
        push_unique("a", strings);
        EXPECT_EQ(strings.size(), (long unsigned int) 1);

        push_unique("b", strings);
        EXPECT_EQ(strings.size(), (long unsigned int) 2);

        push_unique("b", strings);
        EXPECT_EQ(strings.size(), (long unsigned int) 2);
    }

    TEST_F(test_util, drand) {
        EXPECT_TRUE(drand() > 0);
    }

    TEST_F(test_util, rand_normal) {
        rand_normal();
        rand_normal();
        SUCCEED();
    }

#if !defined(_WIN32) && !defined(__APPLE__)
    TEST_F(test_util, linux_cpu_time) {
        EXPECT_TRUE(linux_cpu_time(1) > 0);
    }

#endif
#ifndef _WIN32
    TEST_F(test_util, resolve_exe) {
        char buf[4096];
        EXPECT_EQ(boinc_resolve_exe("mv", buf, sizeof(buf)), 0);
        EXPECT_NE(buf[0], '\0');
        EXPECT_EQ(boinc_resolve_exe("/bin/sh", buf, sizeof(buf)), 0);
        EXPECT_STREQ(buf, "/bin/sh");
        EXPECT_EQ(boinc_resolve_exe("", buf, sizeof(buf)), -1);
        EXPECT_EQ(
            boinc_resolve_exe("no-such-program-6741", buf, sizeof(buf)), -1
        );
    }

    TEST_F(test_util, resolve_exe_relative_to_dir) {
        char buf[4096];
        EXPECT_EQ(boinc_resolve_exe("./prog", buf, sizeof(buf), "/tmp/d"), 0);
        EXPECT_STREQ(buf, "/tmp/d/./prog");
    }

    TEST_F(test_util, run_program_chdir) {
        char tmpl[] = "/tmp/boinc_run_program_XXXXXX";
        char* dir = mkdtemp(tmpl);
        ASSERT_NE(dir, nullptr);

        char prog[8192];
        snprintf(prog, sizeof(prog), "%s/prog", dir);
        FILE* f = fopen(prog, "w");
        ASSERT_NE(f, nullptr);
        fputs("#!/bin/sh\n", f);
        fclose(f);
        chmod(prog, 0700);

        PROCESS_REF id;
        char* argv[] = {
            (char*)"sh", (char*)"-c", (char*)"test -f ./prog", NULL
        };
        ASSERT_EQ(run_program(dir, "sh", 3, argv, id), 0);
        int status = 0;
        while (waitpid(id, &status, 0) < 0 && errno == EINTR) {
        }
        EXPECT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);

        unlink(prog);
        rmdir(dir);
    }
    TEST_F(test_util, resolve_exe_relative_dir) {
        char cwd[4096];
        ASSERT_NE(getcwd(cwd, sizeof(cwd)), nullptr);
        char tmpl[] = "/tmp/boinc_reldir_XXXXXX";
        char* dir = mkdtemp(tmpl);
        ASSERT_NE(dir, nullptr);
        ASSERT_EQ(chdir(dir), 0);

        char buf[4096];
        EXPECT_EQ(boinc_resolve_exe("./prog", buf, sizeof(buf), "sub"), 0);
        char expect[8192];
        snprintf(expect, sizeof(expect), "%s/sub/./prog", dir);
        EXPECT_STREQ(buf, expect);

        ASSERT_EQ(chdir(cwd), 0);
        rmdir(dir);
    }

    TEST_F(test_util, resolve_exe_skips_directory) {
        char tmpl[] = "/tmp/boinc_shadow_XXXXXX";
        char* dir = mkdtemp(tmpl);
        ASSERT_NE(dir, nullptr);
        char shadow[8192];
        snprintf(shadow, sizeof(shadow), "%s/mv", dir);
        ASSERT_EQ(mkdir(shadow, 0700), 0);

        char oldpath[4096] = "";
        const char* old = getenv("PATH");
        if (old) snprintf(oldpath, sizeof(oldpath), "%s", old);
        char newpath[8192];
        snprintf(newpath, sizeof(newpath), "%s:/usr/bin:/bin", dir);
        setenv("PATH", newpath, 1);

        char buf[4096];
        EXPECT_EQ(boinc_resolve_exe("mv", buf, sizeof(buf)), 0);
        // Do not assume /usr/bin/mv: accept any /mv that is not the
        // directory we planted in PATH.
        const char* sl = strrchr(buf, '/');
        EXPECT_NE(sl, nullptr);
        EXPECT_STREQ(sl ? sl : "", "/mv");
        EXPECT_STRNE(buf, shadow);

        if (old) {
            setenv("PATH", oldpath, 1);
        } else {
            unsetenv("PATH");
        }
        rmdir(shadow);
        rmdir(dir);
    }

    TEST_F(test_util, run_program_relative_dir) {
        char cwd[4096];
        ASSERT_NE(getcwd(cwd, sizeof(cwd)), nullptr);
        char tmpl[] = "/tmp/boinc_relrun_XXXXXX";
        char* dir = mkdtemp(tmpl);
        ASSERT_NE(dir, nullptr);

        char sub[8192];
        snprintf(sub, sizeof(sub), "%s/sub", dir);
        ASSERT_EQ(mkdir(sub, 0700), 0);
        char prog[sizeof(sub)+8];
        snprintf(prog, sizeof(prog), "%s/prog", sub);
        FILE* f = fopen(prog, "w");
        ASSERT_NE(f, nullptr);
        fputs("#!/bin/sh\nexit 0\n", f);
        fclose(f);
        chmod(prog, 0700);

        ASSERT_EQ(chdir(dir), 0);
        PROCESS_REF id;
        char* argv[] = {(char*)"./prog", NULL};
        ASSERT_EQ(run_program("sub", "./prog", 1, argv, id), 0);
        int status = 0;
        while (waitpid(id, &status, 0) < 0 && errno == EINTR) {
        }
        EXPECT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);

        ASSERT_EQ(chdir(cwd), 0);
        unlink(prog);
        rmdir(sub);
        rmdir(dir);
    }
#endif
}
