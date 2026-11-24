/*
 Copyright (c) 2013 yvt

 Portion of the code is based on Serverbrowser.cpp (Copyright (c) 2013 learn_more).

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

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <sstream>
#include <sys/stat.h>
#include <vector>

#include <curl/curl.h>
#include <json/json.h>

#include "Main.h"
#include "MainScreen.h"
#include "MainScreenHelper.h"
#include <Client/DemoRecorder.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/Settings.h>
#include <Core/Thread.h>
#include <Gui/ServerListMerge.h>
#include <Gui/Utils/LanDiscovery.h>
#include <Gui/Utils/PingTester.h>
#include <ZeroSpades.h>

DEFINE_SPADES_SETTING(cl_serverListUrl, "http://services.buildandshoot.com/serverlist.json");
DEFINE_SPADES_SETTING(cl_serverListUrlFallback, "https://checkpoint.aos.coffee/serverlist.json");
// The player's own server lists, separated by spaces, merged with the known ones
// below. Prefixed `zs` because the client shares SPConfig.cfg with OpenSpades.
DEFINE_SPADES_SETTING(cl_zsServerListUrls, "");
// Server lists not to download, separated by spaces, known ones included.
DEFINE_SPADES_SETTING(cl_zsServerListsExcluded, "");
// Set once a custom cl_serverListUrl / cl_serverListUrlFallback was copied into
// cl_zsServerListUrls, so a list the player then removes stays removed.
DEFINE_SPADES_SETTING(cl_zsServerListsMigrated, "0");

namespace spades {
	extern std::string g_pendingMapName;
	extern std::string g_pendingServerName;
	extern std::string g_pendingGameMode;

	namespace {
		struct CURLEasyDeleter {
			void operator()(CURL *ptr) const { curl_easy_cleanup(ptr); }
		};

		struct CURLMultiDeleter {
			void operator()(CURLM* ptr) const { curl_multi_cleanup(ptr); }
		};

		/**
		 * Every server list known to be live, always merged. A list is added here
		 * rather than to a setting's default: settings are saved with their value,
		 * so a new default would never reach players who already have a config.
		 * Earlier entries win ties when lists disagree (see `MergeServerLists`), so
		 * lists served over HTTPS come first: a tampered plain-HTTP response cannot
		 * then rename a server or change the game version used to join it.
		 */
		constexpr const char* kKnownServerLists[] = {
		  // A mirror of Build and Shoot's, over HTTPS.
		  "https://checkpoint.aos.coffee/serverlist.json",
		  // Build and Shoot's master; caps capacity at 32. Plain HTTP only.
		  "http://services.buildandshoot.com/serverlist.json",
		  // LSd's author's master, which LSd servers announce to; reports real
		  // capacity, up to 254 players.
		  "https://66.135.15.57/serverlist.json",
		};

		bool IsKnownServerList(const std::string& url) {
			for (const char* known : kKnownServerLists) {
				if (url == known)
					return true;
			}
			return false;
		}

		/**
		 * Copies, once, a custom list set in the settings inherited from
		 * OpenSpades (`cl_serverListUrl`, `cl_serverListUrlFallback`) into the
		 * player's own lists, where Preferences shows it and it can be removed.
		 * The inherited settings are left as they are, since OpenSpades reads
		 * them from the same file, and are not read again.
		 */
		void MigrateInheritedServerListSettings() {
			if (static_cast<bool>(cl_zsServerListsMigrated))
				return;

			std::string own = cl_zsServerListUrls.CString();
			std::vector<std::string> ownUrls;
			{
				std::istringstream in(own);
				std::string url;
				while (in >> url)
					ownUrls.push_back(url);
			}
			for (const char* setting :
			     {cl_serverListUrl.CString(), cl_serverListUrlFallback.CString()}) {
				std::istringstream in(setting);
				std::string url;
				while (in >> url) {
					if (IsKnownServerList(url) ||
					    std::find(ownUrls.begin(), ownUrls.end(), url) != ownUrls.end())
						continue;
					SPLog("Server lists: keeping %s, set in an older setting, as your own list",
					      url.c_str());
					ownUrls.push_back(url);
					own += own.empty() ? url : " " + url;
				}
			}
			cl_zsServerListUrls = own;
			cl_zsServerListsMigrated = 1;
		}

		/**
		 * Every server list to merge: the known ones, then the player's own, split
		 * on spaces, without empty entries, duplicates or excluded lists. Call on
		 * the UI thread: it reads and may write settings.
		 */
		std::vector<std::string> ServerListUrls() {
			MigrateInheritedServerListSettings();

			std::vector<std::string> excluded;
			{
				std::istringstream in(cl_zsServerListsExcluded.CString());
				std::string url;
				while (in >> url)
					excluded.push_back(url);
			}

			std::vector<std::string> urls;
			auto add = [&](const char* text) {
				std::istringstream in(text);
				std::string url;
				while (in >> url) {
					if (std::find(urls.begin(), urls.end(), url) == urls.end() &&
					    std::find(excluded.begin(), excluded.end(), url) == excluded.end())
						urls.push_back(url);
				}
			};
			for (const char* url : kKnownServerLists)
				add(url);
			add(cl_zsServerListUrls.CString());
			return urls;
		}
	} // namespace

	class ServerItem {
		// NetClient::Connect
		std::string mName, mIp, mMap, mGameMode;
		std::string mCountry, mVersion;
		int mPing, mPlayers, mMaxPlayers;

		ServerItem(const std::string &name, const std::string &ip, const std::string &map,
				   const std::string &gameMode, const std::string &country,
				   const std::string &version, int ping, int players, int maxPlayers);

	public:
		static ServerItem* Create(const Json::Value& val);

		inline const std::string &GetName() const { return mName; }
		inline const std::string &GetAddress() const { return mIp; }
		inline const std::string &GetMapName() const { return mMap; }
		inline const std::string &GetGameMode() const { return mGameMode; }
		inline const std::string &GetCountryCode() const { return mCountry; }
		inline const std::string &GetVersion() const { return mVersion; }
		inline int GetPing() const { return mPing; }
		inline int GetNumPlayers() const { return mPlayers; }
		inline int GetMaxNumPlayers() const { return mMaxPlayers; }
	};

	ServerItem::ServerItem(const std::string &name, const std::string &ip, const std::string &map,
						   const std::string &gameMode, const std::string &country,
						   const std::string &version, int ping, int players, int maxPlayers)
		: mName(name),
		  mIp(ip),
		  mMap(map),
		  mGameMode(gameMode),
		  mCountry(country),
		  mVersion(version),
		  mPing(ping),
		  mPlayers(players),
		  mMaxPlayers(maxPlayers) {}

	ServerItem* ServerItem::Create(const Json::Value& val) {
		// The entry comes from the network: a field of the wrong type reads as
		// missing instead of throwing, which would lose the whole list.
		if (!val.isObject())
			return NULL;
		using gui::ServerListIdentifier;
		using gui::ServerListInt;
		using gui::ServerListPlayers;
		using gui::ServerListString;
		return new ServerItem(TrimSpaces(ServerListString(val, "name")), ServerListIdentifier(val),
		                      ServerListString(val, "map"), ServerListString(val, "game_mode"),
		                      ServerListString(val, "country"),
		                      ServerListString(val, "game_version"), ServerListInt(val, "latency"),
		                      ServerListPlayers(val, "players_current"),
		                      ServerListPlayers(val, "players_max"));
	}

	namespace gui {
		constexpr auto FAVORITE_PATH = "/favorite_servers.json";

		class MainScreenHelper::ServerListQuery final : public Thread {
			Handle<MainScreenHelper> owner;

			// Copied on the UI thread when the query is made: the UI changes the
			// owner's favorites and the settings while the query runs.
			const std::unordered_set<std::string> favorites;
			std::vector<std::string> urls;

			// A list can take this long in all, so a dead or trickling one only
			// leaves itself out instead of holding up the others.
			static constexpr long kConnectTimeoutSeconds = 5;
			static constexpr long kTotalTimeoutSeconds = 10;

			// About ten times the largest list today (Build and Shoot's, 8246 bytes on
			// 2026-11-24); a larger response is cut off rather than held in memory.
			static constexpr std::size_t kMaxListBytes = 85 * 1024;

			/** One server list being downloaded. */
			struct Source {
				std::string url;
				std::string body;
				std::unique_ptr<CURL, CURLEasyDeleter> handle;
				CURLcode result = CURLE_OK;
				bool done = false;
				bool tooLarge = false;
			};

			/**
			 * Detaches every transfer from the group before the group is freed,
			 * as libcurl requires, including when an error cuts the fetch short.
			 */
			struct MultiGuard {
				std::unique_ptr<CURLM, CURLMultiDeleter> multi;
				std::vector<CURL*> added;
				~MultiGuard() {
					for (CURL* h : added)
						curl_multi_remove_handle(multi.get(), h);
				}
			};

			void ReturnResult(std::unique_ptr<MainScreenServerList> &&list) {
				owner->resultCell.store(std::move(list));
				owner = NULL; // release owner
			}

			static size_t Write(void* ptr, size_t size, size_t nmemb, Source* source) {
				size_t numBytes = size * nmemb;
				if (numBytes > kMaxListBytes - source->body.size()) {
					// Returning less than given aborts the transfer.
					source->tooLarge = true;
					return 0;
				}
				source->body.append(reinterpret_cast<char*>(ptr), numBytes);
				return numBytes;
			}

			/**
			 * Downloads every source at the same time, so a dead list costs no more
			 * time than a single request did.
			 */
			static void FetchAll(std::vector<std::unique_ptr<Source>>& sources) {
				MultiGuard guard;
				guard.multi.reset(curl_multi_init());
				CURLM* multi = guard.multi.get();
				if (!multi)
					SPRaise("Failed to create cURL object.");

				size_t (*writeCallback)(void*, size_t, size_t, Source*) = &Write;
				for (auto& source : sources) {
					source->handle.reset(curl_easy_init());
					if (!source->handle)
						SPRaise("Failed to create cURL object.");
					CURL* h = source->handle.get();
					curl_easy_setopt(h, CURLOPT_USERAGENT, PACKAGE_STRING);
					curl_easy_setopt(h, CURLOPT_URL, source->url.c_str());
					curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, writeCallback);
					curl_easy_setopt(h, CURLOPT_WRITEDATA, source.get());
					curl_easy_setopt(h, CURLOPT_PRIVATE, source.get());
					curl_easy_setopt(h, CURLOPT_FAILONERROR, 1L);
					// Redirects are not followed, as before lists were merged, and
					// only HTTP(S) is spoken: a list is untrusted, and one is plain
					// HTTP.
#if LIBCURL_VERSION_NUM >= 0x075500 // 7.85.0
					curl_easy_setopt(h, CURLOPT_PROTOCOLS_STR, "http,https");
#else
					curl_easy_setopt(h, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
					curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
					curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSeconds);
					curl_easy_setopt(h, CURLOPT_TIMEOUT, kTotalTimeoutSeconds);
					if (curl_multi_add_handle(multi, h) != CURLM_OK)
						SPRaise("Failed to start a server list request.");
					guard.added.push_back(h);
				}

				int running = 0;
				do {
					CURLMcode mc = curl_multi_perform(multi, &running);
					if (mc == CURLM_OK && running)
						mc = curl_multi_wait(multi, nullptr, 0, 1000, nullptr);
					if (mc != CURLM_OK)
						SPRaise("HTTP request error (%s).", curl_multi_strerror(mc));
				} while (running);

				int queued = 0;
				while (CURLMsg* msg = curl_multi_info_read(multi, &queued)) {
					if (msg->msg != CURLMSG_DONE)
						continue;
					Source* source = nullptr;
					curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &source);
					if (source) {
						source->result = msg->data.result;
						source->done = true;
					}
				}
			}

		public:
			ServerListQuery(MainScreenHelper* owner)
			    : owner{owner}, favorites{owner->favorites}, urls{ServerListUrls()} {}

			void Run() override {
				try {
					std::vector<std::unique_ptr<Source>> sources;
					for (std::string& url : urls) {
						sources.emplace_back(new Source());
						sources.back()->url = std::move(url);
					}

					FetchAll(sources);

					// Merge whatever answered; fail only if nothing did.
					std::vector<Json::Value> lists;
					std::string firstError;
					for (auto& source : sources) {
						std::string error;
						Json::Value root;
						Json::Reader reader;
						if (!source->done)
							error = "HTTP request error (transfer did not finish).";
						else if (source->tooLarge)
							error = "Server list response is too large.";
						else if (source->result != CURLE_OK)
							error = std::string("HTTP request error (") +
							        curl_easy_strerror(source->result) + ").";
						// The bundled jsoncpp parses by recursion, with no depth limit:
						// deep nesting would overflow this thread's stack.
						else if (!ServerListNestingWithin(source->body, kMaxServerListNesting))
							error = "Invalid server list response (nested too deeply).";
						// The bundled jsoncpp also calls null an array.
						else if (!reader.parse(source->body, root, false) ||
						         root.type() != Json::arrayValue)
							error = "Invalid server list response.";

						if (!error.empty()) {
							SPLog("[!] Server list %s failed: %s", source->url.c_str(),
							      error.c_str());
							if (firstError.empty())
								firstError = error;
							continue;
						}
						SPLog("Server list %s: %u servers", source->url.c_str(),
						      static_cast<unsigned>(root.size()));
						lists.push_back(std::move(root));
					}
					if (lists.empty())
						SPRaise("%s", firstError.c_str());

					Json::Value merged = MergeServerLists(lists);
					SPLog("Server lists merged: %u of %u answered, %u servers",
					      static_cast<unsigned>(lists.size()),
					      static_cast<unsigned>(sources.size()),
					      static_cast<unsigned>(merged.size()));

					auto resp = stmp::make_unique<MainScreenServerList>();
					for (const Json::Value& obj : merged) {
						std::unique_ptr<ServerItem> srv{ServerItem::Create(obj)};
						if (srv) {
							resp->list.emplace_back(
							  new MainScreenServerItem(srv.get(),
							                           favorites.count(srv->GetAddress()) >= 1),
							  false);
						}
					}
					ReturnResult(std::move(resp));
				} catch (std::exception &ex) {
					auto lst = stmp::make_unique<MainScreenServerList>();
					lst->message = ex.what();
					ReturnResult(std::move(lst));
				} catch (...) {
					auto lst = stmp::make_unique<MainScreenServerList>();
					lst->message = "Unknown error.";
					ReturnResult(std::move(lst));
				}
			}
		};

		MainScreenHelper::MainScreenHelper(MainScreen *scr) : mainScreen(scr), query(NULL) {
			SPADES_MARK_FUNCTION();
			LoadFavorites();
		}

		MainScreenHelper::~MainScreenHelper() {
			SPADES_MARK_FUNCTION();
			if (query) {
				query->MarkForAutoDeletion();
			}
		}

		void MainScreenHelper::Update() {
			if (pingTester)
				pingTester->Update();

			if (!lanDiscovery)
				return;
			lanDiscovery->Update();

			// Servers found on the LAN are added once the master list has been replaced
			if (!result || query)
				return;

			for (const LanDiscoveryEntry& entry : lanDiscovery->GetServers()) {
				if (lanMerged.count(entry.address))
					continue;

				// Reuse the master list parser so the item is built the same way
				Json::Value obj(Json::objectValue);
				obj["name"] = entry.info.name;
				obj["identifier"] = entry.address;
				obj["map"] = entry.info.mapName;
				obj["game_mode"] = entry.info.gameMode;
				obj["country"] = "";
				obj["game_version"] = entry.info.gameVersion;
				obj["latency"] = entry.ping;
				obj["players_current"] = entry.info.numPlayers;
				obj["players_max"] = entry.info.maxPlayers;

				std::unique_ptr<ServerItem> srv{ServerItem::Create(obj)};
				if (!srv)
					continue;

				result->list.emplace_back(
				  new MainScreenServerItem(srv.get(), favorites.count(entry.address) >= 1),
				  false);
				lanMerged.insert(entry.address);
				lanListChanged = true;

				// Let the ping tester refine the rough round-trip time
				if (pingTester)
					pingTester->AddTarget(entry.address);
			}
		}

		bool MainScreenHelper::PollLanServers() {
			bool changed = lanListChanged;
			lanListChanged = false;
			return changed;
		}

		void MainScreenHelper::MainScreenDestroyed() {
			SPADES_MARK_FUNCTION();
			SaveFavorites();
			mainScreen = NULL;
		}

		void MainScreenHelper::LoadFavorites() {
			SPADES_MARK_FUNCTION();
			Json::Reader reader;

			if (spades::FileManager::FileExists(FAVORITE_PATH)) {
				std::string favs = spades::FileManager::ReadAllBytes(FAVORITE_PATH);
				Json::Value favorite_root;
				if (reader.parse(favs, favorite_root, false)) {
					for (const auto &fav : favorite_root) {
						if (fav.isString())
							favorites.insert(fav.asString());
					}
				}
			}
		}

		void MainScreenHelper::SaveFavorites() {
			SPADES_MARK_FUNCTION();
			Json::StyledWriter writer;
			Json::Value v(Json::ValueType::arrayValue);

			auto fobj = spades::FileManager::OpenForWriting(FAVORITE_PATH);
			for (const auto &favorite : favorites) {
				v.append(Json::Value(favorite));
			}

			fobj->Write(writer.write(v));
		}

		void MainScreenHelper::SetServerFavorite(std::string ip, bool favorite) {
			SPADES_MARK_FUNCTION();
			if (favorite) {
				favorites.insert(ip);
			} else {
				favorites.erase(ip);
			}

			if (result && !result->list.empty()) {
				auto entry = std::find_if(result->list.begin(), result->list.end(),
										  [&](const Handle<MainScreenServerItem> &entry) {
											  return entry->GetAddress() == ip;
										  });
				if (entry != result->list.end()) {
					(*entry)->SetFavorite(favorite);
				}
			}
		}

		bool MainScreenHelper::PollServerListState() {
			SPADES_MARK_FUNCTION();

			// Do we have a new result?
			auto newResult = resultCell.take();
			if (newResult) {
				result = std::move(newResult);
				query->MarkForAutoDeletion();
				query = NULL;

				// Start the ping measurement
				pingTester.reset(new PingTester());
				for (const auto& item : result->list)
					pingTester->AddTarget(item->GetAddress());
				
				// The new list replaces the old one, so LAN servers must be added again
				lanMerged.clear();
				lanListChanged = false;

				return true;
			}

			return false;
		}

		void MainScreenHelper::StartQuery() {
			if (query) {
				// There already is an ongoing query
				return;
			}

			if (pingTester)
				pingTester.reset();
			
			// Look for servers on the local network while the master list loads
			lanDiscovery.reset(new LanDiscovery());
			lanDiscovery->Scan();
			lanMerged.clear();

			query = new ServerListQuery(this);
			query->Start();
		}

