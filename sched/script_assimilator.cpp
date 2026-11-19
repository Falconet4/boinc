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

// An assimilator that runs a script to handle completed jobs,
// so that you can do assimilation in Python, PHP, Perl, bash, etc.
//
// cmdline args to this program:
// --script "scriptname arg1 ... argn"
//
// This program runs the script for each completed job.
//
// arg1 ... argn are 'tokens' representing cmdline args
// to be passed to the script.
// possible tokens are:
//
// files        list of paths of output files of the canonical result
// files2       list of <path logical_name>
//              of output files of the canonical result
// wu_id        workunit ID
// wu_name      workunit name
// result_id    ID of the canonical result
// runtime      runtime of the canonical result
// batch_id     the job's batch ID
//
// If the workunit has no canonical result (i.e. it failed)
// the script is invoked as
// scriptname --error N wu_name wu_id batch_id
// where N is an integer encoding the reasons for the job's failure
// (see WU_ERROR_* in html/inc/common_defs.inc)

#include <vector>
#include <string>
#include <sys/param.h>

#include "boinc_db.h"
#include "error_numbers.h"
#include "sched_msgs.h"
#include "sched_util.h"
#include "validate_util.h"
#include "validator.h"
#include "sched_config.h"

#include "assimilate_handler.h"

using std::vector;
using std::string;

// scriptname, followed by arguments
vector<string> script_args;

int assimilate_handler_init(int argc, char** argv) {
    // handle project specific arguments here
    for (int i=1; i<argc; i++) {
        if (is_arg(argv[i], "script")) {
            script_args = split(argv[++i], ' ');
            if (script_args.size() == 1) {
                // if no tokens specified, use defaults
                script_args.push_back("wu_id");
                script_args.push_back("files");
            }
        }
    }
    if (script_args.empty()) {
        log_messages.printf(MSG_CRITICAL,
            "script name missing from command line\n"
        );
        return 1;
    }
    return 0;
}

void assimilate_handler_usage() {
    // describe the project specific arguments here
    fprintf(stderr,
        "    Custom options:\n"
        "    --script \"X\"  call script to assimilate job\n"
        "                    see comment in script_assimilator.cpp for details\n"
    );
}

int assimilate_handler(
    WORKUNIT& wu, vector<RESULT>& /*results*/, RESULT& canonical_result
) {
    int retval;
    char buf[256];

    log_messages.printf(MSG_DEBUG,
        "assimilate handler: WU ID %ld canonical result %ld\n",
        wu.id, wu.canonical_resultid
    );
    // Every interpolated value below is built from project-supplied data
    // (workunit name, output file paths and logical names) and the result is
    // passed to system(), so quote each one.  Build into a std::string rather
    // than strcat() into a fixed buffer.
    //
    string cmd = "../bin/" + shell_quote(script_args[0]);
    if (wu.canonical_resultid) {
        vector<OUTPUT_FILE_INFO> fis;
        retval = get_output_file_infos(canonical_result, fis);
        if (retval) return retval;
        for (unsigned i=1; i<script_args.size(); i++) {
            string& s = script_args[i];
            if (s == "files") {
                for (OUTPUT_FILE_INFO &fi: fis) {
                    cmd += " " + shell_quote(fi.path);
                }
            } else if (s == "files2") {
                for (OUTPUT_FILE_INFO &fi: fis) {
                    cmd += " " + shell_quote(fi.path);
                    cmd += " " + shell_quote(fi.logical_name);
                }
            } else if (s == "wu_id") {
                snprintf(buf, sizeof(buf), " %lu", wu.id);
                cmd += buf;
            } else if (s == "wu_name") {
                cmd += " " + shell_quote(wu.name);
            } else if (s == "result_id") {
                snprintf(buf, sizeof(buf), " %lu", wu.canonical_resultid);
                cmd += buf;
            } else if (s == "runtime") {
                snprintf(buf, sizeof(buf), " %f", canonical_result.elapsed_time);
                cmd += buf;
            } else if (s == "batch_id") {
                snprintf(buf, sizeof(buf), " %d", wu.batch);
                cmd += buf;
            } else {
                log_messages.printf(MSG_CRITICAL,
                    "Unrecognized script arg '%s'\n", s.c_str()
                );
                return -1;
            }
        }
    } else {
        snprintf(buf, sizeof(buf), "--error %d", wu.error_mask);
        cmd += string(" ") + buf + " " + shell_quote(wu.name);
        snprintf(buf, sizeof(buf), " %lu %d", wu.id, wu.batch);
        cmd += buf;
    }
    log_messages.printf(MSG_DEBUG, "invoking script: %s\n", cmd.c_str());
    retval = system(cmd.c_str());
    if (retval) {
        log_messages.printf(MSG_NORMAL,
            "error %d from script: %s\n", retval, cmd.c_str()
        );
        return retval;
    }
    return 0;
}
