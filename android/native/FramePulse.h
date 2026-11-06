#pragma once
#include <functional>
#include <utility>

namespace spades { namespace android {
// A fast tap can begin and end between two game frames. Keep the press down
// through the next actual frame, then release it; cancel on lifecycle changes.
class FramePulse {
public:
    explicit FramePulse(std::function<void(bool)> dispatch) : dispatch(std::move(dispatch)) {}
    void Press() { if (!active) { active = true; dispatch(true); } }
    void AfterFrame() { Cancel(); }
    void Cancel() { if (active) { active = false; dispatch(false); } }
private:
    std::function<void(bool)> dispatch;
    bool active = false;
};
} }
