// This file is part of BOINC.
// https://boinc.berkeley.edu
// Copyright (C) 2024 University of California
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

// Plan class specifications in XML
// See https://github.com/BOINC/boinc/wiki/Plan-classes

#include <string>
#include <vector>
#include <memory>
#include <regex.h>

// Owns a compiled POSIX regex.  PLAN_CLASS_SPEC is copied into a vector,
// so the compiled regex lives in shared storage: copies share it and the
// last one frees it.  Without this, every parsed plan class leaked its
// compiled regexes and copies shared the same underlying buffer.
//
class PLAN_CLASS_REGEX {
public:
    struct STATE {
        regex_t re;
        bool compiled = false;
        ~STATE() {
            if (compiled) regfree(&re);
        }
    };

    PLAN_CLASS_REGEX() : state(std::make_shared<STATE>()) {}
    regex_t* get() { return &state->re; }
    bool is_set() const { return state->compiled; }

    // Compile a pattern, releasing any previously compiled form first.
    // Returns regcomp()'s result; the regex stays unset on failure.
    //
    int compile(const char* pattern, int flags) {
        if (state->compiled) {
            regfree(&state->re);
            state->compiled = false;
        }
        int ret = regcomp(&state->re, pattern, flags);
        state->compiled = (ret == 0);
        return ret;
    }

private:
    std::shared_ptr<STATE> state;
};

// Represents a plan class, as specified in XML
// if you add anything here:
// - initialize it in the constructor
// - parse it in PLAN_CLASS_SPEC::parse()
// - enforce it in PLAN_CLASS_SPEC::check()
//
struct PLAN_CLASS_SPEC {
    char name[256];
        // this encodes some info about the plan class; see
        // https://github.com/BOINC/boinc/wiki/Plan-classes#Planclassnames
        //
    char gpu_type[256];
        // nvidia, amd, intel, or apple
        // or model name, e.g. 'QUALCOMM Adreno (TM)'
        // Allow ati (== amd) or apple_gpu (== apple) for compatibility
        // with old projects.
        // You can omit this and use gpu_type_regex to match
        // GPU type in a more flexible way
    bool cuda;
    bool cal;
    bool opencl;
    bool virtualbox;
    bool wsl;
    bool docker;
    int min_boinc_wsl_distro_version;
        // if client is Win, min version of the BOINC WSL distro
    bool is64bit;
    std::vector<std::string> cpu_features;
    double min_ncpus;
    int max_threads;
    double mem_usage_base;
    double mem_usage_per_cpu;
    bool nthreads_cmdline;
    double projected_flops_scale;
    PLAN_CLASS_REGEX os_regex;
    PLAN_CLASS_REGEX cpu_vendor_regex;
    PLAN_CLASS_REGEX cpu_model_regex;
    double min_os_version;
        // Win versions can be 9 digits; may as well be safe
    double max_os_version;
    int min_android_version;
    int max_android_version;
    int min_libc_version;
        // if WSL: applies to WSL distro
    char project_prefs_tag[256];
    PLAN_CLASS_REGEX project_prefs_regex;
    bool project_prefs_default_true;
    double avg_ncpus;
    int min_core_client_version;
    int max_core_client_version;
        // for non-compute-intensive, or override for GPU apps
    int user_id;
    double infeasible_random;
    long min_wu_id;
    long max_wu_id;
    long min_batch;
    long max_batch;

    // GPU apps
    //
    double cpu_frac;
    double min_gpu_ram_mb;
        // for older clients that don't report available RAM
    double gpu_ram_used_mb;
    double gpu_peak_flops_scale;
    double ngpus;
    int min_driver_version;
    int max_driver_version;
    char gpu_utilization_tag[256];
        // the project prefs tag for user-supplied gpu_utilization factor
    double min_gpu_peak_flops;
    double max_gpu_peak_flops;
    PLAN_CLASS_REGEX gpu_type_regex;

    // AMD/ATI apps
    //
    bool need_ati_libs;
    bool need_amd_libs;
        // need DLLs w/ ATI or AMD name (default: neither)
    int min_amd_simd_width;
        // SIMD width for old GCN GPUs is 16; for new RDNA GPUs it's 32
        // so you can distinguish them this way
    int min_cal_target;
    int max_cal_target;
    bool without_opencl; // restrict to CAL only GPUs
    bool require_hip;    // require the AMD device to report HIP support
    int min_hip_version; // minimum HIP runtime version (encoded value)
    PLAN_CLASS_REGEX gcn_arch_regex;
        // restricts the AMD gfx target, e.g. ^gfx(11|12)

    // Double-precision selection.  Both are optional: with neither set,
    // FP64 work goes to any GPU (including ones that do not report FP64).
    //
    bool require_fp64;
        // host must report FP64 support (have_fp64)
    double min_gpu_dp_flops;
        // minimum estimated FP64 peak, in FLOPS

    // NVIDIA apps
    //
    int min_nvidia_compcap;
    int max_nvidia_compcap;

    // CUDA apps
    //
    int min_cuda_version;
    int max_cuda_version;

    // OpenCL apps
    //
    int min_opencl_version;
    int max_opencl_version;
    int min_opencl_driver_revision;
    int max_opencl_driver_revision;
    bool double_precision_fp;

    // Apple GPU
    int min_metal_support;

    // VirtualBox apps
    //
    int min_vbox_version;
    int max_vbox_version;
    vector<int> exclude_vbox_version;
    bool vm_accel_required;

    PLAN_CLASS_SPEC();
    int parse(XML_PARSER&);
    bool opencl_check(OPENCL_DEVICE_PROP&);
    bool check(SCHEDULER_REQUEST& sreq, HOST_USAGE& hu, const WORKUNIT* wu);
    bool check_wsl_gpu(SCHEDULER_REQUEST& sreq, std::string gpu_name);
};

struct PLAN_CLASS_SPECS {
    std::vector<PLAN_CLASS_SPEC> classes;
    int parse_file(const char*);
    int parse_specs(FILE*);
    bool check(SCHEDULER_REQUEST& sreq, const char* plan_class,
        HOST_USAGE& hu, const WORKUNIT* wu
    );
    bool wu_is_infeasible(const char* plan_class, const WORKUNIT* wu);
    PLAN_CLASS_SPECS(){};
};