#include "Credits.inc" // C++11 raw string literal makes some tools (ex. xgettext, Xcode) misbehave

		std::string MainScreenHelper::GetCredits() {
			std::string html = credits;
			html = Replace(html, "${PACKAGE_STRING}", PACKAGE_STRING);
			return html;
		}

		std::vector<Handle<MainScreenServerItem>>
		MainScreenHelper::GetServerList(const std::string& sortKey, bool descending) {
			if (result == NULL) {
				return {};
			}

			using Item = const Handle<MainScreenServerItem> &;
			std::vector<Handle<MainScreenServerItem>> &lst = result->list;
			if (lst.empty())
				return {};

			auto compareFavorite = [&](Item x, Item y) -> stmp::optional<bool> {
				if (x->IsFavorite() && !y->IsFavorite()) {
					return true;
				} else if (!x->IsFavorite() && y->IsFavorite()) {
					return false;
				} else {
					return {};
				}
			};

			auto compareInts = [&](int x, int y) -> bool {
				if (descending) {
					return y < x;
				} else {
					return x < y;
				}
			};

			auto compareStrings = [&](const std::string &x0, const std::string &y0) -> bool {
				const auto &x = descending ? y0 : x0;
				const auto &y = descending ? x0 : y0;
				std::string::size_type t = 0;
				for (t = 0; t < x.length() && t < y.length(); ++t) {
					int xx = std::tolower(x[t]);
					int yy = std::tolower(y[t]);
					if (xx != yy) {
						return xx < yy;
					}
				}
				if (x.length() == y.length()) {
					return false;
				}
				return x.length() < y.length();
			};

			if (!sortKey.empty()) {
				if (sortKey == "Ping") {
					// Overwrite the master server's ping values
					// with those relative to the user for sorting
					for (auto& item : lst) {
						item->ping = GetServerPing(item->GetAddress());
						if (item->ping == -1)
							item->ping = std::numeric_limits<int>::max();
					}
					std::stable_sort(lst.begin(), lst.end(), [&](Item x, Item y) {
						return compareFavorite(x, y).value_or(
						  compareInts(x->GetPing(), y->GetPing()));
					});
				} else if (sortKey == "NumPlayers") {
					std::stable_sort(lst.begin(), lst.end(), [&](Item x, Item y) {
						return compareFavorite(x, y).value_or(
						  compareInts(x->GetNumPlayers(), y->GetNumPlayers()));
					});
				} else if (sortKey == "Name") {
					std::stable_sort(lst.begin(), lst.end(), [&](Item x, Item y) {
						return compareFavorite(x, y).value_or(
						  compareStrings(x->GetName(), y->GetName()));
					});
				} else if (sortKey == "MapName") {
					std::stable_sort(lst.begin(), lst.end(), [&](Item x, Item y) {
						return compareFavorite(x, y).value_or(
						  compareStrings(x->GetMapName(), y->GetMapName()));
					});
				} else if (sortKey == "GameMode") {
					std::stable_sort(lst.begin(), lst.end(), [&](Item x, Item y) {
						return compareFavorite(x, y).value_or(
						  compareStrings(x->GetGameMode(), y->GetGameMode()));
					});
				} else if (sortKey == "Protocol") {
					std::stable_sort(lst.begin(), lst.end(), [&](Item x, Item y) {
						return compareFavorite(x, y).value_or(
						  compareStrings(x->GetProtocol(), y->GetProtocol()));
					});
				} else if (sortKey == "Country") {
					std::stable_sort(lst.begin(), lst.end(), [&](Item x, Item y) {
						return compareFavorite(x, y).value_or(
						  compareStrings(x->GetCountry(), y->GetCountry()));
					});
				} else {
					SPRaise("Invalid sort key: %s", sortKey.c_str());
				}
			}

			return lst;
		}

		int MainScreenHelper::GetServerPing(std::string address) {
			if (!pingTester)
				return -1;

			auto result = pingTester->GetTargetResult(address);
			if (!result)
				return -1;

			return result.get().ping.value_or(-1);
		}

		std::string MainScreenHelper::ConnectServer(std::string hostname, int protocolVersion,
													 std::string mapName) {
			if (mainScreen == NULL) {
				return "mainScreen == NULL";
			}
			g_pendingMapName = mapName;
			g_pendingServerName = hostname; // fallback: use address as server name
			g_pendingGameMode.clear();      // never carry over the previous server's mode
			if (result) {
				for (const auto& item : result->list) {
					if (item->GetAddress() == hostname) {
						g_pendingServerName = item->GetName(); // prefer human-readable name
						g_pendingGameMode = item->GetGameMode();
						if (g_pendingMapName.empty())
							g_pendingMapName = item->GetMapName(); // resolve from cached list
						break;
					}
				}
			}
			return mainScreen->Connect(ServerAddress(
			  hostname, protocolVersion == 3 ? ProtocolVersion::v075 : ProtocolVersion::v076));
		}

		std::string MainScreenHelper::GetServerListQueryMessage() {
			if (result == NULL)
				return "";
			return result->message;
		}

		std::vector<std::string> MainScreenHelper::GetDemoList() {
			return client::DemoRecorder::ListRecordings();
		}

		int64_t MainScreenHelper::GetDemoFileSize(const std::string& filename) {
			try {
				auto stream = FileManager::OpenForReading(filename.c_str());
				return static_cast<int64_t>(stream->GetLength());
			} catch (...) {
				return -1;
			}
		}

		std::string MainScreenHelper::PlayDemo(const std::string& filename) {
			if (mainScreen == NULL)
				return "mainScreen == NULL";
			return mainScreen->PlayDemo(filename);
		}

		std::string MainScreenHelper::OpenEditor(const std::string& path, bool isNew,
		                                         SoftwareCursor* cursor) {
			if (mainScreen == NULL)
				return "mainScreen == NULL";
			return mainScreen->OpenEditor(path, isNew, cursor);
		}

		bool MainScreenHelper::DeleteDemo(const std::string& filename) {
			return FileManager::RemoveFile(filename.c_str());
		}

		bool MainScreenHelper::RenameDemo(const std::string& oldName, const std::string& newName) {
			return FileManager::RenameFile(oldName.c_str(), newName.c_str());
		}

		bool MainScreenHelper::ShouldOpenModsTab() { return spades::g_openModsTab; }
		bool MainScreenHelper::IsTryingMod() { return spades::g_tryMod; }
		void MainScreenHelper::RelaunchForMods() { spades::RelaunchForMods(); }

		std::string MainScreenHelper::GetPendingErrorMessage() {
			std::string s = errorMessage;
			errorMessage.clear();
			return s;
		}

		MainScreenServerList::~MainScreenServerList() {}

		MainScreenServerItem::MainScreenServerItem(ServerItem *item, bool favorite) {
			SPADES_MARK_FUNCTION();
			name = item->GetName();
			address = item->GetAddress();
			mapName = item->GetMapName();
			gameMode = item->GetGameMode();
			country = item->GetCountryCode();
			protocol = item->GetVersion();
			ping = item->GetPing();
			numPlayers = item->GetNumPlayers();
			maxPlayers = item->GetMaxNumPlayers();
			this->favorite = favorite;
		}

		MainScreenServerItem::~MainScreenServerItem() { SPADES_MARK_FUNCTION(); }
	} // namespace gui
} // namespace spades
