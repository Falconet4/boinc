# PR 10 — `10-security-manager-notice-html-sanitising`

Sanitising of project-supplied notice HTML in the BOINC Manager, and restricting
notice links to http(s).

- **Base:** `upstream/master` (`f7e5129c5`)
- **Head:** `8429b60ab`
- **5 commits — 4 files — +507/−22** (code and tests; this file excluded)

## Why

A notice's `description` is HTML written by a project and rendered in the
Manager's web view. Before this, it was passed through essentially untouched —
so a project could run script in the Manager, which holds the GUI RPC password
for the local client. The `link` field had the same trust problem.

## Commits

### `4b6c84dea` — manager: sanitise project-supplied notice HTML, keeping presentational attributes

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

### `3b27d3aec` — manager: only link a notice to http(s)

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

## Commits added after this description was written

The `## Commits` section above was written when the branch was shorter.  The
commits below came later, from the review passes and the Linux test work.

### `58a16745d` — lib: do not re-scan for a '>' that is not there

When a tag has no closing '>', html_sanitize() escapes the '<' and advances one
character -- and the next '<' starts the same scan again, all the way to the end
of the input.  A notice description of "<a" repeats, or of unclosed attribute
quotes, therefore costs O(n^2): 1 MB of it is minutes of the GUI thread, on
every refresh, and the description comes from the project's feed with no length
limit.

### `8429b60ab` — manager: deny webview navigation by default, and drop the dead scan flags

CNoticeListCtrl::OnLinkClicked() vetoed http(s) links and let everything else
through, on the argument that nothing else could arrive.  It can: url_is_safe()
deliberately admits relative hrefs, so a notice containing <a href="foo"> raises
this event.  Harmless only because the page's base is about:blank.  The handler
now vetoes everything except the initial load of that page.


## Verification, Linux, 21 November 2026

* The `test_str_util` cases for `html_sanitize()` pass inside the 207-test lib
  suite.
* Live Manager on an X session: the Notices tab renders the real project notices
  (paragraphs, `read more` / `more info…` links, dates) — no blank pane, no
  crash, and the webview stays on the local content.
