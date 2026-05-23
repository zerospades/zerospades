#!/usr/bin/env python3
"""validate_fixtures.py — authoritative fixture gate.

Validates ZeroSpades golden-spec fixtures against fixtures/fixture_schema.json
(JSON Schema 2020-12, via jsonschema.Draft202012Validator) AND enforces
fixtures/INDEX.json registry invariants in a single pass:

  - Each fixture conforms to the schema (validator.iter_errors)
  - INDEX.json ids are unique
  - Every registered path exists on disk (forward check)
  - Every fixture file is registered in INDEX.json (reverse check)
  - id prefix matches the declared subsystem (D-13)
  - Registered paths are confined to the fixtures/ directory (no traversal)

CLI:
  python3 tools/validate_fixtures.py                  # all fixtures in fixtures/
  python3 tools/validate_fixtures.py f1.json f2.json  # specific files (pre-commit mode)

Exit 0 = all valid. Exit 1 = one or more errors (printed to stderr).

Security: untrusted JSON is read with json.load + try/except (never eval).
INDEX path fields are confined to fixtures/ before any filesystem resolution.
Malformed input is reported and causes a non-zero exit, never a crash.

No GPL header: this is new tooling, not derived from game source.
"""
import sys
import json
import pathlib
import argparse

from jsonschema import Draft202012Validator, ValidationError  # noqa: F401

FIXTURES_DIR = pathlib.Path("fixtures")
SCHEMA_PATH = FIXTURES_DIR / "fixture_schema.json"
INDEX_PATH = FIXTURES_DIR / "INDEX.json"

# Non-fixture JSON files that live in fixtures/ and must be excluded from scans.
NON_FIXTURE_FILES = {"INDEX.json", "fixture_schema.json"}

# D-13: per-subsystem id prefix map.
PREFIX_MAP = {
    "proto": "proto_",
    "phys": "phys_",
    "map": "map_",
    "weap": "weap_",
    "mode": "mode_",
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


def validate_fixture(path, validator):
    """Validate a single fixture file against the schema. Returns list of errors."""
    data, err = load_json(path)
    if err is not None:
        return [err]
    errors = []
    for verr in validator.iter_errors(data):
        errors.append(f"{path}: {verr.json_path}: {verr.message}")
    return errors


def check_index_invariants(fixture_paths):
    """Enforce INDEX.json registry invariants. Returns list of errors.

    fixture_paths is the set of fixture files in scope for the reverse
    (unregistered-file) check. When invoked with no CLI args this is the
    full fixtures/ listing; in pre-commit mode it is only the staged files.
    """
    errors = []
    index, err = load_json(INDEX_PATH)
    if err is not None:
        return [err]

    if not isinstance(index, dict):
        return [f"{INDEX_PATH}: top-level value must be an object"]
    entries = index.get("fixtures", [])
    if not isinstance(entries, list):
        return [f"{INDEX_PATH}: 'fixtures' must be an array"]

    # Unique-id check.
    seen = set()
    for entry in entries:
        fid = entry.get("id")
        if fid is None:
            errors.append(f"{INDEX_PATH}: entry missing 'id' field")
            continue
        if fid in seen:
            errors.append(f"{INDEX_PATH}: duplicate id '{fid}'")
        seen.add(fid)

    # Path-traversal confinement: every registered path must live under fixtures/.
    # Use pathlib.Path throughout (Pitfall 4: never raw string compare on separators).
    for entry in entries:
        raw_path = entry.get("path")
        if raw_path is None:
            errors.append(f"{INDEX_PATH}: entry '{entry.get('id', '?')}' missing 'path' field")
            continue
        p = pathlib.Path(raw_path)
        # Security: reject anything that escapes fixtures/ (T-02-05).
        if not str(p).startswith("fixtures/") or ".." in p.parts:
            errors.append(
                f"{INDEX_PATH}: path '{raw_path}' escapes the fixtures/ directory"
            )
            continue
        # Forward check: registered path resolves to a file on disk.
        if not p.exists():
            errors.append(f"{INDEX_PATH}: registered path '{p}' does not exist")

    # id-prefix-matches-subsystem check (D-13).
    for entry in entries:
        fid = entry.get("id")
        sub = entry.get("subsystem", "")
        if fid is None:
            continue
        expected_prefix = PREFIX_MAP.get(sub, "")
        if expected_prefix and not fid.startswith(expected_prefix):
            errors.append(
                f"{INDEX_PATH}: id '{fid}' does not match subsystem '{sub}' "
                f"prefix '{expected_prefix}'"
            )

    # Reverse check: every in-scope fixture file is registered.
    registered_paths = {pathlib.Path(e["path"]) for e in entries if isinstance(e.get("path"), str)}
    for fp in fixture_paths:
        if fp.name in NON_FIXTURE_FILES:
            continue
        if fp not in registered_paths:
            errors.append(f"{fp}: fixture file not registered in INDEX.json")

    return errors


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Validate ZeroSpades fixtures against the schema and INDEX.json invariants."
    )
    parser.add_argument(
        "files",
        nargs="*",
        help=(
            "Fixture files or a directory to validate "
            "(default: all *.json in fixtures/). "
            "A directory arg (e.g. 'fixtures') validates every *.json it contains."
        ),
    )
    args = parser.parse_args(argv)

    schema, err = load_json(SCHEMA_PATH)
    if err is not None:
        print(f"ERROR: {err}", file=sys.stderr)
        sys.exit(1)
    validator = Draft202012Validator(schema)

    if args.files:
        # Expand any directory arg into its *.json children; pass-through file args.
        # This supports both pre-commit hook mode (staged file list) and a
        # directory arg (e.g. `validate_fixtures.py fixtures`) per the gate contract.
        candidates = []
        directory_scan = False
        for raw in args.files:
            p = pathlib.Path(raw)
            if p.is_dir():
                directory_scan = True
                candidates.extend(sorted(p.glob("*.json")))
            else:
                candidates.append(p)
        fixture_paths = [
            p for p in candidates if p.suffix == ".json" and p.name not in NON_FIXTURE_FILES
        ]
        # Reverse (unregistered-file) check scope: when a directory is scanned we
        # treat it like a full scan; otherwise only the explicitly listed files.
        reverse_scope = candidates if directory_scan else fixture_paths
    else:
        # Full-scan mode (CI / default): every fixture except non-fixture JSON.
        fixture_paths = [
            p for p in FIXTURES_DIR.glob("*.json") if p.name not in NON_FIXTURE_FILES
        ]
        reverse_scope = list(FIXTURES_DIR.glob("*.json"))

    all_errors = []
    for fp in fixture_paths:
        all_errors.extend(validate_fixture(fp, validator))

    # INDEX invariants always run against the full INDEX.json for consistency;
    # the reverse (unregistered) check is scoped to the files being validated.
    all_errors.extend(check_index_invariants(reverse_scope))

    if all_errors:
        for e in all_errors:
            print(f"ERROR: {e}", file=sys.stderr)
        print(
            f"FAILED: {len(all_errors)} error(s) across {len(fixture_paths)} fixture(s).",
            file=sys.stderr,
        )
        sys.exit(1)

    print(f"OK: {len(fixture_paths)} fixture(s) valid.")
    sys.exit(0)


if __name__ == "__main__":
    main()
