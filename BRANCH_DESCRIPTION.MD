# PR 6 — `06-ci-sanitizers-and-autotools-quoting`

CI and build hygiene: an ASan/UBSan job for the client, and quoting fixes in the
autotools helpers.

- **Base:** `upstream/master` (`f7e5129c5`)
- **Head:** `d77cd89f3`
- **7 commits — 4 files — +151/−9** (code and tests; this file excluded)

## Why

The series fixes a lot of undefined behaviour and buffer handling. None of it is
worth much without something that fails the build when it regresses. This adds
a sanitizer job that actually runs the client's GPU detection under ASan and
UBSan, and repairs two quoting bugs that break `_autosetup` on paths with
spaces.

## Commits

### `b8393f789` — ci: build the client with ASan/UBSan and run GPU detection

*1 file (+126/−0)* `.github/workflows/sanitizers.yml`

Builds the client and libraries with `-fsanitize=address,undefined`, then:
- runs `client/boinc --detect_gpus` and parses the `coproc_info.xml` it writes,
  so a malformed report fails the job,
- verifies with `nm` that the binaries really are instrumented (a build that
  silently dropped the flags would otherwise pass),
- shows the sanitizer output, which `diagnostics_init()` diverts into the data
  directory rather than the job log, and fails if those files are missing.

The client is configured with `--enable-dynamic-client-linkage`: the default is
a fully static link, which cannot carry ASan/UBSan at all.

The unit-test suites are **not** run by this job — `make check` needs the server
side, which the job configures out — so they remain the developer's step.

The pinned checkout SHA keeps the job reproducible.

### `ee30cd6f5` — build: quote directories in the autotools helpers

*2 files (+7/−8)* `_autosetup`, `m4/sah_check_lib.m4`

Unquoted `$VAR` expansions broke when the source path contained a space. Now
quoted throughout.

### `add0211c1` — ci: run the sanitizer job for sched/ and tools/ changes

*1 file (+4/−0)*

The `paths:` filters only listed client and lib directories, so a change
touching only `sched/` or `tools/` did not trigger the job. Both added to the
push and pull-request trigger lists.

## Testing

The workflow runs on GitHub. The same commands were run by hand here under
ASan/UBSan while developing the series; the `nm` checks are what stop a silent
loss of the sanitizer flags from passing.

## Notes

Nothing in this branch changes product behaviour — it is purely a safety net
and a build fix. If it is contentious, it can be dropped without affecting the
other nine.

## AI assistance

This file and the commit that adds it were produced with AI assistance
(Mistral Vibe). That commit carries an `Assisted-by:` trailer, as BOINC's
"Contributing to BOINC with AI assistants" asks for AI-assisted commits. It
carries no `Co-authored-by:` trailer: BOINC's `AGENTS.md` bans crediting an AI
that way.

The contributor of record is the human who opens the pull request, and they are
responsible for the whole change: they read the full diff, verified it, ran and
inspected the tests, and can speak to its licensing and its origin.

## Commits added after this description was written

The `## Commits` section above was written when the branch was shorter.  The
commits below came later, from the review passes and the Linux test work.

### `85ffee21f` — ci: link the client dynamically so the sanitizer build can exist

configure without --enable-client-release and without
--enable-dynamic-client-linkage makes client/Makefile.am add -static, and a
fully static executable cannot carry ASan/UBSan: gcc has no static libasan to
link against, so this job would either fail at link or produce a binary whose
instrumentation is not usable.  Pass --enable-dynamic-client-linkage.

### `e729287e4` — ci: stop the sanitizer job claiming coverage of sched/ and tools/

The paths: triggers listed sched/** and tools/**, but the job configures with
--disable-server --disable-manager and so never builds either.  A PR touching
only those directories woke a job that cannot see its code.  Drop them and say
in the header that the job is client-only.

### `d77cd89f3` — ci: install fmt in the sanitizer job and run every unit-test module

Two defects, both found by reproducing the job on a Linux box.


## Verification, Linux, 21 November 2026

The job was reproduced by hand on an Ubuntu 24.04 base, which is what it runs on:

* `./configure` **failed** with `fmt development files not found` until
  `libfmt-dev` was added to the job's install list — this branch now installs it,
  so the job can reach its build (commit in the list above).
* With fmt present: the ASan/UBSan client builds, `nm client/boinc` contains
  `__asan_init` and thirteen `__ubsan_handle` symbols (the job's own gate passes),
  `boinc --detect_gpus` exits 0, writes a parseable `coproc_info.xml`, and
  neither `stdoutgpudetect.txt` nor `stderrgpudetect.txt` contains a sanitizer
  diagnostic.
* `executeUnitTests.sh` now runs every module even when one fails; on this tree
  it reports client 7, lib 207, sched 11, vboxwrapper 1 and exits 0 (before the
  change the first failing suite hid the other three).
* `_autosetup` quoting: the whole tree bootstrapped, configured and built from a
  source path containing a space (`…/BOINC 1.0/…`).
