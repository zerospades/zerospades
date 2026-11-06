#pragma once
#include <functional>
#include <utility>

namespace spades { namespace android {
// Preserve a press/release pair that arrives between game updates. A later
// physical press cancels a deferred release; reset/menu transitions release it.
class FrameHeldButton {
public:
    explicit FrameHeldButton(std::function<void(bool)> dispatch) : dispatch(std::move(dispatch)) {}
    void Down() {
        deferredRelease = false;
        if (!held) { held = true; pressedSinceFrame = true; dispatch(true); }
    }
    void Up() {
        if (!held) return;
        if (pressedSinceFrame) deferredRelease = true;
        else Release();
    }
    void AfterFrame() {
        pressedSinceFrame = false;
        if (deferredRelease) Release();
    }
    void Cancel() { if (held) Release(); else { pressedSinceFrame = false; deferredRelease = false; } }
private:
    void Release() {
        held = false; pressedSinceFrame = false; deferredRelease = false;
        dispatch(false);
    }
    std::function<void(bool)> dispatch;
    bool held = false, pressedSinceFrame = false, deferredRelease = false;
};
} }
