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

// Detection of AMD GPUs via ROCm/HIP.
//
// This mirrors the CUDA detector in gpu_nvidia.cpp: the runtime is
// loaded with dlopen()/LoadLibrary() and a few entry points are used.
//
// HIP is the only AMD API that reports the compute unit count, the
// clock and the free VRAM directly, and it works on systems that have
// ROCm but no OpenCL ICD.  The GPU architecture (gfx1100, gfx1200, ...)
// is read from the kernel's KFD topology, which is available whenever
// the amdgpu driver is loaded and does not depend on ROCm userspace.

#ifdef _WIN32
#include "boinc_win.h"
#include "win_util.h"
#else
#include "config.h"
#include <dlfcn.h>
#include <unistd.h>
#endif
#include <cstdio>
#include <cstring>
#include <cstdlib>
#ifdef __linux__
#include <dirent.h>
#endif

#include <vector>
#include <string>

using std::vector;
using std::string;

#include "coproc.h"
#include "str_replace.h"
#include "str_util.h"
#include "util.h"

#include "client_msgs.h"
#include "log_flags.h"
#include "gpu_detect.h"
#include "hip_boinc.h"

static HIP_GET_DEVICE_COUNT p_hipGetDeviceCount = NULL;
static HIP_GET_DEVICE_NAME p_hipDeviceGetName = NULL;
static HIP_GET_DEVICE_ATTRIBUTE p_hipDeviceGetAttribute = NULL;
static HIP_MEM_GET_INFO p_hipMemGetInfo = NULL;
static HIP_RUNTIME_GET_VERSION p_hipRuntimeGetVersion = NULL;
static HIP_SET_DEVICE p_hipSetDevice = NULL;
static HIP_GET_PCI_BUS_ID p_hipDeviceGetPCIBusId = NULL;

#ifdef _WIN32
// Highest ROCm runtime major version whose versioned DLL name we probe.
// The DLL is amdhip64_<major>.dll from ROCm 6 onwards.
//
#define HIP_MAX_VERSIONED_MAJOR 10

static void* hip_dlopen(const char* name) {
    return (void*)LoadLibraryA(name);
}
static void* hip_dlsym(void* handle, const char* name) {
    return (void*)GetProcAddress((HMODULE)handle, name);
}
#else
static void* hip_dlopen(const char* name) {
    return dlopen(name, RTLD_NOW);
}
static void* hip_dlsym(void* handle, const char* name) {
    return dlsym(handle, name);
}
#endif

// ROCm installs into /opt/rocm and does not necessarily register that
// directory with ldconfig, so try the usual locations before the bare
// soname.
//
static void* load_hip_library() {
    vector<string> paths;

#ifdef _WIN32
    // The HIP SDK ships the runtime in <ROCM_PATH>\bin and normally adds
    // that directory to PATH; try the environment first.
    //
    // ROCm 6 and later name the DLL with the runtime major version
    // (amdhip64_6.dll, amdhip64_7.dll) and do not provide an unversioned
    // amdhip64.dll, so both spellings must be tried.  Newest first.
    //
    const char* envs[] = {"HIP_PATH", "ROCM_PATH"};
    for (const char* e: envs) {
        const char* v = getenv(e);
        if (v && v[0]) {
            char name[64];
            paths.push_back(string(v) + "\\bin\\amdhip64.dll");
            paths.push_back(string(v) + "\\bin\\libamdhip64.dll");
            for (int i=HIP_MAX_VERSIONED_MAJOR; i>=1; i--) {
                snprintf(name, sizeof(name), "\\bin\\amdhip64_%d.dll", i);
                paths.push_back(string(v) + name);
            }
        }
    }
    {
        char name[64];
        paths.push_back("amdhip64.dll");
        paths.push_back("libamdhip64.dll");
        for (int i=HIP_MAX_VERSIONED_MAJOR; i>=1; i--) {
            snprintf(name, sizeof(name), "amdhip64_%d.dll", i);
            paths.push_back(name);
        }
    }
#else
    const char* rocm_path = getenv("ROCM_PATH");
    if (rocm_path && rocm_path[0]) {
        paths.push_back(string(rocm_path) + "/lib/libamdhip64.so");
        paths.push_back(string(rocm_path) + "/lib64/libamdhip64.so");
    }
    paths.push_back("/opt/rocm/lib/libamdhip64.so");
    paths.push_back("/opt/rocm/lib64/libamdhip64.so");
    paths.push_back("libamdhip64.so");
    paths.push_back("libamdhip64.so.7");
    paths.push_back("libamdhip64.so.6");
    paths.push_back("libamdhip64.so.5");
#endif

    for (const string& p: paths) {
        void* h = hip_dlopen(p.c_str());
        if (h) return h;
    }
    return NULL;
}

