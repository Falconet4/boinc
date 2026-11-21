# PR 1 — `01-harden-parsing-xml-shell-and-rpc-auth`

Core library hardening: parsing, string handling, XML output and command-line
limits, plus the client's GUI RPC credential handling. This is the foundation
branch — much of what later PRs rely on (`xml_escape`, `parse_int` base
handling) lives here; `shell_quote` is added in the shell-quoting PR, not this
one.

- **Base:** `upstream/master` (`f7e5129c5`)
- **Head:** `e122004c2`
- **12 commits — 33 files — +640/−164** (code and tests; this file excluded)

## Why

A handful of defects shared one shape: a function trusted its input's length or
assumed a parse had succeeded. Several were reachable from project-supplied or
network data. This branch fixes them at the library level so every caller
benefits, rather than patching each call site.

## Commits

### `253caf142` — lib: harden parsing, XML and shell output, and command-line bounds

*16 files (+217/−80)*

- **`parse_int` / `parse_double`** now report failure instead of leaving the
  caller's variable untouched while silently skipping bad input, and consume
  the closing tag either way so the parser stays in sync. Base 10 unless there
  is an explicit `0x` prefix — `base 0` would read a leading zero as octal.
  Overflow is rejected via `<climits>`.
- **`xml_escape`** writes an escape only if it fits whole, so it can no longer
  emit half an entity (`&am`), and its end pointer reserves the NUL byte.
- **`xml_unescape`** numeric entities: a reference has at most 4 digits and
  must end in `;`. The old code searched the whole remainder for `;` — O(n²)
  on hostile input.
- **`parse_command_line`** takes a `max_args` bound; samples and `create_work`
  updated to pass it, and `create_work` now reports a line that did not fit
  instead of silently building the job from the defaults.
- **The number base fix covers every number parser**, not just `parse_int`:
  `parse_long`, `parse_ulong`, `parse_ulonglong` and `parse_bool` share one
  helper, so `<foo>0100</foo>` is 100 in all five rather than octal 64 in four
  of them.

### `b764967ad` — lib,client: harden buffered log, GUI RPC and writable-directory I/O

*7 files (+179/−23)*

- **`MFILE::flush`** kept the *unwritten* bytes when a short write happened;
  it previously dropped them. Also retries correctly after `clearerr`.
- **Slot-directory file creation** uses `O_EXCL` and retries after removing a
  non-regular entry, closing the unlink/lstat/open race (the slot dir is
  writable by the project user).
- **GUI RPC socket** binds to a private temporary path and renames into place.
  `unlink()` followed by `bind()` left a window where another process could
  plant a file or symlink at `GUI_RPC_FILE` (the data dir is group-writable).
- **`gui_rpc_client`** parsing hardened; `[::1]:` port handling fixed.

### `5e91fc847` — lib: bound the strlcat() destination scan

*4 files (+15/−16)*

`strlcat()` measured the destination with `strlen(dst)`, which reads past the
end if the caller passed an unterminated buffer. It now scans only within
`size`. Test data paths moved to `__FILE__`-relative resolution so the tests
find their fixtures regardless of working directory.

### `2ab590c4a` — sched: fix WORK_REQ and SCHED_CONFIG lifetime bugs

*6 files (+82/−8)*

- **`WORK_REQ`** kept `BEST_APP_VERSION` pointers that the scheduling scans
  freed, while the reply being built could still reference them. They are now
  retired to the request and freed with it.
- **`SCHED_CONFIG`** zeroed itself with `memset()`, which corrupts its
  `std::vector` members and leaked compiled regex vectors on a second parse.
  Now value-initialises and frees the old vectors first.

### `5d41d7fdc` — lib: terminate argv when parse_command_line() fails

*1 file (+6/−4)*

On failure the function left `argv` in an intermediate state. It now
NUL-terminates the array so a caller that ignores the return value cannot walk
off the end.

### `b2ea6d742` — client: create the GUI RPC password file private, never follow a symlink, and stop making the auth nonce predictable

*2 files (+27/−1)*

- `gui_rpc_auth.cfg` is created with mode `0600` via `open()`, not a
  world-readable `fopen()` followed by `chmod`.
- `O_NOFOLLOW` so a symlink planted in the group-writable data directory is
  not followed.
- The auth nonce comes from `make_secure_random_string()` instead of
  `snprintf("%f", dtime())`, which was guessable from the clock.

## Testing

