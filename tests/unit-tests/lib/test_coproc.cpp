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
