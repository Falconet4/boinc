# PR 12 — `12-security-web-csrf-tokens`

CSRF tokens on the project's state-changing web actions.

- **Base:** `upstream/master` (`d9fa5fb95`)
- **Head:** `b2fc2bdc5`
- **1 commit — 15 files — +31/−3 — PHP lint clean**

## Why

Project pages let a logged-in user create teams, edit a profile, change a
founder, remove inactive members and edit host venues. None of those actions
verified that the request came from a form the user actually loaded, so any
other site could trigger them with a plain `GET`/`POST` while the user was
signed in.

## The commit

### `b2fc2bdc5` — web: require CSRF tokens on the state-changing action pages

Adds `check_tokens()` to every action page and `url_tokens()` to every form that
submits to one:

| Page | What it guards |
|---|---|
| `team_create_action.php` | creating a team |
| `team_edit_action.php` | editing team/venue details |
| `team_change_founder_form.php` | founder transfer |
| `team_founder_transfer_action.php` | founder transfer |
| `team_remove_inactive_action.php` | removing inactive members |
| `edit_forum_preferences_action.php` | forum preferences |
| `host_edit_action.php` / `host_venue_action.php` | host venue |
| `delete_profile.php` | account deletion |

Forms updated to carry the token: `edit_forum_preferences_form.php`,
`host_edit_form.php`, `team_founder_transfer_form.php`,
`team_remove_inactive_form.php`, plus the team helpers in `html/inc/team.inc`
and `html/inc/host.inc` (`team_create_form` via `team_edit_form`).

`delete_profile.php?cmd=delete` is a link rather than a form, so it carries the
token in the URL via `url_tokens()`. `valid_tokens()` accepts the token from
either GET or POST, so both shapes work.

## Testing

`php -l` clean on all 15 files. The check is mechanical: every listed action
page calls `check_tokens()`, and every form that submits to one carries the
token — verified by listing the two sets and comparing them.

## Notes

- Small diff, high value, no dependency on any other branch. Good candidate to
  send early.
- Depends on the site already having `valid_tokens()`/`url_tokens()` in
  `lib/` — this branch only adds the call sites.

## AI assistance

This file and the commit that adds it were produced with AI assistance
(Mistral Vibe). That commit carries an `Assisted-by:` trailer, as BOINC's
"Contributing to BOINC with AI assistants" asks for AI-assisted commits. It
carries no `Co-authored-by:` trailer: BOINC's `AGENTS.md` bans crediting an AI
that way.

The contributor of record is the human who opens the pull request, and they are
responsible for the whole change: they read the full diff, verified it, ran and
inspected the tests, and can speak to its licensing and its origin.
