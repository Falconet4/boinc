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

#include "config.h"
#include <cstring>
#include <string>
#include <gtest/gtest.h>

#include "coproc.h"

// GCN has one FMA per lane per clock: 2 FLOPs.
//
TEST(test_coproc, peak_flops_gcn) {
    COPROC_ATI c;
    c.clear();
    c.have_hip = true;
    c.hip_cu_count = 64;
    c.hip_clock_khz = 1000000;      // 1 GHz
    c.gfxip_major = 9;              // gfx900
    c.set_peak_flops();
    EXPECT_DOUBLE_EQ(c.peak_flops, 64. * 64. * 2. * 1.e9);
}

// RDNA 1/2 has no dual issue.
//
TEST(test_coproc, peak_flops_rdna2) {
    COPROC_ATI c;
    c.clear();
    c.have_hip = true;
    c.hip_lane_count = 2560;            // 40 CUs
    c.hip_clock_khz = 1905000;
    c.gfxip_major = 10;
    c.set_peak_flops();
    EXPECT_DOUBLE_EQ(c.peak_flops, 40. * 64. * 2. * 1.905e9);
}

// RDNA 4 dual-issues FP32: 4 FLOPs per lane per clock.
// RX 9060 XT: 32 CU, 2780 MHz -> 22.77 TFLOPS.
//
TEST(test_coproc, peak_flops_rdna4) {
    COPROC_ATI c;
    c.clear();
    c.have_hip = true;
    c.hip_lane_count = 2048;            // 32 CUs
    c.hip_clock_khz = 2780000;
    c.gfxip_major = 12;
    c.set_peak_flops();
    EXPECT_DOUBLE_EQ(c.peak_flops, 32. * 64. * 4. * 2.78e9);
}

// The same hardware seen through OpenCL: 16 "compute units", 4 SIMD32
// each, 2780 MHz.  The gfxip version must double the result compared
// with a pre-RDNA3 part.
//
TEST(test_coproc, peak_flops_opencl_dual_issue) {
    COPROC_ATI c;
    c.clear();
    c.opencl_prop.max_compute_units = 16;
    c.opencl_prop.amd_simd_per_compute_unit = 4;
    c.opencl_prop.amd_simd_width = 32;
    c.opencl_prop.amd_simd_instruction_width = 1;
    c.opencl_prop.max_clock_frequency = 2780;

    c.opencl_prop.gfxip_major = 10;
    c.set_peak_flops();
    double old = c.peak_flops;

    c.opencl_prop.gfxip_major = 12;
    c.set_peak_flops();
    EXPECT_DOUBLE_EQ(c.peak_flops, 2. * old);
    EXPECT_DOUBLE_EQ(c.peak_flops, 16. * 4. * 32. * 1. * 4. * 2.78e9);
}

// Estimated double-precision rates.  RDNA 4 is 1/32 of FP32, RDNA 2 is
// 1/16, and an unknown architecture reports no estimate.
//
TEST(test_coproc, fp64_rdna4) {
    COPROC_ATI c;
    c.clear();
    c.have_hip = true;
    c.hip_lane_count = 2048;
    c.hip_clock_khz = 2780000;
    c.gfxip_major = 12;
    c.set_peak_flops();
    EXPECT_TRUE(c.have_fp64);
    EXPECT_DOUBLE_EQ(c.peak_flops_dp, c.peak_flops / 32.);
}

TEST(test_coproc, fp64_rdna2) {
    COPROC_ATI c;
    c.clear();
    c.have_hip = true;
    c.hip_lane_count = 2048;
    c.hip_clock_khz = 2780000;
    c.gfxip_major = 10;
    c.set_peak_flops();
    EXPECT_TRUE(c.have_fp64);
    EXPECT_DOUBLE_EQ(c.peak_flops_dp, c.peak_flops / 16.);
}

