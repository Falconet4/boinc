# PR 1 — `01-harden-parsing-xml-shell-and-rpc-auth`

Core library hardening: parsing, string handling, XML output and command-line
limits, plus the client's GUI RPC credential handling. This is the foundation
branch — much of what later PRs rely on (`shell_quote`, `xml_escape`,
`parse_int` base handling) lives here.

- **Base:** `upstream/master` (`d9fa5fb95`)
- **Head:** `f1556f4e2`
- **8 commits — 33 files — +540/−128 — 153 lib tests**

## Why

A handful of defects shared one shape: a function trusted its input's length or
assumed a parse had succeeded. Several were reachable from project-supplied or
network data. This branch fixes them at the library level so every caller
benefits, rather than patching each call site.

## Commits

### `ca59f8707` — lib: harden parsing, XML and shell output, and command-line bounds

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
- **`shell_quote`** added: single-quote wrapping with `'\''` for embedded
  quotes, the C equivalent of PHP's `escapeshellarg()`.
- **`parse_command_line`** takes a `max_args` bound; samples and `create_work`
  updated to pass it.

### `6e31c70a3` — lib,client: harden buffered log, GUI RPC and writable-directory I/O

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

### `aced92145` — lib: bound the strlcat() destination scan

*4 files (+15/−16)*

`strlcat()` measured the destination with `strlen(dst)`, which reads past the
end if the caller passed an unterminated buffer. It now scans only within
`size`. Test data paths moved to `__FILE__`-relative resolution so the tests
find their fixtures regardless of working directory.

### `3eec47ff9` — sched: fix WORK_REQ and SCHED_CONFIG lifetime bugs

*6 files (+82/−8)*

- **`WORK_REQ`** kept `BEST_APP_VERSION` pointers that the scheduling scans
  freed, while the reply being built could still reference them. They are now
  retired to the request and freed with it.
- **`SCHED_CONFIG`** zeroed itself with `memset()`, which corrupts its
  `std::vector` members and leaked compiled regex vectors on a second parse.
  Now value-initialises and frees the old vectors first.

### `e93213f95` — lib: terminate argv when parse_command_line() fails

*1 file (+6/−4)*

On failure the function left `argv` in an intermediate state. It now
NUL-terminates the array so a caller that ignores the return value cannot walk
off the end.

### `77f11a47d` — client: create the GUI RPC password file private, never follow a symlink, and stop making the auth nonce predictable

*2 files (+27/−1)*

- `gui_rpc_auth.cfg` is created with mode `0600` via `open()`, not a
  world-readable `fopen()` followed by `chmod`.
- `O_NOFOLLOW` so a symlink planted in the group-writable data directory is
  not followed.
- The auth nonce comes from `make_secure_random_string()` instead of
  `snprintf("%f", dtime())`, which was guessable from the clock.

## Testing

153 lib tests (including new cases for `parse_command_line` refusal, `xml_escape`
short buffers and control bytes, and `shell_quote`). `lib`, `client`, `sched`
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
