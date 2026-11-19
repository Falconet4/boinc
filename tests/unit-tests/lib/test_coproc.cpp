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

#include "parse.h"
#include "coproc.h"
#include "cc_config.h"

// GCN has one FMA per lane per clock: 2 FLOPs.
//
TEST(test_coproc, peak_flops_gcn) {
    COPROC_AMD c;
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
    COPROC_AMD c;
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
    COPROC_AMD c;
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
    COPROC_AMD c;
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
    COPROC_AMD c;
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
    COPROC_AMD c;
    c.clear();
    c.have_hip = true;
    c.hip_lane_count = 2048;
    c.hip_clock_khz = 2780000;
    c.gfxip_major = 10;
    c.set_peak_flops();
    EXPECT_TRUE(c.have_fp64);
    EXPECT_DOUBLE_EQ(c.peak_flops_dp, c.peak_flops / 16.);
}

// Every physical AMD GPU is reported individually; aggregate scheduling
// fields remain separate.
//
TEST(test_coproc, parse_device_num_list) {
    int nums[MAX_COPROC_INSTANCES];
    int n = 0;
    parse_device_num_list("0 2 7", nums, n);
    ASSERT_EQ(n, 3);
    EXPECT_EQ(nums[0], 0);
    EXPECT_EQ(nums[1], 2);
    EXPECT_EQ(nums[2], 7);
    parse_device_num_list("  1\t3 bad 999 -1 ", nums, n);
    ASSERT_EQ(n, 2);
    EXPECT_EQ(nums[0], 1);
    EXPECT_EQ(nums[1], 3);
    parse_device_num_list("", nums, n);
    EXPECT_EQ(n, 0);
}

// The wire type must stay "ATI" for compatibility with older peers, while
// the user-visible name is "AMD GPU".
//
// The NVIDIA/Intel/Apple device lists must survive the write/parse round
// trip that the child-process detection path uses.
//
TEST(test_coproc, vendor_device_roundtrip) {
    COPROC_NVIDIA nv;
    nv.clear();
    nv.count = 1;
    GPU_DEVICE g;
    strcpy(g.vendor, "nvidia");
    strcpy(g.name, "Tesla V100");
    g.device_num = 0;
    g.compcap_major = 7;
    g.compcap_minor = 0;
    g.cuda_version = 9000;
    g.driver_version = 440000;
    g.have_fp64 = true;
    g.peak_flops = 15.7e12;
    g.peak_flops_dp = 7.8e12;
    g.total_ram = 32e9;
    nv.devices.push_back(g);
    g.clear();
    strcpy(g.vendor, "nvidia");
    strcpy(g.name, "RTX 4090");
    g.device_num = 1;
    g.compcap_major = 8;
    g.compcap_minor = 9;
    g.have_fp64 = true;
    g.total_ram = 24e9;
    nv.devices.push_back(g);

    char buf[8192];
    MIOFILE w;
    w.init_buf_write(buf, sizeof(buf));
    nv.write_xml(w, false);
    MIOFILE r;
    r.init_buf_read(buf);
    XML_PARSER xp(&r);
    ASSERT_TRUE(xp.parse_start("coproc_cuda"));
    COPROC_NVIDIA back;
    ASSERT_EQ(back.parse(xp), 0);
    ASSERT_EQ(back.devices.size(), 2u);
    EXPECT_STREQ(back.devices[0].name, "Tesla V100");
    EXPECT_EQ(back.devices[0].compcap_major, 7);
    EXPECT_EQ(back.devices[0].cuda_version, 9000);
    EXPECT_DOUBLE_EQ(back.devices[0].total_ram, 32e9);
    EXPECT_STREQ(back.devices[1].name, "RTX 4090");
    EXPECT_EQ(back.devices[1].compcap_minor, 9);
}

