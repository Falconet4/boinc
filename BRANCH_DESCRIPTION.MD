# PR 9 — `09-security-zip-slip-shell-quoting-and-bounds`

Zip extraction, shell command construction, and fixed-size buffer handling in
the client and scheduler.

- **Base:** `upstream/master` (`d9fa5fb95`)
- **Head:** `f850f2355`
- **2 commits — 12 files — +242/−61 — 151 lib tests**

## Why

The zip path is the dangerous one: archives are downloaded from projects, so an
entry name is attacker-controlled. Several scheduler scripts also built shell
commands from project-supplied values, and a few buffers trusted their input's
length.

## The commit

### `d6071f616` — harden zip extraction, shell commands and fixed-size buffers, and reject a non-positive size in unescape_url

**Zip slip** — `zip/boinc_zip.cpp` (+36/−0)
`is_safe_entry_name()` rejects absolute paths, drive letters (`C:\`) and any
`..` component *before* anything is created on disk. End-to-end verified with an
archive containing `../../evil` and `/tmp/evil_abs`: extraction returns an
error and nothing is written outside the destination.

**Shell command construction** — `lib/str_util.cpp` (+29/−0), `lib/str_util.h` (+3/−0)
- `shell_quote()` added: wrap in single quotes, replace each embedded `'` with
  `'\''`. Fuzz-verified to always produce a single shell word.
- Scheduler scripts updated to use it: `script_validator.cpp` (+26/−27),
  `script_assimilator.cpp` (+26/−24), `trickle_echo.cpp` (+24/−3).
  `trickle_echo` also gets a size bound and rejects a close tag it
  previously accepted.

**Buffer bounds**
- `sched/sched_shmem.cpp` (+11/−1), `sched/sched_types.cpp` (+7/−2): sizes
  checked before copying.
- `sched/batch_collect_assimilator.cpp` (+3/−3): `sprintf` → `snprintf`.
- `client/cs_notice.cpp` (+4/−1): bound on the notice string.

**`unescape_url` underflow** — `lib/url.cpp` (+22/−0)
Three separate defects, all in the same function:
- a trailing `%` (or `%X` at the very end) had no two hex digits to read, so
  `x2c()` read past the terminator — the remainder is now copied verbatim;
- the terminator clamp is correct at the buffer edge;
- `url_size <= 0` previously computed `url_size - 1` and wrote **before** the
  buffer. Now returns immediately.

Fuzz-verified: 3000 inputs × 12 sizes under ASan, no out-of-bounds.

## Testing

151 lib tests, including new `shell_quote` cases. The `unescape_url` sweep and
the zip-slip end-to-end test are described in `PR_Review.md`.

## Notes

- The `unescape_url` size-0 guard was added *after* the rest of the branch, when
  ASan caught the underflow — it is folded into this commit rather than carried
  as a follow-up.
- `zip` and the scheduler are both involved, so this branch touches `sched/`
  and therefore triggers the PR 6 CI job.

## Fixups from the review

- `f850f2355` — **zip: normalise archive entry names, not just test them for
  `..`.** The guard compared each component with `".."` exactly and then wrote
  the raw name, but Win32 strips trailing spaces and dots from every component
  before resolving it, so `".. /evil.txt"`, `".. ."` and `"..."` passed the test
  and the extraction landed in the parent directory. Components are now trimmed
  the way Windows trims them, anything left empty or still naming the parent is
  rejected, harmless `"."` components are dropped, and the normalised name is
  what gets created and written.


## AI assistance

This file and the commit that adds it were produced with AI assistance
(Mistral Vibe). That commit carries an `Assisted-by:` trailer, as BOINC's
"Contributing to BOINC with AI assistants" asks for AI-assisted commits. It
carries no `Co-authored-by:` trailer: BOINC's `AGENTS.md` bans crediting an AI
that way.

The contributor of record is the human who opens the pull request, and they are
responsible for the whole change: they read the full diff, verified it, ran and
inspected the tests, and can speak to its licensing and its origin.
