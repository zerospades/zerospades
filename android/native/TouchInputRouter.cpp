#include "TouchInputRouter.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace spades { namespace android {

namespace {
std::string CanonicalKey(std::string key) {
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return key;
}
}

TouchInputRouter::TouchInputRouter(Key key, Lookup lookup, Look look)
    : key(std::move(key)), lookup(std::move(lookup)), look(std::move(look)) {}

void TouchInputRouter::Set(int slot, const char* setting, bool down) {
    auto& stored = slots[slot];
    if (down) {
        if (!stored.empty()) return;
        std::string binding = setting ? lookup(setting) : "Escape";
        // Client::CheckKey compares bindings case-insensitively.
        std::string canonical = CanonicalKey(binding);
        if (canonical == "space" || canonical == "spacebar" || canonical == "spacekey") {
            binding = " ";
            canonical = " ";
        }
        if (binding.empty()) return;
        stored = binding;
        if (heldKeys[canonical]++ == 0) key(binding, true);
    } else if (!stored.empty()) {
        auto binding = std::move(stored);
        stored.clear();
        auto found = heldKeys.find(CanonicalKey(binding));
        if (found != heldKeys.end() && --found->second == 0) {
            heldKeys.erase(found);
            key(binding, false);
        }
    }
}

void TouchInputRouter::Move(float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) { x = 0; y = 0; }
    // The game currently exposes digital movement only. Release opposite
    // directions before pressing a new one to avoid a one-frame reversal.
    bool directions[] = {x < -0.25f, x > 0.25f, y < -0.25f, y > 0.25f};
    const char* names[] = {"cg_keyMoveLeft", "cg_keyMoveRight", "cg_keyMoveForward", "cg_keyMoveBackward"};
    for (int i = 0; i < 4; ++i) if (!directions[i]) Set(i, names[i], false);
    for (int i = 0; i < 4; ++i) if (directions[i]) Set(i, names[i], true);
}
void TouchInputRouter::LookDelta(float x, float y) {
    if (std::isfinite(x) && std::isfinite(y)) look(x, y);
}
void TouchInputRouter::Button(TouchAction action, bool down) {
    switch (action) {
        case TouchAction::Pause:
        case TouchAction::Chat:
            // Do not leave movement/fire held when opening a menu.
            if (down) ReleaseAll();
            Set(action == TouchAction::Pause ? 4 : 5,
                action == TouchAction::Pause ? nullptr : "cg_keyGlobalChat", down);
            break;
        case TouchAction::Fire: Set(6, "cg_keyAttack", down); break;
        case TouchAction::Aim: Set(7, "cg_keyAltAttack", down); break;
        case TouchAction::Reload: Set(8, "cg_keyReloadWeapon", down); break;
        case TouchAction::Sprint: Set(9, "cg_keySprint", down); break;
        case TouchAction::Crouch: Set(10, "cg_keyCrouch", down); break;
        case TouchAction::Jump: Set(11, "cg_keyJump", down); break;
        case TouchAction::PieMenu: Set(14, "cg_keyPieMenu", down); break;
        case TouchAction::Flashlight: Set(15, "cg_keyFlashlight", down); break;
        case TouchAction::CaptureColor: Set(13, "cg_keyCaptureColor", down); break;
        case TouchAction::Loadout:
            // Like a menu key: release movement/fire before opening limbo.
            if (down) ReleaseAll();
            Set(12, "cg_keyLimbo", down);
            break;
        case TouchAction::NextTool:
            if (down) { key("AndroidNextTool", true); key("AndroidNextTool", false); }
            break;
        // Quick-throw requires a game-frame state machine for the switch delay
        // and grenade cook/release; do not silently turn it into "select grenade".
        case TouchAction::Grenade: break;
    }
}
void TouchInputRouter::ReleaseAll() {
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) Set(i, nullptr, false);
}
void TouchInputRouter::Reset() { ReleaseAll(); }

} } // namespace spades::android
