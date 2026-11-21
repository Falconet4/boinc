# PR 17 — `17-manager-theme-change-crash`

BOINC Manager on Windows dies with an access violation when the Windows theme
changes. This branch makes a theme change rebuild the UI once instead of once
per notification, which is what was killing it.

It also fixes what a theme change rebuilds *incompletely*: the window frame, the
separator and padding above the notebook, and the status bar all kept the theme
they were created in, so a switch left a dark title bar over a light window (and
the reverse), a grey band under the menu bar, and a dark status bar in the light
theme. The last two fixups below are that.

- **Base:** `upstream/master` (`d9fa5fb95`)
- **Head:** `227361a84`
- **4 commits — 6 files — +160/−1**
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
- `2c2ada047` — **manager: re-apply the title-bar theme when the Windows theme
  changes.** DWM draws the frame from a per-window flag (`DWMWA_USE_IMMERSIVE_
  DARK_MODE`) that wxWidgets sets when the window is created, and Windows does
  not revisit an explicitly set value when the theme changes, so the title bar
  stayed dark over a light window and light over a dark one until the Manager
  was restarted. `CBOINCBaseFrame::ApplyTitleBarTheme()` sets the flag again at
  the end of both frames' `RebuildForThemeChange()`.
- `227361a84` — **manager: rebuild the chrome a theme change leaves behind.**
  The separator above the notebook, the five pixels of padding under it and the
  status bar are built once and kept the theme they were created in: the
  separator kept the old 3D highlight, the padding kept `COLOR_APPWORKSPACE`
  (171) once the light theme was on, and the status bar — a native
  `msctls_statusbar32` — kept the colour of the theme it was created under.
  Invalidating them, and `WM_THEMECHANGED`, do not fix it; re-creating them the
  way the pages and the Event Log already are does, and the panel now names its
  background colour instead of letting Windows remember one.

## The chrome, measured

The switches were captured with `PrintWindow(PW_RENDERFULLCONTENT)` and compared
against a Manager started fresh in the same theme. Before the last two fixups,
sampled at the middle of each band:

| Region | started dark | after → light | started light | after → dark |
|---|---|---|---|---|
| title bar | 0 | 0 — should be 255 | 255 | 255 — should be 0 |
| separator | 160/160 | 160/160 — should be 160/255 | 160/255 | 160/255 — should be 160/160 |
| padding | 32 | 171 — should be 240 | 240 | 32 |
| status bar | 51 | 51 — should be 240 | 240 | 0 — should be 51 |

The padding is the one that is only wrong in the light direction: 171 is the
classic `COLOR_APPWORKSPACE`, the colour Windows used for the panel the first
time it painted it. The status bar's 0 after a switch to dark is from the
reporter's captures of the shipped 8.3.0 build; the rest are measured on a build
of this branch with the fixups removed.

With the fixups, all four after-switch captures match the fresh start in the
same theme over the frame, menu bar, separator, padding, tab strip and status
bar — two dark switches and two light switches, one from each starting theme.

A forced `RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW` paint on the
frame leaves every one of them unchanged, so they are not stale pixels; and
`WM_THEMECHANGED` sent to the status bar repaints it white, neither theme's
colour. The windows have to be re-created, not repainted.



## AI assistance

This file and the commit that adds it were produced with AI assistance
(Mistral Vibe). That commit carries an `Assisted-by:` trailer, as BOINC's
"Contributing to BOINC with AI assistants" asks for AI-assisted commits. It
carries no `Co-authored-by:` trailer: BOINC's `AGENTS.md` bans crediting an AI
that way.

The contributor of record is the human who opens the pull request, and they are
responsible for the whole change: they reproduced the crash, applied and tested
the fix on their own hardware, and can speak to its licensing and its origin.
