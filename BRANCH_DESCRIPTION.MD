# PR 16 — `16-build-fix-tools-fmt-dependencies`

Build fix: the server tools cannot link when fmt is present.

- **Base:** `upstream/master` (`d9fa5fb95`)
- **Head:** `61a5970fb`
- **1 commit — 1 file — +7/−7**

## Why

Upstream's `dpa_fmt` merge (`afddc17ba`) moved the library to std::format-style
formatting via fmtlib. It added `$(FMT_LIBS)` to seven `_DEPENDENCIES` lines in
`tools/Makefile.am`. That is a linker *flag*, not a make *target*, so automake
generates a rule that tries to build each tool from a prerequisite literally
named `-lfmt`:

```
make[2]: *** No rule to make target '-lfmt', needed by 'cancel_jobs'.
```

Any server build with fmt installed dies in `tools/`. This is an upstream
defect, not one the other PRs introduce — but because every branch here is
based on current `master`, they all hit it until it lands.

## The commit

### `61a5970fb` — build: drop $(FMT_LIBS) from the tools' _DEPENDENCIES

Removes it from the seven offending lines; it stays in every `_LDADD`, which is
where a linker flag belongs. The remaining `_DEPENDENCIES` entries (`LIBSCHED`,
`LIBBOINC_CRYPT`, `LIBBOINC`) are real files and still force a relink when those
libraries change.

The seven tools: `cancel_jobs`, `create_work`, `dir_hier_path`, `dir_hier_move`,
`sign_executable`, `stage_file_native`, `remote_submit_test`.

## Testing

- Before: `make` fails in `tools/` with "no rule to make target `-lfmt`".
- After, from a clean worktree off `master`: `_autosetup && configure && make`
  completes and produces `tools/cancel_jobs` and `tools/create_work`.

## Notes

Checked the rest of the tree for the same mistake: `sched/Makefile.am`'s
`SCHED_DEPS` also contains `$(FMT_LIBS)` but is only ever used in `_LDADD`, so it
is correct. `apps/Makefile.am` uses `$APPLIBS` (`$(LIBAPI) $(LIBBOINC)`), which
are real files. `tools/` was the only offender.

This is the smallest possible PR and unblocks the build — send it first, or
fold the one-line fix upstream directly.

## AI assistance

This file and the commit that adds it were produced with AI assistance
(Mistral Vibe). That commit carries an `Assisted-by:` trailer, as BOINC's
"Contributing to BOINC with AI assistants" asks for AI-assisted commits. It
carries no `Co-authored-by:` trailer: BOINC's `AGENTS.md` bans crediting an AI
that way.

The contributor of record is the human who opens the pull request, and they are
responsible for the whole change: they read the full diff, verified it, ran and
inspected the tests, and can speak to its licensing and its origin.
