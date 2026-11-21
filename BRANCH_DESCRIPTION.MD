# PR 7 — `07-gpu-hip-fp64-per-device-and-flops`

Client and server GPU work: correct FP64/HIP reporting, per-device scheduling
and reporting for every vendor, removal of the dead CAL path, and the
`ATI` → `AMD` naming change while keeping the wire format.

- **Base:** `upstream/master` (`d9fa5fb95`)
- **Head:** `7d5edf79e`
- **18 commits — 55 files — +4722/−2179 — 189 lib tests**

## Why

BOINC treated each GPU type as a pool of *identical* devices. That is wrong on
any heterogeneous host, and it hid FP64 capability badly enough that projects
could not select work properly. Meanwhile the CAL runtime it was built on is
long dead, and HIP — the current AMD path — was not detected at all.

The branch makes three changes that depend on each other:
1. detect HIP and report FP64 honestly,
2. report *every* device with its own capability, and match tasks per device,
3. delete CAL, including the 1,087-line `lib/cal_boinc.h`.

This absorbs the former PR 4 and PR 15; the GPU stack was never separable.

## Commits

### `e442457bb` — lib,client: correct GPU calculations and add HIP detection

*23 files (+1066/−24)*

Adds `client/gpu_hip.cpp` and `lib/hip_boinc.h`. Detects AMD GPUs through the
HIP runtime, reporting architecture (`gfx1200`), CU, lane count, clock and
memory. Corrects peak-FLOP arithmetic for GCN/GCN/CDNA/RDNA: RDNA 3+ dual-issues
FP32 (4 FLOP per lane per clock, not 2). Adds 123 lines of `test_coproc`
coverage for the vendor round-trips.

### `7a573806b` — client,server: add FP64 plan selection and HIP correlation

*7 files (+164/−98)*

`plan_class_spec` gains `require_fp64`, `min_gpu_dp_flops` and HIP requirements.
Correlates HIP devices with their OpenCL counterparts. Removes the old
`gpu_amd.cpp` CAL-based path (−86 lines).

### `1b33933bb` — server,client: expose HIP and gfx family selection, format and preserve HIP reporting, and load versioned amdhip64 DLLs

*8 files (+267/−60)*

Plan-class options `min_hip_version` and `gcn_arch_regex`. `html/inc/host.inc`
shows the full HIP runtime version. `hostinfo.php` switch fallthrough fixed.
Windows loads versioned `amdhip64*.dll`.

### `555566c44` — client,lib: report FP64 for OpenCL, CUDA and Intel, and use one AMD GPU label

*6 files (+87/−11)*

FP64 capability is derived from `cl_khr_fp64` / `double_fp_config` for OpenCL,
from compute capability for CUDA, and reported for Intel. One consistent AMD
label across every path.

### `1295bf3a0` — lib,client: bound the OpenCL instance arrays and the AMD available-RAM lookup, and escape the GPU summary JSON

*8 files (+627/−104)*

Bounds `MAX_COPROC_INSTANCES` overruns in the OpenCL instance arrays, the AMD
available-RAM lookup and the per-device JSON. 242 lines of new tests.

### `ce8a4ef6f` — client,lib,sched: remove CAL, report every AMD GPU, and pin AMD tasks to compatible devices

*16 files (+914/−576)*

First half of the CAL removal (−396 lines from `gpu_amd.cpp`). AMD tasks can be
pinned to specific devices via `<gpu_device_nums>`, carried through
`sched_types` write/parse. 273 lines of tests.

### `082f9d32a` — client,lib,sched: per-device matching and reporting for NVIDIA, Intel and Apple, including every usable device and free VRAM

*10 files (+891/−26)*

The same per-device model for the other three vendors: every usable device is
included in the pool with its own memory and FP64, matched individually, and
shown on the host page.

### `1f4c1110b` — lib: bound the per-device JSON for host.misc

*1 file (+26/−17)*

`summary_json` / `device_json` truncate mid-object on a small buffer by design;
the sizes are now bounded so the result stays well-formed.

### `9ce2155d1` — lib,client,sched: label AMD GPUs as AMD while keeping the ATI wire name, and carry per-device lists through detection

*8 files (+225/−25)*

Display name becomes AMD; the wire string stays `ATI` because an unknown type
name is silently dropped by older peers (`coproc_type_name_to_num()` returns
−1). Per-device lists survive the child-process detection boundary.

### `84bc607f9` — web: escape client-reported GPU model names

*2 files (+5/−3)*

`host.inc` and `gpu_list.php` printed the model name unescaped; a client can
send any string.

### `e4ab0753c` — client,sched: follow BOINC coding style in the GPU work

*3 files (+39/−31)*

Consistency pass over the new code.

### `d048eee34` — lib: deduplicate the per-device JSON loops

*1 file (+19/−18)*

Two near-identical loops collapsed into one.

### `9d258d1d0` — sched: correct the AMD driver-version log message

*1 file (+7/−2)*

The message printed the wrong field.

### `eb2e1db61` — client,lib: report a task pinned to absent GPU devices, accept per-device entries in generic coproc records, and abort such a task

*7 files (+216/−49)*

If a result is pinned to devices this host lacks, the client now says so and
aborts the task rather than mis-scheduling it. Generic `<coproc>` records accept
`<device>` entries so `cc_config.xml` can describe devices.

