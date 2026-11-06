#include "../HudMessageBuffer.h"
#include <cassert>
#include <string>
using spades::android::HudMessageBuffer;

int main() {
    HudMessageBuffer buffer;
    std::string value;
    assert(buffer.TakeKills(value) && value.empty());
    assert(!buffer.TakeKills(value));
    buffer.AddChat("hello");
    buffer.AddChat(u8"नमस्ते 😀");
    assert(buffer.TakeChat() == std::string("hello\n") + u8"नमस्ते 😀");
    assert(buffer.TakeChat().empty());
    for (int i = 0; i < 101; ++i) buffer.AddChat(std::to_string(i));
    value = buffer.TakeChat();
    assert(value.substr(0, 2) == "1\n");
    assert(value.substr(value.size() - 3) == "100");
    for (int i = 0; i < 7; ++i) buffer.AddKill("kill\n" + std::to_string(i));
    assert(buffer.TakeKills(value));
    assert(value == "kill 2\nkill 3\nkill 4\nkill 5\nkill 6");
    assert(!buffer.TakeKills(value));
    buffer.AddChat("old server");
    buffer.Reset();
    assert(buffer.TakeChat().empty());
    assert(buffer.TakeKills(value) && value.empty());
}
