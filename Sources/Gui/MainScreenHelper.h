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
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <vector>

#include <Core/RefCountedObject.h>
#include <Core/TMPUtils.h>

namespace spades {
	class ServerItem;
	class PingTester;
	class LanDiscovery;
	namespace gui {
		class MainScreen;
		class SoftwareCursor;

		/** What one server list says of a server. */
		struct MainScreenServerListReport {
			std::string url; // the list
			int numPlayers;
			int maxPlayers;
		};

		class MainScreenServerItem : public RefCountedObject {
			friend class MainScreenHelper;
			std::string name;
			std::string address;
			std::string mapName;
			std::string gameMode;
			std::string country;
			std::string protocol;
			int ping;
			int numPlayers;
			int maxPlayers;
			bool favorite;
			/** The server lists that report this server; empty for a LAN server. */
			std::vector<MainScreenServerListReport> sources;

		protected:
			~MainScreenServerItem();

		public:
			MainScreenServerItem(ServerItem *, bool favorite);
			/** `base` as `report`'s list alone describes it: with its player counts. */
			MainScreenServerItem(const MainScreenServerItem& base,
			                     const MainScreenServerListReport& report);

			std::string GetName() const { return name; }
			std::string GetAddress() const { return address; }
			std::string GetMapName() const { return mapName; }
			std::string GetGameMode() const { return gameMode; }
			std::string GetCountry() const { return country; }
			std::string GetProtocol() const { return protocol; }
			/** Ping seen from *the master server*. */
			int GetPing() const { return ping; }
			int GetNumPlayers() const { return numPlayers; }
			int GetMaxPlayers() const { return maxPlayers; }
			bool IsFavorite() const { return favorite; }
			void SetFavorite(bool favorite) { this->favorite = favorite; }
			const std::vector<MainScreenServerListReport>& GetSources() const { return sources; }
			void SetSources(std::vector<MainScreenServerListReport> s) { sources = std::move(s); }
			/** What the list at `url` says of this server; null if it does not list it. */
			const MainScreenServerListReport* GetReport(const std::string& url) const {
				for (const MainScreenServerListReport& r : sources) {
					if (r.url == url)
						return &r;
				}
				return nullptr;
			}
		};

		/** A server list that answered the last query. */
		struct MainScreenServerListSource {
			std::string url;
			std::string name; // short, for the filter
		};

		struct MainScreenServerList {
			std::vector<Handle<MainScreenServerItem>> list;
			/** The server lists merged into `list`, in merge order. */
			std::vector<MainScreenServerListSource> sources;
			std::string message;

			~MainScreenServerList();
		};

		class MainScreenHelper : public RefCountedObject {
			friend class MainScreen;
			class ServerListQuery;

			std::unique_ptr<PingTester> pingTester;
			MainScreen *mainScreen;
			std::unique_ptr<MainScreenServerList> result;
			stmp::atomic_unique_ptr<MainScreenServerList> resultCell;
			ServerListQuery *query;
			std::string errorMessage;
			std::unordered_set<std::string> favorites;
			
			std::unique_ptr<LanDiscovery> lanDiscovery;
			/** Addresses of LAN servers already added to the current `result`. */
			std::unordered_set<std::string> lanMerged;
			/** Set when `Update()` added a LAN server to `result`. */
			bool lanListChanged = false;

			void Update();

		protected:
			~MainScreenHelper();

		public:
			MainScreenHelper(MainScreen *scr);
			void MainScreenDestroyed();

			void LoadFavorites();
			void SaveFavorites();
			void SetServerFavorite(std::string ip, bool favorite);

			bool PollServerListState();
			/** Returns `true` once after a LAN server was added to the list. */
			bool PollLanServers();
			void StartQuery();
			/**
			 * The servers, sorted. With `listUrl`, only those that list reports, with
			 * that list's own player counts instead of the highest of every list.
			 */
			std::vector<Handle<MainScreenServerItem>>
			GetServerList(const std::string& sortKey, bool descending,
			              const std::string& listUrl = "");
			std::string GetServerListQueryMessage();
			/**
			 * The server lists merged into the current list. Empty until the first
			 * list arrives; during a refresh, the lists of the previous one.
			 */
			std::vector<MainScreenServerListSource> GetServerListSources();
			int GetServerPing(std::string address);
			std::string ConnectServer(std::string hostname, int protocolVersion, std::string mapName = "");
			std::string GetPendingErrorMessage();

			std::string GetCredits();

			std::vector<std::string> GetDemoList();
			std::string PlayDemo(const std::string& filename);
			std::string OpenEditor(const std::string& path, bool isNew, SoftwareCursor* cursor = nullptr);
			int64_t GetDemoFileSize(const std::string& filename);
			bool DeleteDemo(const std::string& filename);
			bool RenameDemo(const std::string& oldName, const std::string& newName);

			bool ShouldOpenModsTab();
			bool IsTryingMod();
			void RelaunchForMods();
		};
	}
}
