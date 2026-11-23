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

#include "UpdateChecker.h"

#include <cctype>
#include <memory>

#include <curl/curl.h>
#include <json/json.h>

#include <Core/Debug.h>
#include <Core/Thread.h>
#include <ZeroSpades.h>

namespace spades {
	namespace gui {
		namespace {
			// Only published, non-prerelease releases are returned here, so a
			// draft or a release candidate never triggers the notice.
			constexpr const char* kLatestReleaseUrl =
			  "https://api.github.com/repos/zerospades/zerospades/releases/latest";

			// The release CI rebuilds on every push to main. A prerelease, so
			// `releases/latest` never returns it.
			constexpr const char* kNightlyReleaseUrl =
			  "https://api.github.com/repos/zerospades/zerospades/releases/tags/nightly";

			// Where the `nightly` tag points, for when the release does not name its
			// commit as a SHA.
			constexpr const char* kNightlyTagRefUrl =
			  "https://api.github.com/repos/zerospades/zerospades/git/ref/tags/nightly";

			// Followed by `base...head`: how two commits relate.
			constexpr const char* kCompareUrl =
			  "https://api.github.com/repos/zerospades/zerospades/compare/";

			// Every link the check hands to the browser must start with this; the
			// response is untrusted, and the link ends up in a shell open call.
			constexpr const char* kReleasesPageUrl =
			  "https://github.com/zerospades/zerospades/releases/";

			// A release's page is this followed by its tag. Used only when the
			// response carries no acceptable page link of its own.
			constexpr const char* kReleaseTagPageUrl =
			  "https://github.com/zerospades/zerospades/releases/tag/";

			/**
			 * True if `url` is one of the project's release pages, with a path made
			 * only of characters that need no quoting in a URL or a shell argument.
			 */
			bool IsReleasePageUrl(const std::string& url) {
				const std::string prefix = kReleasesPageUrl;
				if (url.size() <= prefix.size() || url.compare(0, prefix.size(), prefix) != 0)
					return false;
				for (std::size_t i = prefix.size(); i < url.size(); ++i) {
					const unsigned char c = static_cast<unsigned char>(url[i]);
					if (!std::isalnum(c) && c != '.' && c != '-' && c != '_' && c != '/' &&
					    c != '+')
						return false;
				}
				return url.find("..") == std::string::npos;
			}

			constexpr const char* kNightlyPageUrl =
			  "https://github.com/zerospades/zerospades/releases/tag/nightly";
			constexpr const char* kLatestReleasePageUrl =
			  "https://github.com/zerospades/zerospades/releases/latest";

			// Short commits are shown at this length, as on the nightly's title.
			constexpr std::size_t kShortCommitLength = 7;

			/** True if `s` is a full commit SHA, so it is safe to put in a URL. */
			bool IsFullCommitSha(const std::string& s) {
				if (s.size() != 40)
					return false;
				for (char c : s) {
					if (!std::isxdigit(static_cast<unsigned char>(c)))
						return false;
				}
				return true;
			}

			std::string ToLower(std::string s) {
				for (char& c : s)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				return s;
			}

			std::string ShortCommit(const std::string& sha) {
				return sha.substr(0, kShortCommitLength);
			}

			// The check must never hold anything up, so it gives up quickly.
			constexpr long kConnectTimeoutSeconds = 10;
			constexpr long kTotalTimeoutSeconds = 20;

			struct CURLEasyDeleter {
				void operator()(CURL* p) const { curl_easy_cleanup(p); }
			};

			struct CURLSlistDeleter {
				void operator()(curl_slist* p) const { curl_slist_free_all(p); }
			};
		} // namespace

		ReleaseVersion ReleaseVersion::Current() {
			ReleaseVersion v;
			v.components = {ZEROSPADES_VERSION_MAJOR, ZEROSPADES_VERSION_MINOR,
			                ZEROSPADES_VERSION_PATCH};
			return v;
		}

		std::optional<ReleaseVersion> ReleaseVersion::Parse(const std::string& tag) {
			std::size_t i = 0;
			if (i < tag.size() && (tag[i] == 'v' || tag[i] == 'V'))
				++i;

			ReleaseVersion v;
			std::size_t count = 0;
			while (count < v.components.size()) {
				if (i >= tag.size() || !std::isdigit(static_cast<unsigned char>(tag[i])))
					break;

				int value = 0;
				while (i < tag.size() && std::isdigit(static_cast<unsigned char>(tag[i]))) {
					if (value > 100000) // not a version number
						return {};
					value = value * 10 + (tag[i] - '0');
					++i;
				}
				v.components[count++] = value;

				if (i < tag.size() && tag[i] == '.')
					++i;
				else
					break;
			}

			if (count == 0)
				return {};
			return v;
		}

