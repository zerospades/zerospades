/*
 Copyright (c) 2026 ZeroSpades developers.

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
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <Core/FileManager.h>
#include <Core/ModPak.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace client {
		/**
		 * A mod a game gives to some players only: a player skin, a weapon skin,
		 * or both. Its paks are read into memory but mounted nowhere: they sit on
		 * top of what the game sees only while the skin's assets load (`Scope`).
		 *
		 * A skin holds game assets (see GameAssetKind). If it brings scripts, it
		 * gets a script module of its own, built from the game's scripts with its
		 * own on top.
		 */
		class Skin : public RefCountedObject {
		public:
			/**
			 * Reads the mod `name` from Mods/ and builds its scripts, if it has any.
			 * Files that are not game assets, such as a readme, are left out.
			 * Raises if the mod isn't there or its scripts don't build.
			 */
			static Handle<Skin> Load(const std::string& name);

			const std::string& GetName() const { return name; }
			/** Whether the skin has its own `path`, or its own metadata for it. */
			bool Has(const std::string& path) const;
			/** Its models, images and sounds, to load before anyone wears it. */
			const std::vector<std::string>& GetAssets() const { return assets; }
			/** The script module to make its players' skins from. */
			const std::string& GetScriptModule() const { return scriptModule; }

			/**
			 * While one is alive, reads on this thread see the skin on top. Only on
			 * this thread: every game asset loads on the main thread, and a load
			 * elsewhere would not see the skin.
			 */
			class Scope {
				FileManager::Overlay overlay;

			public:
				explicit Scope(const Skin&);
			};

		protected:
			~Skin();

		private:
			std::string name;
			std::vector<std::unique_ptr<ModPak>> paks;
			std::unordered_set<std::string> files; // normalized
			std::vector<std::string> assets;
			std::string scriptModule = "Client";

			Skin() = default;
			std::vector<IFileSystem*> GetFileSystems() const;
		};
	} // namespace client
} // namespace spades
