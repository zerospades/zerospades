# ZeroSpades Golden-Spec Fixtures

This directory holds the **portable golden-spec fixture corpus** for the ZeroSpades test
harness. Each fixture is a self-describing JSON document conforming to
[`fixture_schema.json`](fixture_schema.json) (JSON Schema 2020-12). Fixtures record
deterministic input→output pairs from the current C++ ZeroSpades implementation (the oracle)
so that any future port (e.g. Rust/Bevy) can be auto-verified against identical golden data
without a human in the loop.

## Coordinate Convention

ZeroSpades / Ace of Spades uses a **left-handed** coordinate system. The vertical axis is
`Z`, and it is inverted relative to most engines:

- **Z=0** is the **sky** (top of the world).
- **Z=63** is the **water** line (bottom of the playable column).
- Larger `Z` means *lower* in the world.

All 3-vectors are encoded as `{x, y, z}` float objects (never bare arrays) so axis identity
survives language boundaries unambiguously. This convention is also recorded in the schema's
`$comment` header.

## Hook Setup

A repo-tracked pre-commit hook validates staged fixture JSON before every commit. Enable it
once per clone:

```
git config core.hooksPath .githooks
```

CI re-runs the validator over **all** fixtures as the authoritative gate — the local hook is
a convenience, never the source of truth.

## Fixture Schema

The frozen envelope contract lives at [`fixture_schema.json`](fixture_schema.json), pinned to
schema **version `1.0.0`**. Every fixture must declare `"version": "1.0.0"` and validate
against this schema. The `kind` field discriminates the shape of the `expected` object
(`world_snapshot` | `step_trace` | `value_lookup` | `packet_roundtrip`).

## Validation

Validate every fixture against the schema and check `INDEX.json` invariants:

```
python3 tools/validate_fixtures.py
```

To validate specific files (as the pre-commit hook does for staged files):

```
python3 tools/validate_fixtures.py fixtures/phys_step_trace_001.json
```

The validator requires Python 3 and the `jsonschema` package (`pip install jsonschema`).

## Subsystem Prefixes

Every fixture `id` is prefixed by its subsystem to prevent parallel-authoring drift across
phases. The schema enforces the prefix via a regex pattern, and `INDEX.json` records the
declared `subsystem` for each fixture:

| Prefix   | Subsystem                          |
|----------|------------------------------------|
| `proto_` | Network protocol (AoS 0.75/0.76)   |
| `phys_`  | Player physics / movement          |
| `map_`   | Voxel map and block mechanics      |
| `weap_`  | Weapons and grenades               |
| `mode_`  | Game modes (CTF / TC)              |

## Registry

[`INDEX.json`](INDEX.json) is the fixture registry: it maps each `id` to its `path`,
`subsystem`, `behavior`, and a one-line `description`. The validator enforces unique ids and
that every registered path resolves to a file (and vice versa).
