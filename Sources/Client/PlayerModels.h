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

#include <map>
#include <string>
#include <vector>

#include <Core/Math.h>
#include <Core/RefCountedObject.h>
#include <Core/VoxelModel2KV6.h>

namespace spades {
	namespace client {
		class IModel;
		class IRenderer;

		/**
		 * One part of a player model set, drawn with the matrix the game puts
		 * its joint at, times `local`: identity for a classic .kv6 (whose
		 * pivot is the joint), and the part's own place on its hinge for one
		 * of a scene, which comes to the same.
		 */
		struct PlayerPart {
			Handle<IModel> model;
			Matrix4 local = Matrix4::Identity();
			explicit operator bool() const { return model.GetPointerOrNull() != nullptr; }
		};

		/**
		 * The player model sets ("Models/Player/Zombie/"), each either the 13
		 * classic .kv6 parts (Torso.kv6, LegCrouch.kv6...) or one Player.2kv6
		 * holding them as the player template does (player-scene.sh). Each part
		 * comes from whichever the stronger pack gives, as any file does: the
		 * scene's, unless a pack above it (a mod's over the game's own) gives
		 * that part's .kv6, or the scene lacks it. So a skin of either kind,
		 * or a mod replacing a single .kv6 of a scene's set, loads as it is.
		 *
		 * The game still places every joint itself (walking, aiming,
		 * crouching, the ragdoll): a scene gives the parts, not the poses.
		 */
		class PlayerModels {
		public:
			explicit PlayerModels(IRenderer& renderer);
			~PlayerModels();

			/**
			 * Part `name` of the set in `dir`, named as its classic .kv6 is
			 * ("Torso"). Throws as RegisterModel does when neither the scene
			 * nor the .kv6 has it.
			 */
			PlayerPart Get(const std::string& dir, const std::string& name);
			/**
			 * The Christmas hat every set shares (SantaHat.kv6), where the set's
			 * scene wears it over its head (drawn with the head's matrix); none
			 * (false) for a set without a scene, a scene with nowhere for it, or
			 * a head a stronger pack gives as a .kv6: those show XmasHat.kv6 in
			 * place of the head instead, as ever.
			 */
			PlayerPart Hat(const std::string& dir);
			/** Loads every part of the set in `dir` ahead of the game. */
			void Preload(const std::string& dir, bool christmas);

			/** The classic parts' names, the .kv6 files of a set. */
			static const std::vector<std::string>& PartNames();

		private:
			struct Set {
				bool loaded = false;
				std::vector<VoxelObject> scene; // empty for a classic set
				int sceneRank = -1;             // FileManager::Rank of the scene
				std::map<std::string, PlayerPart> parts;
			};
			IRenderer& renderer;
			std::map<std::string, Set> sets;

			Set& SetIn(const std::string& dir);
			/** The part at `path` in the set's scene; none if it holds none. */
			PlayerPart ScenePart(Set& set, const std::string& path);
		};
	} // namespace client
} // namespace spades
