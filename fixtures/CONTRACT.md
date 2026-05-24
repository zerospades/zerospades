# ZeroSpades Golden-Spec Portable Contract

This document is the **language-agnostic specification** of how the ZeroSpades
golden-spec fixtures are replayed, compared, and decoded. It exists so a non-C++
implementer (e.g. a Rust/Bevy port) can build a replay-and-compare runner from
**this file plus `fixture_schema.json` and the `fixtures/*.json` corpus alone** —
without reading a single line of the C++ test harness or game source.

The current C++ implementation is the *oracle*: the fixtures snapshot its outputs,
and this contract describes the behavior those snapshots assume. Where the contract
and the running C++ tooling could ever disagree, an automated drift-guard (see
§2) pins the tolerance values together; the remaining semantics are frozen here as
prose.

---

## 0. Preamble

### 0.1 Purpose

A fixture is a recorded *input → output* pair captured from the C++ oracle under a
deterministic, headless, fixed-timestep simulation. A conforming port proves
behavioral parity by:

1. **Replaying** a fixture's inputs through its own implementation (per-kind rules
   are in §3, "Per-Kind Replay Semantics");
2. **Comparing** the output it produces against the fixture's recorded `expected`
   object using the algorithm in §1;
3. **Decoding** any wire-format bytes a fixture carries using the conventions in §4.

If the comparison passes for every fixture in the corpus, the port matches the
oracle within tolerance — no human playtesting required.

### 0.2 Coordinate Convention

ZeroSpades / Ace of Spades uses a **left-handed** coordinate system whose vertical
axis is `Z`, inverted relative to most engines:

- **Z = 0** is the **sky** (top of the world).
- **Z = 63** is the **water** line (bottom of the playable column).
- Larger `Z` means *lower* in the world.

All 3-vectors are encoded as `{x, y, z}` float objects — never bare arrays — so axis
identity survives language boundaries unambiguously.

### 0.3 Determinism Contract

The corpus is only comparable because the oracle ran under a controlled, repeatable
simulation. A port must reproduce the same controls:

- **Fixed timestep:** the simulation advances in steps of `dt = 1/60 second`
  (approximately `0.0166667 s`). There is no wall-clock, no variable frame timing,
  and no thread-scheduling nondeterminism inside a fixture.
- **Seeded RNG:** every fixture declares a `seed` (the corpus uses `42` throughout).
  RNG-derived paths and their non-portability boundary are documented in §5.
- **Tolerance compare:** floating-point math is *not* bit-identical across languages
  and platforms, so numeric fields are compared within per-field absolute tolerances
  (§2) rather than for exact equality.

### 0.4 Glossary

- **Fixture** — one JSON document conforming to `fixture_schema.json`; a single
  recorded input → output case.
- **Envelope** — the top-level fixture object. Its keys are `version`, `id`,
  `subsystem`, `behavior`, `seed`, `protocol_version`, `map`, `inputs`, `kind`, and
  `expected` (plus an optional `api` block on some kinds).
- **`kind`** — the discriminator that selects how a fixture is replayed and what
  shape its `expected` object takes. The four kinds are `step_trace`,
  `world_snapshot`, `value_lookup`, and `packet_roundtrip`.
- **`expected`** — the recorded oracle output for the fixture. This is the object
  the comparison algorithm walks.
- **Baseline vs. candidate** — the comparison takes two snapshots. The **baseline**
  is the reference/oracle output (the value frozen in the fixture). The **candidate**
  is the snapshot under test, e.g. a port's output. The verdict is independent of
  which side is which (see §1.7).

---

## 1. Comparison Algorithm (CONTRACT-01)

This section specifies, as language-agnostic prose, the exact field-level diff the
golden-spec uses. A port that implements this algorithm faithfully will agree with
the oracle's verdict on any input.

### 1.1 Entry Point

The comparison takes two whole fixture/snapshot JSON documents — a **baseline** and a
**candidate**. Each document **must** carry a top-level `expected` key; if either is
missing it, the comparison is a usage error (exit code `2`, see §1.8).

The walk begins by comparing the **`expected` object of the baseline** against the
**`expected` object of the candidate**, using the dotted path `expected` as the
starting field path. Only the `expected` subtree is compared — the envelope metadata
(`version`, `id`, `seed`, etc.) is not part of the verdict.

### 1.2 Recursive Field Walk

The comparison recurses over the two values in lockstep, building a human-readable
**field path** as it descends:

- Descending into an object key `k` appends `.k` to the path (e.g. `expected.player`).
- Descending into array index `i` appends `[i]` to the path (e.g.
  `expected.ticks[3]`).

The path is significant: it is the string fed to the tolerance dispatch in §2, and it
is what diff lines report.

The behavior at each node depends on the JSON types of the two values, evaluated in
the following order: **object rule → array rule → numeric rule → exact rule.**

### 1.3 Object Rule (symmetric MISSING / EXTRA)

If **both** values are objects, the comparison is *symmetric*:

- For every key present in the **baseline** object: if the candidate object does not
  contain that key, emit a **`MISSING`** difference at `path.key` and mark a mismatch.
  Otherwise recurse into both values at that key.
