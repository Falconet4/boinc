# PR 10 — `10-security-manager-notice-html-sanitising`

Sanitising of project-supplied notice HTML in the BOINC Manager, and restricting
notice links to http(s).

- **Base:** `upstream/master` (`d9fa5fb95`)
- **Head:** `5def9357d`
- **2 commits — 4 files — +485/−21 — 156 lib tests**

## Why

A notice's `description` is HTML written by a project and rendered in the
Manager's web view. Before this, it was passed through essentially untouched —
so a project could run script in the Manager, which holds the GUI RPC password
for the local client. The `link` field had the same trust problem.

## Commits

### `e344d1a3e` — manager: sanitise project-supplied notice HTML, keeping presentational attributes

*4 files (+428/−13)*

Adds `html_sanitize()` to `lib/str_util.cpp`:
- real tag parser, not a regex: quoted attribute values, stray `<` escaped,
  `<script>` and `<style>` dropped **with their content**;
- attribute allowlist: every `on*` handler, `style`, `class`, `id`, `name` and
  `target` is dropped;
- per-tag attribute allowlist, so `<font color=red size=2>` keeps its
  presentation while `<a onclick=...>` loses the handler — a blanket
  "only a/img keep attributes" rule broke ordinary formatting (that regression
  was caught and fixed before this branch was cut);
- `href`/`src` additionally URL-checked: http, https or relative only;
- kept values are escaped and double-quoted.

`NoticeListCtrl` wiring: `project_name` and `title` escaped *before* localise,
description sanitised *after* localise and `eol_to_br`.

### `5def9357d` — manager: only link a notice to http(s)

*2 files (+65/−16)*

`is_http_url()` — case-insensitive, `http://` or `https://` only — gates the
`href` at render and both click handlers. A `javascript:` or `file:` URL was
otherwise followed by the web view and executed in the Manager's page context.

## Testing

156 lib tests, including the sanitiser corpus. Fuzz-verified over 3000 random
inputs under ASan: output is idempotent and never contains `<script`, `<style`,
a `javascript:` href, or an `on*` handler.

## Notes — the one non-obvious implementation detail

The click handler must **`Skip`** the page's own `about:blank` load. A blanket
"veto every link" in the else branch blanks the notice pane entirely; that was
found by testing, not by reading.

## Relationship to the former PR 15

The attribute allowlist was originally a separate PR. It changes
`attr_allowed()`, which this PR introduces, so the two could not be
independent — it is folded into the sanitiser commit here.

PR 9 also edits `lib/str_util.cpp`. To keep both branches based on `master`,
this PR places its additions at different points than PR 9 does (its functions
before `path_to_filename()`, its declarations at the end of the header, its
tests before `string_substitute`) instead of appending at end-of-file.
Verified: merging the two succeeds cleanly in **either** order.

## AI assistance

This file and the commit that adds it were produced with AI assistance
(Mistral Vibe). That commit carries an `Assisted-by:` trailer, as BOINC's
"Contributing to BOINC with AI assistants" asks for AI-assisted commits. It
carries no `Co-authored-by:` trailer: BOINC's `AGENTS.md` bans crediting an AI
that way.

The contributor of record is the human who opens the pull request, and they are
responsible for the whole change: they read the full diff, verified it, ran and
inspected the tests, and can speak to its licensing and its origin.
