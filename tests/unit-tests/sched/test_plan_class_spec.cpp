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
#include "plan_class_spec.h"
#include <cstdio>

static COPROC_AMD_DEVICE make_device(
    const char* name, const char* arch, int device_num, bool fp64,
    double fp32, double fp64_peak, double ram
) {
    COPROC_AMD_DEVICE d;
    snprintf(d.name, sizeof(d.name), "%s", name);
    snprintf(d.gcn_arch, sizeof(d.gcn_arch), "%s", arch);
    d.device_num = device_num;
    d.have_hip = true;
    d.hip_version = 60200000;
    d.have_fp64 = fp64;
    d.peak_flops = fp32;
    d.peak_flops_dp = fp64_peak;
    d.total_ram = ram;
    return d;
}

TEST(test_plan_class_spec, amd_require_fp64) {
    AMD_DEVICE_REQUIREMENTS spec;
    COPROC_AMD_DEVICE fp64 = make_device(
        "MI50", "gfx906", 0, true, 26.5e12, 13.25e12, 32.*1024.*1024.*1024.
    );
    COPROC_AMD_DEVICE consumer = make_device(
        "RX 9060 XT", "gfx1200", 1, false, 22.7e12, 0, 16.*1024.*1024.*1024.
    );
    spec.require_fp64 = true;
    double ram = 0;
    const char* why = NULL;
    EXPECT_TRUE(amd_device_matches(spec, fp64, ram, &why));
    EXPECT_DOUBLE_EQ(ram, fp64.total_ram);
    EXPECT_FALSE(amd_device_matches(spec, consumer, ram, &why));
}

TEST(test_plan_class_spec, amd_arch_and_hip) {
    AMD_DEVICE_REQUIREMENTS spec;
    PLAN_CLASS_REGEX arch_regex;
    ASSERT_EQ(arch_regex.compile("^gfx9", REG_EXTENDED), 0);
    spec.gcn_arch_regex = &arch_regex;
    COPROC_AMD_DEVICE mi = make_device(
        "MI50", "gfx906", 0, true, 26.5e12, 13.25e12, 32.*1024.*1024.*1024.
    );
    COPROC_AMD_DEVICE rdna = make_device(
        "RX 9060 XT", "gfx1200", 1, false, 22.7e12, 0, 16.*1024.*1024.*1024.
    );
    double ram = 0;
    EXPECT_TRUE(amd_device_matches(spec, mi, ram, NULL));
    EXPECT_FALSE(amd_device_matches(spec, rdna, ram, NULL));

    AMD_DEVICE_REQUIREMENTS spec2;
    spec2.require_hip = true;
    COPROC_AMD_DEVICE no_hip = mi;
    no_hip.have_hip = false;
    EXPECT_FALSE(amd_device_matches(spec2, no_hip, ram, NULL));
    spec2.min_hip_version = 60300000;
    EXPECT_FALSE(amd_device_matches(spec2, mi, ram, NULL));
}

static GPU_DEVICE make_gpu_device(
    const char* name, int device_num, int cc_major, int cc_minor,
    int cuda, bool fp64, double fp32, double fp64_peak, double ram,
    int metal = 0
) {
    GPU_DEVICE d;
    snprintf(d.name, sizeof(d.name), "%s", name);
    d.device_num = device_num;
    d.compcap_major = cc_major;
    d.compcap_minor = cc_minor;
    d.cuda_version = cuda;
    d.have_fp64 = fp64;
    d.peak_flops = fp32;
    d.peak_flops_dp = fp64_peak;
    d.total_ram = ram;
    d.metal_support = metal;
    return d;
}

TEST(test_plan_class_spec, nvidia_device_match) {
    GPU_DEVICE_REQUIREMENTS req;
    req.min_compcap = 700;
    GPU_DEVICE p100 = make_gpu_device(
        "Tesla P100", 0, 6, 0, 8000, true, 9.3e12, 4.65e12, 16e9
    );
    GPU_DEVICE v100 = make_gpu_device(
        "Tesla V100", 1, 7, 0, 9000, true, 15.7e12, 7.8e12, 32e9
    );
    double ram = 0;
    EXPECT_FALSE(gpu_device_matches(req, p100, ram, NULL));
    EXPECT_TRUE(gpu_device_matches(req, v100, ram, NULL));
    EXPECT_DOUBLE_EQ(ram, v100.total_ram);

    req.min_compcap = 0;
    req.max_compcap = 600;
    EXPECT_TRUE(gpu_device_matches(req, p100, ram, NULL));
    EXPECT_FALSE(gpu_device_matches(req, v100, ram, NULL));

    GPU_DEVICE_REQUIREMENTS req2;
    req2.min_cuda_version = 9000;
    EXPECT_FALSE(gpu_device_matches(req2, p100, ram, NULL));
    EXPECT_TRUE(gpu_device_matches(req2, v100, ram, NULL));

    GPU_DEVICE_REQUIREMENTS req3;
    req3.require_fp64 = true;
    GPU_DEVICE consumer = make_gpu_device(
        "RTX 4090", 2, 8, 9, 12000, true, 82.6e12, 1.29e12, 24e9
    );
    EXPECT_TRUE(gpu_device_matches(req3, consumer, ram, NULL));
    consumer.have_fp64 = false;
    EXPECT_FALSE(gpu_device_matches(req3, consumer, ram, NULL));
}

