#include "../FrameHeldButton.h"
#include <cassert>
#include <vector>
using spades::android::FrameHeldButton;
int main() {
    std::vector<bool> events;
    FrameHeldButton fire([&](bool down) { events.push_back(down); });
    fire.Down(); fire.Up();
    assert(events == std::vector<bool>({true}));
    fire.AfterFrame();
    assert(events == std::vector<bool>({true, false}));
    fire.Down(); fire.AfterFrame(); fire.Up();
    assert(events == std::vector<bool>({true, false, true, false}));
    fire.Down(); fire.Up(); fire.Down(); fire.AfterFrame();
    assert(events == std::vector<bool>({true, false, true, false, true}));
    fire.Up();
    assert(events == std::vector<bool>({true, false, true, false, true, false}));
    fire.Down(); fire.Up(); fire.Cancel(); fire.AfterFrame();
    assert(events == std::vector<bool>({true, false, true, false, true, false, true, false}));
}
