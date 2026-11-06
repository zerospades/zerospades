#pragma once
#include <functional>

namespace spades { namespace android {

// Drives existing game key/tool APIs across *game frames*. It never creates a
// grenade or sends a packet itself. All methods run on the SDL/game thread.
class QuickGrenade {
public:
    enum class Action { ReleaseControls, SelectGrenade, PressFire, ReleaseFire, RestoreTool, CancelTool };
    struct State {
        bool valid = false;        // same live player/world, gameplay owns input
        bool grenadeSelected = false;
        bool ready = false;        // tool switch/sprint/weapon cooldown completed
        bool actualFire = false;   // input accepted by Player in a previous frame
        int grenades = 0;
    };
    using Dispatch = std::function<void(Action)>;
    explicit QuickGrenade(Dispatch dispatch) : dispatch(dispatch) {}
    bool Active() const { return phase != Phase::Idle; }
    void Start(const State& state) {
        if (Active() || !state.valid || state.grenades <= 0) return;
        dispatch(Action::ReleaseControls);
        // If a grenade was already being cooked by the fire button, releasing
        // those controls will throw it. Do not queue a second grenade.
        if (state.grenadeSelected && state.actualFire) return;
        phase = Phase::WaitReleased;
    }
    void Step(const State& state) {
        if (!Active()) return;
        if (!state.valid) { Cancel(false); return; }
        if (phase != Phase::WaitReleased && !state.grenadeSelected) {
            Cancel(false); // the player/tool changed outside this sequence
            return;
        }
        switch (phase) {
            case Phase::Idle: break;
            case Phase::WaitReleased:
                if (state.grenades <= 0) { Cancel(false); break; }
                // A held gun trigger must first reach Player as released, or it
                // would mask the grenade's rising edge and fail to cook it.
                if (!state.actualFire) {
                    dispatch(Action::SelectGrenade);
                    phase = Phase::WaitReady;
                }
                break;
            case Phase::WaitReady:
                if (state.grenades <= 0) { Cancel(true); break; }
                if (state.ready && !state.actualFire) {
                    dispatch(Action::PressFire);
                    phase = Phase::WaitCook;
                }
                break;
            case Phase::WaitCook:
                if (state.grenades <= 0) { Cancel(true); break; }
                if (state.actualFire) {
                    // The rising edge has reached Player and started cooking.
                    dispatch(Action::ReleaseFire);
                    phase = Phase::WaitThrow;
                }
                break;
            case Phase::WaitThrow:
                if (!state.actualFire) {
                    dispatch(Action::RestoreTool);
                    phase = Phase::Idle;
                }
                break;
        }
    }
    void Cancel(bool canSwitchTool) {
        if (!Active()) return;
        // Switching before releasing an accepted grenade avoids throwing when
        // a menu/lifecycle transition cancels a pending action.
        if (canSwitchTool && phase != Phase::WaitReleased)
            dispatch(Action::CancelTool);
        dispatch(Action::ReleaseFire);
        phase = Phase::Idle;
    }
private:
    enum class Phase { Idle, WaitReleased, WaitReady, WaitCook, WaitThrow };
    Dispatch dispatch;
    Phase phase = Phase::Idle;
};

} }
