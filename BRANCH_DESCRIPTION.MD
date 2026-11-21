# PR 3 — `03-manager-rpc-ownership-and-ui-fixes`

BOINC Manager correctness: RPC argument ownership, statistics, string
localisation, remote-desktop validation, and link/row-colour fixes.

- **Base:** `upstream/master` (`d9fa5fb95`)
- **Head:** `b211faac0`
- **5 commits — 12 files — +360/−58 — 150 lib tests**

## Why

`AsyncRPC` queued requests by copying an `ASYNC_RPC_REQUEST` whose `arg1..arg4`
pointed at the *caller's* stack variables. By the time the RPC thread ran, those
frames were gone. Two rounds of static buffers were tried first; both were
wrong (wrong args on queue, then a data race). This branch gives each queued
request its own storage.

## Commits

### `571053eb3` — manager: fix statistics, localize parsing and remote desktop validation

*1 file (+76/−14)*

- **`localize()`** could loop forever on malformed input.
- **Statistics** timestamps and result handling corrected.
- **Remote desktop address**: the value comes from the client/VM description and
  is interpolated into a command line, so it is now restricted to characters
  valid in a hostname, IPv4/IPv6 literal or `host:port`. A leading dash is
  rejected (it would be read as an option by the remote-desktop client).
  ASCII alphanumerics only — `wxIsalnum()` is locale-dependent and would accept
  non-ASCII characters the remote-desktop clients cannot handle.

### `dd33e98bd` — manager,lib: fix RPC completion, RPC argument ownership and simple-GUI logic

*9 files (+250/−40)*

- **RPC argument ownership**: each queued request carries `owned_str1..4`,
  `owned_int1`, `owned_double1`, `owned_bool1`. The copy constructor and
  `operator=` copy them and call `repoint()` to re-derive `arg1..arg4`.
  `RPC_Wait_Owned()` is the entry point; `isSameAs()` compares owned *values*
  so duplicate detection still works.
- **RPC completion** signalled reliably, so the wait dialog does not hang.
- `BOINCGUIApp` `OnExit` handling of sync objects.
- Simple-GUI panels: `rsc_desc_apple_gpu` cleared, `no_ati_pref` renamed to
  `rsc_desc_ati`, `CTransparentStaticBitmap` base class corrected.

### `7f4666400` — manager: only open http(s) for a project-supplied graphics URL

*1 file (+16/−0)*

The graphics "open in browser" path passed a project-supplied URL straight to
the shell. Now gated on a http(s) scheme.

### `009239420` — manager: disable alternate row colouring in the lists and the event log

*3 files (+9/−2)*

`BASEVIEW_STRIPES` was *never defined*, so `#ifdef BASEVIEW_STRIPES` fell
through and the native `EnableAlternateRowColours(true)` still ran — which is
why stripes persisted. Now given an explicit `0`, and `EVENT_LOG_STRIPES 0`.

## Testing

150 lib tests; the Manager compiles and was verified installed on real hardware
(striping gone from both the task list and the event log, connecting to the
packaged service without auth errors).

## Notes

- `AsyncRPC` changes are the riskiest part of this branch: a wrong `repoint()`
  is a use-after-free. Reviewed by tracing every copy site and confirming the
  worker reads `current_rpc_request` (never the queue entry).

## Fixups from the review

- `b211faac0` — **manager: pass the account-manager flag by value again.**
  `repoint()` points `arg4` at the request's own bool, so the dispatcher's
  `arg4 != NULL` test was always true: `acct_mgr_rpc()` ignored credentials
  typed into the attach dialog and "stop using this account manager" did
  nothing. The dispatcher now reads the value.


## AI assistance

This file and the commit that adds it were produced with AI assistance
(Mistral Vibe). That commit carries an `Assisted-by:` trailer, as BOINC's
"Contributing to BOINC with AI assistants" asks for AI-assisted commits. It
carries no `Co-authored-by:` trailer: BOINC's `AGENTS.md` bans crediting an AI
that way.

The contributor of record is the human who opens the pull request, and they are
responsible for the whole change: they read the full diff, verified it, ran and
inspected the tests, and can speak to its licensing and its origin.
