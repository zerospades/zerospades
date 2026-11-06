#include "../TouchInputRouter.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>
using namespace spades::android;

int main() {
    std::map<std::string, std::string> bindings = {
        {"cg_keyMoveLeft", "a"}, {"cg_keyMoveRight", "d"},
        {"cg_keyMoveForward", "w"}, {"cg_keyMoveBackward", "s"},
        {"cg_keyAttack", "LeftMouseButton"}, {"cg_keyAltAttack", "RightMouseButton"},
        {"cg_keyReloadWeapon", "r"}, {"cg_keySprint", "Shift"},
        {"cg_keyCrouch", "Control"}, {"cg_keyGlobalChat", "t"}, {"cg_keyJump", "Space"},
        {"cg_keyLimbo", "l"}, {"cg_keyCaptureColor", "e"}
    };
    std::vector<std::string> events;
    float dx = 0, dy = 0;
    TouchInputRouter router(
        [&](const std::string& key, bool down) { events.push_back(key + (down ? "+" : "-")); },
        [&](const char* name) { return bindings[name]; },
        [&](float x, float y) { dx += x; dy += y; });
    auto expect = [&](std::vector<std::string> wanted) { assert(events == wanted); events.clear(); };

    router.Move(-1, -1); expect({"a+", "w+"});
    router.Move(1, 0); expect({"a-", "w-", "d+"});
    router.Move(0, 0); expect({"d-"});
    router.Button(TouchAction::Fire, true);
    bindings["cg_keyAttack"] = "f"; // release the key that was actually pressed
    router.Button(TouchAction::Fire, false); expect({"LeftMouseButton+", "LeftMouseButton-"});
    router.Button(TouchAction::Fire, true); router.Move(0, -1);
    router.Button(TouchAction::Chat, true);
    expect({"f+", "w+", "w-", "f-", "t+"});
    router.Button(TouchAction::Chat, false); expect({"t-"});
    bindings["cg_keyAttack"] = "W"; // two logical actions share one binding
    router.Move(0, -1); router.Button(TouchAction::Fire, true);
    router.Move(0, 0); router.Button(TouchAction::Fire, false);
    expect({"w+", "W-"});
    router.Button(TouchAction::Aim, true); router.Reset();
    expect({"RightMouseButton+", "RightMouseButton-"});
    router.LookDelta(4, -2); router.LookDelta(std::numeric_limits<float>::infinity(), 0);
    assert(dx == 4 && dy == -2);
    router.Button(TouchAction::Jump, true); router.Button(TouchAction::Jump, false);
    expect({" +", " -"}); // the game's default Space binding uses a literal space
    router.Move(0, -1); router.Button(TouchAction::Fire, true);
    router.Button(TouchAction::Loadout, true); router.Button(TouchAction::Loadout, false);
    expect({"w+", "W-", "l+", "l-"}); // movement and fire currently share W
    router.Button(TouchAction::CaptureColor, true); router.Button(TouchAction::CaptureColor, false);
    expect({"e+", "e-"});
    router.Button(TouchAction::Grenade, true); expect({}); // quick-throw is handled by the bridge
    router.Button(TouchAction::NextTool, true); router.Button(TouchAction::NextTool, false);
    expect({"AndroidNextTool+", "AndroidNextTool-"});
}
