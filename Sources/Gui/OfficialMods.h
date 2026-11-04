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

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace spades {
	namespace gui {
		/**
		 * The compatibility version of the official mods this build can use.
		 *
		 * The official mods are published as releases tagged `vC.N`: C is their
		 * compatibility version, which changes only when a release needs a newer
		 * client, and N counts the releases of a compatibility version. This build
		 * installs the latest release whose C is this number, and asks for a game
		 * update when only a newer C is available. Raise it together with the
		 * client changes a new compatibility version of the mods needs.
		 */
		constexpr int kOfficialModsCompatibility = 1;

		/** Where the official mods are published, for showing to the user. */
		constexpr const char* kOfficialModsReleasesPageUrl =
		  "https://github.com/zerospades/zerospades-paks/releases";

		/** An official mods release number, as in the tag `v1.4`. */
		struct OfficialModsVersion {
			int compatibility = 0;
			int number = 0;

			/** Parses a tag of exactly the form `vC.N`; anything else is not a release. */
			static std::optional<OfficialModsVersion> Parse(const std::string& tag);

			/** The tag, such as `v1.4`. */
			std::string ToString() const;

			bool operator<(const OfficialModsVersion& o) const {
				return std::tie(compatibility, number) < std::tie(o.compatibility, o.number);
			}
			bool operator==(const OfficialModsVersion& o) const {
				return compatibility == o.compatibility && number == o.number;
			}
		};

		/** A published release this build can install. */
		struct OfficialModsRelease {
			OfficialModsVersion version;
			/** The release's zip of paks; always on the official releases. */
			std::string zipUrl;
			/** The zip's size in bytes, as GitHub reports it; 0 if unknown. */
			std::int64_t zipSize = 0;
		};

		/** What the official mods releases offer to this build. */
		struct OfficialModsListing {
			/** The latest release this build can install, if any. */
			std::optional<OfficialModsRelease> latestCompatible;
			/** The newest release that needs a newer build, if any. */
			std::optional<OfficialModsVersion> newestIncompatible;
		};

		/**
		 * Asks GitHub for the official mods releases. Blocks on the network, so call
		 * it from a worker thread. Returns an empty string on success, otherwise a
		 * description of the failure.
		 */
		std::string FetchOfficialModsListing(OfficialModsListing& out);

		/**
		 * Whether `name`, a file of a release's zip, is a pak to install into the
		 * Mods folder: a plain `X.pak` at the zip's root. Anything else
		 * (other files, folders, absolute paths, `..`, hidden names) is skipped.
		 */
		bool IsOfficialModsPakName(const std::string& name);

		/**
		 * `Mods/index.txt`: what the last official mods install put in the Mods
		 * folder. It names the release and each pak with its size and CRC-32 (the
		 * zip's own), so a pak can be told official and untouched (verified) from
		 * one the player added or changed. Only an install writes it; a pak it
		 * does not list is unverified.
		 */
		struct OfficialModsIndex {
			struct Pak {
				std::string name; ///< as in Mods/, e.g. `SMG-Sten-Fran6nd.pak`
				std::uint32_t crc32 = 0;
				std::int64_t size = 0;
			};
			OfficialModsVersion version;
			std::vector<Pak> paks;

			const Pak* Find(const std::string& name) const;
		};

		/** The index, if there is one and it reads as one (an edited or damaged one
		 *  does not, and is logged). */
		std::optional<OfficialModsIndex> ReadOfficialModsIndex();
		/** The installed release alone, from the index. */
		std::optional<OfficialModsVersion> ReadInstalledOfficialMods();

		/** Writes the index of the release just installed. Returns false if it could
		 *  not. */
		bool WriteOfficialModsIndex(const OfficialModsIndex& index);

		/** What the index says of a pak of the Mods folder. */
		enum class OfficialPakState {
			/** Not one the index lists: the player's own. */
			Unverified,
			/** Listed, and its size and CRC-32 match: as the release shipped it. */
			Verified,
			/** Listed, but its content changed since it was installed. */
			Modified,
		};

		/**
		 * Checks the pak `name` of the Mods folder, at `pathAbs`, against `index`.
		 * Remembers the answer for a file of the same size and modification time,
		 * so listing the mods again does not read every pak again.
		 */
		OfficialPakState VerifyOfficialPak(const OfficialModsIndex& index, const std::string& name,
		                                   const std::string& pathAbs);

		/**
		 * Asks, once per launch and in the background, whether a newer official mods
		 * release is available to a player who has installed them. It only reports;
		 * installing is up to the Mods tab.
		 *
		 * The check is process-wide, so the main screen keeps its result when it is
		 * rebuilt (resize, return from a game) instead of asking again.
		 */
		class OfficialModsChecker {
		public:
			enum class State {
				/** The check has not been started. */
				Idle,
				/** The official mods were never installed, so there is nothing to check. */
				NotInstalled,
				Checking,
				UpToDate,
				/** A newer release this build can install is available. */
				UpdateAvailable,
				/** Only a release that needs a newer ZeroSpades is newer than the installed one. */
				GameUpdateNeeded,
				/** The request or its response failed; nothing is shown. */
				Failed,
			};

			struct Result {
				State state = State::Idle;
				/** The release to announce, for `UpdateAvailable` and `GameUpdateNeeded`. */
				OfficialModsVersion latestVersion;
				/** The installed release, once the official mods are installed. */
				OfficialModsVersion installedVersion;
			};

			static OfficialModsChecker& Get();

			/** Starts the check unless it already ran in this process. Call on the main thread. */
			void Start();

			Result GetResult();

			/** Tells the check that `version` was just installed, so it stops announcing it. */
			void Installed(const OfficialModsVersion& version);

		private:
			class Query;
			friend class Query;

			std::mutex mutex;
			Result result;
			bool started = false;
			OfficialModsVersion installedVersion;

			OfficialModsChecker() = default;
			OfficialModsChecker(const OfficialModsChecker&) = delete;
			OfficialModsChecker& operator=(const OfficialModsChecker&) = delete;

			void Finish(const OfficialModsListing* listing);
		};
	} // namespace gui
} // namespace spades
