# PR 2 — `02-client-spawn-privilege-and-file-io`

Client runtime safety: process spawning, privilege dropping, file and directory
handling, signal/thread races, and CPU benchmark setup.

- **Base:** `upstream/master` (`d9fa5fb95`)
- **Head:** `d8ebdf104`
- **6 commits — 20 files — +795/−87 — 156 lib tests**

## Why

The client forks, drops privileges, and runs project-supplied binaries. Several
paths either used non-async-signal-safe functions after `fork()`, ignored a
failure that should stop them, or left a `-1` where a pid belonged — which turns
into `kill(-1, SIGKILL)` and `waitpid(-1, ...)`.

## Commits

### `54efadc6f` — client: fix file creation, device validation and statfs handling

*6 files (+132/−26)*

- **Benchmark fork failure** no longer stores `-1` as the pid: `abort_benchmark()`
  would `kill(-1, SIGKILL)` (every process the user may signal) and
  `check_benchmark()` would `waitpid(-1, ...)`, stealing another child's status.
- **Device validation** rebuilds the identity mapping instead of leaving a
  shifted device-number list.
- **`statfs`** rewritten with Linux filesystem magic numbers defined locally.
- `log_flags`, `hostinfo_linux` and `coproc` adjusted to match.

### `d821033a5` — client,lib,packages: fail closed on privilege drop, avoid shell rename, and treat zombies as exited

*5 files (+163/−36)*

- **`switcher`**: a failed privilege drop is now fatal rather than ignored;
  `setgroups()` returning `EPERM` is tolerated only where the installer has
  already switched identity (mode 6551), and is commented as such.
  Values interpolated into a root `su -l ... -c` command line are restricted to
  characters that cannot escape it.
- **`lib/filesys`**: cross-filesystem directory moves use `mv` with an argv
  vector rather than a shell string, so project-supplied names cannot inject.
- **`lib/proc_control`**: `any_process_exists()` treats zombies as exited.
- The two generic-installer `sea` scripts updated to match.

### `587a56b20` — client: fix async verification, descriptor leaks and signal/thread races

*7 files (+93/−16)*

- `ASYNC_VERIFY` destructor ownership fixed: it could delete buffers the caller
  still owned.
- Descriptor leaks closed on error paths.
- The exit flag set from the signal handler is no longer cleared by the main
  loop.
- `pthread_detach` used where a join would block; `sandbox` and `thread` paths
  corrected.

### `76e05cf87` — lib: preserve directory moves across filesystems, and resolve executables before fork

*4 files (+402/−12)*

- **`boinc_move_dir`** infers the outcome of `mv` correctly (including `ECHILD`)
  and preserves the source on failure.
- **`boinc_resolve_exe`** resolves a program name through `PATH` *before*
  `fork()`, so the child can use `execve()` — `execvp()`/`execlp()` are not
  async-signal-safe in a multithreaded process. Uses `AT_EACCESS` where
  available and skips directory-shadow entries.
- `run_program()`'s child `_exit()`s if `chdir` fails instead of exec'ing in the
  wrong directory.
- 138 lines of new tests in `test_util`.

### `f3ae25ba6` — client: bound the device index in the no-resource check

*1 file (+3/−1)*

The "no usable resource" check indexed the device list with an unvalidated
value read from the state file.

## Testing

156 lib tests. The new `boinc_resolve_exe` and `run_program` cases cover PATH
resolution, a relative name with a `dir`, and a directory shadowing the binary.

## Notes — not tested here

The macOS-specific paths in `switcher.cpp` are unverified: no Mac is available,
and this branch is not proposed as ready for macOS until that is done.

## Fixups from the review

- `d8ebdf104` — **win: compile `boinc_copy_atomic()` only where its helpers
  exist.** It calls the POSIX-only three-argument `boinc_temp_file()` and
  `boinc_copy_attributes()`, and was defined outside any guard, so MSVC failed
  on `filesys.cpp`; its only caller is the `EXDEV` fallback, which is already
  POSIX-only. Moved here from the separate Windows-fixes branch so that this PR
  builds on Windows by itself.


## AI assistance

This file and the commit that adds it were produced with AI assistance
(Mistral Vibe). That commit carries an `Assisted-by:` trailer, as BOINC's
"Contributing to BOINC with AI assistants" asks for AI-assisted commits. It
carries no `Co-authored-by:` trailer: BOINC's `AGENTS.md` bans crediting an AI
that way.

The contributor of record is the human who opens the pull request, and they are
responsible for the whole change: they read the full diff, verified it, ran and
inspected the tests, and can speak to its licensing and its origin.
