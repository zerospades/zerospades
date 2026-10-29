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

#include <string>
#include <vector>

namespace spades {
	namespace client {
		class IAudioDevice;
		class IRenderer;
	} // namespace client
	namespace gui {
		/** What applying the enabled mods took. */
		enum class ModApplyResult {
			Applied,      // without a restart: the next game loads the new set
			NeedsRestart, // the software's own files change; the enabled set waits for it
			Refused,      // nothing changed, and the mounted set is enabled again
		};

		/** A mod that failed to load, and why. It is disabled. */
		struct ModFailure {
			std::string mod;
			std::string reason;
		};

		/**
		 * The enabled mods' paks, mounted over the base paks when the game starts:
		 * the last enabled mod is on top and wins conflicts, and again when they
		 * are applied (`Apply`). A --try-mod run mounts none of them.
		 */
		class ModOverlay {
		public:
			/**
			 * Mounts the enabled set. A mod with a pak that can't be read, or a game
			 * asset that doesn't load (see client::GameResources::CheckFile), is
			 * disabled; `TakeStartupFailures` returns them.
			 */
			static void Mount();
			/**
			 * Starts the script engine on the mounted scripts. While they don't
			 * build, the mod the failing script comes from, or else the last enabled
			 * mod with scripts, is disabled.
			 */
			static void StartScripts();
			static std::vector<ModFailure> TakeStartupFailures();

			/**
			 * Mounts the enabled set in place of the mounted one, from the main menu,
			 * where no game runs. A mod with a pak, a game asset or a script that
			 * doesn't load is left out and goes to `failures`. When the change only
			 * touches game assets, it is `Applied`: the next game loads it. When it
			 * touches the software's own files, those `renderer` and `audio` loaded
			 * or ones that aren't game assets, it `NeedsRestart`, and `error` says
			 * which mod and why; files nothing reads, such as a readme, don't count.
			 * A script failure no mod can be blamed for refuses it, with `error`.
			 */
			static ModApplyResult Apply(client::IRenderer& renderer, client::IAudioDevice& audio,
			                            std::vector<ModFailure>& failures, std::string& error);

			/**
			 * The failures, for the user; "" if there are none. `disabled`: the mods
			 * were taken out of the enabled set.
			 */
			static std::string Describe(const std::vector<ModFailure>& failures,
			                            bool disabled = true);
		};
	} // namespace gui
} // namespace spades
