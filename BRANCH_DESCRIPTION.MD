# PR 2 — `02-client-spawn-privilege-and-file-io`

Client runtime safety: process spawning, privilege dropping, file and directory
handling, signal/thread races, and CPU benchmark setup.

- **Base:** `upstream/master` (`f7e5129c5`)
- **Head:** `6ce8641b9`
- **11 commits — 23 files — +1082/−90** (code and tests; this file excluded)

## Why

The client forks, drops privileges, and runs project-supplied binaries. Several
paths either used non-async-signal-safe functions after `fork()`, ignored a
failure that should stop them, or left a `-1` where a pid belonged — which turns
into `kill(-1, SIGKILL)` and `waitpid(-1, ...)`.

## Commits

### `d42fbefa7` — client: fix file creation, device validation and statfs handling

*6 files (+132/−26)*

- **Benchmark fork failure** no longer stores `-1` as the pid: `abort_benchmark()`
  would `kill(-1, SIGKILL)` (every process the user may signal) and
  `check_benchmark()` would `waitpid(-1, ...)`, stealing another child's status.
- **Device validation** rebuilds the identity mapping instead of leaving a
  shifted device-number list.
- **`statfs`** rewritten with Linux filesystem magic numbers defined locally.
- `log_flags`, `hostinfo_linux` and `coproc` adjusted to match.

### `7eab55665` — client,lib,packages: fail closed on privilege drop, avoid shell rename, and treat zombies as exited

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

### `620038340` — client: fix async verification, descriptor leaks and signal/thread races

*7 files (+93/−16)*

- `ASYNC_VERIFY` destructor ownership fixed: it could delete buffers the caller
  still owned.
- Descriptor leaks closed on error paths.
- The signal handler no longer calls `msg_printf()` from inside the handler —
  it locks, formats and allocates, and could deadlock the client if the signal
  arrived while the main thread held the stdio lock. It records the signal in a
  `sig_atomic_t`; the main loop logs it and sets `requested_exit`.
- `pthread_detach` used where a join would block; `sandbox` and `thread` paths
  corrected.

### `0e9524d89` — lib: preserve directory moves across filesystems, and resolve executables before fork

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

### `ae6cfb337` — client: bound the device index in the no-resource check

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

## Commits added after this description was written

The `## Commits` section above was written when the branch was shorter.  The
commits below came later, from the review passes and the Linux test work.

### `4c0fb7096` — client: report errno as errno, and say what the switcher actually does

open_slots_file() formatted a failed fopen() with boincerror(errno), which
switches over BOINC and HTTP codes: EACCES printed "Error 13", and a colliding
number would print an unrelated description.  strerror(errno), as the rest of
this branch does.

### `8320c9570` — lib: do not follow a symlink when copying across filesystems

boinc_rename_aux() lstat()s the source and then hands the name to
boinc_copy_atomic(), which reopened it with a plain fopen().  Between the two a
symlink can be swapped in, and the copy then takes the link target's content --
anything the client can read, gui_rpc_auth.cfg included -- instead of the file
the caller checked.  Open with O_RDONLY|O_NOFOLLOW, require a regular file, and
fdopen the descriptor.

### `0afdd8445` — lib: do not signal a pid whose process has been replaced

kill_descendants() builds the list of the calling process's descendants, asks
the app to exit, then polls that list for up to 10 seconds before SIGTERMing
whatever is left.  A pid identifies a process only until that process exits:
during those 10 seconds a descendant can exit, the OS can hand its pid to an
unrelated process running as the same user, and kill_all() then signals that
process.  The same stale pid also kept the poll from returning as soon as our
own processes were gone.

### `6ce8641b9` — tests: read and write the right ends of the pipe in the POSIX case

test_proc_control's start_time_is_gone_after_the_process_exits used the pipe
backwards: pipe() returns read then write, but the child read fds[1] and the
parent wrote fds[0], so both got EBADF and the ASSERT_EQ on the write failed on
every POSIX run.  The Windows half was never affected because the case is
compiled out there, which is why this is the first run that saw it.


## Verification, Linux, 21 November 2026

* `boinc_rename()` cross-filesystem paths, against a real `EXDEV` mount
  (`/tmp` ↔ `/dev/shm`), 18 checks: regular file (content and mode 0640
  preserved), directory tree, symlink (the link moves, the target is not
  copied), FIFO, `waitpid()` `ECHILD` inference under `SIGCHLD=SIG_IGN`, and the
  "destination is a directory" refusal (nothing nested).  Only cosmetic
  deviation: an unwritable cross-device destination reports `ERR_FOPEN` (-108)
  rather than `ERR_RENAME` (-109); the source is kept and nothing is written.
* No shell in the fallback: names containing `;`, `$()`, backticks, quotes and a
  newline all move without executing anything.
* `proc_start_time()` equals an independent parse of `/proc/<pid>/stat` field 22,
  including a `comm` containing `)` and a space, and is unavailable after the
  process exits and is reaped.
* `kill_descendants(child_pid)`: a well-behaved tree is killed after the 10 s
  grace; a tree that ignores `SIGTERM` survives, because POSIX `kill_all()`
  sends `SIGTERM` while Windows terminates — pre-existing, unchanged here.
* `test_proc_control`'s POSIX case is fixed on this branch: it read and wrote the
  wrong ends of its pipe and failed on every Linux run (`fix` in the list above).