TEST(test_plan_class_spec, apple_metal_match) {
    GPU_DEVICE_REQUIREMENTS req;
    req.min_metal_support = 3;
    GPU_DEVICE m1 = make_gpu_device("Apple M1", 0, 0, 0, 0, false, 2.6e12, 0, 8e9, 3);
    GPU_DEVICE m2 = make_gpu_device("Apple M2", 1, 0, 0, 0, false, 3.6e12, 0, 16e9, 4);
    double ram = 0;
    EXPECT_TRUE(gpu_device_matches(req, m1, ram, NULL));
    EXPECT_TRUE(gpu_device_matches(req, m2, ram, NULL));
    req.min_metal_support = 4;
    EXPECT_FALSE(gpu_device_matches(req, m1, ram, NULL));
    EXPECT_TRUE(gpu_device_matches(req, m2, ram, NULL));
}

// Two heterogeneous NVIDIA GPUs are both in the pool; a plan class that
// requires a newer compute capability selects only the newer one.
//
TEST(test_plan_class_spec, nvidia_heterogeneous_set) {
    std::vector<GPU_DEVICE> devices;
    devices.push_back(make_gpu_device(
        "GTX 1080", 0, 6, 1, 9000, true, 8.9e12, 0.28e12, 8e9
    ));
    devices.push_back(make_gpu_device(
        "RTX 4090", 1, 8, 9, 12000, true, 82.6e12, 1.29e12, 24e9
    ));

    GPU_DEVICE_REQUIREMENTS req;
    req.min_compcap = 800;
    int nums[MAX_COPROC_INSTANCES];
    int best = -1;
    double min_ram = 0;
    int n = gpu_matching_devices(
        req, devices, nums, MAX_COPROC_INSTANCES, best, min_ram
    );
    ASSERT_EQ(n, 1);
    EXPECT_EQ(nums[0], 1);
    EXPECT_EQ(best, 1);

    GPU_DEVICE_REQUIREMENTS none;
    n = gpu_matching_devices(
        none, devices, nums, MAX_COPROC_INSTANCES, best, min_ram
    );
    EXPECT_EQ(n, 2);
    EXPECT_DOUBLE_EQ(min_ram, 8e9);
}

// Every device the client reported is a candidate: the list it sends has
// already had <ignore_gpu_instance> applied.  A COPROC that arrived from a
// client document has its device_nums array zeroed (that array is the client's
// own index mapping and is never transmitted), and filtering by it used to
// admit device 0 only -- no work at all for a host whose usable devices are
// 1..N.
//
TEST(test_plan_class_spec, zeroed_device_nums_is_not_a_filter) {
    std::vector<GPU_DEVICE> devices;
    devices.push_back(make_gpu_device(
        "RTX 4090", 0, 8, 9, 12000, true, 82.6e12, 1.29e12, 24e9
    ));
    devices.push_back(make_gpu_device(
        "RTX 4090", 1, 8, 9, 12000, true, 82.6e12, 1.29e12, 24e9
    ));
    devices.push_back(make_gpu_device(
        "RTX 4090", 2, 8, 9, 12000, true, 82.6e12, 1.29e12, 24e9
    ));

    int nums[MAX_COPROC_INSTANCES];
    int best = -1;
    double min_ram = 0;
    GPU_DEVICE_REQUIREMENTS req;
    int n = gpu_matching_devices(
        req, devices, nums, MAX_COPROC_INSTANCES, best, min_ram
    );
    EXPECT_EQ(n, 3);
    EXPECT_EQ(nums[2], 2);

    // The same, through a COPROC as parsed from a client: device_nums zeroed.
    //
    COPROC_AMD cp;
    cp.clear();
    cp.count = 2;
    cp.devices.push_back(make_device(
        "RX 9060 XT", "gfx1200", 1, false, 22.7e12, 0, 16.*1024.*1024.*1024.
    ));
    cp.devices.push_back(make_device(
        "RX 9070 XT", "gfx1201", 2, false, 48.7e12, 0, 16.*1024.*1024.*1024.
    ));
    AMD_DEVICE_REQUIREMENTS areq;
    n = amd_matching_devices(areq, cp, nums, MAX_COPROC_INSTANCES, best, min_ram);
    EXPECT_EQ(n, 2);
    EXPECT_EQ(nums[0], 1);
    EXPECT_EQ(nums[1], 2);
    EXPECT_EQ(best, 1);
}

