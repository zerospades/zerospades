#!/usr/bin/env python3
"""dedup_fixtures.py — semantic-dedup scan + file-vs-INDEX behavior-consistency audit.

Composes with (does NOT fold into) validate_fixtures.py. The validator is the
schema/registry gate; this tool is the SC-1 (semantic dedup) and SC-2
(behavior-consistency) audit. It NEVER re-validates the schema and imports no
schema-validation library (stdlib-only).

What it does, in a single scan over fixtures/*.json (excluding the two
non-fixture JSON files):

  - SC-1 semantic dedup: hash the canonical (inputs, expected) sub-object ONLY
    (envelope metadata id/path/description/seed/subsystem/kind/map are excluded)
    with SHA-256 over a sorted, separator-normalised json.dumps. Group fixtures
    by that key; any group of size > 1 is a collision group.
      * A collision group whose EXACT id-set matches a REVIEWED_COLLISIONS key
        (frozenset equality) is reported informationally and is NOT a failure —
        these are reviewed driver-level collisions whose differentiating input/
        map setup lives in the C++ TEST_F driver, not the serialized payload, so
        a payload-only hash cannot distinguish them yet both are retained for
        coverage (CONTEXT SC-1).
      * Any OTHER collision group is an UNREVIEWED duplicate -> ERROR + exit 1.
        The allowlist is keyed by the exact frozenset so a NEW duplicate (or a
        third id joining a reviewed group) can never be laundered (T-08-08).

  - Missing-required-field guard: a fixture missing top-level "inputs" OR
    "expected" is an ERROR (exit non-zero). The tool NEVER hashes a null/default
    payload as if it were real data (membership test, not .get()).

  - KNOWN_SUPERSESSIONS: a conditional manual-removal register. For each id, if
    it is PRESENT in the corpus it is reported on a separate informational line
    (NOT a dedup hit); if it is ABSENT (e.g. after the Plan-02 removal) nothing
    happens for that id. So the tool exits 0 both pre- and post-removal.

  - SC-2 behavior-consistency audit: the validator does NOT cross-check that a
    fixture file's top-level "behavior" matches its INDEX entry "behavior". This
    tool does, and reports any mismatch as an ERROR (exit non-zero). This is the
    ONLY place that invariant is enforced.

Security: untrusted JSON is read with json.load + try/except (never eval). Any
INDEX path field is confined to fixtures/ before filesystem resolution
(T-08-01). Malformed input is reported and causes a non-zero exit, never a crash
(T-08-02).

No GPL header: this is new tooling, not derived from game source.
"""
import sys
import json
import pathlib
import argparse
import hashlib

# Anchor to the repo root via the script location (tools/<script> and fixtures/
# are siblings under the repo root) so the scan is NOT silently CWD-dependent
# (WR-02). Run from any directory, the tool always audits the repo's fixtures/.
# When invoked from the repo root this resolves identically to the prior
# CWD-relative Path("fixtures"), preserving the verified happy-path behavior.
REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
FIXTURES_DIR = REPO_ROOT / "fixtures"
INDEX_PATH = FIXTURES_DIR / "INDEX.json"

# Non-fixture JSON files that live in fixtures/ and must be excluded from scans.
NON_FIXTURE_FILES = {"INDEX.json", "fixture_schema.json"}

