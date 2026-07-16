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
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include "ModsScreenHelper.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <thread>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#endif

#include <curl/curl.h>
#include <json/json.h>

#include "Main.h"
#include <Core/Debug.h>
#include <Core/DynamicMemoryStream.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Core/Thread.h>
#include <Core/ZipFileSystem.h>
#include <ZeroSpades.h>

// Escape hatch only: when empty (the default), the canonical repo below is
// used. Older builds persisted the repo URL directly into this setting, which
// then permanently shadowed any change to the code default — see
// ResolveModsIndexUrl for the migration that heals those stale values.
DEFINE_SPADES_SETTING(cl_modsIndexUrl, "");

namespace spades {
	namespace gui {

		namespace {
			constexpr const char* kModsDir = "Mods";
			// The one true source of the official pak listing. Kept as a
			// compile-time constant, never through a persisted setting, so a
			// new build's URL always reaches every user.
			constexpr const char* kCanonicalIndexUrl =
			  "https://api.github.com/repos/zerospades/zerospades-paks/contents/";

			// True when a persisted value is a stale auto-saved default: it still
			// targets our own repo but differs from the canonical form (trailing
			// slash, http/https, path drift). Such a value is not a deliberate
			// override and should be treated as canonical.
			bool IsStaleOwnRepoUrl(const std::string& url) {
				return !url.empty() && url != kCanonicalIndexUrl &&
					   url.find("zerospades-paks") != std::string::npos;
			}

			// The index URL to actually fetch. Pure — no side effects, so it is
			// safe to call from anywhere (including for display):
			//   - empty              -> canonical (the normal case)
			//   - stale pointer      -> canonical
			//   - genuinely custom   -> respected as-is
			std::string ResolveModsIndexUrl() {
				std::string url = cl_modsIndexUrl.CString();
				if (url.empty() || IsStaleOwnRepoUrl(url))
					return kCanonicalIndexUrl;
				return url;
			}

			// Rewrite a stale persisted value back to empty so the config stops
			// shadowing the canonical URL. Call on the main thread only (writes
			// the settings store). This is what lets users who upgraded from an
			// older build recover without hand-editing SPConfig.cfg.
			void HealPersistedModsIndexUrl() {
				if (IsStaleOwnRepoUrl(cl_modsIndexUrl.CString()))
					cl_modsIndexUrl = std::string();
			}
			// The enabled set lives here, at the resource root: one mod name per
			// line, in apply order (bottom wins conflicts). No build stamp — mods
			// are mounted as a startup overlay, never merged onto disk, so the
			// base paks can't go stale.
			constexpr const char* kEnabledFile = "EnabledMods.txt";

			struct CURLDeleter {
				void operator()(CURL* p) const {
					if (p)
						curl_easy_cleanup(p);
				}
			};

			bool EndsWithPak(const std::string& s) {
				if (s.size() < 4)
					return false;
				std::string ext = s.substr(s.size() - 4);
				return EqualsIgnoringCase(ext, ".pak")
					|| EqualsIgnoringCase(ext, ".zip");
			}

			// A mod name parsed against the official CATEGORY-NAME-AUTHOR.pak
			// convention. `structured` is true whenever the base name splits on
			// '-' into exactly three fields (two separators) — any single field
			// may be blank, e.g. SMG--Author (no name) or SMG-Name- (no author),
			// so those still show their tag and whichever fields are present.
			// Anything else (no separators, or too many) is left whole in `name`
			// so the UI falls back to the raw filename.
			struct ParsedModName {
				bool structured = false;
				std::string category; // e.g. SEMI, SMG, SHOTGUN, SPADE, FONT, SFX, VFX
				std::string name;     // display name, or the full raw name if unstructured
				std::string author;
			};

			ParsedModName ParseModName(const std::string& modName) {
				ParsedModName p;
				// Drop a trailing .pak/.zip (folder mods have no extension).
				std::string base = modName;
				if (EndsWithPak(base))
					base = base.substr(0, base.size() - 4);

				// Split on '-'.
				std::vector<std::string> parts;
				std::size_t start = 0;
				while (true) {
					std::size_t dash = base.find('-', start);
					if (dash == std::string::npos) {
						parts.push_back(base.substr(start));
						break;
					}
					parts.push_back(base.substr(start, dash - start));
					start = dash + 1;
				}

				// Exactly CATEGORY-NAME-AUTHOR; individual fields may be empty.
				if (parts.size() == 3) {
					p.structured = true;
					p.category = parts[0];
					p.name = parts[1];
					p.author = parts[2];
				} else {
					// Fallback: keep the whole filename as the name.
					p.name = modName;
				}
				return p;
			}

