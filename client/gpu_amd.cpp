// This file is part of BOINC.
// http://boinc.berkeley.edu
// Copyright (C) 2012 University of California
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

// AMD GPU detection.
//
// CAL (AMD Compute Abstraction Layer) is deprecated and no longer used.
// AMD GPUs are detected through HIP, with OpenCL as the fallback.  This
// entry point is kept so existing build files and callers still link; it
// intentionally does nothing.

#include <string>
#include <vector>

#include "coproc.h"

void COPROC_AMD::get(std::vector<std::string>&) {
}