// A device name comes from the driver and may contain characters that are
// special in XML.  write_xml must escape them, and the escaped form must parse
// back to the original name - unescaped, the whole coproc_info.xml is
// malformed and detection result is lost.
//
TEST(test_coproc, device_name_xml_escaped) {
    COPROC_NVIDIA nv;
    nv.clear();
    nv.count = 1;
    GPU_DEVICE g;
    strcpy(g.vendor, "nvidia");
    strcpy(g.name, "Radeon & Co <RX 580> \"quoted\"");
    g.device_num = 0;
    g.have_fp64 = true;
    g.total_ram = 8e9;
    nv.devices.push_back(g);

    char buf[8192];
    MIOFILE w;
    w.init_buf_write(buf, sizeof(buf));
    nv.write_xml(w, false);

    // the raw '&' and '<' must not survive into the document
    EXPECT_EQ(strstr(buf, "<name>Radeon & Co"), nullptr);

    MIOFILE r;
    r.init_buf_read(buf);
    XML_PARSER xp(&r);
    ASSERT_TRUE(xp.parse_start("coproc_cuda"));
    COPROC_NVIDIA back;
    ASSERT_EQ(back.parse(xp), 0);
    ASSERT_EQ(back.devices.size(), 1u);
    EXPECT_STREQ(back.devices[0].name, "Radeon & Co <RX 580> \"quoted\"");
}

TEST(test_coproc, intel_device_roundtrip) {
    COPROC_INTEL ig;
    ig.clear();
    ig.count = 1;
    GPU_DEVICE g;
    strcpy(g.vendor, "intel");
    strcpy(g.name, "Intel Arc A770");
    g.device_num = 0;
    g.opencl_version = 300;
    g.have_fp64 = true;
    g.peak_flops = 19.6e12;
    g.peak_flops_dp = 2.4e12;
    g.total_ram = 16e9;
    ig.devices.push_back(g);

    char buf[8192];
    MIOFILE w;
    w.init_buf_write(buf, sizeof(buf));
    ig.write_xml(w, false);
    MIOFILE r;
    r.init_buf_read(buf);
    XML_PARSER xp(&r);
    ASSERT_TRUE(xp.parse_start("coproc_intel_gpu"));
    COPROC_INTEL back;
    ASSERT_EQ(back.parse(xp), 0);
    ASSERT_EQ(back.devices.size(), 1u);
    EXPECT_STREQ(back.devices[0].name, "Intel Arc A770");
    EXPECT_EQ(back.devices[0].opencl_version, 300);
    EXPECT_DOUBLE_EQ(back.devices[0].total_ram, 16e9);
}

TEST(test_coproc, amd_type_names) {
    EXPECT_STREQ(proc_type_name_xml(PROC_TYPE_AMD_GPU), "ATI");
    EXPECT_STREQ(proc_type_name(PROC_TYPE_AMD_GPU), "AMD GPU");
    EXPECT_EQ(coproc_type_name_to_num("ATI"), PROC_TYPE_AMD_GPU);
    EXPECT_EQ(coproc_type_name_to_num("AMD GPU"), -1);
}

TEST(test_coproc, device_num_allowed) {
    int nums[] = {0, 2};
    EXPECT_TRUE(device_num_allowed(NULL, 0, 1));   // no restriction
    EXPECT_TRUE(device_num_allowed(nums, 2, 0));
    EXPECT_TRUE(device_num_allowed(nums, 2, 2));
    EXPECT_FALSE(device_num_allowed(nums, 2, 1));
}