153 lib tests (including new cases for `parse_command_line` refusal, `xml_escape`
short buffers and control bytes, and the `parse_int` base handling; `shell_quote`
and its tests come with the shell-quoting PR). `lib`, `client`, `sched`
build. Covered by the CI sanitizer job added in PR 6.

## Notes

- The `O_NOFOLLOW` change was previously a separate commit; it is folded into
  the password-file commit it belongs to.
- Android: `make_secure_random_string_os()` returns −1 there and falls back to
  a still clock-based generator, so the nonce remains predictable on Android.
  The RPC socket lives in the abstract namespace (same-UID only), which bounds
  the impact.

## Fixups from the review

- `97282bd9d` — **lib: clear the buffer length when a buffered file is
  reopened.** `flush()` keeps unwritten bytes for a retry, but the retry is
  `close()` + `open()` and neither reset `len`, so a second attempt wrote
  uninitialised heap at the head of the state file.
- `f1556f4e2` — **win: choose the socket family without reading `ss_family`.**
  On Windows `sockaddr_storage` is `sockaddr_in` and has no `ss_family`, so the
  three `boinc_socket()` calls did not compile with MSVC. Moved here from the
  separate Windows-fixes branch so that this PR builds on Windows by itself.

The "require authentication for run_graphics_app" commit this branch used to
carry has been dropped. The RPC exists only on macOS, and the screensaver's
`clientscr/gfx_cleanup.mm` initialises the connection without a password, so
requiring auth would stop it killing the graphics application when a GUI RPC
password exists — which is the default. It belongs in its own change, together
with the matching `authorize()` call in that file.


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

### `2c2681072` — lib, client, tools: close four gaps the second review found, and correct the summary

- parse_long(), parse_ulong(), parse_ulonglong() and parse_bool() still passed
  base 0 to strtol(), so the octal bug this branch fixed in parse_int() was
  alive two functions below it: <foo>0100</foo> parsed as 64 for every long,
  ulong, ulonglong and bool field.  One helper now, used by all five.
- MFILE::close() freed buf but left len, and flush() keeps unwritten bytes in
  len on a short write: the next puts() would write at buf+len in a fresh,
  uninitialised block, and past it once len was large.  The retry path is
  close() + open(), and open() starts empty, so clear len with the buffer.
- switcher_exec() parsed a 1024-byte copy of the command line while logging the
  real one; on macOS MAXPATHLEN is 1024, so a long slot path was cut mid-token
  and the closing quote was lost.  Parse a copy of the whole line.
- create_work passed parse_command_line()'s new -1 straight to
  parse_stdin_line(), which then looped zero times: a stdin line with too many
  fields produced a job from the defaults with no error.  Report it and skip
  the line.

### `6b37f84db` — client: close the read side of the GUI RPC password file, and fchmod the fd

gui_rpc_auth.cfg was opened with fopen() for reading, and the O_NOFOLLOW
hardening added earlier only covered creating it.  In sandbox mode the data
directory is group-writable, so another member could replace the file with a
symlink to a file of their choosing; at the next client start the first line of
that file would become the GUI RPC password.  Read it with
open(O_RDONLY|O_NOFOLLOW), require a regular file, and fdopen the descriptor.

### `e122004c2` — tests: locate the OpenWrt model fixtures the way the other fixtures are found

The three parse_openwrt_model* cases built their path from
current_path().parent_path(), so they only found tests/unit-tests/testdata when
the build directory happened to be a direct child of tests/ (the tests/gtest
directory executeUnitTests.sh creates).  Built anywhere else they failed with a
null model_file.  Their neighbours in the same file already use
absolute(__FILE__), which is independent of the working directory; use that.


## Verification, Linux, 21 November 2026

* `./tests/executeUnitTests.sh` → **226/226** (client 7, lib 207, sched 11,
  vboxwrapper 1), exit 0.  The lib cases this branch adds — `test_parse`,
  `test_xml_escape`, `test_str_util`, `test_md5_file`, `test_shmem` — all pass.
* Live client on Linux, private data directory and private GUI RPC port:
  `gui_rpc_auth.cfg` is created `0600`; a symlink planted at that name is not
  followed on read or on write (the target keeps its contents, the client warns
  `Can't open gui_rpc_auth.cfg - fix permissions` and keeps a random in-memory
  password, so the RPC stays authenticated).
* GUI RPC end to end: `--get_state`, `--client_version`, `--get_cc_status`
  answer, a wrong password returns `-155`, `--quit` exits cleanly.
* Whole tree builds with 0 errors (43 warnings, all pre-existing classes: cURL
  deprecations and format-truncation).
