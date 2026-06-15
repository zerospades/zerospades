# Contributing to ZeroSpades Golden-Spec

This project is a **portable golden-spec test harness**: the fixture corpus in
[`fixtures/`](fixtures/README.md) records deterministic input→output pairs from the current
C++ ZeroSpades implementation (the oracle) so any future port can be auto-verified against
identical golden data without a human in the loop. The single most important rule for
contributors is therefore about that corpus: **fixtures are committed data, not generated
build output.**

## Fixtures Are Committed Golden Data

The files under `fixtures/` are the frozen oracle snapshot. **CI never auto-regenerates
them** — there is no "regenerate fixtures" CI step, and the test suite does not rewrite them
during a normal run. A fixture only changes when a human deliberately changes it and commits
the result. Treat every fixture edit as a change to the spec itself.

## The `[update-fixtures]` Policy

Because fixtures are golden data, the CI fixture-immutability guard **fails any push or PR
that modifies anything under `fixtures/`** unless *both* of the following hold:

1. The PR title (on `pull_request`) or the head commit message (on `push`) carries the
   literal token `[update-fixtures]`.
2. The PR body justifies the change in writing — what behavior changed, why the oracle
   snapshot must move, and which fixture ids are affected.

The token is the gate. There is **no `--update-fixtures` CLI flag** in the test code; the
"flag" is this PR-level `[update-fixtures]` token plus the written justification. An
accidental fixture diff (no token) is meant to fail the build — that failure is the guard
working, not a bug.

When the guard rejects a change it prints the reason and points back here, so a reviewer
can confirm the `[update-fixtures]` token and the justification are both present before the
change is allowed in.

## Regenerating Fixtures Locally

Regeneration is deliberate and human-driven. It is done through the `DISABLED_*`
generate-then-freeze tests (disabled by default precisely so a normal `ctest` run never
rewrites the corpus). A human enables and runs the relevant `DISABLED_*` test, inspects the
diff, then commits the new golden data behind an `[update-fixtures]` PR with a written
justification as described above.

After regenerating, validate before committing:

```
python3 tools/validate_fixtures.py fixtures
```

## Pre-Commit Hook Setup

A repo-tracked pre-commit hook ([`.githooks/pre-commit`](.githooks/pre-commit)) validates
staged fixture JSON against the schema before every commit. Enable it once per clone:

```
git config core.hooksPath .githooks
```

The hook is a local convenience — the first line of defense, not the source of truth. CI
re-runs `tools/validate_fixtures.py` over **all** fixtures on every push as the authoritative
gate and never trusts the local hook.

## Tests Run on Every Push

The test suite runs in CI on every push and pull request, shuffled, across three native
toolchains (GCC on Linux, Clang on macOS, MSVC on Windows). A failing test produces a red
status check. Run the suite locally before pushing:

```
ctest --test-dir build-tests --output-on-failure --no-tests=error -j$(nproc)
```

For the order-independence proof used in CI:

```
sh tools/shuffle_check.sh build-tests
```

## See Also

- [`fixtures/README.md`](fixtures/README.md) — fixture format, schema, coordinate
  convention, and subsystem prefixes.
- [`fixtures/known_bugs.md`](fixtures/known_bugs.md) — fixtures tagged
  `known_bug_do_not_preserve` and documented lenient behaviors a port must understand.