### `4dca15085` — client,lib: remove the last CAL references, rename ATI identifiers to AMD, and share the HIP lanes-per-CU rule

*26 files (+421/−1644)*

Deletes `lib/cal_boinc.h` (1,087 lines) and every remaining CAL-only path,
including the `have_cal`/`CALVersion`/`target` wire tags. Renames `ati_*`
identifiers to `amd_*` throughout. The HIP lanes-per-CU rule moves to one shared
place used by both call sites.

### `cf3932f4c` — web,lib: escape host-reported output file names and device names in the per-device XML

*6 files (+164/−15)*

Device names went into `<device><name>` raw; a name containing `&` or `<` made
the file the client had just written unparseable. Escaping alone is not enough —
`parse_str()` unescapes in place into a 256-byte field and would overflow — so
reads go through `parse_escaped_name()` with a worst-case buffer. Also escapes
host-reported output file names in `result.inc` and the forum preview title.

### `16d87bd6e` — sched,lib: report the real total GPU FLOPS for a mixed host

*4 files (+91/−3)*

The scheduler computed `p_gpu_fpops` as `count * peak_flops`, which assumes
every device matches the fastest one. That held while `count` meant
"equivalent devices", but this branch makes it mean "all devices", so a
20 TFLOPS + 5 TFLOPS host was recorded as 40 TFLOPS instead of 25.

Now sums the per-device peaks the client already sends
(`COPROC::sum_device_peak_flops()`), falling back to the aggregate when a
client sends none. This exposed a second bug: `COPROC_AMD` declares its own
richer `devices` member which shadows `COPROC::devices`, so AMD parse filled
only the shadow and generic scheduler code saw an empty list — every AMD host
would have summed to zero. Both are populated now.

## Testing

189 lib tests (including `test_coproc` and `test_plan_class_spec`), 10 sched, 3
client. Validated on an AMD RX 9060 XT (RDNA4): `gfx1200`, 16 CUs, 2048 lanes,
FP64 exactly 1/32 — see "Hardware validation" in `pr-7-gpu-devices.md`.

## Notes

- **Wire compatibility is why `ATI` stays.** Renaming it to `AMD` would make
  stock clients and servers drop the resource entirely.
- **The aggregate scalars are unchanged**, so an old client against an old
  server behaves exactly as before. `p_gpu_fpops` is a host-record/display
  field only; nothing schedules on it.
- **`available_ram` is free VRAM for HIP and total VRAM for OpenCL**, and that
  is not a miscalculation. The client log and the coproc XML print
  `total_ram` then `available_ram`, so this HIP path reports e.g.
  `15.92GB, 15.78GB available` while the same card's OpenCL line reports
  `15.92GB, 15.92GB available`. HIP fills the field from `hipMemGetInfo()`
  free bytes, the same convention the CUDA/NVML path already uses; OpenCL has
  no free-memory query, so it copies `CL_DEVICE_GLOBAL_MEM_SIZE` into both
  slots (pre-existing upstream behaviour, unchanged here). Nothing schedules
  on it: per-device matching tests `total_ram` against `min_gpu_ram_mb`, and
  the aggregate AMD path uses `opencl_prop.global_mem_size` with a
  `total_ram` fallback. The one remaining consumer is the legacy
  `app_plan_amd()` path, which compares it against a fixed per-plan-class
  minimum far below any real card's free VRAM.
- This branch is the one to discuss before sending: it is 4,700 lines and
  includes a deliberate behaviour change (every usable device is now counted).
  It splits along the existing commit boundaries if finer PRs are wanted —
  CAL removal, HIP detection, per-device pinning, FP64 correction, and the
  web/sample escaping are separable.

## Fixups from the review

- `7d5edf79e` — **sched,lib: start the NVIDIA per-device object from the
  aggregate.** `cp_dev_n` was default-constructed and only the CUDA fields were
  filled in, so `have_opencl`, `opencl_prop` and `have_cuda` kept their cleared
  values: every plan class that also sets `<opencl/>` (`opencl_nvidia_101` and
  the classes projects use) was rejected on any host whose client reports a
  device list, and `gpu_ram` would have been zeroed. Copying the aggregate
  overwrote the per-device fields `gpu_select_devices()` had just set, so
  `available_ram`, `have_fp64`, `peak_flops` and `peak_flops_dp` are re-applied
  from the selected device, as the Intel and Apple paths do — otherwise
  `require_fp64`, `min_gpu_dp_flops` and `min`/`max_gpu_peak_flops` would be
  matched against the fastest device on the host rather than the one the task
  would get. The same commit drops the
  deleted `lib/cal_boinc.h` from the two CMake header lists -- it was still in
  `LIBBOINC_HEADERS_GENERIC`, so `cmake --install` and the vcpkg port failed on
  a missing file.


## AI assistance

This file and the commit that adds it were produced with AI assistance
(Mistral Vibe). That commit carries an `Assisted-by:` trailer, as BOINC's
"Contributing to BOINC with AI assistants" asks for AI-assisted commits. It
carries no `Co-authored-by:` trailer: BOINC's `AGENTS.md` bans crediting an AI
that way.

The contributor of record is the human who opens the pull request, and they are
responsible for the whole change: they read the full diff, verified it, ran and
inspected the tests, and can speak to its licensing and its origin.
