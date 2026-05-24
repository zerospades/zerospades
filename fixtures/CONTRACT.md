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

---

## 2. Tolerance Table (CONTRACT-02)

The numeric rule (§1.5) compares floating-point fields within a **per-field absolute
tolerance** chosen from the field's dotted path. This section is the canonical,
portable source of those tolerances. It is self-contained: a port implements the table
and the dispatch below verbatim, with no reference to any external source.

### 2.1 The Four Tolerance Constants

| Constant | Value | Applies to |
|----------|-------|------------|
| Position (default) | `1e-4` | Positions, velocities, and every field not matched by a more specific rule below. This is the fallback tolerance. |
| Orientation | `1e-5` | Orientation / view-direction unit vectors. Tightest non-zero tolerance because orientation is produced by transcendental float ops (see §5) and must round-trip closely. |
| Grenade | `1e-3` | Grenade trajectory fields and fuse timers. Loosest tolerance — grenade physics accumulates more float drift over a trajectory. |
| Raycast | `1e-6` | Raycast / hit-scan results. Effectively exact within float noise — raycasts resolve to discrete voxel coordinates. |

### 2.2 Dispatch Order (substring match on the dotted path, first match wins)

To pick the tolerance for a field, test its **dotted path** (the same path built during
the walk, e.g. `expected.player.orientation.x`) for the substrings below **in this
exact order** and return the tolerance of the **first** substring that occurs anywhere
in the path. If none occurs, return the position default.

1. path contains `orientation` → **`1e-5`** (orientation)
2. path contains `fuse` → **`1e-3`** (grenade)
3. path contains `grenade` → **`1e-3`** (grenade)
4. path contains `raycast` → **`1e-6`** (raycast)
5. otherwise (no match) → **`1e-4`** (position default)

The dispatch is a **substring** test, not an exact field-name match: a path like
`expected.player.orientation.x` matches rule 1 because the substring `orientation`
occurs in it, even though the leaf field name is `x`. Order matters because a path can
contain more than one keyword; the first rule that matches wins. (For example, both
`fuse` and `grenade` map to the same `1e-3` value, so their relative order is
immaterial in practice, but the order is fixed here for an unambiguous specification.)

### 2.3 Pin Note

These four values mirror the tolerance constants embedded in the current C++ oracle.
They are **frozen for this milestone**; a later phase (TOL-01) may re-derive them
empirically and revise the table. When that happens, the machine-readable block below
and the oracle's constants are updated together — the drift-guard in §2.4 fails the
build if the two ever disagree.

### 2.4 Machine-Readable Drift-Guard Block

The fenced block below is the **drift-guard source of truth**: an automated test
parses these four `name=value` pairs and asserts they equal the oracle's tolerance
constants. Editing a value here without updating the oracle (or vice versa) fails the
build, keeping the portable table and the running implementation in lockstep. The keys
are exactly `position_tol`, `orientation_tol`, `grenade_tol`, and `raycast_tol`.

```text
position_tol=1e-4
orientation_tol=1e-5
grenade_tol=1e-3
raycast_tol=1e-6
```

---

## 3. Per-Kind Replay Semantics (CONTRACT-03)

§1 told a port *how to compare* two `expected` objects. This section tells it *how to
produce* the candidate `expected` object in the first place: for each `kind`, what the
`inputs` mean, how to construct the initial world state, how to drive the simulation,
which engine operation to invoke, and how the result maps onto the `expected` object the
oracle froze. A port that follows §3 produces a candidate snapshot; §1 then judges it.

### 3.1 The Envelope

Every fixture is a flat JSON object with these top-level keys:

