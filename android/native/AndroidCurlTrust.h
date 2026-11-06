#pragma once
#ifdef __ANDROID__
#include <SDL_system.h>
#include <curl/curl.h>
#include <string>

namespace spades { namespace android {
// CURLOPT_CAINFO is copied by libcurl. Verification and hostname checks remain
// enabled; an absent/unreadable bundle fails the HTTPS request, not open.
inline CURLcode ConfigureCurlTrust(CURL* handle) {
    const char* root = SDL_AndroidGetInternalStoragePath();
    if (!root || !*root) return CURLE_SSL_CACERT_BADFILE;
    const std::string path = std::string(root) + "/system-ca-bundle.pem";
    return curl_easy_setopt(handle, CURLOPT_CAINFO, path.c_str());
}
} }
#endif
