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

#if defined(_WIN32)
#include "boinc_win.h"
#else
#include "boinc_stdio.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#endif

#include <string>
using std::string;

#ifdef _WIN32
#include "win_util.h"
#else
#ifdef __APPLE__
// Suppress obsolete warning when building for OS 10.3.9
#define DLOPEN_NO_WARN
#include <mach-o/dyld.h>
#endif
#include "config.h"
#include <dlfcn.h>
#include <setjmp.h>
#include <signal.h>
#endif

#include "error_numbers.h"
#include "filesys.h"
#include "parse.h"
#include "str_replace.h"
#include "util.h"

#include "coproc.h"

int COPROC_REQ::parse(XML_PARSER& xp) {
    safe_strcpy(type, "");
    count = 0;
    while (!xp.get_tag()) {
        if (xp.match_tag("/coproc")) {
            if (!strlen(type)) return ERR_XML_PARSE;
            return 0;
        }
        if (xp.parse_str("type", type, sizeof(type))) continue;
        if (xp.parse_double("count", count)) continue;
    }
    return ERR_XML_PARSE;
}

int PCI_INFO::parse(XML_PARSER& xp) {
    present = false;
    bus_id = device_id = domain_id = 0;
    while (!xp.get_tag()) {
        if (xp.match_tag("/pci_info")) {
            return 0;
        }
        if (xp.parse_int("bus_id", bus_id)) continue;
        if (xp.parse_int("device_id", device_id)) continue;
        if (xp.parse_int("domain_id", domain_id)) continue;
    }
    return ERR_XML_PARSE;
}

#ifndef _USING_FCGI_

void PCI_INFO::write(MIOFILE& f) {
    f.printf(
        "<pci_info>\n"
        "   <bus_id>%d</bus_id>\n"
        "   <device_id>%d</device_id>\n"
        "   <domain_id>%d</domain_id>\n"
        "</pci_info>\n",
        bus_id,
        device_id,
        domain_id
    );
}

void COPROC::write_xml(MIOFILE& f, bool scheduler_rpc) {
    f.printf(
        "<coproc>\n"
        "   <type>%s</type>\n"
        "   <count>%d</count>\n"
        "   <peak_flops>%f</peak_flops>\n"
        "   <peak_flops_dp>%f</peak_flops_dp>\n"
        "   <have_fp64>%d</have_fp64>\n",
        type, count, peak_flops, peak_flops_dp, have_fp64 ? 1 : 0
    );

    if (scheduler_rpc) {
        write_request(f);
    }

    if (have_opencl) {
        opencl_prop.write_xml(f, "coproc_opencl");
    }

    f.printf("</coproc>\n");
}

void COPROC::write_request(MIOFILE& f) {
    f.printf(
        "   <req_secs>%f</req_secs>\n"
        "   <req_instances>%f</req_instances>\n"
        "   <estimated_delay>%f</estimated_delay>\n",
        req_secs,
        req_instances,
        estimated_delay
    );
}

#endif

int COPROC::parse(XML_PARSER& xp) {
    char buf[256];
    safe_strcpy(type, "");
    clear();
    for (int i=0; i<MAX_COPROC_INSTANCES; i++) {
        device_nums[i] = i;
    }
    while (!xp.get_tag()) {
        if (!xp.is_tag) continue;
        if (xp.match_tag("/coproc")) {
            if (!strlen(type)) return ERR_XML_PARSE;
            check_count("generic");
            clear_usage();
            return 0;
        }
        if (xp.match_tag("coproc_opencl")) {
            opencl_prop.parse(xp, "/coproc_opencl");
            continue;
        }
        if (xp.parse_str("type", type, sizeof(type))) continue;
        if (xp.parse_int("count", count)) continue;
        if (xp.parse_double("req_secs", req_secs)) continue;
        if (xp.parse_double("req_instances", req_instances)) continue;
        if (xp.parse_double("peak_flops", peak_flops)) continue;
        if (xp.parse_double("peak_flops_dp", peak_flops_dp)) continue;
        if (xp.parse_bool("have_fp64", have_fp64)) continue;
        if (xp.parse_str("device_nums", buf, sizeof(buf))) {
            int i=0;
            char* p = strtok(buf, " ");
            while (p && i<MAX_COPROC_INSTANCES) {
                device_nums[i++] = atoi(p);
                p = strtok(NULL, " ");
            }
            continue;
        }
        if (xp.parse_bool("non_gpu", non_gpu)) continue;
    }
    return ERR_XML_PARSE;
}

