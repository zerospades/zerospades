# Known Bugs (`known_bug_do_not_preserve` fixtures)

This file documents every fixture tagged `behavior: "known_bug_do_not_preserve"`. These
fixtures capture observable C++ ZeroSpades behavior that is a **bug** — a future Rust/Bevy
port must NOT be forced to reproduce it. Each entry cross-references the fixture by `id`
and explains why the captured behavior is incorrect.

No `known_bug_do_not_preserve` fixtures exist in this phase. Entries are added here as
fixtures are tagged with this behavior in Phases 4-7.

## Cross-Reference Table

| Fixture ID | Bug Description | Phase Added |
|------------|-----------------|-------------|
| _(none yet)_ | — | — |

## Pre-Existing Lenient Behaviors (NOT bugs — documented for porters)

These are deliberate `implementation_detail` characterizations, explicitly **NOT**
tagged `known_bug_do_not_preserve`. They record the actual C++ ZeroSpades contract a
Rust/Bevy port must understand. A port MAY choose stricter handling where noted; the
divergence allowance is documented so it is an explicit, reviewed choice rather than an
accidental incompatibility.

### Lenient non-divisible WorldUpdate payload (integer-division entry count)

- **Where:** `DecodeWorldUpdate` (`Sources/Client/ProtocolCodec.cpp:633`), mirroring
  `NetClient` `HandleWorldUpdate` (NetClient.cpp:804).
- **Behavior:** the entry count is `GetLength() / bytesPerEntry` (integer division;
  v3 = 24 B/entry, v4 = 25 B/entry). `GetLength()` includes the 1-byte type tag. A
  WorldUpdate whose payload is NOT a clean multiple of the entry size has its trailing
  partial entry **silently dropped** — no `SPRaise`, no out-of-bounds read. Example: a
  v3 packet of `tag + 10 bytes` (11 total) yields `11 / 24 == 0` entries and ignores the
  10 remainder bytes.
- **Why it is NOT a bug (WR-03 / A5):** integer-division entry counting is the genuine,
  intentional C++ wire contract — it is the mechanism that prevents an over-read on a
  short payload (the loop only runs for whole entries that are fully present). It is a
  fail-safe under-read, not corruption. Tagged `implementation_detail`, never
  `known_bug_do_not_preserve`. Do **NOT** "fix" this in the C++ oracle.
- **Characterized by:** `ProtocolMalformedTest.WorldUpdateRemainderLenient`
  (`EXPECT_NO_THROW` + asserts 0 entries) and documented by fixture
  `proto_malformed_001` (a *true* mid-entry truncation — distinct from the lenient case —
  that DOES raise is also covered by `ProtocolMalformedTest.TruncatedWorldUpdateMidEntry`).
- **Rust-port divergence allowance:** a stricter port MAY reject a non-divisible
  WorldUpdate payload outright. That is an acceptable, documented divergence (the
  observable lenient outcome is "0 trailing entries", which a strict port can treat as a
  rejection without changing any successfully-decoded entries). Porters choosing strict
  rejection should note it against this entry.

### ClipBox-vs-ClipWorld z-ceiling + XY-out-of-bounds voxel convention

- **Where:** `ClipBox` and `ClipWorld` (`Sources/Client/GameMap` voxel-query path).
- **Behavior:** the two queries diverge **intentionally** on out-of-bounds coordinates.
  `ClipBox` clamps an XY out-of-bounds coordinate to **`true`** (treated as clipped/solid);
  `ClipWorld` returns **`false`** for the same XY out-of-bounds coordinate (not clipped).
  On the z axis both share the AoS ceiling convention: `z < 0 -> false`, `z = 62` (flat-map
  ground) `-> true`, `z = 63` **remaps to 62** `-> true`, and `z = 64` (`>= DefaultDepth`)
  `-> true`. The XY divergence (ClipBox `true` vs ClipWorld `false`) is the key difference
  between the two.
- **Why it is NOT a bug:** this is the documented Ace-of-Spades voxel-boundary convention,
  not a defect. The two functions answer different questions (box-vs-world clipping) and
  their out-of-bounds answers are deliberately distinct; the z=63→62 remap and the
  `z >= DefaultDepth -> true` ceiling are the genuine intentional C++ contract. Both
  fixtures characterize the convention exactly; neither is `known_bug_do_not_preserve`.
  Tagged `implementation_detail` — a port may use any internal representation as long as the
  observable answers match.
- **Characterized by:** `map_value_lookup_009_clip_box` and `map_value_lookup_010_clip_world`
  (both `implementation_detail`).
- **Rust-port divergence allowance:** none required — a port MUST reproduce these answers
  (they govern collision/movement). The internal representation is free; the observable
  ClipBox/ClipWorld results are not.

### HitTypeMelee `SPAssert` server-authoritative contract

- **Where:** `GetDamage` switch (`Sources/Client/Weapon.cpp:228`):
  `default: SPAssert(false); return 0;`.
- **Behavior:** `GetDamage(HitTypeMelee)` is **never called by the client** — melee damage is
  server-authoritative, so the client has no melee branch in `GetDamage`. The `default:`
  `SPAssert(false)` is a defensive contract guarding an impossible client call: in a debug
  build it asserts; in a release build it returns `0`.
- **Why it is NOT a bug:** the assert documents an invariant ("the client never queries melee
  damage"), it does not implement a missing feature. There is no observable in-game effect
  because the path is unreachable from client code. Each weapon fixture records this in its
  `melee_note`. Tagged with whatever behavior its weapon fixture carries (now
  `protocol_compat` after the SC-2 retag); never `known_bug_do_not_preserve`.
- **Characterized by:** the `melee_note` field in every `weap_value_lookup_*` damage fixture
  (e.g. `weap_value_lookup_rifle_075`).
- **Rust-port divergence allowance:** a port should mirror the contract — assert in debug,
  return `0` in release — and keep melee damage server-authoritative (no client-side
  `GetDamage(HitTypeMelee)` branch).