			void MakeDir(const std::string& path) {
#ifdef _WIN32
				_mkdir(path.c_str());
#else
				::mkdir(path.c_str(), 0775);
#endif
			}

			void EnsureDir(const std::string& path) {
				// Iteratively create each prefix.
				for (std::size_t i = 1; i < path.size(); ++i) {
					if (path[i] == '/' || path[i] == '\\')
						MakeDir(path.substr(0, i));
				}
				MakeDir(path);
			}

			std::string EnabledFileAbs() {
				return std::string(spades::g_userResourceDirectory) + "/" + kEnabledFile;
			}

			// The ordered enabled mod names, one per line. Empty if the file is
			// absent. Top = applied first, bottom wins conflicts.
			std::vector<std::string> ReadEnabled() {
				std::vector<std::string> out;
				std::FILE* f = std::fopen(EnabledFileAbs().c_str(), "rb");
				if (!f)
					return out;
				std::string all;
				char buf[4096];
				std::size_t rd;
				while ((rd = std::fread(buf, 1, sizeof(buf), f)) > 0)
					all.append(buf, rd);
				std::fclose(f);
				std::size_t start = 0;
				while (start <= all.size()) {
					std::size_t nl = all.find('\n', start);
					std::string line = all.substr(
					  start, (nl == std::string::npos ? all.size() : nl) - start);
					// Strip only the CR of a CRLF — a trailing space can be part
					// of a real mod name, so leave it intact.
					while (!line.empty() && line.back() == '\r')
						line.pop_back();
					if (!line.empty())
						out.push_back(line);
					if (nl == std::string::npos)
						break;
					start = nl + 1;
				}
				return out;
			}

			// Overwrite the enabled file with the given ordered names. Best-effort.
			void WriteEnabled(const std::vector<std::string>& lines) {
				if (std::FILE* f = std::fopen(EnabledFileAbs().c_str(), "wb")) {
					for (const std::string& line : lines) {
						std::fputs(line.c_str(), f);
						std::fputc('\n', f);
					}
					std::fclose(f);
				}
			}

			std::int64_t FileSizeAbs(const std::string& path) {
				struct stat st;
				if (::stat(path.c_str(), &st) == 0)
					return static_cast<std::int64_t>(st.st_size);
				return -1;
			}

			std::vector<std::string> ListDir(const std::string& path) {
				std::vector<std::string> out;
#ifdef _WIN32
				WIN32_FIND_DATAA fd;
				HANDLE h = FindFirstFileA((path + "\\*").c_str(), &fd);
				if (h == INVALID_HANDLE_VALUE)
					return out;
				do {
					if (std::strcmp(fd.cFileName, ".") == 0 ||
						std::strcmp(fd.cFileName, "..") == 0)
						continue;
					out.emplace_back(fd.cFileName);
				} while (FindNextFileA(h, &fd));
				FindClose(h);
#else
				DIR* d = ::opendir(path.c_str());
				if (!d)
					return out;
				while (auto* e = ::readdir(d)) {
					if (e->d_name[0] == '.')
						continue;
					out.emplace_back(e->d_name);
				}
				::closedir(d);
#endif
				return out;
			}

			bool IsDirAbs(const std::string& path) {
				struct stat st;
				if (::stat(path.c_str(), &st) != 0)
					return false;
				return (st.st_mode & S_IFDIR) != 0;
			}

			std::string UserRoot() { return spades::g_userResourceDirectory; }
			std::string ModsRootAbs() { return UserRoot() + "/" + kModsDir; }

			std::size_t CurlWriteToString(void* ptr, std::size_t size, std::size_t nmemb,
										  void* userdata) {
				std::string* s = static_cast<std::string*>(userdata);
				std::size_t n = size * nmemb;
				s->append(static_cast<char*>(ptr), n);
				return n;
			}

			std::size_t CurlWriteToFile(void* ptr, std::size_t size, std::size_t nmemb,
										void* userdata) {
				std::FILE* f = static_cast<std::FILE*>(userdata);
				// fwrite returns the number of whole items written; curl wants the
				// number of bytes handled, so scale back up by the item size.
				std::size_t itemsWritten = std::fwrite(ptr, size, nmemb, f);
				return itemsWritten * size;
			}

			// Number of attempts (initial + retries) for a single HTTP request.
			constexpr long kMaxHttpAttempts = 3;