#ifdef __linux__
// Read a "key value" pair from a KFD topology properties file.
//
static bool read_kfd_prop(const char* path, const char* key, long long& value) {
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char buf[256];
    bool found = false;
    while (fgets(buf, sizeof(buf), f)) {
        char k[128];
        long long v;
        if (sscanf(buf, "%127s %lld", k, &v) == 2 && !strcmp(k, key)) {
            value = v;
            found = true;
            break;
        }
    }
    fclose(f);
    return found;
}

// Find the KFD GPU node whose PCI bus matches.  Fills in the graphics
// IP version, SIMD count, wavefront size, max compute clock and VRAM.
//
static bool kfd_lookup(
    int pci_bus, int& gfx_target, int& simd_count, int& wave_front_size,
    int& clock_mhz, double& vram, int& pci_device_id, char* node_name,
    int node_name_len
) {
    const char* base = "/sys/class/kfd/kfd/topology/nodes";
    DIR* d = opendir(base);
    if (!d) return false;

    bool found = false;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;

        char props[512], namepath[512];
        snprintf(props, sizeof(props), "%s/%s/properties", base, e->d_name);
        snprintf(namepath, sizeof(namepath), "%s/%s/name", base, e->d_name);

        long long vendor_id = 0, location_id = 0;
        if (!read_kfd_prop(props, "vendor_id", vendor_id)) continue;
        if (vendor_id != 4098) continue;    // 0x1002 = AMD
        // A node without location_id cannot be matched to a bus; treating
        // the failed read as 0 would match the first APU node.
        //
        if (!read_kfd_prop(props, "location_id", location_id)) continue;

        // location_id encodes domain<<32 | bus<<8 | device<<3 | function
        //
        if (((location_id >> 8) & 0xff) != pci_bus) continue;

        long long v = 0;
        if (read_kfd_prop(props, "gfx_target_version", v)) gfx_target = (int)v;
        if (read_kfd_prop(props, "simd_count", v)) simd_count = (int)v;
        if (read_kfd_prop(props, "wave_front_size", v)) wave_front_size = (int)v;
        if (read_kfd_prop(props, "max_engine_clk_fcompute", v)) clock_mhz = (int)v;
        if (read_kfd_prop(props, "device_id", v)) pci_device_id = (int)v;

        // The largest memory bank is VRAM
        //
        double best = 0;
        for (int b = 0; b < 16; b++) {
            char mb[512];
            snprintf(mb, sizeof(mb),
                "%s/%s/mem_banks/%d/properties", base, e->d_name, b
            );
            long long size = 0;
            if (read_kfd_prop(mb, "size_in_bytes", size)) {
                if ((double)size > best) best = (double)size;
            }
        }
        vram = best;

        FILE* f = fopen(namepath, "r");
        if (f) {
            if (fgets(node_name, node_name_len, f)) {
                strip_whitespace(node_name);
            }
            fclose(f);
        }
        found = true;
        break;
    }
    closedir(d);
    return found;
}
#endif