// gfx906 covers both MI50/MI60 (1:2) and Radeon VII (1:4).  When the
// marketing name is missing, the PCI device ID decides.
//
TEST(test_coproc, fp64_gfx906_device_id) {
    COPROC_ATI c;
    c.clear();
    c.have_hip = true;
    c.hip_lane_count = 2048;
    c.hip_clock_khz = 1746000;
    c.gfxip_major = 9;
    c.gfx_target_version = 90006;
    strcpy(c.name, "gfx906");

    c.pci_info.device_id = 0x66af;      // Radeon VII
    c.set_peak_flops();
    EXPECT_DOUBLE_EQ(c.peak_flops_dp, c.peak_flops / 4.);

    c.pci_info.device_id = 0x66a1;      // MI50 / Radeon Pro VII
    c.set_peak_flops();
    EXPECT_DOUBLE_EQ(c.peak_flops_dp, c.peak_flops / 2.);

    c.pci_info.device_id = 0x66a3;      // MI60 / Radeon Pro Vega II
    c.set_peak_flops();
    EXPECT_DOUBLE_EQ(c.peak_flops_dp, c.peak_flops / 2.);

    c.pci_info.device_id = 0;           // unknown: stay conservative
    c.set_peak_flops();
    EXPECT_DOUBLE_EQ(c.peak_flops_dp, c.peak_flops / 16.);
}

TEST(test_coproc, fp64_unknown_arch) {
    COPROC_ATI c;
    c.clear();
    c.have_hip = true;
    c.hip_lane_count = 2048;
    c.hip_clock_khz = 2780000;
    c.gfxip_major = 0;
    c.set_peak_flops();
    EXPECT_FALSE(c.have_fp64);
    EXPECT_DOUBLE_EQ(c.peak_flops_dp, 0.);
}

// Tesla P100 (compute capability 6.0) has a 1:2 FP64 rate.
//
TEST(test_coproc, fp64_p100_cc60) {
    COPROC_NVIDIA c;
    c.clear();
    c.cuda_prop.major = 6;
    c.cuda_prop.minor = 0;
    c.cuda_prop.clockRate = 1000000;
    c.cuda_prop.multiProcessorCount = 56;
    c.set_peak_flops();
    EXPECT_TRUE(c.have_fp64);
    EXPECT_DOUBLE_EQ(c.peak_flops_dp, c.peak_flops / 2.);
}

// Geforce GTX 1080 (compute capability 6.1, GP104) has a 1:32 FP64
// rate: 8.873 TFLOPS FP32 vs 277.3 GFLOPS FP64.  Only cc6.0 (P100) is
// 1:2.
//
TEST(test_coproc, fp64_cc61) {
    COPROC_NVIDIA c;
    c.clear();
    c.cuda_prop.major = 6;
    c.cuda_prop.minor = 1;
    c.cuda_prop.clockRate = 1000000;
    c.cuda_prop.multiProcessorCount = 20;
    c.set_peak_flops();
    EXPECT_TRUE(c.have_fp64);
    EXPECT_DOUBLE_EQ(c.peak_flops_dp, c.peak_flops / 32.);
}

// cc6.2 (P40/P4) is also 1:32; neither may be reported as 1:2.
//
TEST(test_coproc, fp64_cc62) {
    COPROC_NVIDIA c;
    c.clear();
    c.cuda_prop.major = 6;
    c.cuda_prop.minor = 2;
    c.cuda_prop.clockRate = 1000000;
    c.cuda_prop.multiProcessorCount = 30;
    c.set_peak_flops();
    EXPECT_DOUBLE_EQ(c.peak_flops_dp, c.peak_flops / 32.);
}

// gfx90c (Renoir/Cezanne APUs, target 90012) is a Vega part at 1:16, not
// a CDNA part.  A range test on the gfx target used to give it 1:1.
//
TEST(test_coproc, fp64_gfx90c_is_vega) {
    COPROC_ATI c;
    c.clear();
    c.have_hip = true;
    c.hip_cu_count = 8;
    c.hip_warp_size = 64;
    c.hip_clock_khz = 2100000;
    c.gfxip_major = 9;
    c.gfx_target_version = 90012;
    c.set_peak_flops();
    EXPECT_DOUBLE_EQ(c.peak_flops_dp, c.peak_flops / 16.);
}

