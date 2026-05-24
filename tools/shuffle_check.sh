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

# Validate the test binary exists and is executable BEFORE the seed loop.
if [ ! -x "$BINARY" ]; then
    echo "ERROR: test binary not found at $BINARY — build the zerospades_tests target first" >&2
    exit 1
fi

for SEED in 111 222 333; do
    # Layer 1 — corpus-scale launch-order shuffle (the real leaked-singleton proof).
    if ! ctest --test-dir "$BUILD" --schedule-random --schedule-random-seed "$SEED" --output-on-failure; then
        echo "ERROR: seed $SEED layer 1 (ctest --schedule-random) FAILED" >&2
        exit 1
    fi
    # Layer 2 — within-binary suite shuffle (literal SC-4).
    if ! "$BINARY" --gtest_shuffle --gtest_random_seed="$SEED"; then
        echo "ERROR: seed $SEED layer 2 (--gtest_shuffle) FAILED" >&2
        exit 1
    fi
    echo "PASS: seed $SEED (layer 1 ctest --schedule-random + layer 2 --gtest_shuffle)"
done

echo "OK: 6/6 runs passed (3 seeds x 2 layers)"
