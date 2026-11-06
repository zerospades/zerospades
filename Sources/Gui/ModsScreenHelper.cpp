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
#include <functional>
#include <sys/stat.h>
#include <thread>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

#include <curl/curl.h>
#include <json/json.h>

#include <unzip/unzip.h>

#include "Main.h"
#include "OfficialMods.h"
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/ModPak.h>
#include <Core/Strings.h>
#include <Core/Thread.h>
#include <Core/ZipFileSystem.h>
#include <ZeroSpades.h>

namespace spades {
	namespace gui {

		namespace {
			constexpr const char* kModsDir = "Mods";

			// A release's zip and its paks are a few tens of MB; refuse anything far
			// bigger, so a broken release cannot fill a player's disk.
			constexpr std::int64_t kMaxReleaseBytes = std::int64_t(1) << 30;

			// Inside Mods/, the hidden working names of a download: the zip, and
			// the folder its paks are unpacked into before they move into Mods/.
			constexpr const char* kOfficialZipPartial = ".official.zip.partial";
			constexpr const char* kOfficialStaging = ".official.partial";

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

			// The entries of a folder. Hidden ones (a leading '.', such as a
			// download's working files) are left out unless asked for.
			std::vector<std::string> ListDir(const std::string& path, bool includeHidden = false) {
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
					if (!includeHidden && fd.cFileName[0] == '.')
						continue;
					out.emplace_back(fd.cFileName);
				} while (FindNextFileA(h, &fd));
				FindClose(h);
#else
				DIR* d = ::opendir(path.c_str());
				if (!d)
					return out;
				while (auto* e = ::readdir(d)) {
					if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0)
						continue;
					if (!includeHidden && e->d_name[0] == '.')
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

			// Deletes a file, or a folder and everything in it. Returns false if
			// anything is left behind (e.g. a pak held open by the running game).
			bool RemoveTree(const std::string& path) {
				if (IsDirAbs(path)) {
					bool ok = true;
					for (const std::string& entry : ListDir(path, true))
						ok = RemoveTree(path + "/" + entry) && ok;
#ifdef _WIN32
					return _rmdir(path.c_str()) == 0 && ok;
#else
					return ::rmdir(path.c_str()) == 0 && ok;
#endif
				}
				return std::remove(path.c_str()) == 0 || FileSizeAbs(path) < 0;
			}