TEST(test_plan_class_spec, vendor_matching_set) {
    GPU_DEVICE_REQUIREMENTS req;
    req.min_compcap = 800;
    std::vector<GPU_DEVICE> devices;
    devices.push_back(make_gpu_device(
        "A100", 0, 8, 0, 11000, true, 19.5e12, 9.7e12, 40e9
    ));
    devices.push_back(make_gpu_device(
        "RTX 3080", 1, 8, 6, 11000, true, 29.8e12, 0.93e12, 10e9
    ));
    devices.push_back(make_gpu_device(
        "GTX 1080", 2, 6, 1, 9000, true, 8.9e12, 0.28e12, 8e9
    ));
    COPROC cp;
    cp.count = 3;
    int nums[MAX_COPROC_INSTANCES];
    int best = -1;
    double min_ram = 0;
    int n = gpu_matching_devices(
        req, devices, nums, MAX_COPROC_INSTANCES, best, min_ram
    );
    ASSERT_EQ(n, 2);
    EXPECT_EQ(nums[0], 0);
    EXPECT_EQ(nums[1], 1);
    EXPECT_EQ(best, 1);                 // RTX 3080 has the higher FP32
    EXPECT_DOUBLE_EQ(min_ram, 10e9);    // smallest match
}

TEST(test_plan_class_spec, amd_matching_set) {
    AMD_DEVICE_REQUIREMENTS req;
    req.require_fp64 = true;

    COPROC_AMD cp;
    cp.clear();
    cp.devices.push_back(make_device(
        "MI50", "gfx906", 0, true, 26.5e12, 13.25e12, 32.*1024.*1024.*1024.
    ));
    cp.devices.push_back(make_device(
        "RX 9060 XT", "gfx1200", 1, false, 22.7e12, 0, 16.*1024.*1024.*1024.
    ));
    cp.count = 2;

    int nums[MAX_COPROC_INSTANCES];
    int best = -1;
    double min_ram = 0;
    int n = amd_matching_devices(req, cp, nums, MAX_COPROC_INSTANCES, best, min_ram);
    ASSERT_EQ(n, 1);
    EXPECT_EQ(nums[0], 0);
    EXPECT_EQ(best, 0);
    EXPECT_DOUBLE_EQ(min_ram, 32.*1024.*1024.*1024.);
}

TEST(test_plan_class_spec, amd_ram_and_peak) {
    AMD_DEVICE_REQUIREMENTS spec;
    spec.min_gpu_ram_mb = 20. * 1024.;
    COPROC_AMD_DEVICE mi = make_device(
        "MI50", "gfx906", 0, true, 26.5e12, 13.25e12, 32.*1024.*1024.*1024.
    );
    COPROC_AMD_DEVICE small = make_device(
        "RX 9060 XT", "gfx1200", 1, false, 22.7e12, 0, 16.*1024.*1024.*1024.
    );
    double ram = 0;
    EXPECT_TRUE(amd_device_matches(spec, mi, ram, NULL));
    EXPECT_FALSE(amd_device_matches(spec, small, ram, NULL));

    AMD_DEVICE_REQUIREMENTS spec2;
    spec2.min_gpu_peak_flops = 24e12;
    EXPECT_TRUE(amd_device_matches(spec2, mi, ram, NULL));
    EXPECT_FALSE(amd_device_matches(spec2, small, ram, NULL));

    AMD_DEVICE_REQUIREMENTS spec3;
    spec3.max_gpu_peak_flops = 24e12;
    EXPECT_FALSE(amd_device_matches(spec3, mi, ram, NULL));
    EXPECT_TRUE(amd_device_matches(spec3, small, ram, NULL));

    AMD_DEVICE_REQUIREMENTS spec4;
    spec4.min_gpu_dp_flops = 10e12;
    EXPECT_TRUE(amd_device_matches(spec4, mi, ram, NULL));
    EXPECT_FALSE(amd_device_matches(spec4, small, ram, NULL));
}
