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