int amd_compare(COPROC_AMD& c1, COPROC_AMD& c2, bool loose) {
    // Strict comparison picks the most capable card: peak FP32 first, so
    // an FP64 guess for an unknown architecture cannot outrank a much
    // faster device.  Loose comparison groups equivalent instances.
    //
    if (!loose) {
        if (c1.peak_flops > c2.peak_flops) return 1;
        if (c1.peak_flops < c2.peak_flops) return -1;
    }
    // Prefer a device that actually reports FP64.
    //
    if (c1.have_fp64 && !c2.have_fp64) return 1;
    if (!c1.have_fp64 && c2.have_fp64) return -1;
    if (loose) {
        if (c1.total_ram > 1.4*c2.total_ram) return 1;
        if (c1.total_ram < .7* c2.total_ram) return -1;
        return 0;
    }
    if (c1.total_ram > c2.total_ram) return 1;
    if (c1.total_ram < c2.total_ram) return -1;
    return 0;
}

void COPROC_AMD::correlate(
    bool use_all,
    vector<int>& ignore_devs
) {
    (void)use_all;      // AMD now reports every usable device

    if (!amd_gpus.size()) return;

    // Find the most capable non-ignored instance.  It supplies the
    // vendor/driver fields; the per-device report and the conservative
    // aggregate fields below describe the whole pool.
    //
    bool first = true;
    unsigned int i;
    for (i=0; i<amd_gpus.size(); i++) {
        if (in_vector(amd_gpus[i].device_num, ignore_devs)) continue;
        if (first) {
            *this = amd_gpus[i];
            first = false;
        } else if (amd_compare(amd_gpus[i], *this, false) > 0) {
            *this = amd_gpus[i];
        }
    }

    // Include every usable instance, not only those equivalent to the
    // best.  The scheduler can now target a subset by device number.
    //
    count = 0;
    bool all_fp64 = true, all_hip = true, all_opencl = true;
    bool arch_common = true;
    double min_dp = 0, min_avail = 0, min_ocl = 0, max_peak = 0;
    double min_ram = 0;
    char common_arch[64];
    common_arch[0] = 0;
    bool first_used = true;

    for (i=0; i<amd_gpus.size(); i++) {
        COPROC_AMD& d = amd_gpus[i];
        if (in_vector(d.device_num, ignore_devs)) {
            d.is_used = COPROC_IGNORED;
            continue;
        }
        if (this->have_opencl && !d.have_opencl) {
            d.is_used = COPROC_UNUSED;
            continue;
        }
        if (this->have_hip && !d.have_hip) {
            d.is_used = COPROC_UNUSED;
            continue;
        }
        if (count >= MAX_COPROC_INSTANCES) {
            d.is_used = COPROC_UNUSED;
            continue;
        }

        int slot = count;
        device_nums[slot] = d.device_num;
        instance_has_opencl[slot] = d.have_opencl;
        opencl_device_indexes[slot] = d.opencl_prop.opencl_device_index;
        opencl_device_ids[slot] = d.opencl_prop.device_id;
        count++;
        d.is_used = COPROC_USED;

        if (first_used) {
            safe_strcpy(common_arch, d.gcn_arch);
            min_dp = d.peak_flops_dp;
            min_avail = d.available_ram;
            min_ocl = d.opencl_prop.global_mem_size;
            min_ram = d.total_ram;
            max_peak = d.peak_flops;
            first_used = false;
        } else {
            if (strcmp(common_arch, d.gcn_arch)) arch_common = false;
            if (d.peak_flops_dp < min_dp) min_dp = d.peak_flops_dp;
            if (d.available_ram < min_avail) min_avail = d.available_ram;
            if (d.opencl_prop.global_mem_size
                && d.opencl_prop.global_mem_size < min_ocl
            ) {
                min_ocl = d.opencl_prop.global_mem_size;
            }
            if (d.total_ram < min_ram) min_ram = d.total_ram;
            if (d.peak_flops > max_peak) max_peak = d.peak_flops;
        }
        if (!d.have_fp64) all_fp64 = false;
        if (!d.have_hip) all_hip = false;
        if (!d.have_opencl) all_opencl = false;
    }

    if (!count) return;

    // Drop the devices the user told us to ignore.  set_devices() builds the
    // list from every detected GPU and does not know about
    // <ignore_gpu_instance>, but the scheduler treats each entry as a device it
    // may schedule on, so an ignored card must not be in it.
    //
    if (!ignore_devs.empty()) {
        vector<COPROC_AMD_DEVICE> keep;
        for (unsigned int k=0; k<devices.size(); k++) {
            if (in_vector(devices[k].device_num, ignore_devs)) continue;
            keep.push_back(devices[k]);
        }
        devices.swap(keep);
    }

    // Conservative summary for old servers (which only see the aggregate),
    // plus the full device list for the new per-device path.
    //
    have_fp64 = all_fp64;
    peak_flops_dp = all_fp64? min_dp : 0;
    if (!arch_common) gcn_arch[0] = 0;
    else safe_strcpy(gcn_arch, common_arch);
    total_ram = min_ram;
    available_ram = min_avail;
    opencl_prop.global_mem_size = min_ocl;
    peak_flops = max_peak;
    have_hip = all_hip;
    have_opencl = all_opencl;
    opencl_device_count = count;
}

