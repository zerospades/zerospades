#include "../FramePulse.h"
#include <cassert>
#include <vector>
using spades::android::FramePulse;
int main() {
    std::vector<bool> events;
    FramePulse pulse([&](bool pressed) { events.push_back(pressed); });
    pulse.Press(); pulse.Press();
    assert(events == std::vector<bool>({true}));
    // No release before a game frame, including when a UI down/up is coalesced.
    pulse.AfterFrame();
    assert(events == std::vector<bool>({true, false}));
    pulse.AfterFrame();
    assert(events.size() == 2);
    pulse.Press(); pulse.Cancel(); pulse.AfterFrame();
    assert(events == std::vector<bool>({true, false, true, false}));
}