TEST(test_coproc, per_device_list_from_hip) {
    std::vector<COPROC_AMD> hip(2);
    hip[0].clear();
    strcpy(hip[0].name, "AMD Radeon RX 9060 XT");
    strcpy(hip[0].gcn_arch, "gfx1200");
    hip[0].gfx_target_version = 120000;
    hip[0].device_num = 0;
    hip[0].hip_version = 60200000;
    hip[0].have_fp64 = true;
    hip[0].peak_flops = 22.7e12;
    hip[0].peak_flops_dp = 22.7e12/32.;
    hip[0].total_ram = 16384.*1024.*1024.;

    hip[1].clear();
    strcpy(hip[1].name, "AMD Instinct MI50");
    strcpy(hip[1].gcn_arch, "gfx906");
    hip[1].gfx_target_version = 90006;
    hip[1].device_num = 1;
    hip[1].hip_version = 60200000;
    hip[1].have_fp64 = true;
    hip[1].peak_flops = 26.5e12;
    hip[1].peak_flops_dp = 13.25e12;
    hip[1].total_ram = 32768.*1024.*1024.;

    COPROC_AMD agg;
    agg.clear();
    std::vector<OPENCL_DEVICE_PROP> none;
    agg.set_devices(hip, none);
    ASSERT_EQ(agg.devices.size(), 2u);
    EXPECT_STREQ(agg.devices[0].name, "AMD Radeon RX 9060 XT");
    EXPECT_STREQ(agg.devices[0].gcn_arch, "gfx1200");
    EXPECT_EQ(agg.devices[0].device_num, 0);
    EXPECT_DOUBLE_EQ(agg.devices[0].total_ram, 16384.*1024.*1024.);
    EXPECT_STREQ(agg.devices[1].name, "AMD Instinct MI50");
    EXPECT_STREQ(agg.devices[1].gcn_arch, "gfx906");
    EXPECT_EQ(agg.devices[1].device_num, 1);
    EXPECT_DOUBLE_EQ(agg.devices[1].total_ram, 32768.*1024.*1024.);
    EXPECT_DOUBLE_EQ(agg.devices[1].peak_flops_dp, 13.25e12);
}

TEST(test_coproc, per_device_xml_roundtrip) {
    COPROC_AMD a;
    a.clear();
    a.devices.clear();
    COPROC_AMD_DEVICE d;
    strcpy(d.name, "AMD Radeon RX 9060 XT");
    strcpy(d.gcn_arch, "gfx1200");
    d.gfx_target_version = 120000;
    d.device_num = 0;
    d.hip_version = 60200000;
    d.have_fp64 = true;
    d.peak_flops = 22.7e12;
    d.peak_flops_dp = 22.7e12/32.;
    d.total_ram = 16.*1024.*1024.*1024.;
    a.devices.push_back(d);
    d.clear();
    strcpy(d.name, "AMD Instinct MI50");
    strcpy(d.gcn_arch, "gfx906");
    d.gfx_target_version = 90006;
    d.device_num = 1;
    d.have_fp64 = true;
    d.total_ram = 32.*1024.*1024.*1024.;
    a.devices.push_back(d);

    char buf[4096];
    MIOFILE w;
    w.init_buf_write(buf, sizeof(buf));
    a.write_xml(w, false);
    MIOFILE r;
    r.init_buf_read(buf);
    XML_PARSER xp(&r);
    ASSERT_TRUE(xp.parse_start("coproc_ati"));
    COPROC_AMD b;
    ASSERT_EQ(b.parse(xp), 0);
    ASSERT_EQ(b.devices.size(), 2u);
    EXPECT_STREQ(b.devices[0].name, "AMD Radeon RX 9060 XT");
    EXPECT_STREQ(b.devices[0].gcn_arch, "gfx1200");
    EXPECT_EQ(b.devices[0].device_num, 0);
    EXPECT_DOUBLE_EQ(b.devices[0].total_ram, 16.*1024.*1024.*1024.);
    EXPECT_STREQ(b.devices[1].name, "AMD Instinct MI50");
    EXPECT_STREQ(b.devices[1].gcn_arch, "gfx906");
    EXPECT_EQ(b.devices[1].device_num, 1);
    EXPECT_DOUBLE_EQ(b.devices[1].total_ram, 32.*1024.*1024.*1024.);
}

