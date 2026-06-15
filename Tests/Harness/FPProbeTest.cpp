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

// ONE-TIME SETUP: On first run, GOLDEN_BITS == 0 causes GTEST_SKIP.
// ctest reports the test as SKIPPED (exit 0). Read the printed hex value from
// the [FPProbe RECORD] line in the ctest --output-on-failure output.
// Replace 0x00000000U with the printed value, commit, and rerun.
// After the replacement, ctest reports the test as PASSED on every run.

#include <cstdint>
#include <cstring>
#include <cstdio>

#include <gtest/gtest.h>

namespace {

	// Reinterpret a float's bits as a uint32 without UB.
	static uint32_t F2U(float f) {
		uint32_t u;
		std::memcpy(&u, &f, sizeof(u));
		return u;
	}

} // namespace

// ---------------------------------------------------------------------------
// FPProbe/IEEE754BitPattern
//
// Verifies that IEEE-754 floating-point arithmetic is deterministic under the
// FP flags applied to this translation unit:
//   -fno-fast-math  -ffp-contract=off  -msse2  -mfpmath=sse
//
// Op sequence chosen to be sensitive to FP mode changes:
//   1.0F / 3.0F      — rounds to nearest representable single (not exact)
//   a * a            — squares the rounding error
//   b + a - 0.5F     — two additions whose order matters under fast-math reassociation
//   c * 1024.0F      — amplifies any difference
//
// With -ffp-contract=off (no FMA) and -fno-fast-math (no reassociation), the
// result is deterministic across GCC/Clang on x86-64 SSE2.
// ---------------------------------------------------------------------------
TEST(FPProbe, IEEE754BitPattern) {
	float a = 1.0F / 3.0F;
	float b = a * a;
	float c = b + a - 0.5F;
	float d = c * 1024.0F;

	uint32_t bits = F2U(d);

	// GOLDEN_BITS = 0x00000000U is the unset sentinel.
	// Step 1 (first run): if sentinel, print the value and SKIP (not FAIL).
	//   ctest reports SKIPPED — still green. The developer reads the output,
	//   replaces 0x00000000U with the printed value, and commits.
	// Step 2 (subsequent runs): GOLDEN_BITS is non-zero, EXPECT_EQ is a strict
	//   regression check — any FP mode change causes a clear failure.
	constexpr uint32_t GOLDEN_BITS = 0xC2638E30U; // self-recorded on 2026-05-23 (x86-64 SSE2)

	if (GOLDEN_BITS == 0) {
		std::printf("\n[FPProbe RECORD] Computed bits = 0x%08XU\n"
		            "Replace GOLDEN_BITS in FPProbeTest.cpp with this value.\n",
		            bits);
		GTEST_SKIP() << "FP-probe golden not yet recorded. "
		             << "Set GOLDEN_BITS = 0x" << std::hex << bits
		             << "U in FPProbeTest.cpp and rerun (test will then be EXPECT_EQ).";
	}

	EXPECT_EQ(bits, GOLDEN_BITS)
	    << "FP-probe bit pattern changed. "
	    << "Expected 0x" << std::hex << GOLDEN_BITS << " got 0x" << bits
	    << ". Possible cause: FP flags missing on this translation unit, "
	    << "or cross-platform IEEE-754 drift (see Phase 9 CI gate).";
}