// MI100 (gfx908, target 90008) is CDNA 1 at 1:2.
//
TEST(test_coproc, fp64_mi100_one_to_two) {
    COPROC_ATI c;
    c.clear();
    c.have_hip = true;
    c.hip_cu_count = 120;
    c.hip_warp_size = 64;
    c.hip_clock_khz = 1000000;
    c.gfxip_major = 9;
    c.gfx_target_version = 90008;
    c.set_peak_flops();
    EXPECT_DOUBLE_EQ(c.peak_flops_dp, c.peak_flops / 2.);
}

// MI300X (gfx942, target 90402) is CDNA 3 at 1:2.
//
TEST(test_coproc, fp64_mi300_one_to_two) {
    COPROC_ATI c;
    c.clear();
    c.have_hip = true;
    c.hip_cu_count = 304;
    c.hip_warp_size = 64;
    c.hip_clock_khz = 1000000;
    c.gfxip_major = 9;
    c.gfx_target_version = 90402;
    c.set_peak_flops();
    EXPECT_DOUBLE_EQ(c.peak_flops_dp, c.peak_flops / 2.);
}

// KFD target -> architecture name.  The minor and stepping digits are
// rendered as single hex digits, which is what makes gfx90a and gfx90c
// come out right; the old "%d%d%d" rendering produced "gfx9010".
//
static std::string gfx_name(int target) {
    char buf[64];
    memset(buf, 0xAA, sizeof(buf));
    gfx_target_to_string(target, buf, sizeof(buf));
    return std::string(buf);
}

TEST(test_coproc, gfx_target_to_string) {
    EXPECT_EQ(gfx_name(90000), "gfx900");
    EXPECT_EQ(gfx_name(90004), "gfx904");
    EXPECT_EQ(gfx_name(90006), "gfx906");
    EXPECT_EQ(gfx_name(90008), "gfx908");
    EXPECT_EQ(gfx_name(90010), "gfx90a");
    EXPECT_EQ(gfx_name(90012), "gfx90c");
    EXPECT_EQ(gfx_name(90400), "gfx940");
    EXPECT_EQ(gfx_name(90402), "gfx942");
    EXPECT_EQ(gfx_name(90500), "gfx950");
    EXPECT_EQ(gfx_name(100300), "gfx1030");
    EXPECT_EQ(gfx_name(100100), "gfx1010");
    EXPECT_EQ(gfx_name(110000), "gfx1100");
    EXPECT_EQ(gfx_name(110001), "gfx1101");
    EXPECT_EQ(gfx_name(110500), "gfx1150");
    EXPECT_EQ(gfx_name(120000), "gfx1200");
    EXPECT_EQ(gfx_name(120001), "gfx1201");
}

// A target whose minor or stepping does not fit in one digit cannot be a
// real architecture; it must still not overflow the buffer.
//
TEST(test_coproc, gfx_target_to_string_wide_components) {
    EXPECT_EQ(gfx_name(103000), "gfx101e0");
}

// Unknown / missing versions must render as an empty string rather than
// "gfx000" or garbage.
//
TEST(test_coproc, gfx_target_to_string_empty) {
    EXPECT_EQ(gfx_name(0), "");
    EXPECT_EQ(gfx_name(-1), "");
}

// A short buffer must be truncated, not overflowed.
//
TEST(test_coproc, gfx_target_to_string_truncates) {
    char buf[6];
    memset(buf, 0xAA, sizeof(buf));
    gfx_target_to_string(100300, buf, sizeof(buf));
    EXPECT_EQ(buf[5], 0);
    EXPECT_STREQ(buf, "gfx10");
}

TEST(test_coproc, gfx_target_to_string_zero_len) {
    char buf[8];
    memset(buf, 0xAA, sizeof(buf));
    gfx_target_to_string(120000, buf, 0);
    EXPECT_EQ(buf[0], (char)0xAA);     // untouched
}

