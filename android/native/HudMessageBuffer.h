#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>

namespace spades { namespace android {

// Game and UI threads serialize access externally. Chat uses a length-prefixed
// UTF-8 record so names, Unicode, newlines and server-supplied text cannot be
// confused with formatting delimiters. Color is 0xRRGGBB.
class HudMessageBuffer {
public:
    struct Chat { std::string prefix, name, suffix; std::uint32_t rgb; };
    void Reset() { chat.clear(); kills.clear(); killsChanged = true; }
    void AddChat(const std::string& line) { AddPlayerChat("", "", line, 0xffffff); }
    void AddPlayerChat(const std::string& prefix, const std::string& name,
                       const std::string& suffix, std::uint32_t rgb) {
        if (prefix.empty() && name.empty() && suffix.empty()) return;
        if (chat.size() == 100) chat.pop_front();
        chat.push_back({prefix.substr(0, 512), name.substr(0, 1024), suffix.substr(0, 4096), rgb & 0xffffff});
    }
    void AddKill(const std::string& line) {
        if (line.empty()) return;
        std::string clean = line.substr(0, 1024);
        std::replace(clean.begin(), clean.end(), '\n', ' ');
        std::replace(clean.begin(), clean.end(), '\r', ' ');
        if (kills.size() == 5) kills.pop_front();
        kills.push_back(std::move(clean)); killsChanged = true;
    }
    std::string TakeChat() {
        std::string result;
        for (const auto& line : chat) {
            AppendInt(result, line.rgb);
            AppendInt(result, static_cast<std::uint32_t>(line.prefix.size()));
            AppendInt(result, static_cast<std::uint32_t>(line.name.size()));
            AppendInt(result, static_cast<std::uint32_t>(line.suffix.size()));
            result += line.prefix; result += line.name; result += line.suffix;
        }
        chat.clear(); return result;
    }
    bool TakeKills(std::string& result) {
        if (!killsChanged) return false;
        result = Join(kills); killsChanged = false; return true;
    }
private:
    static void AppendInt(std::string& out, std::uint32_t n) {
        out.push_back(static_cast<char>((n >> 24) & 255)); out.push_back(static_cast<char>((n >> 16) & 255));
        out.push_back(static_cast<char>((n >> 8) & 255)); out.push_back(static_cast<char>(n & 255));
    }
    static std::string Join(const std::deque<std::string>& lines) {
        std::string result;
        for (const auto& line : lines) { if (!result.empty()) result += '\n'; result += line; }
        return result;
    }
    std::deque<Chat> chat;
    std::deque<std::string> kills;
    bool killsChanged = true;
};
} }
