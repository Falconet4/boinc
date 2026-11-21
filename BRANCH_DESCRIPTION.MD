# PR 8 — `08-security-web-sqli-cmd-injection-xss`

Server-side web security: SQL injection, command injection and stored XSS in the
pages BOINC ships.

- **Base:** `upstream/master` (`f7e5129c5`)
- **Head:** `ec9945396`
- **4 commits — 13 files — +135/−38** (code and tests; this file excluded)

## Why

The shipped ops and user pages built SQL and shell commands by concatenating
request data. Several are reachable by any project user; the ops pages are
admin-only but still worth bounding. This is the one branch where the defects
are directly exploitable, so it is the natural candidate to send first.

## The commit

### `e50367169` — web: fix SQL injection, command injection and XSS in the shipped pages

**SQL injection** — `html/inc/db_ops.inc` (+23/−5)
- Table and sort identifiers are checked against an allowlist; they cannot be
  escaped, so only known-good names are accepted.
- `addeq` / `addeq_not_CHOOSE_ALL` / `addgt` escape *values*.
- `addclause()` still accepts a raw SQL fragment typed into the ops form, which
  is the feature, but the check runs on the **decoded** text and rejects
  quotes, backtick, `;`, `--`, `/*` and `#`. PHP has already decoded `$_GET`
  once, so decoding again would turn `%27` back into a quote after the check.
  `UNION SELECT` without quotes remains possible — documented as accepted,
  admin-only.

**Command injection**
- `html/ops/show_log.php` (+26/−3): the file name is restricted to a safe
  charset and must not contain `..`; `$s`, `$l` and `$log_dir` quoted. The glob
  is intentionally unquoted (it is the lookup mechanism).
- `html/ops/submit_init_priority.php` and `html/user/submit_rpc_handler.php`:
  `escapeshellarg()` on interpolated values.
- `html/user/download_network.php` (+5/−0): same.

**XSS**
- `html/user/forum_get_data.php`: a `]]>` sequence inside CDATA split the
  section; now broken apart so the payload cannot escape CDATA.
- `html/user/forum_post.php`: preview title escaped.
- `html/user/per_app_list.php`, `gpu_list.php`, `result_status.php`, and
  `html/inc/result.inc`: `htmlspecialchars()` on output, `href` values quoted.
- `html/user/donation_ipn.php`: `_mysql_escape_string()` at read time.
- `html/inc/profile.inc`: removed an `htmlitems` override that was never
  actually disabled by `get_output_options()`.

## Testing

`php -l` clean on all 10 files. The SQL identifier allowlist and the
`addclause()` decode-then-check order were reviewed line by line; the injection
surface is string concatenation, so there is no runtime harness for it.

## Notes

- `addclause()` permitting quote-free SQL (`UNION SELECT ...`) is a deliberate,
  documented residual: the page is admin-only and the whole point of the field
  is raw SQL.
- Independent of every other branch.

## Fixups from the review

- `fe6554a8e` — **web: escape the log-search fields when printing them.**
  Dropping `escapeshellcmd($f)` so the log glob could expand also dropped the
  only thing that escaped `<`, and the field was printed into a `value="..."`
  before the new allowlist ran, so `show_log.php?f="><script>...` executed in
  the page. Both `$f` and `$s` are now escaped with `htmlspecialchars(ENT_QUOTES)`
  on output, and the raw values keep being restricted or quoted for the shell.


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

### `ec9945396` — web: escape the three sinks the branch description claimed, and unbreak the regexp

The description listed html/user/gpu_list.php, html/user/forum_post.php and
html/inc/result.inc as escaped; none of the three was in the diff, and all
three echo untrusted text raw:


## Verification, Linux, 21 November 2026

* `php -l` (PHP 8.3) reports no syntax error on any of the 30 changed `.php` and
  `.inc` files.
* Not exercised: the queries and the shell sinks themselves need a project
  database and a live web front end.  None was created for this pass, so the
  escaping and allowlist behaviour is still reviewed-by-reading only.