# Reviewed driver-level collisions (BLOCKER fix — these are NOT redundant).
# Keyed by the EXACT frozenset of colliding fixture ids. Each pair is byte-
# identical in its serialized (inputs, expected) payload yet tests genuinely
# distinct behavior whose differentiating input/map setup lives in the C++
# TEST_F driver (not the payload) — so a payload-only hash cannot distinguish
# them, but both are retained for coverage. Any collision group NOT in this map
# fails as an unreviewed duplicate. Adding a third id to a reviewed group also
# fails (frozenset equality), so the allowlist cannot launder a new duplicate.
REVIEWED_COLLISIONS = {
    frozenset({"phys_jump_001", "phys_jump_cooldown_001"}): (
        "distinct behavior in C++ TEST_F driver (Jump uses inp.jump=(i==0); "
        "JumpCooldown uses jumpSeq {true,false,true}) — input setup not in "
        "serialized payload; output coincidentally identical on flat map "
        "(re-jump suppressed while airborne); both fixtures retained for coverage"
    ),
    frozenset({"phys_crouch_offset_001", "phys_try_uncrouch_blocked_001"}): (
        "distinct behavior in C++ TEST_F driver (CrouchOffset crouches on open "
        "flat map; TryUncrouchBlocked sets blocking voxels at 256,256,59-60 so "
        "uncrouch is denied) — map/input setup not in serialized payload; output "
        "coincidentally identical (crouched-stationary); both fixtures retained "
        "for coverage"
    ),
}

# Conditional manual-removal register (review item 1). If an id here is PRESENT
# it is reported informationally (NOT a dedup hit — its semantic_key differs
# from its successor because expected.value differs); if ABSENT, that is the
# expected post-removal state and is never an error. This keeps the tool green
# both before the Plan-02 removal (orphan present) and after it (orphan gone).
KNOWN_SUPERSESSIONS = {
    "map_value_lookup_001": (
        "Phase-2 demo sample superseded by map_value_lookup_001_is_solid + the "
        "18 Phase-6 map fixtures"
    ),
}


def load_json(path):
    """Load JSON from path. Returns (data, error_message). error is None on success."""
    try:
        with path.open(encoding="utf-8") as fh:
            return json.load(fh), None
    except FileNotFoundError:
        return None, f"{path}: file not found"
    except json.JSONDecodeError as exc:
        return None, f"{path}: JSON parse error: {exc}"
    except OSError as exc:
        return None, f"{path}: read error: {exc}"