// Length of a valid UTF-8 sequence at p, or 0 if the bytes are not valid
// UTF-8 (overlong forms, surrogates and out-of-range code points rejected).
//
static int utf8_seq_len(const unsigned char* p) {
    unsigned char c = p[0];
    if (c < 0xC2) return 0;
    if (c <= 0xDF) return (p[1] & 0xC0) == 0x80 ? 2 : 0;
    if (c == 0xE0) {
        return ((p[1] & 0xE0) == 0xA0 && (p[2] & 0xC0) == 0x80) ? 3 : 0;
    }
    if (c <= 0xEC) {
        return ((p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) ? 3 : 0;
    }
    if (c == 0xED) {
        return ((p[1] & 0xE0) == 0x80 && (p[2] & 0xC0) == 0x80) ? 3 : 0;
    }
    if (c <= 0xEF) {
        return ((p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) ? 3 : 0;
    }
    if (c == 0xF0) {
        return ((p[1] & 0xF0) == 0x90 && (p[2] & 0xC0) == 0x80
            && (p[3] & 0xC0) == 0x80) ? 4 : 0;
    }
    if (c <= 0xF3) {
        return ((p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80
            && (p[3] & 0xC0) == 0x80) ? 4 : 0;
    }
    if (c == 0xF4) {
        return ((p[1] & 0xF0) == 0x80 && (p[2] & 0xC0) == 0x80
            && (p[3] & 0xC0) == 0x80) ? 4 : 0;
    }
    return 0;
}

// Escape a string for use inside a JSON string literal.  Driver-supplied
// names can contain quotes or backslashes, which would otherwise break
// the JSON that carries this summary.  Valid UTF-8 is passed through;
// invalid bytes are written as \u00XX so the result is always valid JSON.
//
static void json_escape_string(const char* in, char* out, size_t out_size) {
    size_t j = 0;
    if (!out_size) return;
    if (!in) {
        out[0] = 0;
        return;
    }
    for (; *in; in++) {
        unsigned char c = (unsigned char)*in;
        char tmp[8];
        const char* rep;
        size_t n;
        if (c == '"' || c == '\\') {
            tmp[0] = '\\';
            tmp[1] = (char)c;
            rep = tmp;
            n = 2;
        } else if (c == '\n') {
            rep = "\\n";
            n = 2;
        } else if (c == '\r') {
            rep = "\\r";
            n = 2;
        } else if (c == '\t') {
            rep = "\\t";
            n = 2;
        } else if (c < 0x20) {
            snprintf(tmp, sizeof(tmp), "\\u%04x", c);
            rep = tmp;
            n = strlen(tmp);
        } else if (c >= 0x80) {
            int len = utf8_seq_len((const unsigned char*)in);
            if (len > 0) {
                rep = in;
                n = (size_t)len;
                in += len - 1;
            } else {
                snprintf(tmp, sizeof(tmp), "\\u%04x", c);
                rep = tmp;
                n = strlen(tmp);
            }
        } else {
            tmp[0] = (char)c;
            rep = tmp;
            n = 1;
        }
        if (j + n >= out_size) break;
        memcpy(out+j, rep, n);
        j += n;
    }
    out[j] = 0;
}

void summary_json(
    char *buf, size_t len,
    const char *type, const char *model, int count, int ram_mb,
    const char *driver_version, int opencl_version,
    bool have_hip, const char* gcn_arch, int hip_version
) {
    // Escape buffers: worst case is 6 bytes per input byte (control
    // characters as \u00XX); the largest input field is the 512-byte
    // OpenCL driver string, so 4096 is sufficient for every caller.
    //
    char etype[4096], emodel[4096], edrv[4096], earch[4096];
    json_escape_string(type, etype, sizeof(etype));
    json_escape_string(model, emodel, sizeof(emodel));
    json_escape_string(driver_version, edrv, sizeof(edrv));
    json_escape_string(gcn_arch, earch, sizeof(earch));

    if (have_hip) {
        snprintf(buf, len,
"        {\n"\
"            \"type\": \"%s\",\n"\
"            \"model\": \"%s\",\n"\
"            \"count\": %d,\n"\
"            \"ram_mb\": %d,\n"\
"            \"driver_version\": \"%s\",\n"\
"            \"opencl_version\": \"%d\",\n"\
"            \"hip\": true,\n"\
"            \"hip_version\": %d,\n"\
"            \"gcn_arch\": \"%s\"\n"\
"        }",
            etype, emodel, count, ram_mb, edrv, opencl_version,
            hip_version, earch
        );
    } else {
        snprintf(buf, len,
"        {\n"\
"            \"type\": \"%s\",\n"\
"            \"model\": \"%s\",\n"\
"            \"count\": %d,\n"\
"            \"ram_mb\": %d,\n"\
"            \"driver_version\": \"%s\",\n"\
"            \"opencl_version\": \"%d\"\n"\
"        }",
            etype, emodel, count, ram_mb, edrv, opencl_version
        );
    }
}

void COPROCS::summary_string_json(string &out) {
    // Large enough for the worst case: four fields of 256 bytes each,
    // every byte escaped as \u00XX, plus the surrounding JSON.
    //
    char buf2[8192];
    char buf[256];
    out = "";
    if (nvidia.count) {
        snprintf(buf, sizeof(buf), "%d", nvidia.display_driver_version);
        summary_json(buf2, sizeof(buf2),
            "nvidia",
            nvidia.cuda_prop.name,
            nvidia.count,
            (int)(nvidia.cuda_prop.totalGlobalMem/MEGA),
            buf,
            nvidia.opencl_prop.opencl_device_version_int,
            false, "", 0
        );
        out += buf2;
    }
    if (ati.count) {
        if (!out.empty()) out += ",\n";
        summary_json(buf2, sizeof(buf2),
            "amd",
            ati.name, ati.count,
            ati.attribs.localRAM, ati.version,
            ati.opencl_prop.opencl_device_version_int,
            ati.have_hip, ati.gcn_arch, ati.hip_version
        );
        out += buf2;
    }
    if (intel_gpu.count) {
        if (!out.empty()) out += ",\n";
        summary_json(buf2, sizeof(buf2),
            "intel",
            intel_gpu.opencl_prop.name, intel_gpu.count,
            (int)((double)intel_gpu.opencl_prop.global_mem_size/MEGA),
            intel_gpu.opencl_prop.opencl_driver_version,
            intel_gpu.opencl_prop.opencl_device_version_int,
            false, "", 0
        );
        out += buf2;
    }
    if (apple_gpu.count) {
        if (!out.empty()) out += ",\n";
        snprintf(buf, sizeof(buf), "%d", apple_gpu.metal_support);
        summary_json(buf2, sizeof(buf2),
            "apple",
            apple_gpu.model, apple_gpu.count,
            (int)((double)apple_gpu.opencl_prop.global_mem_size/MEGA),
            buf,
            apple_gpu.opencl_prop.opencl_device_version_int,
            false, "", 0
        );
        out += buf2;
    }
    for (int i=1; i<n_rsc; i++) {
        COPROC& cp = coprocs[i];
        int type = coproc_type_name_to_num(cp.type);
        if (type == PROC_TYPE_NVIDIA_GPU) continue;
        if (type == PROC_TYPE_AMD_GPU) continue;
        if (type == PROC_TYPE_INTEL_GPU) continue;
        if (type == PROC_TYPE_APPLE_GPU) continue;
        if (!strlen(cp.opencl_prop.name)) continue;
        if (!out.empty()) out += ",\n";
        summary_json(buf2, sizeof(buf2),
            cp.opencl_prop.vendor,
            cp.opencl_prop.name,
            cp.count,
            (int)((double)cp.opencl_prop.global_mem_size/MEGA),
            cp.opencl_prop.opencl_device_version,
            cp.opencl_prop.opencl_device_version_int,
            false, "", 0
        );
        out += buf2;
    }
}

void COPROC::check_count(const char* ctype) {
    if (count < 1) {
        boinc::fprintf(stderr, "%s count %d too small\n", ctype, count);
        count = 1;
    }
    if (count > MAX_COPROC_INSTANCES) {
        boinc::fprintf(stderr, "%s count %d too large\n", ctype, count);
        count = MAX_COPROC_INSTANCES;
    }
}

int COPROCS::parse(XML_PARSER& xp) {
    int retval;

    clear();
    n_rsc = 1;
    safe_strcpy(coprocs[0].type, "CPU");
    while (!xp.get_tag()) {
        if (xp.match_tag("/coprocs")) {
            if (n_rsc == MAX_RSC) {
                boinc::fprintf(stderr, "too many coproc entries\n");
            }
            return 0;
        }
        if (xp.match_tag("coproc_cuda")) {
            retval = nvidia.parse(xp);
            if (retval) {
                nvidia.clear();
            } else {
                if (n_rsc < MAX_RSC) {
                    coprocs[n_rsc++] = nvidia;
                }
            }
            continue;
        }
        if (xp.match_tag("coproc_ati")) {
            retval = ati.parse(xp);
            if (retval) {
                ati.clear();
            } else {
                if (n_rsc < MAX_RSC) {
                    coprocs[n_rsc++] = ati;
                }
            }
            continue;
        }
        if (xp.match_tag("coproc_intel_gpu")) {
            retval = intel_gpu.parse(xp);
            if (retval) {
                intel_gpu.clear();
            } else {
                if (n_rsc < MAX_RSC) {
                    coprocs[n_rsc++] = intel_gpu;
                }
            }
            continue;
        }
        if (xp.match_tag("coproc_apple_gpu")) {
            retval = apple_gpu.parse(xp);
            if (retval) {
                apple_gpu.clear();
            } else {
                if (n_rsc < MAX_RSC) {
                    coprocs[n_rsc++] = apple_gpu;
                }
            }
            continue;
        }
        if (xp.match_tag("coproc")) {
            COPROC cp;
            retval = cp.parse(xp);
            if (retval) {
                boinc::fprintf(stderr, "failed to parse <coproc>: %d\n", retval);
            } else {
                if (n_rsc < MAX_RSC) {
                    coprocs[n_rsc++] = cp;
                }
            }
        }
    }
    return ERR_XML_PARSE;
}

#ifdef _USING_FCGI_
void COPROCS::write_xml(MIOFILE&, bool) {
}
#else
void COPROCS::write_xml(MIOFILE& mf, bool scheduler_rpc) {
    mf.printf("    <coprocs>\n");

    for (int i=1; i<n_rsc; i++) {
        switch (coproc_type_name_to_num(coprocs[i].type)) {
        case PROC_TYPE_NVIDIA_GPU:
            nvidia.write_xml(mf, scheduler_rpc);
            break;
        case PROC_TYPE_AMD_GPU:
            ati.write_xml(mf, scheduler_rpc);
            break;
        case PROC_TYPE_INTEL_GPU:
            intel_gpu.write_xml(mf, scheduler_rpc);
            break;
        case PROC_TYPE_APPLE_GPU:
            apple_gpu.write_xml(mf, scheduler_rpc);
            break;
        default:
            coprocs[i].write_xml(mf, scheduler_rpc);
        }
    }

    mf.printf("    </coprocs>\n");
}
#endif

void COPROC_NVIDIA::description(char* buf, int buflen) {
    char vers[256], cuda_vers[256], dp[64];
    if (display_driver_version) {
#ifdef __APPLE__
     	int maj = display_driver_version >> 16;
    	int min = (display_driver_version >> 8) & 0xff;
    	int rev = display_driver_version & 0xff;
        snprintf(vers, sizeof(vers), "%d.%d.%d", maj, min, rev);
#else
        int maj = display_driver_version/100;
        int min = display_driver_version%100;
        snprintf(vers, sizeof(vers), "%d.%02d", maj, min);
#endif
    } else {
        safe_strcpy(vers, "unknown");
    }
    if (cuda_version) {
        int maj = cuda_version/1000;
        int min = (cuda_version%1000)/10;
        snprintf(cuda_vers, sizeof(cuda_vers), "%d.%d", maj, min);
    } else {
        safe_strcpy(cuda_vers, "unknown");
    }
    if (peak_flops_dp > 0) {
        snprintf(dp, sizeof(dp), ", %s FP64",
            flops_to_string(peak_flops_dp).c_str()
        );
    } else {
        dp[0] = 0;
    }
    snprintf(buf, buflen,
        "%s (driver version %s, CUDA version %s, compute capability %d.%d, %.2fGB, %.2fGB available, %s peak%s)",
        cuda_prop.name, vers, cuda_vers, cuda_prop.major, cuda_prop.minor,
        cuda_prop.totalGlobalMem/GIGA, available_ram/GIGA,
        flops_to_string(peak_flops).c_str(), dp
    );
}

#ifndef _USING_FCGI_
void COPROC_NVIDIA::write_xml(MIOFILE& f, bool scheduler_rpc) {
    f.printf(
        "<coproc_cuda>\n"
        "   <count>%d</count>\n"
        "   <name>%s</name>\n"
        "   <available_ram>%f</available_ram>\n"
        "   <have_cuda>%d</have_cuda>\n"
        "   <have_opencl>%d</have_opencl>\n",
        count,
        have_cuda?cuda_prop.name:opencl_prop.name,
        available_ram,
        have_cuda ? 1 : 0,
        have_opencl ? 1 : 0
    );
    if (scheduler_rpc) {
        write_request(f);
    }
    f.printf(
        "   <peak_flops>%f</peak_flops>\n"
        "   <peak_flops_dp>%f</peak_flops_dp>\n"
        "   <have_fp64>%d</have_fp64>\n"
        "   <cudaVersion>%d</cudaVersion>\n"
        "   <drvVersion>%d</drvVersion>\n"
        "   <totalGlobalMem>%f</totalGlobalMem>\n"
        "   <sharedMemPerBlock>%f</sharedMemPerBlock>\n"
        "   <regsPerBlock>%d</regsPerBlock>\n"
        "   <warpSize>%d</warpSize>\n"
        "   <memPitch>%f</memPitch>\n"
        "   <maxThreadsPerBlock>%d</maxThreadsPerBlock>\n"
        "   <maxThreadsDim>%d %d %d</maxThreadsDim>\n"
        "   <maxGridSize>%d %d %d</maxGridSize>\n"
        "   <clockRate>%d</clockRate>\n"
        "   <totalConstMem>%f</totalConstMem>\n"
        "   <major>%d</major>\n"
        "   <minor>%d</minor>\n"
        "   <textureAlignment>%f</textureAlignment>\n"
        "   <deviceOverlap>%d</deviceOverlap>\n"
        "   <multiProcessorCount>%d</multiProcessorCount>\n",
        peak_flops,
        peak_flops_dp,
        have_fp64 ? 1 : 0,
        cuda_version,
        display_driver_version,
        cuda_prop.totalGlobalMem,
        cuda_prop.sharedMemPerBlock,
        cuda_prop.regsPerBlock,
        cuda_prop.warpSize,
        cuda_prop.memPitch,
        cuda_prop.maxThreadsPerBlock,
        cuda_prop.maxThreadsDim[0], cuda_prop.maxThreadsDim[1], cuda_prop.maxThreadsDim[2],
        cuda_prop.maxGridSize[0], cuda_prop.maxGridSize[1], cuda_prop.maxGridSize[2],
        cuda_prop.clockRate,
        cuda_prop.totalConstMem,
        cuda_prop.major,
        cuda_prop.minor,
        cuda_prop.textureAlignment,
        cuda_prop.deviceOverlap,
        cuda_prop.multiProcessorCount
    );

    if (have_opencl) {
        opencl_prop.write_xml(f, "coproc_opencl");
    }

    if (!scheduler_rpc) {
        for (int i=0; i<count; i++) {
            pci_infos[i].write(f);
        }
    }

    f.printf("</coproc_cuda>\n");
}
#endif

void COPROC_NVIDIA::clear() {
    static const COPROC_NVIDIA x(0);
    *this = x;
    safe_strcpy(type, proc_type_name_xml(PROC_TYPE_NVIDIA_GPU));
    estimated_delay = -1;   // mark as absent
    cuda_version = 0;
    display_driver_version = 0;
    safe_strcpy(cuda_prop.name, "");
    cuda_prop.totalGlobalMem = 0.0;
    cuda_prop.sharedMemPerBlock = 0;
    cuda_prop.regsPerBlock = 0;
    cuda_prop.warpSize = 0;
    cuda_prop.memPitch = 0;
    cuda_prop.maxThreadsPerBlock = 0;
    cuda_prop.maxThreadsDim[0] = 0;
    cuda_prop.maxThreadsDim[1] = 0;
    cuda_prop.maxThreadsDim[2] = 0;
    cuda_prop.maxGridSize[0] = 0;
    cuda_prop.maxGridSize[1] = 0;
    cuda_prop.maxGridSize[2] = 0;
    cuda_prop.clockRate = 0;
    cuda_prop.totalConstMem = 0;
    cuda_prop.major = 0;
    cuda_prop.minor = 0;
    cuda_prop.textureAlignment = 0;
    cuda_prop.deviceOverlap = 0;
    cuda_prop.multiProcessorCount = 0;
    is_used = COPROC_USED;
}

int COPROC_NVIDIA::parse(XML_PARSER& xp) {
    char buf2[256];
    int retval;
    int ipci = 0;

    clear();
    while (!xp.get_tag()) {
        if (xp.match_tag("/coproc_cuda")) {
            if (!peak_flops) {
                set_peak_flops();
            }
            if (!available_ram) {
                available_ram = cuda_prop.totalGlobalMem;
            }
            check_count("NVIDIA");
            return 0;
        }
        if (xp.parse_int("count", count)) continue;
        if (xp.parse_double("peak_flops", peak_flops)) continue;
        if (xp.parse_double("peak_flops_dp", peak_flops_dp)) continue;
        if (xp.parse_bool("have_fp64", have_fp64)) continue;
        if (xp.parse_bool("have_cuda", have_cuda)) continue;
        if (xp.parse_bool("have_opencl", have_opencl)) continue;
        if (xp.parse_double("available_ram", available_ram)) continue;
        if (xp.parse_double("req_secs", req_secs)) continue;
        if (xp.parse_double("req_instances", req_instances)) continue;
        if (xp.parse_double("estimated_delay", estimated_delay)) continue;
        if (xp.parse_int("cudaVersion", cuda_version)) continue;
        if (xp.parse_int("drvVersion", display_driver_version)) continue;
        if (xp.parse_str("name", cuda_prop.name, sizeof(cuda_prop.name))) continue;
        if (xp.parse_double("totalGlobalMem", cuda_prop.totalGlobalMem)) continue;
        if (xp.parse_double("sharedMemPerBlock", cuda_prop.sharedMemPerBlock)) continue;
        if (xp.parse_int("regsPerBlock", cuda_prop.regsPerBlock)) continue;
        if (xp.parse_int("warpSize", cuda_prop.warpSize)) continue;
        if (xp.parse_double("memPitch", cuda_prop.memPitch)) continue;
        if (xp.parse_int("maxThreadsPerBlock", cuda_prop.maxThreadsPerBlock)) continue;
        if (xp.parse_str("maxThreadsDim", buf2, sizeof(buf2))) {
            // can't use sscanf here (FCGI)
            //
            cuda_prop.maxThreadsDim[0] = atoi(buf2);
            char* p = strchr(buf2, ' ');
            if (p) {
                p++;
                cuda_prop.maxThreadsDim[1] = atoi(p);
                p = strchr(p, ' ');
                if (p) {
                    p++;
                    cuda_prop.maxThreadsDim[2] = atoi(p);
                }
            }
            continue;
        }
        if (xp.parse_str("maxGridSize", buf2, sizeof(buf2))) {
            cuda_prop.maxGridSize[0] = atoi(buf2);
            char* p = strchr(buf2, ' ');
            if (p) {
                p++;
                cuda_prop.maxGridSize[1] = atoi(p);
                p = strchr(p, ' ');
                if (p) {
                    p++;
                    cuda_prop.maxGridSize[2] = atoi(p);
                }
            }
            continue;
        }
        if (xp.parse_int("clockRate", cuda_prop.clockRate)) continue;
        if (xp.parse_double("totalConstMem", cuda_prop.totalConstMem)) continue;
        if (xp.parse_int("major", cuda_prop.major)) continue;
        if (xp.parse_int("minor", cuda_prop.minor)) continue;
        if (xp.parse_double("textureAlignment", cuda_prop.textureAlignment)) continue;
        if (xp.parse_int("deviceOverlap", cuda_prop.deviceOverlap)) continue;
        if (xp.parse_int("multiProcessorCount", cuda_prop.multiProcessorCount)) continue;
        if (xp.match_tag("pci_info")) {
            PCI_INFO p;
            p.parse(xp);
            if (ipci < MAX_COPROC_INSTANCES) {
                pci_infos[ipci++] = p;
            }
        }
        if (xp.match_tag("coproc_opencl")) {
            retval = opencl_prop.parse(xp, "/coproc_opencl");
            if (retval) return retval;
            continue;
        }
    }
    return ERR_XML_PARSE;
}

// Estimated ratio of double- to single-precision peak throughput.
// No API reports this directly, and it is an architectural property.
// These are conservative values for the families BOINC is likely to
// meet; returning 0 means "unknown" and reports no FP64 figure.
//
static double amd_fp64_ratio(
    int gfxip_major, int gfx_target, const char* name, int pci_device_id
) {
    if (gfxip_major >= 11) return 1./32.;   // RDNA 3/4
    if (gfxip_major == 10) return 1./16.;   // RDNA 1/2
    if (gfxip_major == 9) {
        // The OpenCL-only path may know the marketing name but not the
        // gfx target, so identify the FP64-heavy models by name first.
        // (Not every "Instinct"/"MI" is 1:2: MI25 is a Vega 10 part.)
        //
        if (strstr(name, "MI250") || strstr(name, "MI210")) return 1.;
        if (strstr(name, "MI300") || strstr(name, "MI350")) return 1./2.;
        if (strstr(name, "MI100")) return 1./2.;
        if (strstr(name, "MI50") || strstr(name, "MI60")) return 1./2.;
        if (strstr(name, "Radeon Pro VII")) return 1./2.;
        if (strstr(name, "Radeon VII")) return 1./4.;
        if (strstr(name, "MI25")) return 1./16.;
        // When only the KFD node name is available it is the arch string.
        if (!strncmp(name, "gfx", 3)) {
            if (!strcmp(name, "gfx90a")) return 1.;
            if (!strcmp(name, "gfx908") || !strcmp(name, "gfx940")
                || !strcmp(name, "gfx942") || !strcmp(name, "gfx950")
            ) {
                return 1./2.;
            }
            if (!strcmp(name, "gfx90c")) return 1./16.;
        }

        // gfx906 covers both MI50/MI60 (1:2) and Radeon VII (1:4).
        // The PCI device ID distinguishes them when no marketing name is
        // available: 0x66af is Radeon VII, 0x66a1/0x66a3 are the
        // MI50/Pro VII and MI60/Pro Vega II.
        //
        if (gfx_target == 90006 || !strcmp(name, "gfx906")) {
            if (pci_device_id == 0x66af) return 1./4.;
            if (pci_device_id == 0x66a1 || pci_device_id == 0x66a3) {
                return 1./2.;
            }
        }

        // Target-based rules.
        if (gfx_target == 90010) return 1.;    // CDNA 2 (MI200)
        if (gfx_target == 90008) return 1./2.; // CDNA 1 (MI100)
        if (gfx_target >= 90400 && gfx_target < 90600) return 1./2.;
        if (gfx_target >= 90000 && gfx_target <= 90012) return 1./16.;
        if (gfx_target == 0) return 1./16.;    // gfx9, target unknown: Vega-like
        return 0.;                             // unknown future gfx9
    }
    if (gfxip_major == 8) return 1./16.;    // Polaris/Fiji
    return 0.;
}

static double nvidia_fp64_ratio(int major, int minor) {
    switch (major) {
    case 12: return 1./64.;                 // Blackwell GeForce
    case 10: return 1./2.;                  // Blackwell datacenter
    case 9:  return 1./2.;                  // Hopper
    case 8:  return (minor == 0) ? 1./2. : 1./64.;
    case 7:  return (minor == 0) ? 1./2. : 1./32.;
    case 6:  return (minor == 0) ? 1./2. : 1./32.;
        // Only cc6.0 (GP100, Tesla P100) is 1:2.  cc6.1 (GP102/104/106:
        // GTX 1080/1070/1060, Titan Xp, Tesla P40) and cc6.2 (GP10B)
        // are 1:32.
    default: return 0.;
    }
}

// Convert a KFD gfx target version (major*10000 + minor*100 + stepping)
// to an architecture name (e.g. 120000 -> "gfx1200").  The minor and
// stepping components are single hex digits in the public name: 9/0/10
// is "gfx90a", 9/0/12 is "gfx90c", 9/4/2 is "gfx942", 10/3/0 is
// "gfx1030".
//
void gfx_target_to_string(int target, char* buf, int len) {
    if (len <= 0) return;
    if (target <= 0) {
        buf[0] = 0;
        return;
    }
    int major = target / 10000;
    int minor = (target / 100) % 100;
    int stepping = target % 100;
    snprintf(buf, len, "gfx%d%x%x",
        major, (unsigned)minor, (unsigned)stepping
    );
}

void COPROC_NVIDIA::set_peak_flops() {
    double x=0;
    int flops_per_clock=0, cores_per_proc=0;

    if (cuda_prop.major || opencl_prop.nv_compute_capability_major) {
        int major = cuda_prop.major;
        int minor = cuda_prop.minor;

        if (opencl_prop.nv_compute_capability_major) major = opencl_prop.nv_compute_capability_major;
        if (opencl_prop.nv_compute_capability_minor) minor = opencl_prop.nv_compute_capability_minor;

        switch (major) {
        case 1:
            flops_per_clock = 3;
            cores_per_proc = 8;
            break;
        case 2:
            flops_per_clock = 2;
            switch (minor) {
            case 0:
                cores_per_proc = 32;
                break;
            default:
                cores_per_proc = 48;
                break;
            }
            break;
        case 3:
            flops_per_clock = 2;
            cores_per_proc = 192;
            break;
        case 5:
            flops_per_clock = 2;
            cores_per_proc = 128;
            break;
        case 6:
            flops_per_clock = 2;
            switch (minor) {
            case 0:    // special for Tesla P100 (GP100)
                cores_per_proc = 64;
                break;
            default:
                cores_per_proc = 128;
                break;
            }
            break;
        case 7:    // for both cc7.0 (Titan V, Tesla V100) and cc7.5 (RTX, Tesla T4)
            flops_per_clock = 2;
            cores_per_proc = 64;
            break;
        case 8:    // for cc8.0 (A100) and cc8.6 (GeForce RTX 30x0 - GA102)
            flops_per_clock = 2;
            switch (minor) {
            case 0:    // special for A100 Tensor Core datacenter GPU
                cores_per_proc = 64;
                break;
            default:
                cores_per_proc = 128;
                break;
            }
            break;
        default:   // for cc9.0-12.0 (and above) (Hopper Datacenter H100/H200, Blackwell Datacenter B200, Blackwell GeForce 50x0)
            flops_per_clock = 2;
            cores_per_proc = 128;
            break;
        }

    }

    if (cuda_prop.clockRate) {
        // clock rate is scaled down by 1000
        //
        x = (1000.*cuda_prop.clockRate)
            * cuda_prop.multiProcessorCount
            * cores_per_proc
            * flops_per_clock
        ;
    } else if (opencl_prop.nv_compute_capability_major) {

        // OpenCL w/ cl_nv_device_attribute_query extension
        // Per: https://www.khronos.org/registry/cl/extensions/nv/cl_nv_device_attribute_query.txt
        //
        // The theoretical single-precision processing power of a Maxwell GPU in GFLOPS is computed as 2 (operations per FMA instruction per CUDA core per cycle) × number of CUDA cores × core clock speed (in GHz).
        // Per: https://en.wikipedia.org/wiki/Maxwell_(microarchitecture)#Performance
        // Per: https://en.wikipedia.org/wiki/List_of_Nvidia_graphics_processing_units
        //
        // clock is in MHz
        //
        x = opencl_prop.max_compute_units * cores_per_proc * flops_per_clock * (opencl_prop.max_clock_frequency * 1e6);

    } else if (opencl_prop.max_compute_units) {
        // OpenCL doesn't give us compute capability.
        // assume CC 2: cores_per_proc is 48 and flops_per_clock is 2
        //
        x = opencl_prop.max_compute_units * 48 * 2 * opencl_prop.max_clock_frequency * 1e6;
    }
    peak_flops = x;

    peak_flops_dp = 0;
    have_fp64 = false;
    int dp_major = cuda_prop.major;
    int dp_minor = cuda_prop.minor;
    if (opencl_prop.nv_compute_capability_major) {
        dp_major = opencl_prop.nv_compute_capability_major;
        dp_minor = opencl_prop.nv_compute_capability_minor;
    }
    double ratio = nvidia_fp64_ratio(dp_major, dp_minor);
    if (ratio > 0 && x > 0) {
        peak_flops_dp = x * ratio;
        have_fp64 = true;
    } else if (x > 0) {
        // No FP64 ratio applies: either the compute capability is unknown
        // (no CUDA properties and no NVIDIA extension) or the table does
        // not cover the architecture.  Fall back to what OpenCL reports,
        // matching COPROC_ATI::set_peak_flops(): the device may still
        // support FP64.
        //
        have_fp64 = (opencl_prop.double_fp_config != 0);
    }
}

// fake a NVIDIA GPU (for debugging)
//
void COPROC_NVIDIA::fake(
    int driver_version, double ram, double avail_ram, int n
) {
    static const COPROC_NVIDIA x;
    *this = x;
    safe_strcpy(type, proc_type_name_xml(PROC_TYPE_NVIDIA_GPU));
    count = n;
    for (int i=0; i<count; i++) {
        device_nums[i] = i;
    }
    available_ram = avail_ram;
    display_driver_version = driver_version;
    cuda_version = 5000;
    have_cuda = true;
    safe_strcpy(cuda_prop.name, "Fake NVIDIA GPU");
    cuda_prop.totalGlobalMem = ram;
    cuda_prop.sharedMemPerBlock = 100;
    cuda_prop.regsPerBlock = 8;
    cuda_prop.warpSize = 10;
    cuda_prop.memPitch = 10;
    cuda_prop.maxThreadsPerBlock = 20;
    cuda_prop.maxThreadsDim[0] = 2;
    cuda_prop.maxThreadsDim[1] = 2;
    cuda_prop.maxThreadsDim[2] = 2;
    cuda_prop.maxGridSize[0] = 10;
    cuda_prop.maxGridSize[1] = 10;
    cuda_prop.maxGridSize[2] = 10;
    cuda_prop.totalConstMem = 10;
    cuda_prop.major = 1;
    cuda_prop.minor = 2;
    cuda_prop.clockRate = 1250000;
    cuda_prop.textureAlignment = 1000;
    cuda_prop.multiProcessorCount = 14;
    have_opencl = true;
    safe_strcpy(opencl_prop.opencl_device_version, "OpenCL 3.17");
    opencl_prop.opencl_device_version_int = 317;
    set_peak_flops();
}

////////////////// ATI STARTS HERE /////////////////

#ifndef _USING_FCGI_
void COPROC_ATI::write_xml(MIOFILE& f, bool scheduler_rpc) {
    f.printf(
        "<coproc_ati>\n"
        "   <count>%d</count>\n"
        "   <name>%s</name>\n"
        "   <available_ram>%f</available_ram>\n"
        "   <have_cal>%d</have_cal>\n"
        "   <have_opencl>%d</have_opencl>\n"
        "   <have_hip>%d</have_hip>\n"
        "   <hip_version>%d</hip_version>\n"
        "   <hip_device_num>%d</hip_device_num>\n"
        "   <hip_cu_count>%d</hip_cu_count>\n"
        "   <hip_lane_count>%d</hip_lane_count>\n"
        "   <hip_clock_khz>%d</hip_clock_khz>\n"
        "   <gfx_target_version>%d</gfx_target_version>\n"
        "   <gcn_arch>%s</gcn_arch>\n",
        count,
        name,
        available_ram,
        have_cal ? 1 : 0,
        have_opencl ? 1 : 0,
        have_hip ? 1 : 0,
        hip_version,
        hip_device_num,
        hip_cu_count,
        hip_lane_count,
        hip_clock_khz,
        gfx_target_version,
        gcn_arch
    );
    if (scheduler_rpc) {
        write_request(f);
    }
    f.printf(
        "   <peak_flops>%f</peak_flops>\n"
        "   <peak_flops_dp>%f</peak_flops_dp>\n"
        "   <have_fp64>%d</have_fp64>\n"
        "   <CALVersion>%s</CALVersion>\n"
        "   <target>%d</target>\n"
        "   <localRAM>%d</localRAM>\n"
        "   <uncachedRemoteRAM>%d</uncachedRemoteRAM>\n"
        "   <cachedRemoteRAM>%d</cachedRemoteRAM>\n"
        "   <engineClock>%u</engineClock>\n"
        "   <memoryClock>%d</memoryClock>\n"
        "   <wavefrontSize>%d</wavefrontSize>\n"
        "   <numberOfSIMD>%d</numberOfSIMD>\n"
        "   <doublePrecision>%d</doublePrecision>\n"
        "   <pitch_alignment>%d</pitch_alignment>\n"
        "   <surface_alignment>%d</surface_alignment>\n"
        "   <maxResource1DWidth>%d</maxResource1DWidth>\n"
        "   <maxResource2DWidth>%d</maxResource2DWidth>\n"
        "   <maxResource2DHeight>%d</maxResource2DHeight>\n",
        peak_flops,
        peak_flops_dp,
        have_fp64 ? 1 : 0,
        version,
        attribs.target,
        attribs.localRAM,
        attribs.uncachedRemoteRAM,
        attribs.cachedRemoteRAM,
        attribs.engineClock,
        attribs.memoryClock,
        attribs.wavefrontSize,
        attribs.numberOfSIMD,
        attribs.doublePrecision,
        attribs.pitch_alignment,
        attribs.surface_alignment,
        info.maxResource1DWidth,
        info.maxResource2DWidth,
        info.maxResource2DHeight
    );

    if (atirt_detected) {
        f.printf("    <atirt_detected/>\n");
    }

    if (amdrt_detected) {
        f.printf("    <amdrt_detected/>\n");
    }

    if (have_opencl) {
        opencl_prop.write_xml(f, "coproc_opencl");
    }

    f.printf("</coproc_ati>\n");
}
#endif

void COPROC_ATI::clear() {
    static const COPROC_ATI x(0);
    *this = x;
    safe_strcpy(type, proc_type_name_xml(PROC_TYPE_AMD_GPU));
    estimated_delay = -1;
    safe_strcpy(name, "");
    safe_strcpy(version, "");
    atirt_detected = false;
    amdrt_detected = false;
    version_num = 0;
    have_hip = false;
    hip_version = 0;
    hip_device_num = -1;
    hip_cu_count = 0;
    hip_lane_count = 0;
    hip_warp_size = 0;
    hip_clock_khz = 0;
    hip_memory_clock_khz = 0;
    hip_memory_bus_width = 0;
    pci_bus = -1;
    safe_strcpy(pci_bus_id, "");
    gfx_target_version = 0;
    gfxip_major = 0;
    gfxip_minor = 0;
    safe_strcpy(gcn_arch, "");
    is_used = COPROC_USED;
}

int COPROC_ATI::parse(XML_PARSER& xp) {
    int n, retval;

    clear();

    while (!xp.get_tag()) {
        if (xp.match_tag("/coproc_ati")) {
            if (strlen(version)) {
                int major, minor, release;
                n = sscanf(version, "%d.%d.%d", &major, &minor, &release);
                if (n ==3) {
                    version_num = ati_version_int(major, minor, release);
                }
            }

            if (!peak_flops) {
				set_peak_flops();
            }
            if (!available_ram) {
                available_ram = attribs.localRAM*MEGA;
            }
            check_count("ATI");
            return 0;
        }
        if (xp.parse_int("count", count)) continue;
        if (xp.parse_double("peak_flops", peak_flops)) continue;
        if (xp.parse_bool("have_cal", have_cal)) continue;
        if (xp.parse_bool("have_opencl", have_opencl)) continue;
        if (xp.parse_bool("have_hip", have_hip)) continue;
        if (xp.parse_double("peak_flops_dp", peak_flops_dp)) continue;
        if (xp.parse_bool("have_fp64", have_fp64)) continue;
        if (xp.parse_int("hip_version", hip_version)) continue;
        if (xp.parse_int("hip_device_num", hip_device_num)) continue;
        if (xp.parse_int("hip_cu_count", hip_cu_count)) continue;
        if (xp.parse_int("hip_lane_count", hip_lane_count)) continue;
        if (xp.parse_int("hip_clock_khz", hip_clock_khz)) continue;
        if (xp.parse_int("gfx_target_version", gfx_target_version)) {
            gfxip_major = gfx_target_version / 10000;
            gfxip_minor = (gfx_target_version / 100) % 100;
            continue;
        }
        if (xp.parse_str("gcn_arch", gcn_arch, sizeof(gcn_arch))) continue;
        if (xp.parse_double("available_ram", available_ram)) continue;
        if (xp.parse_double("req_secs", req_secs)) continue;
        if (xp.parse_double("req_instances", req_instances)) continue;
        if (xp.parse_double("estimated_delay", estimated_delay)) continue;
        if (xp.parse_str("name", name, sizeof(name))) continue;
        if (xp.parse_str("CALVersion", version, sizeof(version))) continue;
        if (xp.parse_bool("amdrt_detected", amdrt_detected)) continue;
        if (xp.parse_bool("atirt_detected", atirt_detected)) continue;

        if (xp.parse_int("target", n)) {
            attribs.target = (CALtarget)n;
            continue;
        }
        if (xp.parse_int("localRAM", n)) {
            attribs.localRAM = n;
            continue;
        }
        if (xp.parse_int("uncachedRemoteRAM", n)) {
            attribs.uncachedRemoteRAM = n;
            continue;
        }
        if (xp.parse_int("cachedRemoteRAM", n)) {
            attribs.cachedRemoteRAM = n;
            continue;
        }
        if (xp.parse_int("engineClock", n)) {
            attribs.engineClock = n;
            continue;
        }
        if (xp.parse_int("memoryClock", n)) {
            attribs.memoryClock = n;
            continue;
        }
        if (xp.parse_int("wavefrontSize", n)) {
            attribs.wavefrontSize = n;
            continue;
        }
        if (xp.parse_int("numberOfSIMD"  , n)) {
            attribs.numberOfSIMD = n;
            continue;
        }
        if (xp.parse_int("doublePrecision", n)) {
            attribs.doublePrecision = n?CAL_TRUE:CAL_FALSE;
            continue;
        }
        if (xp.parse_int("pitch_alignment", n)) {
            attribs.pitch_alignment = n;
            continue;
        }
        if (xp.parse_int("surface_alignment", n)) {
            attribs.surface_alignment = n;
            continue;
        }
        if (xp.parse_int("maxResource1DWidth", n)) {
            info.maxResource1DWidth = n;
            continue;
        }
        if (xp.parse_int("maxResource2DWidth", n)) {
            info.maxResource2DWidth = n;
            continue;
        }
        if (xp.parse_int("maxResource2DHeight", n)) {
            info.maxResource2DHeight = n;
            continue;
        }
        if (xp.match_tag("coproc_opencl")) {
            retval = opencl_prop.parse(xp, "/coproc_opencl");
            if (retval) return retval;
            continue;
        }
    }
    return ERR_XML_PARSE;
}

// HIP encodes the runtime version as major*10000000 + minor*100000 + patch.
// Keep the integer for comparisons; format it for human-readable output.
//
void format_hip_version(int v, char* buf, int buflen) {
    int major = v / 10000000;
    int minor = (v / 100000) % 100;
    int patch = v % 100000;
    if (patch) {
        snprintf(buf, buflen, "%d.%d.%d", major, minor, patch);
    } else {
        snprintf(buf, buflen, "%d.%d", major, minor);
    }
}

void COPROC_ATI::description(char* buf, int buflen) {
    char dp[64];
    char hip_ver[32];
    if (peak_flops_dp > 0) {
        snprintf(dp, sizeof(dp), ", %s FP64",
            flops_to_string(peak_flops_dp).c_str()
        );
    } else {
        dp[0] = 0;
    }
    if (have_hip) {
        format_hip_version(hip_version, hip_ver, sizeof(hip_ver));
        snprintf(buf, buflen,
            "%s (HIP version %s, %s, %.2fGB, %.2fGB available, %s peak%s)",
            name, hip_ver, gcn_arch[0] ? gcn_arch : "unknown arch",
            attribs.localRAM/1024., available_ram/GIGA,
            flops_to_string(peak_flops).c_str(), dp
        );
        return;
    }
    if (have_opencl) {
        snprintf(buf, buflen,
            "%s (OpenCL, %.2fGB, %.2fGB available, %s peak%s)",
            name, attribs.localRAM/1024., available_ram/GIGA,
            flops_to_string(peak_flops).c_str(), dp
        );
        return;
    }
    snprintf(buf, buflen,
        "%s (CAL version %s, %.2fGB, %.2fGB available, %s peak%s)",
        name, version, attribs.localRAM/1024.,
        available_ram/GIGA, flops_to_string(peak_flops).c_str(), dp
    );
}

void COPROC_ATI::set_peak_flops() {
    double x = 0;
    if (have_hip && hip_clock_khz > 0 && (hip_lane_count > 0 || hip_cu_count > 0)) {
        // Number of FP32 lanes.  KFD gives it directly (simd_count x
        // wavefront size).  Otherwise derive it from the HIP compute unit
        // count: HIP reports one "compute unit" per workgroup processor
        // on RDNA (wave32), i.e. 128 lanes, and one per CU on GCN
        // (wave64), i.e. 64 lanes.
        //
        int lanes = hip_lane_count;
        if (!lanes && hip_cu_count) {
            int lanes_per_cu = 64;
            if (hip_warp_size == 32 || (!hip_warp_size && gfxip_major >= 10)) {
                lanes_per_cu = 128;     // RDNA: HIP reports one WGP
            } else if (gfxip_major == 9 && gfx_target_version >= 90400) {
                lanes_per_cu = 128;     // CDNA 3/4
            }
            lanes = hip_cu_count * lanes_per_cu;
        }
        // One FMA per lane per clock is 2 FLOPs; RDNA 3+ (gfx11/gfx12)
        // dual-issues FP32, i.e. 4 FLOPs.  If the architecture is
        // unknown, stay with the conservative factor.
        //
        double flops_per_lane_per_clock = (gfxip_major >= 11) ? 4. : 2.;
        x = (double)lanes * flops_per_lane_per_clock
            * (hip_clock_khz * 1.e3);
    } else if (attribs.numberOfSIMD) {
        x = attribs.numberOfSIMD * attribs.wavefrontSize * 5 * attribs.engineClock * 1.e6;
        // clock is in MHz
    } else if (opencl_prop.amd_simd_per_compute_unit) {

        // OpenCL w/ cl_amd_device_attribute_query extension
        // Per: https://www.khronos.org/registry/cl/extensions/amd/cl_amd_device_attribute_query.txt
        //
        // Single precision performance is calculated as two times the number of shaders multiplied by the base core clock speed.
        // Per: https://en.wikipedia.org/wiki/List_of_AMD_graphics_processing_units
        //
        // The "2" above is one FMA (2 FLOPs) per lane per clock, which is
        // correct for VLIW, GCN and RDNA 1/2.  From RDNA 3 (gfx11) the
        // SIMD engines dual-issue FP32, i.e. 2 FMAs = 4 FLOPs per lane per
        // clock; without this factor the peak is under-reported by 2x.
        //
        // clock is in MHz
        double flops_per_lane_per_clock = (opencl_prop.gfxip_major >= 11) ? 4. : 2.;
        int lanes;
        if (opencl_prop.gfxip_major >= 10) {
            // RDNA: max_compute_units is a WGP, and SIMD width is 32.
            lanes = opencl_prop.max_compute_units *
                opencl_prop.amd_simd_per_compute_unit *
                opencl_prop.amd_simd_width *
                opencl_prop.amd_simd_instruction_width;
        } else if (!opencl_prop.gfxip_major
            && opencl_prop.amd_simd_width
            && opencl_prop.amd_simd_instruction_width
        ) {
            // Architecture unknown (the driver did not report GFXIP):
            // use the reported SIMD geometry, which is right for both
            // GCN and RDNA.  Keep the conservative 2 FLOPs per lane.
            lanes = opencl_prop.max_compute_units *
                opencl_prop.amd_simd_per_compute_unit *
                opencl_prop.amd_simd_width *
                opencl_prop.amd_simd_instruction_width;
        } else {
            // GCN/CDNA: the reported SIMD width is the wavefront (64), not
            // the number of FP32 lanes.  Each CU has 64 (128 on CDNA 3+).
            int minor = gfxip_minor ? gfxip_minor : (int)opencl_prop.gfxip_minor;
            int lanes_per_cu = (minor >= 4) ? 128 : 64;
            lanes = opencl_prop.max_compute_units * lanes_per_cu;
        }
        x = lanes * flops_per_lane_per_clock
            * (opencl_prop.max_clock_frequency * 1.e6);

    } else if (opencl_prop.max_compute_units) {
        // OpenCL gives us only:
        // - max_compute_units
        //   (which I'll assume is the same as attribs.numberOfSIMD)
        // - max_clock_frequency (which I'll assume is the same as engineClock)
        // It doesn't give wavefrontSize, which can be 16/32/64.
        // So let's be conservative and use 16
        //
        x = opencl_prop.max_compute_units * 16 * 5 * opencl_prop.max_clock_frequency * 1e6;
    }
    peak_flops = x;

    peak_flops_dp = 0;
    have_fp64 = false;
    int gm = gfxip_major ? gfxip_major : (int)opencl_prop.gfxip_major;
    double ratio = amd_fp64_ratio(gm, gfx_target_version, name, pci_info.device_id);
    if (ratio > 0 && x > 0) {
        peak_flops_dp = x * ratio;
        have_fp64 = true;
    } else if (x > 0) {
        // Rate unknown, but the device may still support FP64.
        have_fp64 = (opencl_prop.double_fp_config != 0) || attribs.doublePrecision;
    }
}

void COPROC_ATI::fake(double ram, double avail_ram, int n) {
    clear();
    safe_strcpy(type, proc_type_name_xml(PROC_TYPE_AMD_GPU));
    safe_strcpy(version, "1.4.3");
    safe_strcpy(name, "foobar");
    count = n;
    available_ram = avail_ram;
    have_cal = true;
    attribs.localRAM = (int)(ram/MEGA);
    attribs.numberOfSIMD = 32;
    attribs.wavefrontSize = 32;
    attribs.engineClock = 50;
    for (int i=0; i<count; i++) {
        device_nums[i] = i;
    }
    set_peak_flops();
}

////////////////// INTEL GPU STARTS HERE /////////////////

#ifndef _USING_FCGI_
void COPROC_INTEL::write_xml(MIOFILE& f, bool scheduler_rpc) {
    f.printf(
        "<coproc_intel_gpu>\n"
        "   <count>%d</count>\n"
        "   <name>%s</name>\n"
        "   <available_ram>%f</available_ram>\n"
        "   <have_opencl>%d</have_opencl>\n",
        count,
        opencl_prop.name,
        available_ram,
        have_opencl ? 1 : 0
    );
    if (scheduler_rpc) {
        write_request(f);
    }
    f.printf(
        "   <peak_flops>%f</peak_flops>\n"
        "   <peak_flops_dp>%f</peak_flops_dp>\n"
        "   <have_fp64>%d</have_fp64>\n",
        peak_flops,
        peak_flops_dp,
        have_fp64 ? 1 : 0
    );

    if (have_opencl) {
        opencl_prop.write_xml(f, "coproc_opencl");
    }

    f.printf("</coproc_intel_gpu>\n");
}
#endif

void COPROC_INTEL::clear() {
    static const COPROC_INTEL x(0);
    *this = x;
    safe_strcpy(type, proc_type_name_xml(PROC_TYPE_INTEL_GPU));
    estimated_delay = -1;
    is_used = COPROC_USED;
}

int COPROC_INTEL::parse(XML_PARSER& xp) {
    int retval;

    clear();

    while (!xp.get_tag()) {
        if (xp.match_tag("/coproc_intel_gpu")) {
            if (!peak_flops) {
				set_peak_flops();
            }
            if (!available_ram) {
                available_ram = (double)opencl_prop.global_mem_size;
            }
            check_count("Intel");
            return 0;
        }
        if (xp.parse_int("count", count)) continue;
        if (xp.parse_double("peak_flops", peak_flops)) continue;
        if (xp.parse_double("peak_flops_dp", peak_flops_dp)) continue;
        if (xp.parse_bool("have_fp64", have_fp64)) continue;
        if (xp.parse_bool("have_opencl", have_opencl)) continue;
        if (xp.parse_double("available_ram", available_ram)) continue;
        if (xp.parse_double("req_secs", req_secs)) continue;
        if (xp.parse_double("req_instances", req_instances)) continue;
        if (xp.parse_double("estimated_delay", estimated_delay)) continue;

        if (xp.match_tag("coproc_opencl")) {
            retval = opencl_prop.parse(xp, "/coproc_opencl");
            if (retval) return retval;
            continue;
        }
    }
    return ERR_XML_PARSE;
}

// http://en.wikipedia.org/wiki/Comparison_of_Intel_graphics_processing_units says:
// The raw performance of integrated GPU, in single-precision FLOPS,
// can be calculated as follows:
// EU * 4 [dual-issue x 2 SP] * 2 [multiply + accumulate] * clock speed.
//
// However, there is some question of the accuracy of this due to Intel's
// Turbo Boost and Dynamic Frequency technologies.
//
void COPROC_INTEL::set_peak_flops() {
    double x = 0;
    if (opencl_prop.max_compute_units) {
        x = opencl_prop.max_compute_units * 8 * opencl_prop.max_clock_frequency * 1e6;
    }
    peak_flops = x;
    // Intel OpenCL can report whether FP64 is present, but there is no
    // reliable generic FP64-to-FP32 ratio.  Report capability, not a
    // fabricated peak.
    //
    peak_flops_dp = 0;
    have_fp64 = (opencl_prop.double_fp_config != 0);
}

void COPROC_INTEL::fake(double ram, double avail_ram, int n) {
    safe_strcpy(type, proc_type_name_xml(PROC_TYPE_INTEL_GPU));
    count = n;
    available_ram = avail_ram;
    have_opencl = true;
    for (int i=0; i<count; i++) {
        device_nums[i] = i;
    }
    set_peak_flops();
    opencl_prop.global_mem_size = (cl_ulong)ram;
}

////////////////// APPLE GPU STARTS HERE /////////////////

#ifndef _USING_FCGI_
void COPROC_APPLE::write_xml(MIOFILE& f, bool scheduler_rpc) {
    f.printf(
        "<coproc_apple_gpu>\n"
        "   <count>%d</count>\n"
        "   <model>%s</model>\n"
        "   <available_ram>%f</available_ram>\n"
        "   <have_metal>%d</have_metal>\n"
        "   <have_opencl>%d</have_opencl>\n"
        "   <ncores>%d</ncores>\n"
        "   <metal_support>%d</metal_support>\n",
        count,
        model,
        available_ram,
        have_metal ? 1 : 0,
        have_opencl ? 1 : 0,
        ncores,
        metal_support
    );
    if (scheduler_rpc) {
        write_request(f);
    }
    f.printf(
        "   <peak_flops>%f</peak_flops>\n",
        peak_flops
    );

    if (have_opencl) {
        opencl_prop.write_xml(f, "coproc_opencl");
    }

    f.printf("</coproc_apple_gpu>\n");
}
#endif

void COPROC_APPLE::clear() {
    static const COPROC_APPLE x(0);
    *this = x;
    safe_strcpy(type, proc_type_name_xml(PROC_TYPE_APPLE_GPU));
    estimated_delay = -1;
    is_used = COPROC_USED;
}

int COPROC_APPLE::parse(XML_PARSER& xp) {
    int retval;

    clear();
    while (!xp.get_tag()) {
        if (xp.match_tag("/coproc_apple_gpu")) {
            if (!peak_flops) {
				set_peak_flops();
            }
            if (!available_ram) {
                available_ram = (double)opencl_prop.global_mem_size;
            }
            check_count("Apple");
            return 0;
        }
        if (xp.parse_int("count", count)) continue;
        if (xp.parse_str("model", model, sizeof(model))) continue;
        if (xp.parse_double("peak_flops", peak_flops)) continue;
        if (xp.parse_bool("have_opencl", have_opencl)) continue;
        if (xp.parse_bool("have_metal", have_metal)) continue;
        if (xp.parse_double("available_ram", available_ram)) continue;
        if (xp.parse_int("ncores", ncores)) continue;
        if (xp.parse_int("metal_support", metal_support)) continue;
        if (xp.parse_double("req_secs", req_secs)) continue;
        if (xp.parse_double("req_instances", req_instances)) continue;
        if (xp.parse_double("estimated_delay", estimated_delay)) continue;

        if (xp.match_tag("coproc_opencl")) {
            retval = opencl_prop.parse(xp, "/coproc_opencl");
            if (retval) return retval;
            continue;
        }
    }
    return ERR_XML_PARSE;
}

void COPROC_APPLE::set_peak_flops() {
    if (opencl_prop.max_compute_units) {
        peak_flops = opencl_prop.max_compute_units * 8 * opencl_prop.max_clock_frequency * 1e6;
    } else {
        peak_flops = 1e11;    // default 100 GFLOPS
    }
}

void COPROC_APPLE::fake(double ram, double avail_ram, int n) {
    safe_strcpy(type, proc_type_name_xml(PROC_TYPE_APPLE_GPU));
    count = n;
    available_ram = avail_ram;
    have_opencl = true;
    for (int i=0; i<count; i++) {
        device_nums[i] = i;
    }
    set_peak_flops();
    opencl_prop.global_mem_size = (cl_ulong)ram;
}

///////////////////// END GPU TYPES ///////////////

// processor types (CPU and GPUs) are (confusingly) identified in various ways:
//
// - proc_type (int):
//      PROC_TYPE_NVIDIA_GPU etc.
//      The processor types known to BOINC.
//      -1 if unknown (e.g. returned by OpenCL GPU enumeration)
// - rsc_type (int):
//      index into the coproc.coprocs[] array
//      0 is always CPU
// - name (char*)
//      XML name (like intel_gpu)
//      e.g. <coproc><type>intel_gpu</type>...</coproc>
//      also COPROC.type (confusing)
// - user friendly name (char*)
//      user-facing, e.g. 'Intel GPU'
// - element name (char*)
//      e.g. <coproc_cuda>
//      used in client_state.xml,
//      and within <coproc> elements in sched requests

// TODO: move rsc_name() etc from client_state.cpp;
// make them members of COPROCS

// proc_type to name
// TODO: fix the function name
//
const char* proc_type_name_xml(int pt) {
    switch(pt) {
    case PROC_TYPE_CPU: return "CPU";
    case PROC_TYPE_NVIDIA_GPU: return "NVIDIA";
    case PROC_TYPE_AMD_GPU: return "ATI";
    case PROC_TYPE_INTEL_GPU: return "intel_gpu";
    case PROC_TYPE_APPLE_GPU: return "apple_gpu";
    }
    return "unknown";
}

// proc_type to user friendly name
// TODO: fix the function name
//
const char* proc_type_name(int pt) {
    switch(pt) {
    case PROC_TYPE_CPU: return "CPU";
    case PROC_TYPE_NVIDIA_GPU: return "NVIDIA GPU";
    case PROC_TYPE_AMD_GPU: return "AMD/ATI GPU";
    case PROC_TYPE_INTEL_GPU: return "Intel GPU";
    case PROC_TYPE_APPLE_GPU: return "Apple GPU";
    }
    return "unknown";
}

// name to proc_type
// TODO: fix the function name
//
int coproc_type_name_to_num(const char* name) {
    if (!strcmp(name, "CPU")) return PROC_TYPE_CPU;
    if (!strcmp(name, "CUDA")) return PROC_TYPE_NVIDIA_GPU;
    if (!strcmp(name, "NVIDIA")) return PROC_TYPE_NVIDIA_GPU;
    if (!strcmp(name, "ATI")) return PROC_TYPE_AMD_GPU;
    if (!strcmp(name, "intel_gpu")) return PROC_TYPE_INTEL_GPU;
    if (!strcmp(name, "apple_gpu")) return PROC_TYPE_APPLE_GPU;
    return -1;      // Some other type
}
