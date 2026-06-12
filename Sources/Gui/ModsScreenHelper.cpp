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

#include "ModsScreenHelper.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

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
#include <ScriptBindings/ScriptManager.h>
#include <ZeroSpades.h>

DEFINE_SPADES_SETTING(cl_modsIndexUrl,
                      "https://api.github.com/repos/zerospades/zerospades-paks/contents/");

namespace spades {
	namespace gui {

		namespace {
			constexpr const char* kModsDir = "Mods";
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

			std::string ToLower(const std::string& s) {
				std::string out = s;
				std::transform(out.begin(), out.end(), out.begin(),
				               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return out;
			}

			bool EndsWithPak(const std::string& s) {
				std::string l = ToLower(s);
				return l.size() >= 4 && (l.compare(l.size() - 4, 4, ".pak") == 0 ||
				                         l.compare(l.size() - 4, 4, ".zip") == 0);
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

			// HTTP GET into a string. Returns "" on success, otherwise an error
			// description. User-Agent is required by the GitHub API.
			std::string HttpGetText(const std::string& url, std::string& out) {
				std::unique_ptr<CURL, CURLDeleter> h{curl_easy_init()};
				if (!h)
					return "curl_easy_init failed";
				curl_easy_setopt(h.get(), CURLOPT_URL, url.c_str());
				curl_easy_setopt(h.get(), CURLOPT_USERAGENT, PACKAGE_STRING);
				curl_easy_setopt(h.get(), CURLOPT_FOLLOWLOCATION, 1L);
				curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, CurlWriteToString);
				curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, &out);
				curl_easy_setopt(h.get(), CURLOPT_CONNECTTIMEOUT, 30L);
				curl_easy_setopt(h.get(), CURLOPT_LOW_SPEED_TIME, 30L);
				curl_easy_setopt(h.get(), CURLOPT_LOW_SPEED_LIMIT, 15L);
				CURLcode rc = curl_easy_perform(h.get());
				if (rc != CURLE_OK)
					return curl_easy_strerror(rc);
				long code = 0;
				curl_easy_getinfo(h.get(), CURLINFO_RESPONSE_CODE, &code);
				if (code >= 400)
					return Format("HTTP {0}", code);
				return std::string{};
			}

			std::string HttpDownloadToFile(const std::string& url,
			                               const std::string& destAbs) {
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
				if (rc != CURLE_OK) {
					std::remove(destAbs.c_str());
					return curl_easy_strerror(rc);
				}
				if (code >= 400) {
					std::remove(destAbs.c_str());
					return Format("HTTP {0}", code);
				}
				return std::string{};
			}
		} // namespace

		class ModsScreenHelper::RefreshQuery final : public Thread {
			Handle<ModsScreenHelper> owner;

			void Done(const std::string& msg) {
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

			std::string FetchOneLevel(const Json::Value& root, const std::string& dirAbs) {
				EnsureDir(dirAbs);
				for (const auto& entry : root) {
					std::string type = entry.get("type", "").asString();
					std::string name = entry.get("name", "").asString();
					if (type != "file" || !EndsWithPak(name))
						continue;
					std::string dl = entry.get("download_url", "").asString();
					if (dl.empty())
						continue;
					SetCurrent(name);
					std::string partial = dirAbs + "/" + name + ".partial";
					std::string finalPath = dirAbs + "/" + name;
					std::string e = HttpDownloadToFile(dl, partial);
					if (!e.empty())
						return Format("Download '{0}': {1}", name, e);
					std::remove(finalPath.c_str());
					if (std::rename(partial.c_str(), finalPath.c_str()) != 0) {
						std::remove(partial.c_str());
						return Format("Rename '{0}' failed", name);
					}
					++owner->progressDone;
				}
				return std::string{};
			}

		public:
			explicit RefreshQuery(ModsScreenHelper* o) : owner{o} {}

			void Run() override {
				try {
					owner->progressTotal.store(0);
					owner->progressDone.store(0);
					SetCurrent("");
					EnsureDir(ModsRootAbs());

					// Fetch and parse the root listing.
					std::string body;
					std::string err = HttpGetText(cl_modsIndexUrl.CString(), body);
					if (!err.empty()) {
						Done(Format("List '{0}': {1}", std::string(cl_modsIndexUrl.CString()), err));
						return;
					}
					Json::Reader reader;
					Json::Value root;
					if (!reader.parse(body, root, false) || !root.isArray()) {
						Done("Index parse failed (expected JSON array)");
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
							Done(Format("List '{0}': {1}", name, e));
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
					// folder mods land at Mods/<folder>/<name>.
					std::string e = FetchOneLevel(root, ModsRootAbs());
					if (!e.empty()) {
						Done(e);
						return;
					}
					for (const auto& s : subs) {
						std::string sub = ModsRootAbs() + "/" + s.name;
						e = FetchOneLevel(s.items, sub);
						if (!e.empty()) {
							Done(e);
							return;
						}
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
			query = new RefreshQuery(this);
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

		CScriptArray* ModsScreenHelper::GetModNames() {
			if (!modsCached)
				RebuildModsCache();
			asIScriptEngine* eng = ScriptManager::GetInstance()->GetEngine();
			asITypeInfo* t = eng->GetTypeInfoByDecl("array<string>");
			SPAssert(t != nullptr);
			CScriptArray* arr = CScriptArray::Create(t, static_cast<asUINT>(mods.size()));
			for (std::size_t i = 0; i < mods.size(); ++i)
				arr->SetValue(static_cast<asUINT>(i), &mods[i].name);
			return arr;
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

		CScriptArray* ModsScreenHelper::GetModContents(std::string modName) {
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
			asIScriptEngine* eng = ScriptManager::GetInstance()->GetEngine();
			asITypeInfo* t = eng->GetTypeInfoByDecl("array<string>");
			SPAssert(t != nullptr);
			CScriptArray* arr = CScriptArray::Create(t, static_cast<asUINT>(out.size()));
			for (std::size_t i = 0; i < out.size(); ++i)
				arr->SetValue(static_cast<asUINT>(i), &out[i]);
			return arr;
		}

		CScriptArray* ModsScreenHelper::GetEnabledMods() {
			std::vector<std::string> list = ReadEnabled();
			asIScriptEngine* eng = ScriptManager::GetInstance()->GetEngine();
			asITypeInfo* t = eng->GetTypeInfoByDecl("array<string>");
			SPAssert(t != nullptr);
			CScriptArray* arr = CScriptArray::Create(t, static_cast<asUINT>(list.size()));
			for (std::size_t i = 0; i < list.size(); ++i)
				arr->SetValue(static_cast<asUINT>(i), &list[i]);
			return arr;
		}

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
