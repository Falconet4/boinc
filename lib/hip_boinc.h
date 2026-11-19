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

// Minimal HIP runtime definitions, used to detect AMD GPUs through ROCm.
//
// BOINC loads the HIP runtime with dlopen() and resolves a few entry
// points, exactly as it does for CUDA and OpenCL.  We deliberately do
// not include ROCm headers: hipDeviceProp_t changes layout between ROCm
// releases, so we query individual attributes instead and get the
// architecture name from the kernel's KFD topology where possible.

#ifndef HIP_BOINC_H
#define HIP_BOINC_H

#include <cstddef>

typedef int hipError_t;
#define HIP_SUCCESS 0

// Subset of hipDeviceAttribute_t.  These values are part of the CUDA-
// compatible range of the enum and have been stable across ROCm
// releases.  See ROCm/HIP hip_runtime_api.h.
//
#define HIP_DEV_ATTR_CLOCK_RATE               5
#define HIP_DEV_ATTR_MEMORY_BUS_WIDTH         59
#define HIP_DEV_ATTR_MEMORY_CLOCK_RATE        60
#define HIP_DEV_ATTR_MULTIPROCESSOR_COUNT     63
#define HIP_DEV_ATTR_PCI_BUS_ID               67
#define HIP_DEV_ATTR_PCI_DEVICE_ID            68
#define HIP_DEV_ATTR_WARP_SIZE                87
#define HIP_DEV_ATTR_COMPUTE_CAPABILITY_MAJOR 23
#define HIP_DEV_ATTR_COMPUTE_CAPABILITY_MINOR 61

typedef hipError_t (*HIP_GET_DEVICE_COUNT)(int*);
typedef hipError_t (*HIP_GET_DEVICE_NAME)(char*, int, int);
typedef hipError_t (*HIP_GET_DEVICE_ATTRIBUTE)(int*, int, int);
typedef hipError_t (*HIP_MEM_GET_INFO)(std::size_t*, std::size_t*);
typedef hipError_t (*HIP_RUNTIME_GET_VERSION)(int*);
typedef hipError_t (*HIP_SET_DEVICE)(int);
typedef hipError_t (*HIP_GET_PCI_BUS_ID)(char*, int, int);

#endif