			// Transient curl failures worth a retry — a connection dropped, a DNS
			// hiccup, a timeout. A hard failure (bad URL, SSL cert) is not retried.
			bool IsTransientCurl(CURLcode rc) {
				switch (rc) {
					case CURLE_COULDNT_CONNECT:
					case CURLE_COULDNT_RESOLVE_HOST:
					case CURLE_OPERATION_TIMEDOUT:
					case CURLE_GOT_NOTHING:
					case CURLE_RECV_ERROR:
					case CURLE_SEND_ERROR:
					case CURLE_PARTIAL_FILE: return true;
					default: return false;
				}
			}

			void BackoffSleep(long attempt) {
				std::this_thread::sleep_for(std::chrono::seconds(attempt));
			}

			// Map an HTTP response code to an error string, or "" if it is a
			// success. GitHub answers a throttled client with 403 (or 429), so
			// call those out specifically — the user just needs to wait, not
			// debug a URL.
			std::string HttpCodeError(long code) {
				if (code < 400)
					return std::string{};
				if (code == 403 || code == 429)
					return Format("GitHub rate limit reached (HTTP {0}). Try again later.", code);
				return Format("HTTP {0}", code);
			}

			// HTTP GET into a string. Returns "" on success, otherwise an error
			// description. User-Agent and Accept are required/expected by the
			// GitHub API. Transient failures and 5xx are retried with backoff.
			std::string HttpGetText(const std::string& url, std::string& out) {
				std::string lastErr;
				for (long attempt = 1; attempt <= kMaxHttpAttempts; ++attempt) {
					out.clear();
					std::unique_ptr<CURL, CURLDeleter> h{curl_easy_init()};
					if (!h)
						return "curl_easy_init failed";
					struct curl_slist* headers = nullptr;
					headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
					curl_easy_setopt(h.get(), CURLOPT_HTTPHEADER, headers);
					curl_easy_setopt(h.get(), CURLOPT_URL, url.c_str());
					curl_easy_setopt(h.get(), CURLOPT_USERAGENT, PACKAGE_STRING);
					curl_easy_setopt(h.get(), CURLOPT_FOLLOWLOCATION, 1L);
					curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, CurlWriteToString);
					curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, &out);
					curl_easy_setopt(h.get(), CURLOPT_CONNECTTIMEOUT, 30L);
					curl_easy_setopt(h.get(), CURLOPT_LOW_SPEED_TIME, 30L);
					curl_easy_setopt(h.get(), CURLOPT_LOW_SPEED_LIMIT, 15L);
					CURLcode rc = curl_easy_perform(h.get());
					long code = 0;
					curl_easy_getinfo(h.get(), CURLINFO_RESPONSE_CODE, &code);
					curl_slist_free_all(headers);
					if (rc != CURLE_OK) {
						lastErr = curl_easy_strerror(rc);
						if (IsTransientCurl(rc) && attempt < kMaxHttpAttempts) {
							BackoffSleep(attempt);
							continue;
						}
						return lastErr;
					}
					lastErr = HttpCodeError(code);
					if (lastErr.empty())
						return std::string{};
					// Retry only on server-side 5xx; 4xx (incl. rate limit) won't
					// clear within a few seconds.
					if (code >= 500 && attempt < kMaxHttpAttempts) {
						BackoffSleep(attempt);
						continue;
					}
					return lastErr;
				}
				return lastErr;
			}