void COPROC_AMD::get_hip(vector<string>& warnings) {
    char buf[256];
    string s;
    int n = 0;

    void* lib = load_hip_library();
    if (!lib) {
        // Not an error: ROCm is optional.
        return;
    }

    p_hipGetDeviceCount = (HIP_GET_DEVICE_COUNT)hip_dlsym(lib, "hipGetDeviceCount");
    p_hipDeviceGetName = (HIP_GET_DEVICE_NAME)hip_dlsym(lib, "hipDeviceGetName");
    p_hipDeviceGetAttribute = (HIP_GET_DEVICE_ATTRIBUTE)hip_dlsym(lib, "hipDeviceGetAttribute");
    p_hipMemGetInfo = (HIP_MEM_GET_INFO)hip_dlsym(lib, "hipMemGetInfo");
    p_hipRuntimeGetVersion = (HIP_RUNTIME_GET_VERSION)hip_dlsym(lib, "hipRuntimeGetVersion");
    p_hipSetDevice = (HIP_SET_DEVICE)hip_dlsym(lib, "hipSetDevice");
    p_hipDeviceGetPCIBusId = (HIP_GET_PCI_BUS_ID)hip_dlsym(lib, "hipDeviceGetPCIBusId");

    if (!p_hipGetDeviceCount || !p_hipDeviceGetName
        || !p_hipDeviceGetAttribute || !p_hipMemGetInfo
    ) {
        gpu_warning(warnings, "HIP: runtime is missing required entry points");
        return;
    }

    if (p_hipGetDeviceCount(&n) != HIP_SUCCESS) {
        snprintf(buf, sizeof(buf), "HIP: hipGetDeviceCount() failed");
        gpu_warning(warnings, buf);
        return;
    }
    if (n <= 0) return;
    if (n > MAX_COPROC_INSTANCES) n = MAX_COPROC_INSTANCES;

    int runtime_version = 0;
    if (p_hipRuntimeGetVersion) {
        p_hipRuntimeGetVersion(&runtime_version);
    }

    for (int i=0; i<n; i++) {
        COPROC_AMD cc;
        cc.clear();
        cc.count = 1;
        cc.have_hip = true;
        cc.hip_device_num = i;
        cc.hip_version = runtime_version;
        cc.device_num = i;

        // hipDeviceGetName() reports the marketing name ("AMD Radeon RX
        // 9060 XT").  If it fails, leave the name empty so that the KFD
        // node name can be used below; the generic fallback is applied
        // after GPU detection.
        //
        if (p_hipDeviceGetName(cc.name, sizeof(cc.name), i) != HIP_SUCCESS) {
            cc.name[0] = 0;
        }

        int v = 0;
        if (p_hipDeviceGetAttribute(&v, HIP_DEV_ATTR_MULTIPROCESSOR_COUNT, i) == HIP_SUCCESS) {
            cc.hip_cu_count = v;
        }
        if (p_hipDeviceGetAttribute(&v, HIP_DEV_ATTR_CLOCK_RATE, i) == HIP_SUCCESS) {
            cc.hip_clock_khz = v;
        }
        if (p_hipDeviceGetAttribute(&v, HIP_DEV_ATTR_WARP_SIZE, i) == HIP_SUCCESS) {
            cc.hip_warp_size = v;
        }
        if (p_hipDeviceGetAttribute(&v, HIP_DEV_ATTR_MEMORY_CLOCK_RATE, i) == HIP_SUCCESS) {
            cc.hip_memory_clock_khz = v;
        }
        if (p_hipDeviceGetAttribute(&v, HIP_DEV_ATTR_MEMORY_BUS_WIDTH, i) == HIP_SUCCESS) {
            cc.hip_memory_bus_width = v;
        }

        // VRAM: available_ram is the free amount the scheduler may use;
        // localRAM is the total.  hipMemGetInfo() takes size_t; the
        // TotalGlobalMem attribute is an int and overflows above 2 GB.
        //
        double total_mem = 0, free_mem = 0;
        if (p_hipSetDevice && p_hipSetDevice(i) == HIP_SUCCESS) {
            std::size_t free_bytes = 0, total_bytes = 0;
            if (p_hipMemGetInfo(&free_bytes, &total_bytes) == HIP_SUCCESS) {
                total_mem = (double)total_bytes;
                free_mem = (double)free_bytes;
            }
        }
        // available_ram is what the scheduler may use; total_ram is total.
        //
        cc.available_ram = free_mem;
        cc.total_ram = total_mem;

        int bus = -1;
        if (p_hipDeviceGetPCIBusId) {
            if (p_hipDeviceGetPCIBusId(cc.pci_bus_id, sizeof(cc.pci_bus_id), i) == HIP_SUCCESS) {
                // "dddd:bb:dd.f"
                const char* p = strchr(cc.pci_bus_id, ':');
                if (p) bus = (int)strtol(p+1, NULL, 16);
            }
        }
        if (bus < 0) {
            if (p_hipDeviceGetAttribute(&v, HIP_DEV_ATTR_PCI_BUS_ID, i) == HIP_SUCCESS) {
                bus = v;
            }
        }
        cc.pci_bus = bus;
        if (p_hipDeviceGetAttribute && p_hipDeviceGetAttribute(
            &v, HIP_DEV_ATTR_PCI_DEVICE_ID, i) == HIP_SUCCESS
        ) {
            cc.pci_info.device_id = v;
        }

#ifdef __linux__
        if (bus >= 0) {
            int gfx_target = 0, kfd_simd_count = 0, wave_front = 0, clk_mhz = 0;
            int pci_device_id = 0;
            double vram = 0;
            char node_name[256];
            node_name[0] = 0;
            if (kfd_lookup(
                bus, gfx_target, kfd_simd_count, wave_front, clk_mhz, vram,
                pci_device_id, node_name, sizeof(node_name))
            ) {
                if (pci_device_id) cc.pci_info.device_id = pci_device_id;
                cc.gfx_target_version = gfx_target;
                cc.gfxip_major = gfx_target / 10000;
                cc.gfxip_minor = (gfx_target / 100) % 100;
                // KFD's node name is authoritative when it provides one:
                // it preserves letter suffixes such as gfx90a.
                //
                if (node_name[0] && !strncmp(node_name, "gfx", 3)) {
                    safe_strcpy(cc.gcn_arch, node_name);
                } else {
                    gfx_target_to_string(gfx_target, cc.gcn_arch, sizeof(cc.gcn_arch));
                }
                // Only wave32 maps cleanly: SIMD count x wavefront size is
                // the FP32 lane count.  For wave64 (GCN/CDNA) the SIMD
                // count is the number of 16/32-lane units, so the product
                // is not the lane count; set_peak_flops() derives it from
                // the CU count instead.
                //
                if (kfd_simd_count && wave_front == 32) {
                    cc.hip_lane_count = kfd_simd_count * wave_front;
                }
                if (!cc.hip_cu_count && cc.hip_lane_count) {
                    // GCN reports one CU of 64 lanes; RDNA reports WGPs.
                    cc.hip_cu_count = (wave_front == 32)
                        ? cc.hip_lane_count / 128 : cc.hip_lane_count / 64;
                }
                if (!cc.hip_clock_khz && clk_mhz) {
                    cc.hip_clock_khz = clk_mhz * 1000;
                }
                if (!cc.total_ram && vram) {
                    cc.total_ram = vram;
                }
                if (!cc.name[0] && node_name[0]
                    && strcmp(node_name, "ip discovery")
                ) {
                    safe_strcpy(cc.name, node_name);
                }
            }
        }
#endif

        if (!cc.name[0]) {
            snprintf(cc.name, sizeof(cc.name), "AMD GPU %d", i);
        }

        // Without KFD (Windows, or a kernel without amdkfd), the gfx
        // version is still available through the compute capability
        // attributes: HIP maps the gfx major and minor onto them (12/0
        // for gfx1200 on RDNA 4).  The stepping is not exposed, so the
        // rendered name is best-effort: family regexes still work, but an
        // exact per-model pattern may not match.
        //
        // Legacy path: fill in the gfx major/minor when KFD did not give
        // them, but keep a node-name-derived gcn_arch authoritative.
        //
        if (!cc.gfxip_major) {
            int major = 0, minor = 0;
            if (p_hipDeviceGetAttribute(&major, HIP_DEV_ATTR_COMPUTE_CAPABILITY_MAJOR, i) != HIP_SUCCESS
                || p_hipDeviceGetAttribute(&minor, HIP_DEV_ATTR_COMPUTE_CAPABILITY_MINOR, i) != HIP_SUCCESS
                || major <= 0
            ) {
                major = minor = 0;
            }
            cc.gfxip_major = major;
            cc.gfxip_minor = minor;
            if (major) {
                cc.gfx_target_version = major*10000 + minor*100;
                if (!cc.gcn_arch[0]) {
                    gfx_target_to_string(
                        cc.gfx_target_version, cc.gcn_arch, sizeof(cc.gcn_arch)
                    );
                }
            }
        }

        // The driver's CL/HIP "max clock" is often a DPM level well below
        // the card's boost clock (2780 vs 3320 MHz on a Gigabyte RX 9060
        // XT Gaming OC).  Use the highest DPM level when it is higher.
        //
        int dpm_clock = amd_max_dpm_clock_mhz(cc.pci_bus);
        if (dpm_clock > 0 && dpm_clock*1000 > cc.hip_clock_khz) {
            cc.hip_clock_khz = dpm_clock*1000;
        }

        // Derive the lane count if KFD did not provide it.  This is the
        // same rule set_peak_flops() uses, so both agree.
        if (!cc.hip_lane_count && cc.hip_cu_count) {
            cc.hip_lane_count = cc.hip_cu_count * hip_lanes_per_cu(
                cc.hip_warp_size, cc.gfxip_major, cc.gfx_target_version
            );
        }

        cc.set_peak_flops();
        if (cc.bad_gpu_peak_flops("HIP", s)) {
            gpu_warning(warnings, s.c_str());
        }
        amd_gpus.push_back(cc);
    }

    if (log_flags.coproc_debug) {
        char hip_ver[32];
        format_hip_version(runtime_version, hip_ver, sizeof(hip_ver));
        msg_printf(NULL, MSG_INFO,
            "[coproc] HIP detected %d AMD GPU(s), runtime %s",
            (int)amd_gpus.size(), hip_ver
        );
    }

    // Do not dlclose(): the runtime may keep threads and a context alive,
    // and this process exits immediately after GPU detection anyway.
}
