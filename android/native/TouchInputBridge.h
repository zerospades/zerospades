#pragma once
#include <string>
#include <cstdint>
namespace spades { namespace gui { class View; } }
namespace spades { namespace android {
// Call exclusively from the SDL/game thread; producers run on the Android UI thread.
void StartTouchInput();
void DrainTouchInput(gui::View& view);
// Release frame-spanning jump and short-fire taps only after a real game update.
void AdvanceTouchInputAfterFrame();
// Gather numeric game state on the SDL thread; Java receives an owned copy.
void PublishTouchHud(gui::View& view);
struct HudMapLayout { float x, y, size; };
HudMapLayout GetTouchMapLayout();
// Called on the game thread for real, filtered inbound chat lines.
void RecordTouchChat(const std::string& text);
void RecordTouchPlayerChat(const std::string& prefix, const std::string& name,
                           const std::string& suffix, std::uint32_t color);
void RecordTouchKill(const std::string& text);
void StopTouchInput(gui::View& view);
} }
