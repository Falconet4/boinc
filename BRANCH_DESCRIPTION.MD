# PR 6 — `06-ci-sanitizers-and-autotools-quoting`

CI and build hygiene: an ASan/UBSan job for the client, and quoting fixes in the
autotools helpers.

- **Base:** `upstream/master` (`d9fa5fb95`)
- **Head:** `1c35c3395`
- **3 commits — 3 files — +145/−8 — 150 lib tests in the tree (not run by this job)**

## Why

The series fixes a lot of undefined behaviour and buffer handling. None of it is
worth much without something that fails the build when it regresses. This adds
a sanitizer job that actually runs the client's GPU detection under ASan and
UBSan, and repairs two quoting bugs that break `_autosetup` on paths with
spaces.

## Commits

### `5680d1d54` — ci: build the client with ASan/UBSan and run GPU detection

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

### `4efcf8ef7` — build: quote directories in the autotools helpers

*2 files (+7/−8)* `_autosetup`, `m4/sah_check_lib.m4`

Unquoted `$VAR` expansions broke when the source path contained a space. Now
quoted throughout.

### `1c35c3395` — ci: run the sanitizer job for sched/ and tools/ changes

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
