# PR 17 — `17-manager-theme-change-crash`

BOINC Manager on Windows dies with an access violation when the Windows theme
changes. This branch makes a theme change rebuild the UI once instead of once
per notification, which is what was killing it.

- **Base:** `upstream/master` (`d9fa5fb95`)
- **Head:** `c2137f9f2`
- **2 commits — 4 files — +52/−0**
- **Verified on Windows 10 22H2**, before and after, on the same machine

## Why

A theme change does not deliver one notification to the Manager, it delivers
several. `CBOINCBaseFrame` binds `wxEVT_SYS_COLOUR_CHANGED` to
`OnDarkModeChanged`, and both frame classes implement that handler as a full
teardown: `CAdvancedFrame` calls `RepopulateNotebook()`, which deletes and
recreates all six notebook pages, and `CSimpleFrame` destroys and recreates its
task and project panels so BOINC's own dark-mode colours are rebuilt.

Running that once per notification means the second rebuild destroys the
windows the first rebuild has just created and is still using. The Manager dies
with `0xC0000005` immediately after `DeleteAllPages()` is entered, on the second
rebuild of a single switch.

An instrumented build (each step of the handler appending a line to a file) puts
the boundary exactly there:

```
advanced:enter                          <- notification 1
advanced:done                           <- first rebuild completes
advanced:enter                          <- notification 2, same switch
advanced:timers-stopped-state-saved
repopulate:begin
deleteNotebook:before-DeleteAllPages    <- last line before 0xC0000005
```

## Commits

### `6cd9dc4f6` — manager: rebuild the notebook once per theme change, not once per event

*4 files (+39/−0)*

- `CAdvancedFrame::OnDarkModeChanged` and `CSimpleFrame::OnDarkModeChanged` now
  collapse a burst of notifications into a single rebuild: a
  `m_themeChangeRebuildPending` flag drops the repeats.
- The rebuild is deferred with `CallAfter`, so windows are torn down after the
  current message has been fully dispatched, rather than while Windows is still
  iterating windows for the same change.
- The body of the old handler moved unchanged into `RebuildForThemeChange()`.
  Nothing about what gets rebuilt, or which colours are applied, changes.

## Why this and not a guard

The obvious fix is to do what 8.2.11 did: compile the handler out on Windows.
8.2.11 has it under `SUPPORTDARKMODE`, which `clientgui/BOINCGUIApp.h` defines as

```c
#if (defined(__WXMAC__) || defined(__WXGTK__))
#define SUPPORTDARKMODE true
#else
#define SUPPORTDARKMODE false      // Windows
#endif
```

and `master` deletes the macro entirely, which is why this path started running
on Windows in 8.3.0. But the rebuild is not superfluous: it is what makes
BOINC's own dark-mode colours follow the system setting instead of staying on
whatever they were at start-up, and it is genuinely needed on macOS. Deferring
and coalescing keeps the behaviour and removes the crash.

## Testing

Four Windows builds crashed on the first theme switch with exit code
`-1073741819` (`0xC0000005`), all on the same machine:

| Build | wx dark mode | BOINC dark colours | Theme switch |
|---|---|---|---|
| master, as shipped | on | on | crash |
| `MSWEnableDarkMode()` removed | off | on | crash |
| both dark-mode paths removed | off | off | crash |
| instrumented (tracing only) | on | on | crash |

That table is why this is not a dark-mode bug: disabling dark mode entirely does
not save it, so the trigger is the handler running at all.

With this branch, the same build survives **minimise → Light → maximise →
minimise → Dark → maximise**, in both directions, and the tabs and colours
follow the theme each time. The crash does not produce a WER dump or an
Application-log entry (the fault does not reach the unhandled-exception filter),
so the exit code has to be captured by launching the Manager from a parent that
waits on the process handle.

## Notes

- The deferral means two theme changes in quick succession produce one rebuild.
  That is the intended behaviour: one switch produces the burst, and a second
  switch arriving before the deferred rebuild runs would have rebuilt anyway.
- Not fixed here, but visible in the same function: the handler ends with
  `delete eventLog` on a live `wxWindow`, with the comment "`eventLog->Destroy()`
  creates a race condition if used here". It is unrelated to this crash (the
  event log was not open — the trace records `advanced:eventlog-null`), so it is
  left alone to keep this change reviewable.

## Fixups from the review

- `c2137f9f2` — **manager: hold the theme-change guard for the whole rebuild.**
  The pending flag was cleared at the top of the deferred callback, so the guard
  did not cover the rebuild itself: a notification arriving while it ran, or a
  second flip just after it, queued another rebuild — the pair this branch
  exists to prevent, and in the simple frame the second one replaces and
  destroys panels the first has already replaced. The flag is now cleared after
  the rebuild, and the callback returns early if the frame is being deleted,
  which a close or a GUI-mode switch can make happen while it is queued.


## AI assistance

This file and the commit that adds it were produced with AI assistance
(Mistral Vibe). That commit carries an `Assisted-by:` trailer, as BOINC's
"Contributing to BOINC with AI assistants" asks for AI-assisted commits. It
carries no `Co-authored-by:` trailer: BOINC's `AGENTS.md` bans crediting an AI
that way.

The contributor of record is the human who opens the pull request, and they are
responsible for the whole change: they reproduced the crash, applied and tested
the fix on their own hardware, and can speak to its licensing and its origin.