			// Moves `from` over `to` in one step: if it cannot, `to` is left as it
			// was (never deleted first, so a failure loses nothing).
			bool ReplaceFile(const std::string& from, const std::string& to) {
#ifdef _WIN32
				return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
				return std::rename(from.c_str(), to.c_str()) == 0;
#endif
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

			// Called with the bytes received so far and the total (0 while unknown).
			using DownloadProgress = std::function<void(std::int64_t, std::int64_t)>;

			int CurlProgress(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t,
							 curl_off_t) {
				(*static_cast<const DownloadProgress*>(clientp))(
				  static_cast<std::int64_t>(dlnow), static_cast<std::int64_t>(dltotal));
				return 0; // keep going
			}

			// HTTP GET into a file. Returns "" on success, otherwise an error
			// description. Transient failures and 5xx are retried with backoff.
			std::string HttpDownloadToFile(const std::string& url, const std::string& destAbs,
										   const DownloadProgress& progress) {
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
					// The release asset redirects to GitHub's storage: over https only.
#if LIBCURL_VERSION_NUM >= 0x075500
					curl_easy_setopt(h.get(), CURLOPT_PROTOCOLS_STR, "https");
					curl_easy_setopt(h.get(), CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
					curl_easy_setopt(h.get(), CURLOPT_PROTOCOLS, long(CURLPROTO_HTTPS));
					curl_easy_setopt(h.get(), CURLOPT_REDIR_PROTOCOLS, long(CURLPROTO_HTTPS));
#endif
					curl_easy_setopt(h.get(), CURLOPT_MAXFILESIZE_LARGE,
					                 static_cast<curl_off_t>(kMaxReleaseBytes));
					curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, CurlWriteToFile);
					curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, f);
					curl_easy_setopt(h.get(), CURLOPT_CONNECTTIMEOUT, 30L);
					curl_easy_setopt(h.get(), CURLOPT_LOW_SPEED_TIME, 30L);
					curl_easy_setopt(h.get(), CURLOPT_LOW_SPEED_LIMIT, 15L);
					curl_easy_setopt(h.get(), CURLOPT_NOPROGRESS, 0L);
					curl_easy_setopt(h.get(), CURLOPT_XFERINFOFUNCTION, CurlProgress);
					curl_easy_setopt(h.get(), CURLOPT_XFERINFODATA, &progress);
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

		/**
		 * A download, shared by its worker and every helper following it, so it
		 * outlives the main screen that started it (rebuilt on a resize or on
		 * return from a game).
		 */
		struct ModsScreenHelper::RefreshJob {
			std::mutex mutex;
			std::string status;  // what it is doing now
			float fraction = -1.0F; // of the current step; negative while unknown
			bool finished = false;
			std::string message; // once finished: empty, or what went wrong
		};

		std::mutex ModsScreenHelper::runningJobMutex;
		std::weak_ptr<ModsScreenHelper::RefreshJob> ModsScreenHelper::runningJob;

		class ModsScreenHelper::RefreshQuery final : public Thread {
			std::shared_ptr<RefreshJob> job;

			void Done(const std::string& msg) {
				// A non-empty message is always a failure; log it so a broken
				// download leaves a diagnosable trail even if the user misses the
				// on-screen alert.
				if (!msg.empty())
					SPLog("Official mods download failed: %s", msg.c_str());
				std::lock_guard<std::mutex> lk(job->mutex);
				job->message = msg;
				job->finished = true;
			}

			void SetProgress(const std::string& status, float fraction) {
				std::lock_guard<std::mutex> lk(job->mutex);
				job->status = status;
				job->fraction = fraction;
			}

			static std::string Megabytes(std::int64_t bytes) {
				char buf[32];
				std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(bytes) / 1048576.0);
				return buf;
			}

			/**
			 * Extracts the paks at the root of the zip into `destAbs`, keeping their
			 * names exactly (ZipFileSystem would lower-case them). Fills `installed`.
			 * Returns an empty string on success, otherwise what went wrong.
			 */
			std::string ExtractPaks(const std::string& zipAbs, const std::string& destAbs,
			                        const std::string& tag,
			                        std::vector<OfficialModsIndex::Pak>& installed) {
				std::unique_ptr<void, int (*)(unzFile)> zip{unzOpen(zipAbs.c_str()), unzClose};
				if (!zip)
					return _Tr("ModsScreenHelper", "The downloaded file is not a zip.");

				// First pass: the paks to install, so progress has a real total. A
				// name met twice, even in another case (one file on Windows and
				// macOS), is installed once.
				// Each pak's size and CRC-32 come from the zip, which checks them as
				// it unpacks: the index records them.
				std::vector<OfficialModsIndex::Pak> paks;
				for (int rc = unzGoToFirstFile(zip.get()); rc == UNZ_OK;
				     rc = unzGoToNextFile(zip.get())) {
					char name[512];
					unz_file_info info;
					if (unzGetCurrentFileInfo(zip.get(), &info, name, sizeof(name), nullptr, 0,
					                          nullptr, 0) != UNZ_OK)
						return _Tr("ModsScreenHelper", "The downloaded zip is damaged.");
					if (!IsOfficialModsPakName(name))
						continue;
					bool seen = false;
					for (const OfficialModsIndex::Pak& p : paks)
						seen = seen || EqualsIgnoringCase(p.name, name);
					if (seen)
						continue;
					OfficialModsIndex::Pak pak;
					pak.name = name;
					pak.crc32 = static_cast<std::uint32_t>(info.crc);
					pak.size = static_cast<std::int64_t>(info.uncompressed_size);
					paks.push_back(std::move(pak));
				}
				if (paks.empty())
					return _Tr("ModsScreenHelper", "The release has no paks.");

				std::vector<char> buf(1 << 16);
				std::int64_t unpacked = 0;
				for (std::size_t i = 0; i < paks.size(); ++i) {
					const std::string& name = paks[i].name;
					SetProgress(_Tr("ModsScreenHelper", "Installing official mods {0}: {1}", tag,
					                name),
					            static_cast<float>(i) / static_cast<float>(paks.size()));

					// Case-sensitive lookup: the name is the zip's own spelling.
					if (unzLocateFile(zip.get(), name.c_str(), 1) != UNZ_OK ||
					    unzOpenCurrentFile(zip.get()) != UNZ_OK)
						return _Tr("ModsScreenHelper", "{0}: cannot read it from the zip.", name);

					const std::string outAbs = destAbs + "/" + name;
					std::FILE* f = std::fopen(outAbs.c_str(), "wb");
					if (!f) {
						unzCloseCurrentFile(zip.get());
						return _Tr("ModsScreenHelper", "{0}: cannot write it.", name);
					}
					int read;
					bool written = true;
					while ((read = unzReadCurrentFile(zip.get(), buf.data(),
					                                  static_cast<unsigned>(buf.size()))) > 0) {
						unpacked += read;
						if (unpacked > kMaxReleaseBytes)
							break;
						written = written &&
						          std::fwrite(buf.data(), 1, static_cast<std::size_t>(read), f) ==
						            static_cast<std::size_t>(read);
					}
					if (unpacked > kMaxReleaseBytes) {
						std::fclose(f);
						unzCloseCurrentFile(zip.get());
						return _Tr("ModsScreenHelper", "The release is too big to install.");
					}
					written = std::fclose(f) == 0 && written;
					// UNZ_CRCERROR here means the data does not match the zip's checksum.
					const bool intact = unzCloseCurrentFile(zip.get()) == UNZ_OK && read == 0;
					if (!intact)
						return _Tr("ModsScreenHelper", "{0}: damaged in the zip.", name);
					if (!written)
						return _Tr("ModsScreenHelper", "{0}: cannot write it.", name);
					installed.push_back(paks[i]);
				}
				return std::string{};
			}

			/**
			 * Removes every pak the previous release installed (its index lists
			 * them), so an update starts from a clean slate: nothing renamed or
			 * dropped keeps loading next to its replacement, and an edited official
			 * pak is replaced too. Their enabled entries follow the new release: kept
			 * for a pak it ships still (in whatever case it now spells it), dropped
			 * for one it no longer ships.
			 */
			void RemovePreviousRelease(const std::vector<OfficialModsIndex::Pak>& paks) {
				std::optional<OfficialModsIndex> previous = ReadOfficialModsIndex();
				if (!previous)
					return;
				std::vector<std::string> enabled = ReadEnabled();
				bool changed = false;
				for (const OfficialModsIndex::Pak& oldPak : previous->paks) {
					const std::string& old = oldPak.name;
					// Held open, it stays: the new copy is then moved over it.
					std::remove((ModsRootAbs() + "/" + old).c_str());
					const std::string* kept = nullptr;
					for (const OfficialModsIndex::Pak& pak : paks) {
						if (EqualsIgnoringCase(pak.name, old))
							kept = &pak.name;
					}
					if (kept && *kept == old)
						continue;
					for (auto it = enabled.begin(); it != enabled.end();) {
						if (*it != old) {
							++it;
						} else if (kept) {
							*it++ = *kept;
							changed = true;
						} else {
							it = enabled.erase(it);
							changed = true;
						}
					}
				}
				if (changed)
					WriteEnabled(enabled);
			}

			void Install() {
				SetProgress(_Tr("ModsScreenHelper", "Looking for the latest official mods..."),
				            -1.0F);
				OfficialModsListing listing;
				std::string err = FetchOfficialModsListing(listing);
				if (!err.empty()) {
					Done(_Tr("ModsScreenHelper", "Cannot list the official mods: {0}", err));
					return;
				}
				if (!listing.latestCompatible) {
					if (listing.newestIncompatible)
						Done(_Tr("ModsScreenHelper",
						         "The official mods {0} need a newer version of ZeroSpades. "
						         "Update the game to get them.",
						         listing.newestIncompatible->ToString()));
					else
						Done(_Tr("ModsScreenHelper",
						         "No official mods have been released for this version of "
						         "ZeroSpades yet."));
					return;
				}
				const OfficialModsRelease& release = *listing.latestCompatible;
				const std::string tag = release.version.ToString();

				EnsureDir(ModsRootAbs());
				const std::string zipAbs = ModsRootAbs() + "/" + kOfficialZipPartial;
				const std::string stagingAbs = ModsRootAbs() + "/" + kOfficialStaging;

				// Leftovers of an interrupted download are never usable.
				RemoveTree(zipAbs);
				RemoveTree(stagingAbs);

				const std::string downloading =
				  _Tr("ModsScreenHelper", "Downloading official mods {0}", tag);
				DownloadProgress progress = [&](std::int64_t done, std::int64_t total) {
					if (total <= 0)
						total = release.zipSize;
					if (total > 0)
						SetProgress(downloading + ": " + Megabytes(done) + " / " +
						              Megabytes(total) + " MB",
						            static_cast<float>(static_cast<double>(done) /
						                               static_cast<double>(total)));
					else
						SetProgress(downloading + "...", -1.0F);
				};
				progress(0, 0);
				err = HttpDownloadToFile(release.zipUrl, zipAbs, progress);
				if (!err.empty()) {
					Done(_Tr("ModsScreenHelper", "Cannot download the official mods {0}: {1}",
					         tag, err));
					return;
				}

				// Unpack aside first, so a damaged zip leaves Mods/ untouched.
				EnsureDir(stagingAbs);
				std::vector<OfficialModsIndex::Pak> paks;
				err = ExtractPaks(zipAbs, stagingAbs, tag, paks);
				RemoveTree(zipAbs);
				if (!err.empty()) {
					RemoveTree(stagingAbs);
					Done(_Tr("ModsScreenHelper", "Cannot install the official mods {0}: {1}", tag,
					         err));
					return;
				}

				// The previous release goes, then each pak of this one moves into Mods/,
				// over any copy of the player's of the same name. One that cannot be
				// moved (a file held open, scanned by an antivirus) does not stop the
				// others.
				SetProgress(_Tr("ModsScreenHelper", "Installing official mods {0}...", tag), 1.0F);
				RemovePreviousRelease(paks);
				OfficialModsIndex index;
				index.version = release.version;
				std::vector<std::string> failures;
				for (const OfficialModsIndex::Pak& pak : paks) {
					if (ReplaceFile(stagingAbs + "/" + pak.name, ModsRootAbs() + "/" + pak.name))
						index.paks.push_back(pak);
					else
						failures.push_back(
						  _Tr("ModsScreenHelper", "{0}: cannot replace (file in use?)", pak.name));
				}
				RemoveTree(stagingAbs);
				// The index lists what is installed now, even after a failure: the
				// paks that made it are verified, a download again brings the rest.
				if (!WriteOfficialModsIndex(index)) {
					Done(_Tr("ModsScreenHelper", "Cannot write Mods/index.txt."));
					return;
				}
				if (!failures.empty()) {
					std::string msg = _Tr("ModsScreenHelper",
					                      "{0} pak(s) failed to install; download the official "
					                      "mods again to retry:",
					                      static_cast<int>(failures.size()));
					for (const std::string& f : failures)
						msg += "\n" + f;
					Done(msg);
					return;
				}
				// Stop announcing it, whichever main screen is showing by now.
				OfficialModsChecker::Get().Installed(release.version);
				SPLog("Installed the official mods %s (%d paks).", tag.c_str(),
				      static_cast<int>(paks.size()));
				Done(std::string{});
			}

		public:
			RefreshQuery(std::shared_ptr<RefreshJob> job) : job{std::move(job)} {}

			void Run() override {
				try {
					Install();
				} catch (const std::exception& ex) {
					Done(ex.what());
				} catch (...) {
					Done("Unknown error");
				}
			}
		};