		std::string ReleaseVersion::ToString() const {
			return std::to_string(components[0]) + "." + std::to_string(components[1]) + "." +
			       std::to_string(components[2]);
		}

		class UpdateChecker::Query final : public Thread {
			UpdateChecker& owner;
			std::string buffer;

			static std::size_t Write(void* ptr, std::size_t size, std::size_t nmemb,
			                         Query* self) {
				std::size_t numBytes = size * nmemb;
				self->buffer.append(static_cast<const char*>(ptr), numBytes);
				return numBytes;
			}

			/**
			 * GETs `url` from the GitHub API and parses the response as a JSON
			 * object. Logs and returns false on any failure.
			 */
			bool Request(const char* what, const std::string& url, Json::Value& root) {
				buffer.clear();

				std::unique_ptr<CURL, CURLEasyDeleter> handle{curl_easy_init()};
				if (!handle) {
					SPLog("[!] Update check: failed to create a cURL handle.");
					return false;
				}

				// GitHub rejects API requests that carry no User-Agent.
				std::unique_ptr<curl_slist, CURLSlistDeleter> headers{
				  curl_slist_append(nullptr, "Accept: application/vnd.github+json")};

				size_t (*writeCallback)(void*, size_t, size_t, Query*) = &Query::Write;
				curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
				curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, PACKAGE_STRING);
				curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, headers.get());
				curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, writeCallback);
				curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, this);
				curl_easy_setopt(handle.get(), CURLOPT_FAILONERROR, 1L);
				curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
				curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
				curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSeconds);
				curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, kTotalTimeoutSeconds);

				CURLcode rc = curl_easy_perform(handle.get());
				if (rc != CURLE_OK) {
					SPLog("[!] Update check: %s request failed: %s", what, curl_easy_strerror(rc));
					return false;
				}

				Json::Reader reader;
				if (!reader.parse(buffer, root, false) || !root.isObject()) {
					SPLog("[!] Update check: unexpected %s response.", what);
					return false;
				}
				return true;
			}

			/** Release builds: is the latest stable release newer than this one? */
			Result FetchStable() {
				Result r;
				r.state = State::Failed;

				Json::Value root;
				if (!Request("release", kLatestReleaseUrl, root))
					return r;
				if (!root["tag_name"].isString()) {
					SPLog("[!] Update check: unexpected response.");
					return r;
				}

				const std::string tag = root["tag_name"].asString();
				std::optional<ReleaseVersion> latest = ReleaseVersion::Parse(tag);
				if (!latest) {
					SPLog("[!] Update check: unrecognized release tag '%s'.", tag.c_str());
					return r;
				}

				std::string releaseUrl =
				  root["html_url"].isString() ? root["html_url"].asString() : std::string();
				if (!IsReleasePageUrl(releaseUrl))
					releaseUrl = kReleaseTagPageUrl + tag;
				if (!IsReleasePageUrl(releaseUrl)) {
					SPLog("[!] Update check: no usable release page for tag '%s'.", tag.c_str());
					return r;
				}

				const ReleaseVersion current = ReleaseVersion::Current();
				r.latestVersion = *latest;
				r.releaseUrl = std::move(releaseUrl);
				r.state = current < *latest ? State::UpdateAvailable : State::UpToDate;

				SPLog("Update check: running %s, latest release is %s (%s).",
				      current.ToString().c_str(), latest->ToString().c_str(),
				      r.state == State::UpdateAvailable ? "update available" : "up to date");
				return r;
			}

			/**
			 * Nightly builds: is the published nightly ahead of the commit this one
			 * was built from?
			 */
			Result FetchNightly() {
				Result r;
				r.state = State::Failed;

				const std::string mine = ToLower(ZEROSPADES_NIGHTLY_COMMIT);
				if (!IsFullCommitSha(mine)) {
					SPLog("[!] Update check: this nightly has no usable commit.");
					return r;
				}

				// Ask for the release rather than the tag: the workflow moves the tag
				// before it uploads the binaries, and a nightly is only worth
				// announcing once they can be downloaded.
				Json::Value release;
				if (!Request("nightly release", kNightlyReleaseUrl, release))
					return r;

				std::string theirs = release["target_commitish"].isString()
				                       ? ToLower(release["target_commitish"].asString())
				                       : std::string();
				if (!IsFullCommitSha(theirs)) {
					// Created on an existing tag: the release names a branch, not the
					// commit. Ask the tag instead.
					Json::Value ref;
					if (!Request("nightly tag", kNightlyTagRefUrl, ref))
						return r;
					const Json::Value& object = ref["object"];
					theirs = object.isObject() && object["sha"].isString() &&
					             object["type"].isString() && object["type"].asString() == "commit"
					           ? ToLower(object["sha"].asString())
					           : std::string();
					if (!IsFullCommitSha(theirs)) {
						SPLog("[!] Update check: the nightly release names no commit.");
						return r;
					}
				}

				r.latestCommit = ShortCommit(theirs);
				r.releaseUrl = kNightlyPageUrl;

				if (theirs == mine) {
					r.state = State::UpToDate;
				} else {
					Json::Value compare;
					if (!Request("nightly compare", kCompareUrl + mine + "..." + theirs, compare))
						return r;
					const std::string status =
					  compare["status"].isString() ? compare["status"].asString() : std::string();
					if (status == "ahead") {
						r.state = State::UpdateAvailable;
						r.commitsAhead =
						  compare["ahead_by"].isInt() ? compare["ahead_by"].asInt() : -1;
					} else if (status == "diverged") {
						// Rebuilt from another branch: this build is no longer published.
						r.state = State::UpdateAvailable;
					} else if (status == "behind" || status == "identical") {
						r.state = State::UpToDate;
					} else {
						SPLog("[!] Update check: unexpected compare status '%s'.", status.c_str());
						return r;
					}
				}

				if (r.state == State::UpdateAvailable && r.commitsAhead >= 0)
					SPLog("Update check: running nightly %s, latest nightly is %s (%d commit%s "
					      "ahead, update available).",
					      ShortCommit(mine).c_str(), r.latestCommit.c_str(), r.commitsAhead,
					      r.commitsAhead == 1 ? "" : "s");
				else
					SPLog("Update check: running nightly %s, latest nightly is %s (%s).",
					      ShortCommit(mine).c_str(), r.latestCommit.c_str(),
					      r.state == State::UpdateAvailable ? "update available" : "up to date");
				return r;
			}

			Result Fetch() { return IsNightlyBuild() ? FetchNightly() : FetchStable(); }

		public:
			Query(UpdateChecker& owner) : owner(owner) {}

			void Run() override {
				Result r;
				try {
					r = Fetch();
				} catch (const std::exception& ex) {
					SPLog("[!] Update check: %s", ex.what());
					r.state = State::Failed;
				}
				owner.Finish(std::move(r));
			}
		};

		UpdateChecker& UpdateChecker::Get() {
			// Deliberately never destroyed: the worker may still be waiting on the
			// network when the process exits, and must not outlive what it writes to.
			static UpdateChecker* instance = new UpdateChecker();
			return *instance;
		}

		bool UpdateChecker::IsNightlyBuild() {
#ifdef ZEROSPADES_NIGHTLY
			return true;
#else
			return false;
#endif
		}

		std::string UpdateChecker::CurrentNightlyCommit() {
			if (!IsNightlyBuild())
				return {};
			return ShortCommit(ToLower(ZEROSPADES_NIGHTLY_COMMIT));
		}

		const char* UpdateChecker::UpdatePageUrl() {
			return IsNightlyBuild() ? kNightlyPageUrl : kLatestReleasePageUrl;
		}

		void UpdateChecker::Start() {
			{
				std::lock_guard<std::mutex> guard(mutex);
				if (started)
					return;
				started = true;
				result.state = State::Checking;
			}

			auto* query = new Query(*this);
			query->Start();
			query->MarkForAutoDeletion();
		}

		UpdateChecker::Result UpdateChecker::GetResult() {
			std::lock_guard<std::mutex> guard(mutex);
			return result;
		}

		bool UpdateChecker::ClaimStartupPrompt() {
			std::lock_guard<std::mutex> guard(mutex);
			if (startupPromptClaimed || result.state != State::UpdateAvailable)
				return false;
			startupPromptClaimed = true;
			return true;
		}

		void UpdateChecker::ReleaseStartupPrompt() {
			std::lock_guard<std::mutex> guard(mutex);
			startupPromptClaimed = false;
		}

		void UpdateChecker::Finish(Result&& r) {
			std::lock_guard<std::mutex> guard(mutex);
			result = std::move(r);
		}
	} // namespace gui
} // namespace spades
