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
#ifdef __ANDROID__
#include "../../android/native/AndroidCurlTrust.h"
#endif
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

			Result Fetch() {
				Result r;
				r.state = State::Failed;

				std::unique_ptr<CURL, CURLEasyDeleter> handle{curl_easy_init()};
				if (!handle) {
					SPLog("[!] Update check: failed to create a cURL handle.");
					return r;
				}
#ifdef __ANDROID__
				CURLcode trust = android::ConfigureCurlTrust(handle.get());
				if (trust != CURLE_OK) {
					SPLog("[!] Update check: cannot configure Android trust: %s", curl_easy_strerror(trust));
					return r;
				}
#endif

				// GitHub rejects API requests that carry no User-Agent.
				std::unique_ptr<curl_slist, CURLSlistDeleter> headers{
				  curl_slist_append(nullptr, "Accept: application/vnd.github+json")};

				size_t (*writeCallback)(void*, size_t, size_t, Query*) = &Query::Write;
				curl_easy_setopt(handle.get(), CURLOPT_URL, kLatestReleaseUrl);
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
					SPLog("[!] Update check: request failed: %s", curl_easy_strerror(rc));
					return r;
				}

				Json::Reader reader;
				Json::Value root;
				if (!reader.parse(buffer, root, false) || !root.isObject() ||
				    !root["tag_name"].isString()) {
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
