/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

// PROT-06: malformed / truncated packet rejection. Feeds deliberately-truncated
// packet bytes to the frozen Phase-3 codec and asserts the NetPacketReader bounds
// guard ("Received packet truncated", ProtocolCodec.h:116-189) raises
// spades::Exception BEFORE any out-of-bounds read — the test process must NOT crash
// or read OOB (a clean exit code proves it). Also characterizes the PRE-EXISTING
// lenient non-divisible-WorldUpdate quirk (integer-division entry count) as a
// documented implementation_detail (NOT a bug).
//
// The two-arg spades::Exception ctor SPRaise expands to derefs the global Backtrace,
// which is NULL until StartBacktrace() runs. ProtocolReplayUnitTest.cpp (Plan 04-01)
// already installs a process-wide BacktraceEnvironment GTest global Environment that
// calls Backtrace::StartBacktrace() in SetUp(); this binary links that TU, so SPRaise
// is catchable here in-process. Do NOT duplicate that Environment (it is idempotent
// but a second registration is redundant).

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <Client/ProtocolCodec.h>
#include <Core/Exception.h>

#include "SettingsGuard.h"

using namespace spades;
using namespace spades::client;

// Lightweight fixture: pins cg_unicode="1" (matches the other Protocol suites). No
// World/Player/ENet — the codec is pure (D-01).
class ProtocolMalformedTest : public ::testing::Test {
protected:
	spades::tests::SettingsGuard guard_;
};

// ---------------------------------------------------------------------------
// (a) Truncated StateData mid-field: tag + playerId only. DecodeStateData reads
// playerId (ok), then ReadIntColor's first ReadByte runs off the end → SPRaise
// ("Received packet truncated", ProtocolCodec.h:141). No fold is reached; the
// reader never reads past data.size(). (RESEARCH Pattern 4.)
// ---------------------------------------------------------------------------
TEST_F(ProtocolMalformedTest, TruncatedStateDataMidField) {
	// [0]=PacketTypeStateData(15=0x0f), [1]=playerId(0x05). Total 2 bytes.
	std::vector<char> bytes = {(char)PacketTypeStateData, (char)0x05};
	NetPacketReader r(bytes);
	ASSERT_EQ(r.GetType(), PacketTypeStateData);
	EXPECT_THROW(DecodeStateData(r), spades::Exception); // "Received packet truncated"
}

// ---------------------------------------------------------------------------
// (b) Truncated WorldUpdate mid-entry (v4 / 0.76, 25 B/entry): a buffer of
// tag + 24 bytes (total 25) makes the integer-division entry count
// GetLength()/25 == 1, so the codec ATTEMPTS one full 25-byte entry from only 24
// available bytes after the tag → the leading index ReadByte + ReadVector3 reads
// run off the end → SPRaise. (Per Pitfall 8: 25/25==1 entry attempted from 24
// available → throw. A length < 25 would yield 0 entries and NOT throw — getting
// this boundary right is what makes the test fail for the RIGHT reason, Pitfall 6.)
// ---------------------------------------------------------------------------
TEST_F(ProtocolMalformedTest, TruncatedWorldUpdateMidEntry) {
	// 25 bytes total: tag + 24 → 25/25 == 1 entry attempted; a v4 entry needs 25
	// bytes (1 index + 12 pos + 12 front) after the tag, but only 24 remain.
	std::vector<char> bytes(25, 0);
	bytes[0] = (char)PacketTypeWorldUpdate;
	NetPacketReader r(bytes);
	ASSERT_EQ(r.GetType(), PacketTypeWorldUpdate);
	EXPECT_THROW(DecodeWorldUpdate(r, 4), spades::Exception); // truncated mid-entry
}

// ---------------------------------------------------------------------------
// (c) Lenient non-divisible WorldUpdate (v3 / 0.75, 24 B/entry): a buffer of
// tag + 10 bytes (total 11) gives GetLength()/24 == 0 entries — the codec decodes
// ZERO entries and SILENTLY IGNORES the 10 remainder bytes. NO throw, NO OOB read,
// NO crash. This is the PRE-EXISTING integer-division entry-count contract
// (ProtocolCodec.cpp:633, mirroring NetClient :804) — tagged implementation_detail
// (A5), NOT a bug. A future Rust port MAY choose to be stricter (reject a
// non-divisible payload); that divergence is documented in fixtures/known_bugs.md.
// This test DOCUMENTS the C++ oracle behavior so the contract is explicit.
// ---------------------------------------------------------------------------
TEST_F(ProtocolMalformedTest, WorldUpdateRemainderLenient) {
	// 11 bytes total: tag + 10 → 11/24 == 0 entries. The 10 trailing bytes are
	// dropped by integer division, never read — no over-read possible.
	std::vector<char> bytes(11, 0);
	bytes[0] = (char)PacketTypeWorldUpdate;
	NetPacketReader r(bytes);
	ASSERT_EQ(r.GetType(), PacketTypeWorldUpdate);
	WorldUpdatePacket decoded;
	EXPECT_NO_THROW(decoded = DecodeWorldUpdate(r, 3));
	// Integer-division entry count drops the trailing partial entry → 0 entries.
	EXPECT_EQ(decoded.entries.size(), 0u);
}