| Key | Type | Meaning |
|-----|------|---------|
| `version` | string | Schema version. The current corpus is `"1.1.0"`. Not part of the compared output. |
| `id` | string | Unique fixture identifier. Its prefix (`phys_`, `map_`, `weap_`, `mode_`, `proto_`) names the subsystem. |
| `subsystem` | string | One of `phys`, `map`, `weap`, `mode`, `proto`. Mirrors the `id` prefix. |
| `behavior` | string | A human label for the behavior under test (e.g. `protocol_compat`, `implementation_detail`). Documentation only. |
| `seed` | integer | The RNG seed the oracle ran under. The whole corpus uses `42` (see §5). |
| `protocol_version` | string | The Ace of Spades wire protocol the fixture assumes — `"0.75"` or `"0.76"`. Selects the WorldUpdate entry layout (§4) and is otherwise informational. |
| `map` | object | The initial voxel map. Currently always `{ "generator": "flat", "ground_z": 62 }` (see §3.2). |
| `inputs` | array | The per-kind stimulus. Its meaning is defined per kind below. For 49 of the 55 current fixtures it is the empty array `[]`. |
| `kind` | string | The discriminator: `step_trace`, `world_snapshot`, `value_lookup`, or `packet_roundtrip`. Selects everything in this section. |
| `expected` | object | The recorded oracle output — the only subtree §1 compares. |
| `api` | object (optional) | Present only on `value_lookup` fixtures that name the operation they exercise: `{ "op": "<dotted.string>", "args": { … } }` (see §3.5). Absent on every other kind, and absent on the current `value_lookup` corpus whose operation is implied by `id` (a future migration populates it). |
| `comment` | string (optional) | Free-form authoring note. Documentation only, never compared. |

### 3.2 Initial State Construction

Before any replay, a port builds the starting world from the `map` block:

- `generator: "flat"` means a **flat, fully-solid ground plane**. Every voxel column is
  **solid from `ground_z` down to the bottom of the world** (`z = 63`, the water line; see
  §0.2) and **air above** `ground_z`. With `ground_z = 62`, columns `z = 62` and `z = 63`
  are solid and everything from `z = 0` to `z = 61` is air. The horizontal extent is the
  standard playing field of 512 by 512 columns.
- Block colors on the flat ground take the engine's default ground color unless a fixture's
  operation explicitly paints a block (see the block-action operations in §3.5).

**Spawn / actor setup is not yet embedded.** The current corpus records outputs only; the
per-tick input schedule and the actor/spawn setup that produced them live outside the
fixture (which is why 49 fixtures carry `inputs: []`). A later migration will embed that
setup into the `inputs`/`api` blocks this contract already types; until then a port
reproduces a fixture's *output* by constructing the same scenario the fixture's `behavior`
and `id` describe. The replay **rules** below are stable regardless of when the inputs are
embedded.

### 3.3 Kind: `step_trace`

A `step_trace` records the evolution of a single subsystem over a run of fixed-timestep
ticks (weapon firing/reload cadence, grenade trajectories, player physics).