- For every key present in the **candidate** object: if the baseline object does not
  contain that key, emit an **`EXTRA`** difference at `path.key` and mark a mismatch.

Both halves run, so a key in only one side is always flagged. This symmetry is
essential to the parity guarantee: a port that emits *undocumented extra fields* or
*omits expected ones* must FAIL rather than silently pass, and the verdict must not
depend on argument order.

### 1.4 Array Rule (size-strict, then element-wise)

If **both** values are arrays:

- If their sizes differ, emit a **`MISMATCH`** at `path` recording both sizes and mark
  a mismatch. (Size mismatch alone is a failure — arrays are size-strict.)
- **Regardless of whether the sizes matched**, element-compare the first
  `min(baseline_size, candidate_size)` elements pairwise, recursing into each pair at
  `path[i]`.

There is no order-insensitive set comparison and no padding: index `i` in the
baseline is compared only against index `i` in the candidate.

### 1.5 Numeric Rule (tolerance — only when both sides numeric and at least one float)

The tolerance branch is taken **only when both values are numeric AND at least one of
them is a floating-point number.** Concretely:

> Take the tolerance branch when `(baseline is a float OR candidate is a float)` **and**
> `baseline is a number` **and** `candidate is a number`.

When that condition holds, compute the per-field tolerance from the current field path
(§2) and compare the absolute difference of the two values as doubles:

- If `|baseline − candidate| > tolerance`, emit a **`MISMATCH`** at `path` (recording
  both values, the difference, and the tolerance) and mark a mismatch.
- Otherwise the field passes (it is within tolerance).

### 1.6 No Integer-to-Float Widening (both halves)

The numeric rule deliberately does **not** widen integers to floats. State both halves
explicitly:

- **int-vs-int → exact.** When *neither* side is a float (both are integers), the
  numeric branch is **not** taken; the pair falls through to the exact rule (§1.7).
  Therefore `40` vs `41` is an exact integer mismatch — it is *never* "within
  tolerance." Two integers must be byte-equal.
- **(int-or-float)-vs-float → tolerance.** When *at least one* side is a float and
  both are numeric, the tolerance branch *is* taken. Therefore `40` (integer) vs
  `40.0` (float) **is** tolerance-compared and passes — both are numeric and one is a
  float. There is no error in mixing an integer with a float; they are compared within
  tolerance.
- **numeric-vs-non-numeric → exact (type) mismatch.** When one side is numeric and the
  other is not (e.g. a number vs a string), the numeric branch is **not** taken (the
  "both numeric" half of the condition fails); the pair falls through to the exact
  rule, which reports the type difference as a `MISMATCH`. A number is never coerced to
  or from a string.

### 1.7 Exact Rule

Any pair that is neither two objects, nor two arrays, nor the numeric-tolerance case
is compared for **exact equality** of the JSON values. This covers:

- integer vs integer,
- boolean vs boolean,
- string vs string,
- null vs null,
- and any **mismatched container/type** pair (object vs array, number vs string, array
  vs object, etc.).

If the two values are not exactly equal, emit a **`MISMATCH`** at `path` (recording
both values) and mark a mismatch.

### 1.8 Verdict and Exit Codes

The verdict is **order-independent**: if *any* difference was emitted during the walk
(any `MISSING`, `EXTRA`, or `MISMATCH`), the overall result is **FAIL**; otherwise it
is **PASS**. The set of differences found does not depend on which document was passed
as baseline vs. candidate.

The runner reports its result via process exit codes:

| Exit code | Meaning |
|-----------|---------|
| `0` | **PASS** — every compared field matched (numeric fields within tolerance). |
| `1` | **FAIL** — one or more field differences were found; the diff is printed. |
| `2` | **Error** — usage error (wrong argument count), a file that cannot be opened, a JSON parse error, or either input missing the top-level `expected` key. |

### 1.9 Worked Example

Suppose the baseline `expected` is:

```json
{
  "position": { "x": 1.0, "y": 2.0, "z": 3.0 },
  "armor": 100,
  "team": "blue"
}
```

and the candidate `expected` is:

```json
{
  "position": { "x": 1.00005, "y": 2.0, "z": 3.0 },
  "team": 1,
  "ammo": 30
}
```

The walk produces:

- `expected.position.x` — both numeric, candidate is a float → **tolerance branch.**
  The path contains no special substring, so the default position tolerance `1e-4`
  applies. `|1.0 − 1.00005| = 5e-5 ≤ 1e-4` → **within tolerance, PASS.**
- `expected.position.y`, `expected.position.z` — equal floats within tolerance → PASS.
- `expected.armor` — present in baseline, absent in candidate → **`MISSING`
  expected·armor** (a key in the baseline with no counterpart in the candidate).
- `expected.team` — baseline `"blue"` (string) vs candidate `1` (number): one side
  numeric, the other not → falls to the exact rule → type **`MISMATCH` expected.team.**
- `expected.ammo` — present in candidate, absent in baseline → **`EXTRA` expected.ammo.**

Because at least one difference was emitted, the verdict is **FAIL** (exit code `1`).
Had the candidate matched the baseline exactly (only the within-tolerance `position.x`
perturbation), the verdict would be **PASS** (exit code `0`).