def semantic_key(fixture):
    """SHA-256 of the canonical (inputs, expected) payload ONLY.

    Byte-exact (golden-vs-golden identity, NOT port-vs-golden) — no per-field
    tolerance. REQUIRED guard (review item 5): never hash a null/default payload;
    the caller verifies membership of both keys and treats absence as an ERROR.
    """
    if "inputs" not in fixture or "expected" not in fixture:
        raise KeyError("missing required field(s) inputs/expected")
    payload = {"inputs": fixture["inputs"], "expected": fixture["expected"]}
    blob = json.dumps(payload, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(blob.encode("utf-8")).hexdigest()


def scan_fixtures(fixture_paths):
    """Load every fixture, returning (records, errors).

    records: list of (id, path, behavior, key-or-None) for fixtures with both
             required fields present.
    errors:  malformed-JSON, missing-id, and missing-required-field findings.
    """
    records = []
    errors = []
    for fp in fixture_paths:
        data, err = load_json(fp)
        if err is not None:
            errors.append(err)
            continue
        if not isinstance(data, dict):
            errors.append(f"{fp}: top-level value must be an object")
            continue
        fid = data.get("id")
        if fid is None:
            errors.append(f"{fp}: fixture missing 'id' field")
            continue
        # Missing-required-field guard: NEVER silently hash a null payload.
        if "inputs" not in data or "expected" not in data:
            errors.append(f"{fid} missing required field(s) inputs/expected")
            records.append((fid, fp, data.get("behavior"), None))
            continue
        records.append((fid, fp, data.get("behavior"), semantic_key(data)))
    return records, errors


def check_collisions(records):
    """Group by semantic_key; classify collision groups. Returns (info, errors).

    info:   informational lines for reviewed driver-level collisions.
    errors: unreviewed-duplicate findings (force exit non-zero).
    """
    groups = {}
    for fid, _path, _beh, key in records:
        if key is None:
            continue
        groups.setdefault(key, []).append(fid)

    info = []
    errors = []
    for _key, ids in groups.items():
        if len(ids) < 2:
            continue
        id_set = frozenset(ids)
        rationale = REVIEWED_COLLISIONS.get(id_set)
        if rationale is not None:
            joined = " ≡ ".join(sorted(ids))
            info.append(f"reviewed driver-level collision: {joined} — {rationale}")
        else:
            joined = ", ".join(sorted(ids))
            errors.append(f"unreviewed duplicate: {{{joined}}} share an identical (inputs, expected) payload")
    return info, errors


def check_behavior_consistency(records):
    """SC-2: file-behavior must equal INDEX-behavior. Returns (errors, audited_count).

    The only enforcement of the behavior-in-two-places invariant (Pitfall 3).
    Confines any INDEX path resolved here to fixtures/ (T-08-01, ASVS V5).
    """
    index, err = load_json(INDEX_PATH)
    if err is not None:
        return [err], 0
    if not isinstance(index, dict):
        return [f"{INDEX_PATH}: top-level value must be an object"], 0
    entries = index.get("fixtures", [])
    if not isinstance(entries, list):
        return [f"{INDEX_PATH}: 'fixtures' must be an array"], 0

    errors = []
    index_behavior = {}
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        fid = entry.get("id")
        if fid is None:
            continue
        # Path-traversal confinement before any resolution (T-08-01).
        raw_path = entry.get("path")
        if isinstance(raw_path, str):
            p = pathlib.Path(raw_path)
            if not str(p).startswith("fixtures/") or ".." in p.parts:
                errors.append(
                    f"{INDEX_PATH}: path '{raw_path}' escapes the fixtures/ directory"
                )
                continue
        index_behavior[fid] = entry.get("behavior")

    audited = 0
    for fid, _path, file_behavior, _key in records:
        if fid not in index_behavior:
            # Registration is the validator's job, not ours; skip silently.
            continue
        audited += 1
        idx_beh = index_behavior[fid]
        if file_behavior != idx_beh:
            errors.append(
                f"{fid}: file behavior '{file_behavior}' != INDEX behavior '{idx_beh}'"
            )
    return errors, audited


def main(argv=None):
    parser = argparse.ArgumentParser(
        description=(
            "Semantic-dedup scan + file-vs-INDEX behavior-consistency audit over "
            "fixtures/*.json. Composes with validate_fixtures.py; never re-validates "
            "the schema."
        )
    )
    parser.parse_args(argv)

    fixture_paths = [
        p for p in sorted(FIXTURES_DIR.glob("*.json")) if p.name not in NON_FIXTURE_FILES
    ]

    records, scan_errors = scan_fixtures(fixture_paths)
    collision_info, collision_errors = check_collisions(records)
    behavior_errors, audited_count = check_behavior_consistency(records)

    present_ids = {fid for fid, _p, _b, _k in records}
    supersession_notes = []
    for sid, reason in KNOWN_SUPERSESSIONS.items():
        if sid in present_ids:
            supersession_notes.append(
                f"known supersession (manual removal target): {sid} — {reason}"
            )
        # Absent => expected post-removal state, never an error.

    all_errors = scan_errors + collision_errors + behavior_errors

    if all_errors:
        for e in all_errors:
            print(f"ERROR: {e}", file=sys.stderr)
        print(
            f"FAILED: {len(all_errors)} issue(s) across {len(fixture_paths)} fixture(s) "
            f"({len(collision_errors)} unreviewed duplicate(s), {len(behavior_errors)} "
            f"behavior mismatch(es), {len(scan_errors)} load/field error(s)).",
            file=sys.stderr,
        )
        sys.exit(1)

    for line in collision_info:
        print(line)
    for note in supersession_notes:
        print(note)
    print(
        f"OK: {len(fixture_paths)} fixture(s), 0 unreviewed duplicates "
        f"({len(collision_info)} reviewed driver-level collision(s)), "
        f"{audited_count} behavior tag(s) consistent with INDEX.json."
    )
    sys.exit(0)


if __name__ == "__main__":
    main()