- **`inputs`** is a **per-tick input schedule**: an ordered array of per-tick records, one
  describing the controls applied on each simulation tick (e.g. fire/reload/move/jump
  flags). (Currently empty in the corpus; the schedule that produced a trace is implied by
  the fixture's `behavior` until embedded.)
- **Initial state** is built per §3.2, then the subsystem-under-test actor is created (e.g.
  a player holding a rifle, a thrown grenade).
- **The tick-driving loop** advances the simulation in fixed steps of `dt = 1/60 s` (§0.3).
  On each tick the port applies that tick's scheduled input, advances the subsystem by one
  `dt`, and records the subsystem's observable state.
- **`expected`** is an object with a single key **`ticks`**, an array with **one record per
  tick**, in tick order. Each record's fields are subsystem-specific. For a weapon trace the
  per-tick record carries `tick` (the integer tick index), `ammo`, `stock`, `fired`
  (whether a shot was emitted this tick), `reloading`, and `time_to_next_fire` (a float
  countdown — note it goes negative once fire is ready and the trigger is released).
- **Comparison:** `ticks` is an array, so §1.4 applies — the candidate must produce the
  **same number of ticks** and each tick record is compared field-by-field. Float fields
  like `time_to_next_fire` use the position-default tolerance `1e-4` (§2); a grenade trace's
  `fuse`-named fields use the grenade tolerance `1e-3`.

### 3.4 Kind: `world_snapshot`

A `world_snapshot` records the **full multi-player world state at a single tick**. There are
**two distinct production paths**, and a port must support both:

**Path A — packet fold (4 of the 6 current world_snapshots).** Here `inputs` is a
**non-empty ordered array of packet records**, each `{ "bytes_hex": "<hex>", "comment": … }`.
The hex string is one server-to-client packet on the wire (§4). The port decodes each packet
in order and **folds** it into an accumulating world state; the final accumulator, serialized,
is the snapshot. The fold is deterministic and packet-order-significant. Its rules — which a
port must reproduce exactly — are:

A fold accumulator holds, per player index `0..255`: a `saved_pos` 3-vector, a `saved_front`
3-vector (both initialized to `{0,0,0}`), and the live player records keyed by integer id in
an **id-ascending ordered map**. It also holds a `local_player_index` (initialized to a
sentinel "none", i.e. `-1`).

- **StateData** (the game-mode/setup packet): sets `local_player_index` to the packet's
  player id, and establishes the game mode (CTF or TC). In the fold it does **not** create
  players; it marks which index is "local" (the local player is never repositioned by a
  later WorldUpdate, per the rule below).
- **ExistingPlayer**: creates (or updates) the player with the packet's id. Critically, the
  new player's **position is taken from the accumulator's own `saved_pos[id]`, NOT from the
  ExistingPlayer packet** (the packet carries team/weapon/tool/score/color/name, no
  position). The player is marked **alive**. Its orientation is **left at zero** until a
  later WorldUpdate sets it. Team, tool, and weapon come from the packet (see the field
  mappings in §3.4.1).
- **CreatePlayer**: creates the player with the packet's id at the packet's raw wire
  position **with `z` decreased by `2.4`** (the spawn-height adjustment: `pos_z = wire_z −
  2.4`). The player is marked **alive**. A subsequent WorldUpdate for the same index will
  overwrite this position.
- **WorldUpdate**: for **every entry** in the packet, the fold writes `saved_pos[index] =
  entry.position` and `saved_front[index] = entry.front` **unconditionally** (even for
  indices with no live player). It then **repositions** the live player at that index **only
  if** that player **exists, is alive, and is not the local player** — in which case it sets
  that player's `position = entry.position` and `orientation = entry.front`. (Entries are
  indexed per §4's WorldUpdate layout: implicit list-position index in the 0.75 layout,
  explicit leading per-entry index byte in the 0.76 layout.)
- **PlayerLeft**: **erases** the player with the packet's id from the accumulator. Because it
  is erased, a later WorldUpdate for that same index writes `saved_pos`/`saved_front` but
  finds no live player to reposition — **the player is not resurrected.**
- A player's emitted **velocity is always the fold constant `{0,0,0}`** — the protocol carries
  no velocity, so the snapshot reports zero. A player's emitted **health is `100` while alive
  and `0` once dead.**

**Path B — directly-constructed snapshot (2 of the 6 current world_snapshots).** Here
`inputs` is the **empty array** `[]`. The snapshot is not produced by folding packets; it is
a game-mode scenario constructed directly (the two examples are a freshly-set-up CTF world
and a freshly-set-up TC world). A port reproduces the scenario the fixture's `behavior`
describes and serializes it with the **same player record shape** as Path A. (A future
migration embeds the explicit construction steps; the output shape is identical to Path A.)

**`expected`** for either path is an object with:

- **`tick`** — the integer tick the snapshot was taken at (`0` for a pure packet fold, which
  runs no simulation ticks).