		ModsScreenHelper::ModsScreenHelper() : modsCached(false) {
			SPADES_MARK_FUNCTION();
			// Follow a download started by an earlier main screen, if it runs still.
			std::lock_guard<std::mutex> lk(runningJobMutex);
			job = runningJob.lock();
			if (job) {
				std::lock_guard<std::mutex> jobLock(job->mutex);
				if (job->finished)
					job.reset();
			}
		}

		std::string ModsScreenHelper::GetInstalledOfficialVersion() {
			std::optional<OfficialModsVersion> installed = ReadInstalledOfficialMods();
			return installed ? installed->ToString() : std::string{};
		}

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

		bool ModsScreenHelper::IsInstalledOfficialPakEnabled() {
			std::optional<OfficialModsIndex> index = ReadOfficialModsIndex();
			if (!index)
				return false;
			for (const std::string& name : ReadEnabled()) {
				if (index->Find(name))
					return true;
			}
			return false;
		}

		OfficialPakState ModsScreenHelper::GetModOfficialState(std::string modName) {
			if (!modsCached)
				RebuildModsCache();
			ModEntry* m = nullptr;
			for (ModEntry& e : mods) {
				if (e.name == modName)
					m = &e;
			}
			// Checked only when the list asks, not on every scan: mounting the
			// mods at startup scans them too, and need not read every pak.
			if (!m || m->paks.size() != 1 || m->paks[0] != m->name)
				return OfficialPakState::Unverified; // only loose paks can be official
			if (!m->officialChecked) {
				if (!officialIndexRead) {
					officialIndex = ReadOfficialModsIndex();
					officialIndexRead = true;
				}
				if (officialIndex)
					m->official =
					  VerifyOfficialPak(*officialIndex, m->name, ModsRootAbs() + "/" + m->name);
				m->officialChecked = true;
			}
			return m->official;
		}

