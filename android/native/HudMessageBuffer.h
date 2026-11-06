#pragma once
#include <algorithm>
#include <deque>
#include <string>
#include <utility>

namespace spades { namespace android {

// Game and UI threads serialize access externally. Inputs are UTF-8 byte strings;
// the JNI boundary transfers them as bytes rather than modified UTF-8.
class HudMessageBuffer {
public:
    void Reset() { chat.clear(); kills.clear(); killsChanged = true; }
    void AddChat(const std::string& line) {
        if (line.empty()) return;
        if (chat.size() == 100) chat.pop_front();
        chat.push_back(line.substr(0, 4096));
    }
    void AddKill(const std::string& line) {
        if (line.empty()) return;
        std::string clean = line.substr(0, 1024);
        std::replace(clean.begin(), clean.end(), '\n', ' ');
        std::replace(clean.begin(), clean.end(), '\r', ' ');
        if (kills.size() == 5) kills.pop_front();
        kills.push_back(std::move(clean));
        killsChanged = true;
    }
    std::string TakeChat() {
        std::string result = Join(chat);
        chat.clear();
        return result;
    }
    // Returns false if the consumer already has the current killfeed.
    bool TakeKills(std::string& result) {
        if (!killsChanged) return false;
        result = Join(kills);
        killsChanged = false;
        return true;
    }
private:
    static std::string Join(const std::deque<std::string>& lines) {
        std::string result;
        for (const auto& line : lines) {
            if (!result.empty()) result += '\n';
            result += line;
        }
        return result;
    }
    std::deque<std::string> chat, kills;
    bool killsChanged = true;
};

} }