- **`players`** — an array of player records (see §3.4.1), emitted in **id-ascending order**.
- **`game_mode`** — present only when a mode was established (e.g. `{ "mode": "ctf" }`);
  omitted otherwise. (Because §1's object rule is symmetric, a port must emit `game_mode`
  exactly when the oracle did — no more, no less.)

#### 3.4.1 Player Record Shape

Each element of `players` is an object with exactly these keys:

| Key | Type | Meaning |
|-----|------|---------|
| `id` | integer | The player's id. |
| `alive` | boolean | Whether the player is alive in this snapshot. |
| `position` | object `{x,y,z}` | World position (float vector; see §0.2 for the Z convention). |
| `velocity` | object `{x,y,z}` | Always `{0,0,0}` for a packet fold (the wire carries no velocity). |
| `orientation` | object `{x,y,z}` | View / facing unit vector. Zero until a WorldUpdate sets it; compared with the tight orientation tolerance `1e-5` (§2). |
| `health` | integer | `100` while alive, `0` once dead. |
| `tool` | string | The held tool, mapped to a friendly string: `block`, `weapon`, `spade`, or `grenade`. |
| `weapon_type` | string | The held weapon, mapped to a friendly string: `rifle`, `smg`, or `shotgun`. |
| `team_id` | integer | The player's team index. |

The wire encodes `tool` and `weapon` as small integers; the snapshot emits the **friendly
string** (e.g. weapon `0` → `"rifle"`, weapon `1` → `"smg"`, weapon `2` → `"shotgun"`; tool
`0` → `"spade"`, `1` → `"block"`, `2` → `"weapon"`, `3` → `"grenade"`). A port's decoder
must emit the same friendly strings or the symmetric compare fails.

### 3.5 Kind: `value_lookup`

A `value_lookup` records the result of invoking **one engine query/operation** against a
constructed state and reading back a value. It is the broadest kind (it covers map queries,
raycasts, block actions, weapon stat tables, and game-mode progress reads).

- **The operation** is named by an **`api` block**: `{ "op": "<dotted.string>", "args": { …
  } }`. `op` is a dotted operation name (an open string, not a closed enum — the registry
  below is the semantic source of truth); `args` is the operation's argument object.
- **`inputs`** is usually the empty array `[]` (the operation and its args fully specify the
  query). A few `proto`-subsystem `value_lookup` fixtures instead carry a `bytes_hex` packet
  in `inputs` whose decoded effect *is* the looked-up value (see ExtensionInfo below).
- **Initial state** is built per §3.2; some operations then mutate it (e.g. a block action
  paints or removes a voxel) before the value is read.
- **`expected`** is an object with a single key **`value`** holding the operation's result —
  a scalar, an object, or a nested structure depending on the operation.

#### 3.5.1 Operation Registry

The table below maps each operation exercised by the **current** `value_lookup` corpus to its
meaning, its arguments, and how `expected.value` is produced. Operation names are dotted
and namespaced by subsystem (`map.*`, `physics.*`, `weapon.*`, `mode.*`, `proto.*`). The
registry grows as later phases add operations; `op` stays an open string so the schema never
needs a new enum.

