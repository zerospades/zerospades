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

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "OfficialMods.h"
#include <Core/RefCountedObject.h>
#include <Core/TMPUtils.h>

namespace spades {
	namespace gui {

		class ModsScreenHelper : public RefCountedObject {
		public:
			ModsScreenHelper();

			// Async download of the latest official mods release this build can
			// use (see OfficialMods.h). Its paks land loose in Mods/, replacing
			// any copies of the same name. One runs at a time in the whole
			// process: a helper made while one runs (the main screen rebuilt)
			// follows it, and starting another only follows it too.
			void StartRefresh();
			// Whether a download is running; false once PollRefreshState has
			// returned true for it.
			bool IsRefreshing();
			bool PollRefreshState();
			// Empty after a successful download, otherwise what went wrong.
			std::string GetRefreshMessage();

			// The official mods release last installed, e.g. "v1.4", or empty if
			// they were never downloaded.
			std::string GetInstalledOfficialVersion();
			// Whether a pak the last release installed is enabled, so that
			// installing it changed what the next launch mounts.
			bool IsInstalledOfficialPakEnabled();

			// What Mods/index.txt says of the mod: verified only for a loose pak
			// the official mods installed, untouched since.
			OfficialPakState GetModOfficialState(std::string modName);

			// CATEGORY-NAME-AUTHOR parsing for the list UI. Category and author
			// are empty when the name doesn't follow the convention, in which
			// case GetModDisplayName returns the full filename unchanged.
			std::string GetModCategory(std::string modName);
			std::string GetModDisplayName(std::string modName);
			std::string GetModAuthor(std::string modName);

			// Live progress while a refresh is running: the fraction of the current
			// step done (negative while it is unknown) and a line describing it.
			float GetRefreshFraction();
			std::string GetRefreshStatus();

			std::vector<std::string> GetModNames();
			int GetModPakCount(std::string modName);
			int64_t GetModTotalSize(std::string modName);
			std::vector<std::string> GetModContents(std::string modName);

			// The enabled set: ordered mod names, top applied first, bottom wins
			// conflicts. Toggling persists immediately; the change takes effect
			// when applied (see ModOverlay).
			std::vector<std::string> GetEnabledMods();
			void EnableMod(std::string modName);
			void DisableMod(std::string modName);

			// Disable every mod (clears the enabled set). Always succeeds.
			void ClearEnabledMods();

			// Replace the enabled set, e.g. to disable a mod that failed to load.
			void SetEnabledMods(const std::vector<std::string>& modNames);

			// Startup: the paks to mount as an overlay, as (mod name, path relative
			// to the resource root, e.g. "Mods/<folder>/<x>.pak"), see
			// ModPak::FindPaths. In the given order, so the caller can prepend each
			// in turn — the last one ends up on top and wins conflicts. Mods
			// missing from disk are skipped.
			static std::vector<std::pair<std::string, std::string>>
			GetModPaks(const std::vector<std::string>& modNames);

		protected:
			~ModsScreenHelper();

		private:
			class RefreshQuery;
			struct RefreshJob;

			struct ModEntry {
				std::string name;
				// Relative to Mods/: "X.pak" for a loose pak, "<name>/X.pak" for
				// each pak of a folder mod.
				std::vector<std::string> paks;
				std::int64_t totalSize = 0;
				OfficialPakState official = OfficialPakState::Unverified;
				bool officialChecked = false; // `official` is known (GetModOfficialState)
			};

			// The download this helper follows, if any.
			std::shared_ptr<RefreshJob> job;
			// The download running in the process, if any.
			static std::mutex runningJobMutex;
			static std::weak_ptr<RefreshJob> runningJob;
			std::string lastMessage;
			std::vector<ModEntry> mods;
			bool modsCached;
			// Mods/index.txt as GetModOfficialState read it since the last scan.
			std::optional<OfficialModsIndex> officialIndex;
			bool officialIndexRead = false;

			void RebuildModsCache();
			const ModEntry* FindMod(const std::string& name) const;
		};

	} // namespace gui
} // namespace spades
