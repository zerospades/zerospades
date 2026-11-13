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
		 * classic .kv6 parts (Torso.kv6, LegCrouch.kv6...) or one .2kv6 scene
		 * holding them as the player template does (player-scene.sh). The
		 * set's script (Scripts/Skin/Player/Zombie.as) says which, as a skin's
		 * scripts say what its weapon is made of: a skin replaces it with its
		 * own. Every set's script is the same function (PlayerModels), built
		 * on its own, its paths in the set's folder: any skin goes in any
		 * set's place. A classic part from a pack above that script's still wins over
		 * the scene, so skins made before there were scenes load as they are;
		 * a part the scene lacks is its .kv6 too.
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
				int declaredRank = -1; // FileManager::Rank of the script choosing the scene
				std::map<std::string, PlayerPart> parts;
			};
			IRenderer& renderer;
			std::map<std::string, Set> sets;

			Set& SetIn(const std::string& dir);
			/** "Rifle" for "Models/Player/Rifle/", "Classic" for the sets' own. */
			static std::string SetName(const std::string& dir);
			/** What the set's `script` says it is made of: its PlayerModels(),
			 *  built as a module of its own. Throws when it cannot say. */
			static std::string Declared(const std::string& script);
			/** Whether the classic part `file` takes the place of the set's
			 *  scene's: it comes from a pack above the script's. */
			bool ClassicWins(const Set& set, const std::string& file) const;
			/** The part at `path` in the set's scene; none if it holds none. */
			PlayerPart ScenePart(Set& set, const std::string& path);
		};
	} // namespace client
} // namespace spades
