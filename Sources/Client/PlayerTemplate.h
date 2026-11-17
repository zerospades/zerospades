/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

namespace spades {
	namespace client {
		/**
		 * The shape of a player set's templated scene (Player.2kv6, built by
		 * player-scene.sh): where the game looks for what. Said once, for the
		 * game that reads a set from it and the editor that shows one.
		 *
		 * The player's root stands where the game puts the player's origin;
		 * each pose holds what changes with it.
		 */
		namespace PlayerTemplate {
			constexpr const char* kRoot = "Player";
			/** The poses: each hinge stands where the game puts the player's
			 *  origin for it, the legs' joint. */
			constexpr const char* kStanding = "Player/Standing";
			constexpr const char* kCrouching = "Player/Crouching";
			/** The joints the game hangs a player's parts from, as the template's
			 *  hinges: the head's and the arms', shared by both poses, and the
			 *  torso's, a pose's own (under each pose's hinge). */
			constexpr const char* kNeck = "Player/Neck";
			constexpr const char* kShoulders = "Player/Shoulders";
			constexpr const char* kUpperBody = "UpperBody";
			/** Where a set's head wears the Christmas hat: a hinge the hat's
			 *  pivot sits on. */
			constexpr const char* kHatMount = "Player/Neck/SantaHat";

			/**
			 * From the game's world units to the template's voxels: a voxel is
			 * a tenth of a block, and a part's x and y run the other way from
			 * the world's (the matrix the game draws every part with).
			 */
			constexpr float kVoxelsPerBlock = 10.0F;
		} // namespace PlayerTemplate
	} // namespace client
} // namespace spades