// CDNA 2 (MI200) has a 1:1 FP64 rate.
//
TEST(test_coproc, fp64_mi200_one_to_one) {
    COPROC_ATI c;
    c.clear();
    c.have_hip = true;
    c.hip_cu_count = 110;
    c.hip_warp_size = 64;
    c.hip_clock_khz = 1700000;
    c.gfxip_major = 9;
    c.gfx_target_version = 90010;
    c.set_peak_flops();
    EXPECT_DOUBLE_EQ(c.peak_flops, 110. * 64. * 2. * 1.7e9);
    EXPECT_DOUBLE_EQ(c.peak_flops_dp, c.peak_flops);
}

// summary_json() fills a caller-supplied buffer; a long model string must
// truncate inside it instead of running past the end.
//
TEST(test_coproc, summary_json_truncates) {
    char buf[80];
    memset(buf, 0xAA, sizeof(buf));
    summary_json(
        buf, 64, "nvidia", "a-model-name-that-is-long-enough-to-need-truncation",
        1, 1024, "a-long-driver-version-string", 101, false, "", 0
    );
    EXPECT_EQ(buf[63], 0);
    EXPECT_LT(strlen(buf), 64);
    for (int i=64; i<80; i++) {
        EXPECT_EQ((unsigned char)buf[i], 0xAA) << "wrote past the buffer";
    }
}

TEST(test_coproc, summary_json_hip_truncates) {
    char buf[80];
    memset(buf, 0xAA, sizeof(buf));
    summary_json(
        buf, 64, "amd", "a-model-name-that-is-long-enough-to-need-truncation",
        1, 16304, "a-long-driver-version-string", 300, true,
        "gfx1200-a-long-architecture-name", 60000000
    );
    EXPECT_EQ(buf[63], 0);
    EXPECT_LT(strlen(buf), 64);
    for (int i=64; i<80; i++) {
        EXPECT_EQ((unsigned char)buf[i], 0xAA) << "wrote past the buffer";
    }
}

TEST(test_coproc, summary_json_escapes_strings) {
    char buf[512];
    summary_json(
        buf, sizeof(buf), "amd", "a\"b\\c", 1, 1, "d\ne", 100, false, "", 0
    );
    std::string s(buf);
    EXPECT_NE(s.find("\"model\": \"a\\\"b\\\\c\""), std::string::npos)
        << s;
    EXPECT_NE(s.find("\"driver_version\": \"d\\ne\""), std::string::npos)
        << s;
}

// Valid UTF-8 passes through; invalid bytes become \u00XX so the JSON
// stays valid.
//
TEST(test_coproc, summary_json_utf8_passthrough) {
    char buf[512];
    summary_json(
        buf, sizeof(buf), "amd", "caf\xC3\xA9", 1, 1, "", 100, false, "", 0
    );
    std::string s(buf);
    EXPECT_NE(s.find("caf\xC3\xA9"), std::string::npos) << s;
}

TEST(test_coproc, summary_json_invalid_utf8_escaped) {
    char buf[512];
    summary_json(
        buf, sizeof(buf), "amd", "\xA9", 1, 1, "", 100, false, "", 0
    );
    std::string s(buf);
    EXPECT_NE(s.find("\\u00a9"), std::string::npos) << s;
    EXPECT_EQ(s.find('\xA9'), std::string::npos) << s;
}

// Control characters expand sixfold; the summary must still be complete
// JSON rather than a truncated object.
//
TEST(test_coproc, summary_json_worst_case_fits) {
    std::string type(255, '\x01');
    std::string model(255, '\x01');
    std::string drv(255, '\x01');
    std::string arch(63, '\x01');
    char buf[8192];
    summary_json(
        buf, sizeof(buf), type.c_str(), model.c_str(), 1, 1,
        drv.c_str(), 100, true, arch.c_str(), 1
    );
    size_t n = strlen(buf);
    EXPECT_LT(n, sizeof(buf));
    EXPECT_EQ(buf[n-1], '}');
    EXPECT_NE(std::string(buf).find("\\u0001"), std::string::npos);
}
