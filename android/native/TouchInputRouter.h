#pragma once

#include <array>
#include <functional>
#include <map>
#include <string>

namespace spades { namespace android {

// IDs are part of the JNI contract. Do not use Java enum ordinals here.
enum class TouchAction { Pause = 0, Chat = 1, Fire = 2, Aim = 3, Reload = 4,
                         NextTool = 5, Grenade = 6, Sprint = 7, Crouch = 8, Jump = 9,
                         Loadout = 10, CaptureColor = 11, PieMenu = 12, Flashlight = 13 };

// Dispatches on the game thread only. The callbacks must outlive the router.
class TouchInputRouter {
public:
    using Key = std::function<void(const std::string&, bool)>;
    using Lookup = std::function<std::string(const char*)>;
    using Look = std::function<void(float, float)>;
    TouchInputRouter(Key key, Lookup lookup, Look look);
    void Move(float x, float y);
    void LookDelta(float x, float y);
    void Button(TouchAction action, bool down);
    void Reset();
private:
    void Set(int slot, const char* setting, bool down);
    void ReleaseAll();
    Key key;
    Lookup lookup;
    Look look;
    std::array<std::string, 16> slots{};
    std::map<std::string, unsigned> heldKeys;
};

} } // namespace spades::android