| `op` | Meaning | Args (conceptual) | How `expected.value` is produced |
|------|---------|-------------------|-----------------------------------|
| `map.isSolid` | Is a voxel solid? | a solid coordinate and an air coordinate | reports the boolean solidity at each probed coordinate (e.g. solid at ground `z`, air above). |
| `map.getColor` | Read/write a block color | a block coordinate; a color to set | sets a block color, reads it back, and reports the packed color integer (and the stored color/health byte). |
| `map.getSolidMap` | The per-column solidity bitmask | a column `x,y` | reports the column's 64-bit solidity word (decimal + hex) and which of the ground bits (60/61/62) are set. |
| `map.checkNeighbors` | Does a voxel have solid 6-neighbors? | several probe coordinates | reports the boolean neighbor-presence for each probed case. |
| `map.isSurface` | Is a voxel a surface (solid with exposed face)? | probe coordinates | reports the boolean surface flag per case. |
| `map.castRay` | Hit-scan a ray through the voxel world | ray origin `o` and direction `d`, max step count | reports `hit`, the hit block coordinate (`hitBlock` / `block_x/y/z`), the hit position (`hitPos`), the face `normal`, `startSolid`, and `stepCount`. Raycast fields use the tight `1e-6` tolerance (§2). A miss reports `hit:false` and the ray parameters only. |
| `map.clipBox` / `map.clipWorld` | Out-of-bounds / world clipping test | probe coordinates incl. out-of-range `z` | reports per-probe booleans for whether each coordinate is clipped (out of bounds, below `0`, at/below the water line). |
| `map.blockAction` | Apply a build/destroy/grenade block edit | the action center and kind (create / tool-destroy / dig / grenade) | mutates the map then reports solidity (and color) at and around the affected voxels; the grenade variant reports the destroyed `cells`. |
| `map.cubeLine` | The voxel line between two points | two endpoints `v1`,`v2`, a max length | reports the ordered list of `cells` on the line and the count. |
| `map.clusterize` | Floating-block cluster detection after a destroy | a pillar/structure setup and a destroyed voxel | reports which disconnected clusters fall (the `callbacks` / `callback_count` and the floating-`z` columns). |
| `weapon.getStats` | A weapon's stat table for a protocol | weapon kind + protocol version | reports `clip`, `stock`, `delay_s`, `reload_s`, `reload_slow`, `pellets`, `spread`, per-hit-zone `damage` (head/torso/arms/legs/block), and the `weapon`/`protocol` labels. (Melee damage is unhandled by the client — it returns `0` / asserts; see the `melee_note` field.) |
| `weapon.getModifiers` | Aim/crouch spread and recoil modifier rules | none / protocol | reports the base spread per protocol and the `spread_rules` / `recoil_rules` describing how aiming and crouching scale them. |
| `mode.ctfState` | Capture-the-Flag state after a scripted sequence | a sequence of pickup/drop/capture events | reports, per sampled step, each team's score, intel-held flag, flag position, and carrier id. |
| `mode.tcProgress` | Territory-Control capture progress | a pinned world time and a reset case | reports the territory's `progress`, `progress_rate`, `progress_base_pos`, `progress_start_time`, owner/capturing team ids, evaluated at the pinned time. |
| `proto.extensionInfo` | The advertised protocol-extension map | an ExtensionInfo packet in `inputs` (`bytes_hex`) | decodes the ExtensionInfo packet and reports the map of advertised `extension id → version` (keyed by the id as a string). **All** advertised entries are recorded. |

A port implements an operation by name, runs it against the §3.2 state with the given args,
and serializes its result under `expected.value` using the same field names the table and the
real fixtures use.

### 3.6 Kind: `packet_roundtrip`

A `packet_roundtrip` pins the **wire encoding of a single packet** in both directions — it is
the unit-level companion to the `world_snapshot` packet fold.

- **`inputs`** is the empty array `[]`. The **stimulus is the `bytes_hex` field inside
  `expected`**, not `inputs`.
- **`expected`** is an object with two keys:
  - **`bytes_hex`** — the packet's exact bytes as a lowercase hex string (the first byte is
    the packet-type tag; see §4).
  - **`decoded`** — the friendly decoded form of those bytes (see §4's `decoded` convention).
- **The contract is bidirectional:** a port must (a) **decode** `bytes_hex` and produce a
  `decoded` object that matches the recorded one field-for-field (§1 symmetric compare), and
  (b) **re-encode** that decoded form and reproduce **the same `bytes_hex`** byte string.
  Round-trip stability (`encode(decode(bytes)) == bytes`) is the invariant.

The packets the current `packet_roundtrip` and proto `world_snapshot` fixtures use, and how
to decode each from the bytes alone, are specified next in §4.