		float ModsScreenHelper::GetRefreshFraction() {
			if (!job)
				return -1.0F;
			std::lock_guard<std::mutex> lk(job->mutex);
			return job->fraction;
		}
		std::string ModsScreenHelper::GetRefreshStatus() {
			if (!job)
				return std::string{};
			std::lock_guard<std::mutex> lk(job->mutex);
			return job->status;
		}

		ModsScreenHelper::~ModsScreenHelper() { SPADES_MARK_FUNCTION(); }

		void ModsScreenHelper::StartRefresh() {
			SPADES_MARK_FUNCTION();
			if (job)
				return; // already following one
			lastMessage.clear();
			std::lock_guard<std::mutex> lk(runningJobMutex);
			job = runningJob.lock();
			if (job) {
				std::lock_guard<std::mutex> jobLock(job->mutex);
				if (!job->finished)
					return; // another screen's download, now followed here too
			}
			job = std::make_shared<RefreshJob>();
			runningJob = job;
			auto* query = new RefreshQuery(job);
			query->Start();
			query->MarkForAutoDeletion();
		}

		bool ModsScreenHelper::IsRefreshing() { return job != nullptr; }

		bool ModsScreenHelper::PollRefreshState() {
			SPADES_MARK_FUNCTION();
			if (!job)
				return true;
			{
				std::lock_guard<std::mutex> lk(job->mutex);
				if (!job->finished)
					return false;
				lastMessage = job->message;
			}
			job.reset();
			modsCached = false; // freshen cache after download
			return true;
		}