			std::string HttpDownloadToFile(const std::string& url,
										   const std::string& destAbs) {
				std::string lastErr;
				for (long attempt = 1; attempt <= kMaxHttpAttempts; ++attempt) {
					std::FILE* f = std::fopen(destAbs.c_str(), "wb");
					if (!f)
						return Format("Cannot create '{0}'", destAbs);
					std::unique_ptr<CURL, CURLDeleter> h{curl_easy_init()};
					if (!h) {
						std::fclose(f);
						std::remove(destAbs.c_str());
						return "curl_easy_init failed";
					}
					curl_easy_setopt(h.get(), CURLOPT_URL, url.c_str());
					curl_easy_setopt(h.get(), CURLOPT_USERAGENT, PACKAGE_STRING);
					curl_easy_setopt(h.get(), CURLOPT_FOLLOWLOCATION, 1L);
					curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, CurlWriteToFile);
					curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, f);
					curl_easy_setopt(h.get(), CURLOPT_CONNECTTIMEOUT, 30L);
					CURLcode rc = curl_easy_perform(h.get());
					long code = 0;
					curl_easy_getinfo(h.get(), CURLINFO_RESPONSE_CODE, &code);
					std::fclose(f);
					if (rc == CURLE_OK) {
						lastErr = HttpCodeError(code);
						if (lastErr.empty())
							return std::string{};
					} else {
						lastErr = curl_easy_strerror(rc);
					}
					// The partial file is never a usable pak; drop it before retry
					// or bail.
					std::remove(destAbs.c_str());
					bool retriable = (rc != CURLE_OK) ? IsTransientCurl(rc) : (code >= 500);
					if (retriable && attempt < kMaxHttpAttempts) {
						BackoffSleep(attempt);
						continue;
					}
					return lastErr;
				}
				return lastErr;
			}
		} // namespace

		class ModsScreenHelper::RefreshQuery final : public Thread {
			Handle<ModsScreenHelper> owner;
			std::string indexUrl; // resolved on the main thread before Start()

			void Done(const std::string& msg) {
				// A non-empty message is always a failure; log it so a broken
				// download leaves a diagnosable trail even if the user misses the
				// on-screen alert.
				if (!msg.empty())
					SPLog("Mods refresh failed: %s", msg.c_str());
				owner->resultCell.store(std::make_unique<std::string>(msg));
				owner = nullptr;
			}

			void SetCurrent(const std::string& s) {
				std::lock_guard<std::mutex> lk(owner->progressMutex);
				owner->progressItem = s;
			}

			// Two-pass: first walk the listing to count downloadable items, then
			// walk again to actually download. Lets the UI show a real fraction.
			void CountInto(const Json::Value& root, int& counter) {
				if (!root.isArray())
					return;
				for (const auto& entry : root) {
					std::string type = entry.get("type", "").asString();
					std::string name = entry.get("name", "").asString();
					if (type == "file" && EndsWithPak(name))
						++counter;
				}
			}

			// Download every pak in this listing level into dirAbs. A per-file
			// failure is recorded in `failures` and does NOT abort the batch, so
			// one pak that can't be written — e.g. an enabled mod whose file is
			// held open by the running game — can't stop the others from
			// downloading. Every pak entry advances the progress counter exactly
			// once, so the bar still completes.
			void FetchOneLevel(const Json::Value& root, const std::string& dirAbs,
							   std::vector<std::string>& failures) {
				EnsureDir(dirAbs);
				for (const auto& entry : root) {
					std::string type = entry.get("type", "").asString();
					std::string name = entry.get("name", "").asString();
					if (type != "file" || !EndsWithPak(name))
						continue;
					std::string dl = entry.get("download_url", "").asString();
					if (dl.empty()) {
						failures.push_back(_Tr("ModsScreenHelper", "{0}: no download URL", name));
						++owner->progressDone;
						continue;
					}
					SetCurrent(name);
					std::string partial = dirAbs + "/" + name + ".partial";
					std::string finalPath = dirAbs + "/" + name;
					std::string e = HttpDownloadToFile(dl, partial);
					if (!e.empty()) {
						failures.push_back(_Tr("ModsScreenHelper", "{0}: {1}", name, e));
						++owner->progressDone;
						continue;
					}
					std::remove(finalPath.c_str());
					if (std::rename(partial.c_str(), finalPath.c_str()) != 0) {
						std::remove(partial.c_str());
						failures.push_back(
						  _Tr("ModsScreenHelper", "{0}: cannot replace (file in use?)", name));
					}
					++owner->progressDone;
				}
			}

		public:
			RefreshQuery(ModsScreenHelper* o, const std::string& indexUrl)
				: owner{o}, indexUrl{indexUrl} {}

			void Run() override {
				try {
					owner->progressTotal.store(0);
					owner->progressDone.store(0);
					SetCurrent("");
					EnsureDir(ModsRootAbs());

					// Fetch and parse the root listing.
					std::string body;
					std::string err = HttpGetText(indexUrl, body);
					if (!err.empty()) {
						Done(_Tr("ModsScreenHelper", "List '{0}': {1}", indexUrl, err));
						return;
					}
					Json::Reader reader;
					Json::Value root;
					if (!reader.parse(body, root, false) || !root.isArray()) {
						Done(_Tr("ModsScreenHelper", "Index parse failed (expected JSON array)"));
						return;
					}

					// First, collect sub-listings (folder mods) and count
					// everything that will be downloaded so progressTotal is
					// correct from the start.
					struct SubListing {
						std::string name;
						Json::Value items;
					};
					std::vector<SubListing> subs;
					int total = 0;
					CountInto(root, total);
					for (const auto& entry : root) {
						if (entry.get("type", "").asString() != "dir")
							continue;
						std::string url = entry.get("url", "").asString();
						std::string name = entry.get("name", "").asString();
						if (url.empty() || name.empty())
							continue;
						std::string sb;
						std::string e = HttpGetText(url, sb);
						if (!e.empty()) {
							Done(_Tr("ModsScreenHelper", "List '{0}': {1}", name, e));
							return;
						}
						Json::Value sroot;
						if (!reader.parse(sb, sroot, false) || !sroot.isArray())
							continue;
						SubListing s;
						s.name = name;
						s.items = sroot;
						CountInto(sroot, total);
						subs.push_back(std::move(s));
					}
					owner->progressTotal.store(total);

					// Now actually download. Loose paks land at Mods/<name>;
					// folder mods land at Mods/<folder>/<name>. Per-file failures
					// are collected rather than aborting, so a single stuck pak
					// never blocks the rest of the set.
					std::vector<std::string> failures;
					FetchOneLevel(root, ModsRootAbs(), failures);
					for (const auto& s : subs) {
						std::string sub = ModsRootAbs() + "/" + s.name;
						FetchOneLevel(s.items, sub, failures);
					}
					if (!failures.empty()) {
						std::string msg = _Tr("ModsScreenHelper", "{0} pak(s) failed to download:",
											  static_cast<int>(failures.size()));
						for (const std::string& f : failures)
							msg += "\n" + f;
						Done(msg);
						return;
					}
					Done(std::string{});
				} catch (const std::exception& ex) {
					Done(ex.what());
				} catch (...) {
					Done("Unknown error");
				}
			}
		};

		ModsScreenHelper::ModsScreenHelper()
			: query(nullptr), modsCached(false), progressTotal(0), progressDone(0) {
			SPADES_MARK_FUNCTION();
		}

		std::string ModsScreenHelper::GetIndexUrl() { return ResolveModsIndexUrl(); }

		// CATEGORY-NAME-AUTHOR parsing for the list UI. Category and author are
		// empty for a name that doesn't follow the convention; the display name
		// then falls back to the full filename.
		std::string ModsScreenHelper::GetModCategory(std::string modName) {
			return ParseModName(modName).category;
		}
		std::string ModsScreenHelper::GetModDisplayName(std::string modName) {
			return ParseModName(modName).name;
		}
		std::string ModsScreenHelper::GetModAuthor(std::string modName) {
			return ParseModName(modName).author;
		}

		int ModsScreenHelper::GetRefreshTotal() { return progressTotal.load(); }
		int ModsScreenHelper::GetRefreshDone() { return progressDone.load(); }
		std::string ModsScreenHelper::GetRefreshCurrentItem() {
			std::lock_guard<std::mutex> lk(progressMutex);
			return progressItem;
		}

		ModsScreenHelper::~ModsScreenHelper() {
			SPADES_MARK_FUNCTION();
			if (query)
				query->MarkForAutoDeletion();
		}

		void ModsScreenHelper::StartRefresh() {
			SPADES_MARK_FUNCTION();
			if (query)
				return; // already running
			lastMessage.clear();
			// Heal any stale persisted value on the main thread (the worker never
			// touches the settings store), then hand the resolved URL to it.
			HealPersistedModsIndexUrl();
			query = new RefreshQuery(this, ResolveModsIndexUrl());
			query->Start();
		}

		bool ModsScreenHelper::PollRefreshState() {
			SPADES_MARK_FUNCTION();
			auto cell = resultCell.take();
			if (cell) {
				lastMessage = *cell;
				if (query) {
					query->MarkForAutoDeletion();
					query = nullptr;
				}
				modsCached = false; // freshen cache after download
			}
			return query == nullptr;
		}

		std::string ModsScreenHelper::GetRefreshMessage() { return lastMessage; }

		void ModsScreenHelper::RebuildModsCache() {
			mods.clear();
			std::string root = ModsRootAbs();
			if (!IsDirAbs(root)) {
				modsCached = true;
				return;
			}
			for (const std::string& entry : ListDir(root)) {
				std::string abs = root + "/" + entry;
				if (IsDirAbs(abs)) {
					// Multi-pak mod: a folder containing one or more .pak files.
					ModEntry m;
					m.name = entry;
					m.isFolder = true;
					for (const std::string& f : ListDir(abs)) {
						if (!EndsWithPak(f))
							continue;
						m.paks.push_back(f);
						std::int64_t sz = FileSizeAbs(abs + "/" + f);
						if (sz > 0)
							m.totalSize += sz;
					}
					std::sort(m.paks.begin(), m.paks.end());
					if (!m.paks.empty())
						mods.push_back(std::move(m));
				} else if (EndsWithPak(entry)) {
					// Loose pak at the root: treat as a single-pak mod named
					// after the file (extension included for clarity).
					ModEntry m;
					m.name = entry;
					m.isFolder = false;
					m.paks.push_back(entry);
					std::int64_t sz = FileSizeAbs(abs);
					if (sz > 0)
						m.totalSize = sz;
					mods.push_back(std::move(m));
				}
			}
			std::sort(mods.begin(), mods.end(),
					  [](const ModEntry& a, const ModEntry& b) { return a.name < b.name; });
			modsCached = true;
		}

		const ModsScreenHelper::ModEntry*
		ModsScreenHelper::FindMod(const std::string& name) const {
			for (const ModEntry& m : mods)
				if (m.name == name)
					return &m;
			return nullptr;
		}

		std::vector<std::string> ModsScreenHelper::GetModNames() {
			if (!modsCached)
				RebuildModsCache();
			std::vector<std::string> out;
			out.reserve(mods.size());
			for (const ModEntry& m : mods)
				out.push_back(m.name);
			return out;
		}

		int ModsScreenHelper::GetModPakCount(std::string modName) {
			if (!modsCached)
				RebuildModsCache();
			const ModEntry* m = FindMod(modName);
			return m ? static_cast<int>(m->paks.size()) : 0;
		}

		std::int64_t ModsScreenHelper::GetModTotalSize(std::string modName) {
			if (!modsCached)
				RebuildModsCache();
			const ModEntry* m = FindMod(modName);
			return m ? m->totalSize : 0;
		}

		std::vector<std::string> ModsScreenHelper::GetModContents(std::string modName) {
			if (!modsCached)
				RebuildModsCache();
			std::vector<std::string> out;
			const ModEntry* m = FindMod(modName);
			if (m) {
				for (const std::string& pak : m->paks) {
					std::string overlayPath = m->isFolder
						? ("Mods/" + m->name + "/" + pak)
						: ("Mods/" + pak);
					try {
						auto stream = FileManager::OpenForReading(overlayPath.c_str());
						ZipFileSystem zfs(stream.release());
						for (const std::string& f : zfs.GetAllFiles())
							out.push_back(pak + ": " + f);
					} catch (const std::exception& ex) {
						out.push_back(pak + ": <unreadable: " + ex.what() + ">");
					}
				}
			}
			std::sort(out.begin(), out.end());
			return out;
		}

		std::vector<std::string> ModsScreenHelper::GetEnabledMods() { return ReadEnabled(); }

		// Enable a mod: drop any existing entry and append it at the end so it is
		// applied last (and so wins conflicts). Persisted immediately; it takes
		// effect on the next launch, when the overlay is mounted.
		void ModsScreenHelper::EnableMod(std::string modName) {
			std::vector<std::string> list = ReadEnabled();
			list.erase(std::remove(list.begin(), list.end(), modName), list.end());
			list.push_back(modName);
			WriteEnabled(list);
		}

		void ModsScreenHelper::DisableMod(std::string modName) {
			std::vector<std::string> list = ReadEnabled();
			list.erase(std::remove(list.begin(), list.end(), modName), list.end());
			WriteEnabled(list);
		}

		void ModsScreenHelper::ClearEnabledMods() { WriteEnabled({}); }

		std::vector<std::string> ModsScreenHelper::GetEnabledModPakPaths() {
			// Resolve the enabled names to pak paths relative to the resource
			// root, in enabled order. Runs at startup before any instance
			// exists, so a throwaway helper does the directory scan. Names with
			// no matching mod on disk are skipped.
			Handle<ModsScreenHelper> h = Handle<ModsScreenHelper>::New();
			h->RebuildModsCache();
			std::vector<std::string> out;
			for (const std::string& name : ReadEnabled()) {
				const ModEntry* m = h->FindMod(name);
				if (m == nullptr)
					continue;
				for (const std::string& pak : m->paks)
					out.push_back(m->isFolder
						? (std::string(kModsDir) + "/" + m->name + "/" + pak)
						: (std::string(kModsDir) + "/" + pak));
			}
			return out;
		}

	} // namespace gui
} // namespace spades
