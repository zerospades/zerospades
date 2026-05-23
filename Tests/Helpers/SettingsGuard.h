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

#pragma once

#include <cstdint>
#include <memory>

#include <Core/Math.h>
#include <Core/Settings.h>
#include <gtest/gtest.h>

namespace spades {
	namespace tests {

		/**
		 * RAII wrapper that resets all Settings to their descriptor defaults on construction
		 * and again on destruction. Non-copyable, non-moveable.
		 *
		 * Use as a member of HeadlessTest (or construct directly in ad-hoc tests).
		 */
		class SettingsGuard {
		public:
			SettingsGuard() { Settings::GetInstance()->ResetToDefaults(); }
			~SettingsGuard() { Settings::GetInstance()->ResetToDefaults(); }

			SettingsGuard(const SettingsGuard&) = delete;
			SettingsGuard& operator=(const SettingsGuard&) = delete;
			SettingsGuard(SettingsGuard&&) = delete;
			SettingsGuard& operator=(SettingsGuard&&) = delete;
		};

		/**
		 * GoogleTest fixture base for all headless physics / world tests.
		 *
		 * SetUp():  seeds the thread-local RNG to kDefaultSeed, then resets all Settings.
		 * TearDown(): resets all Settings again (guard fires on destruction).
		 *
		 * Inherit from HeadlessTest for any test that constructs World or Player:
		 *   class MyTest : public spades::tests::HeadlessTest {};
		 */
		class HeadlessTest : public ::testing::Test {
		protected:
			static constexpr std::uint64_t kDefaultSeed = 42;

			void SetUp() override {
				SeedLocalRNG(kDefaultSeed, kDefaultSeed ^ 0x9e3779b97f4a7c15ULL);
				guard_.reset(new SettingsGuard());
			}

			void TearDown() override { guard_.reset(); }

		private:
			std::unique_ptr<SettingsGuard> guard_;
		};

	} // namespace tests
} // namespace spades