TEST(test_coproc, per_device_list_opencl_only) {
    std::vector<COPROC_AMD> none;
    std::vector<OPENCL_DEVICE_PROP> ocl(1);
    ocl[0].clear();
    strcpy(ocl[0].name, "AMD Radeon Graphics");
    ocl[0].gfxip_major = 10;
    ocl[0].gfxip_minor = 3;
    ocl[0].device_num = 0;
    ocl[0].global_mem_size = 8.*1024.*1024.*1024.;
    ocl[0].peak_flops = 8.0e12;
    ocl[0].peak_flops_dp = 0.5e12;
    ocl[0].have_fp64 = true;

    COPROC_AMD agg;
    agg.clear();
    agg.set_devices(none, ocl);
    ASSERT_EQ(agg.devices.size(), 1u);
    EXPECT_STREQ(agg.devices[0].name, "AMD Radeon Graphics");
    EXPECT_STREQ(agg.devices[0].gcn_arch, "gfx1030");
    EXPECT_TRUE(agg.devices[0].have_fp64);
    EXPECT_DOUBLE_EQ(agg.devices[0].total_ram, 8.*1024.*1024.*1024.);
}

// gfx906 covers both MI50/MI60 (1:2) and Radeon VII (1:4).  When the
// marketing name is missing, the PCI device ID decides.
//
TEST(test_coproc, fp64_gfx906_device_id) {
    COPROC_AMD c;
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
    COPROC_AMD c;
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
    COPROC_AMD c;
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
    COPROC_AMD c;
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
    COPROC_AMD c;
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
    COPROC_AMD c;
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

// CC_CONFIG::write produced a <device><name> from the same GPU_DEVICE data as
// coproc_info.xml, but wrote it raw.  A name containing '&' or '<' made the
// cc_config.xml the client had just written unparseable on the next read.
//
TEST(test_coproc, cc_config_device_name_xml_escaped) {
    // Names the writer must survive: empty, every XML metacharacter, a CDATA
    // terminator, and the worst case for the 6x escape expansion (a 255-byte
    // field of '&' becomes 1275 bytes).
    //
    std::string worst(255, '&');
    const char* names[] = {
        "Radeon & Co <RX 580> \"q\"",
        "",
        "a&b<c>d\"e'f",
        "]]>",
        worst.c_str(),
    };
    for (const char* name : names) {
        CC_CONFIG cc;      // the constructor applies defaults()
        LOG_FLAGS lf;

        cc.config_coprocs.n_rsc = PROC_TYPE_NVIDIA_GPU + 1;
        COPROC& c = cc.config_coprocs.coprocs[PROC_TYPE_NVIDIA_GPU];
        c.clear();
        strcpy(c.type, "NVIDIA GPU");
        c.specified_in_config = true;
        c.count = 1;

        GPU_DEVICE g;
        strcpy(g.vendor, "nvidia");
        strncpy(g.name, name, sizeof(g.name)-1);
        g.name[sizeof(g.name)-1] = 0;
        g.device_num = 0;
        g.have_fp64 = true;
        g.total_ram = 8e9;
        c.devices.push_back(g);

        char buf[16384];
        MIOFILE w;
        w.init_buf_write(buf, sizeof(buf));
        ASSERT_EQ(cc.write(w, lf), 0);

        // When escaping is needed, the raw text must not reach the document.
        // (For names with nothing to escape the raw text is expected.)
        if (strpbrk(name, "&<>\"'")) {
            std::string raw = std::string("<name>") + name + "</name>";
            EXPECT_EQ(strstr(buf, raw.c_str()), nullptr) << "for [" << name << "]";
        }

        MIOFILE r;
        r.init_buf_read(buf);
        XML_PARSER xp(&r);
        ASSERT_TRUE(xp.parse_start("cc_config"));
        CC_CONFIG back;
        LOG_FLAGS lf2;
        ASSERT_EQ(back.parse(xp, lf2), 0) << "for [" << name << "]";

        COPROC& b = back.config_coprocs.coprocs[PROC_TYPE_NVIDIA_GPU];
        ASSERT_EQ(b.devices.size(), 1u) << "for [" << name << "]";
        EXPECT_STREQ(b.devices[0].name, name) << "for [" << name << "]";
    }
}
