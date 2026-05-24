#!/usr/bin/env sh
# shuffle_check.sh — two-layer, 3-seed test-order-independence proof (SC-4).
#
# Proves the zerospades_tests corpus is order-independent (no leaked singleton /
# inter-test state) by running BOTH shuffle layers across three FIXED seeds:
#
#   Layer 1: ctest --schedule-random   — shuffles the LAUNCH ORDER of all ctest
#            entries. This is the meaningful corpus-scale proof: gtest_discover
#            runs each test in its own process, so it is the only layer that can
#            surface a cross-process / launch-order dependency.
#   Layer 2: zerospades_tests --gtest_shuffle — shuffles the within-binary suite
#            order. This satisfies the literal SC-4 wording.
#
# Strict mode: `#!/usr/bin/env sh` + `set -eu` and NO pipe-status option (POSIX
# sh lacks it). If a future edit ever needs that option or arrays, the shebang
# MUST switch to `#!/usr/bin/env bash` — never mix an `sh` shebang with bash-only
# strict-mode flags.
#
# Each of the 6 runs (3 seeds x 2 layers) is individually guarded with
# `if ! cmd; then echo ERROR...; exit 1; fi` so a failing run prints its OWN
# diagnostic BEFORE the script aborts (a bare command under `set -e` would exit
# before the ERROR line printed) — the helper can never mask a real failure
# (threat T-08-03). `--output-on-failure` is added to the ctest layer for
# diagnostics.
#
# Usage: sh tools/shuffle_check.sh [build-dir]   (build-dir defaults to "build")
# Exit 0 only if all 6 runs pass; non-zero (with a per-run ERROR line) otherwise.
#
# This script contains NO add_test / CMake wiring — CI wiring is Phase 9, out of
# scope here.
set -eu

BUILD="${1:-build}"
BINARY="$BUILD/Tests/zerospades_tests"

# Validate the test binary is present (a build smoke-check). This proves a
# single file exists+executable; it does NOT prove any ctest entries are
# registered — test *registration* is verified by the discovery precheck below
# (WR-03 / IN-01).
if [ ! -x "$BINARY" ]; then
    echo "ERROR: test binary not found at $BINARY — build the zerospades_tests target first" >&2
    exit 1
fi

# Vacuous-run precheck (WR-03): ctest exits 0 on "No tests were found!!!" and the
# gtest binary exits 0 on a zero-match filter, so the per-run `if ! cmd` guards
# below catch a FAILING run but not an EMPTY one. Assert a non-zero discovered
# test count first, so a stale/empty CTestTestfile.cmake, undiscovered tests, or
# a structurally-valid-but-wrong $BUILD can never certify "OK: 6/6" on nothing.
# `set -e` would abort on a failed `ctest -N`; capturing into a var with the
# assignment isolated keeps the script from exiting before our own diagnostic.
TEST_COUNT=$(ctest --test-dir "$BUILD" -N 2>/dev/null | sed -n 's/^Total Tests: //p') || TEST_COUNT=""
if [ -z "$TEST_COUNT" ] || [ "$TEST_COUNT" -eq 0 ] 2>/dev/null; then
    echo "ERROR: ctest discovered 0 tests in $BUILD — nothing to shuffle (refusing to certify a vacuous run)" >&2
    exit 1
fi
echo "Discovered $TEST_COUNT ctest entries in $BUILD"

for SEED in 111 222 333; do
    # Layer 1 — corpus-scale launch-order shuffle (the real leaked-singleton proof).
    # Capture output so we can fail loudly on a vacuous "No tests were found" run
    # that ctest reports with exit 0 (WR-03), in addition to the exit-code guard.
    L1_OUT=$(ctest --test-dir "$BUILD" --schedule-random --schedule-random-seed "$SEED" --output-on-failure 2>&1) || {
        printf '%s\n' "$L1_OUT" >&2
        echo "ERROR: seed $SEED layer 1 (ctest --schedule-random) FAILED" >&2
        exit 1
    }
    printf '%s\n' "$L1_OUT"
    if printf '%s' "$L1_OUT" | grep -q "No tests were found"; then
        echo "ERROR: seed $SEED layer 1 ran 0 tests (ctest reported 'No tests were found')" >&2
        exit 1
    fi
    # Layer 2 — within-binary suite shuffle (literal SC-4). The bundled gtest
    # exits 0 on a zero-match run (and does NOT support --gtest_fail_if_no_test_selected;
    # passing an unknown flag would just print usage and exit 0 — another vacuous
    # pass), so instead of an unsupported flag we assert the gtest summary reports
    # a passed count > 0. A zero run prints "[  PASSED  ] 0 tests." / "0 tests from
    # 0 test suites ran"; matching either fails the run loudly (WR-03).
    L2_OUT=$("$BINARY" --gtest_shuffle --gtest_random_seed="$SEED" 2>&1) || {
        printf '%s\n' "$L2_OUT" >&2
        echo "ERROR: seed $SEED layer 2 (--gtest_shuffle) FAILED" >&2
        exit 1
    }
    printf '%s\n' "$L2_OUT"
    if printf '%s' "$L2_OUT" | grep -Eq '\[  PASSED  \] 0 tests|0 tests from 0 test suites ran'; then
        echo "ERROR: seed $SEED layer 2 ran 0 tests (gtest reported a zero passed-count)" >&2
        exit 1
    fi
    if ! printf '%s' "$L2_OUT" | grep -Eq '\[  PASSED  \] [1-9][0-9]* tests?\.'; then
        echo "ERROR: seed $SEED layer 2 produced no '[  PASSED  ] N tests' summary — refusing to certify" >&2
        exit 1
    fi
    echo "PASS: seed $SEED (layer 1 ctest --schedule-random + layer 2 --gtest_shuffle)"
done

echo "OK: 6/6 runs passed (3 seeds x 2 layers)"
