#include "../QuickGrenade.h"
#include <cassert>
#include <vector>
using spades::android::QuickGrenade;

int main() {
    using Action = QuickGrenade::Action;
    std::vector<Action> events;
    QuickGrenade quick([&](Action action) { events.push_back(action); });
    auto expect = [&](std::vector<Action> wanted) { assert(events == wanted); events.clear(); };
    QuickGrenade::State s;
    quick.Start(s); expect({});
    s.valid = true; s.grenades = 1; s.actualFire = true;
    quick.Start(s); quick.Start(s); expect({Action::ReleaseControls});
    quick.Step(s); expect({}); // wait for previously held fire to release
    s.actualFire = false;
    quick.Step(s); expect({Action::SelectGrenade});
    s.grenadeSelected = true;
    quick.Step(s); expect({}); // switch/sprint/cooldown in progress
    s.ready = true;
    quick.Step(s); expect({Action::PressFire});
    quick.Step(s); expect({}); // no rising edge accepted yet
    s.actualFire = true;
    quick.Step(s); expect({Action::ReleaseFire});
    quick.Step(s); expect({}); // wait for throw's falling edge
    s.actualFire = false;
    quick.Step(s); expect({Action::RestoreTool});
    assert(!quick.Active());

    quick.Start(s); quick.Step(s); quick.Step(s); expect({Action::ReleaseControls, Action::SelectGrenade, Action::PressFire});
    quick.Cancel(true); expect({Action::CancelTool, Action::ReleaseFire});
    assert(!quick.Active());
    quick.Start(s); quick.Step(s); expect({Action::ReleaseControls, Action::SelectGrenade});
    s.valid = false;
    quick.Step(s); expect({Action::ReleaseFire});
    assert(!quick.Active());
    s.valid = true; s.grenades = 0;
    quick.Start(s); expect({});
    s.grenades = 1; s.grenadeSelected = true; s.actualFire = true;
    quick.Start(s); expect({Action::ReleaseControls});
    assert(!quick.Active()); // release the cooked grenade, do not throw another
}