		std::string ModsScreenHelper::GetRefreshMessage() { return lastMessage; }

		void ModsScreenHelper::RebuildModsCache() {
			mods.clear();
			officialIndexRead = false; // the index too may have changed
			std::string root = ModsRootAbs();
			if (!IsDirAbs(root)) {
				modsCached = true;
				return;
			}
			auto addPak = [&](ModEntry& m, const std::string& rel) {
				m.paks.push_back(rel);
				std::int64_t sz = FileSizeAbs(root + "/" + rel);
				if (sz > 0)
					m.totalSize += sz;
			};
			for (const std::string& entry : ListDir(root)) {
				std::string abs = root + "/" + entry;
				if (IsDirAbs(abs)) {
					// Multi-pak mod: a folder containing one or more .pak files.
					ModEntry m;
					m.name = entry;
					for (const std::string& f : ListDir(abs)) {
						if (EndsWithPak(f))
							addPak(m, entry + "/" + f);
					}
					std::sort(m.paks.begin(), m.paks.end());
					if (!m.paks.empty())
						mods.push_back(std::move(m));
				} else if (EndsWithPak(entry)) {
					// Loose pak at the root: treat as a single-pak mod named
					// after the file (extension included for clarity).
					ModEntry m;
					m.name = entry;
					addPak(m, entry);
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
					std::string label = pak.substr(pak.rfind('/') + 1);
					try {
						auto stream =
						  FileManager::OpenForReading((std::string(kModsDir) + "/" + pak).c_str());
						ZipFileSystem zfs(stream.release());
						for (const std::string& f : zfs.GetAllFiles())
							out.push_back(label + ": " + f);
					} catch (const std::exception& ex) {
						out.push_back(label + ": <unreadable: " + ex.what() + ">");
					}
				}
			}
			std::sort(out.begin(), out.end());
			return out;
		}

		std::vector<std::string> ModsScreenHelper::GetEnabledMods() { return ReadEnabled(); }

		// Enable a mod: drop any existing entry and append it at the end so it is
		// applied last (and so wins conflicts). Persisted immediately; it takes
		// effect when the enabled set is applied (see ModOverlay).
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

		void ModsScreenHelper::SetEnabledMods(const std::vector<std::string>& modNames) {
			WriteEnabled(modNames);
		}

		std::vector<std::pair<std::string, std::string>>
		ModsScreenHelper::GetModPaks(const std::vector<std::string>& modNames) {
			// In the given order. Names with no matching mod on disk are skipped.
			std::vector<std::pair<std::string, std::string>> out;
			for (const std::string& name : modNames) {
				for (std::string& path : ModPak::FindPaths(name))
					out.emplace_back(name, std::move(path));
			}
			return out;
		}

	} // namespace gui
} // namespace spades
