#include "TouchInputBridge.h"
#include "TouchInputRouter.h"
#include "HudMessageBuffer.h"
#include "QuickGrenade.h"
#include "FramePulse.h"
#include "FrameHeldButton.h"
#include <Core/Settings.h>
#include <Gui/View.h>
#include <Client/Client.h>
#include <Client/World.h>
#include <Client/CTFGameMode.h>
#include <Client/TCGameMode.h>
#include <Client/Weapon.h>
#include <jni.h>
#include <array>
#include <memory>
#include <mutex>
#include <vector>

namespace spades { namespace android {
namespace {
    enum class Type { Move, Look, Button, Reset, Chat, Palette };
    struct Event {
        Type type; float x = 0, y = 0; int action = 0; bool down = false;
        std::string text;
        const void* eventOwner = nullptr; // comparison only; never dereferenced
    };
    std::mutex mutex;
    std::vector<Event> pending;
    HudMessageBuffer messages;
    // client, world, gameplay, scores[2], limit, health, tool, ammo, reserve,
    // blocks, grenades, fps, weapon type. Only plain values cross threads.
    std::array<jint, 14> hud{};
    const void* hudOwner = nullptr;
    bool accepting = false;
    std::unique_ptr<TouchInputRouter> router;
    std::unique_ptr<QuickGrenade> quickGrenade;
    std::unique_ptr<FramePulse> jumpPulse;
    std::unique_ptr<FrameHeldButton> fireButton;
    client::Client* quickClient = nullptr;
    client::World* quickWorld = nullptr;
    client::Player* quickPlayer = nullptr;
    client::Player::ToolType originalTool = client::Player::ToolSpade;

    // The captured identities are compared, never dereferenced after a world change.
    client::Client* CurrentQuickClient(gui::View& view) {
        auto* current = dynamic_cast<client::Client*>(view.GetTouchGameView());
        if (!current || current != quickClient || current->GetWorld() != quickWorld ||
            !current->AndroidHasLivePlayer()) return nullptr;
        auto player = current->GetWorld()->GetLocalPlayer();
        if (!player || &player.value() != quickPlayer) return nullptr;
        return current;
    }
    QuickGrenade::State GetQuickState(gui::View& view) {
        QuickGrenade::State state;
        auto* current = CurrentQuickClient(view);
        if (!current || view.NeedsAbsoluteMouseCoordinate()) return state;
        auto player = current->GetWorld()->GetLocalPlayer();
        state.valid = true;
        state.grenadeSelected = player->GetTool() == client::Player::ToolGrenade;
        state.grenades = player->GetNumGrenades();
        state.actualFire = player->GetWeaponInput().primary;
        state.ready = state.grenadeSelected && current->AndroidToolReady();
        return state;
    }
    void Enqueue(Event event) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!accepting) return;
        if (event.type == Type::Chat || event.type == Type::Palette) event.eventOwner = hudOwner;
        // Overflow drops stale actions and releases held keys rather than sticking them.
        if (pending.size() >= 1024) { pending.clear(); pending.push_back({Type::Reset}); }
        pending.push_back(event);
    }
}
void StartTouchInput() {
    std::lock_guard<std::mutex> lock(mutex);
    pending.clear(); messages.Reset(); hud.fill(0); hudOwner = nullptr; accepting = true;
}
void RecordTouchChat(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex);
    if (accepting) messages.AddChat(text);
}
void RecordTouchKill(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex);
    if (accepting) messages.AddKill(text);
}
void PublishTouchHud(gui::View& view) {
    std::array<jint, 14> next{};
    auto* client = dynamic_cast<client::Client*>(view.GetTouchGameView());
    if (client) {
        next[0] = 1;
        client::World* world = client->GetWorld();
        if (world) {
            next[1] = 1;
            auto mode = world->GetMode();
            if (mode && mode->ModeType() == client::IGameMode::m_CTF) {
                if (auto* ctf = dynamic_cast<client::CTFGameMode*>(mode.get_pointer())) {
                    next[3] = static_cast<jint>(ctf->GetTeam(0).score);
                    next[4] = static_cast<jint>(ctf->GetTeam(1).score);
                    next[5] = ctf->GetCaptureLimit();
                }
            } else if (mode && mode->ModeType() == client::IGameMode::m_TC) {
                if (auto* tc = dynamic_cast<client::TCGameMode*>(mode.get_pointer())) {
                    next[5] = tc->GetNumTerritories();
                    for (int i = 0; i < next[5]; ++i) {
                        const int owner = tc->GetTerritory(i).ownerTeamId;
                        if (owner == 0 || owner == 1) ++next[3 + owner];
                    }
                }
            }
            auto player = world->GetLocalPlayer();
            if (player) {
                next[2] = client->AndroidHasLivePlayer() && !view.NeedsAbsoluteMouseCoordinate();
                next[6] = player->GetHealth();
                next[7] = static_cast<jint>(player->GetTool());
                next[8] = player->GetWeapon().GetAmmo();
                next[9] = player->GetWeapon().GetStock();
                next[10] = player->GetNumBlocks();
                next[11] = player->GetNumGrenades();
                next[12] = client->AndroidFramesPerSecond();
                next[13] = static_cast<jint>(player->GetWeaponType());
            }
        }
    }
    std::lock_guard<std::mutex> lock(mutex);
    if (accepting) {
        if (hudOwner != client) messages.Reset();
        hudOwner = client;
        hud = next;
    }
}
void DrainTouchInput(gui::View& view) {
    if (!router) {
        router.reset(new TouchInputRouter(
            [&view](const std::string& key, bool down) { view.KeyEvent(key, down); },
            [](const char* name) { return std::string(Settings::ItemHandle(name, nullptr)); },
            [&view](float x, float y) { view.MouseEvent(x, y); }));
        jumpPulse.reset(new FramePulse([](bool down) { router->Button(TouchAction::Jump, down); }));
        fireButton.reset(new FrameHeldButton([](bool down) { router->Button(TouchAction::Fire, down); }));
        quickGrenade.reset(new QuickGrenade([&view](QuickGrenade::Action action) {
            using Action = QuickGrenade::Action;
            switch (action) {
                case Action::ReleaseControls: jumpPulse->Cancel(); fireButton->Cancel(); router->Reset(); break;
                case Action::PressFire: router->Button(TouchAction::Fire, true); break;
                case Action::ReleaseFire: router->Button(TouchAction::Fire, false); break;
                case Action::SelectGrenade:
                    if (auto* current = CurrentQuickClient(view))
                        current->AndroidSelectTool(client::Player::ToolGrenade);
                    break;
                case Action::RestoreTool:
                    if (auto* current = CurrentQuickClient(view)) {
                        if (originalTool != client::Player::ToolGrenade)
                            current->AndroidSelectTool(originalTool);
                    }
                    break;
                case Action::CancelTool:
                    if (auto* current = CurrentQuickClient(view)) {
                        auto fallback = originalTool == client::Player::ToolGrenade
                                            ? client::Player::ToolSpade : originalTool;
                        if (!current->AndroidSelectTool(fallback))
                            current->AndroidSelectTool(client::Player::ToolSpade);
                    }
                    break;
            }
        }));
    }
    std::vector<Event> events;
    { std::lock_guard<std::mutex> lock(mutex); events.swap(pending); }
    if (quickGrenade->Active() && (!CurrentQuickClient(view) || view.NeedsAbsoluteMouseCoordinate()))
        quickGrenade->Cancel(CurrentQuickClient(view) != nullptr);
    if (view.NeedsAbsoluteMouseCoordinate()) { jumpPulse->Cancel(); fireButton->Cancel(); router->Reset(); }
    for (const auto& event : events) {
        // Do not turn the camera or press gameplay keys while a menu/text input
        // owns the cursor. Reset is still processed to release held inputs.
        bool gameplay = !view.NeedsAbsoluteMouseCoordinate();
        if (!gameplay && event.type != Type::Reset && event.type != Type::Chat &&
            !(event.type == Type::Button && event.action == static_cast<int>(TouchAction::Pause))) {
            jumpPulse->Cancel();
            fireButton->Cancel();
            router->Reset();
            continue;
        }
        switch (event.type) {
            case Type::Move: router->Move(event.x, event.y); break;
            case Type::Look: router->LookDelta(event.x, event.y); break;
            case Type::Button:
                if (event.action < 0 || event.action > 11) break;
                if (event.action == static_cast<int>(TouchAction::Grenade)) {
                    if (event.down && !quickGrenade->Active()) {
                        auto* current = dynamic_cast<client::Client*>(view.GetTouchGameView());
                        if (current && current->AndroidHasLivePlayer()) {
                            auto player = current->GetWorld()->GetLocalPlayer();
                            quickClient = current;
                            quickWorld = current->GetWorld();
                            quickPlayer = &player.value();
                            originalTool = player->GetTool();
                            quickGrenade->Start(GetQuickState(view));
                        }
                    }
                    break;
                }
                if (quickGrenade->Active()) {
                    if (event.action == static_cast<int>(TouchAction::Pause)) {
                        if (event.down) quickGrenade->Cancel(CurrentQuickClient(view) != nullptr);
                    } else if (event.action != static_cast<int>(TouchAction::Crouch)) {
                        // Don't let fire, aim, sprint, reload or a tool change steal
                        // the fire edge while the grenade is being switched/thrown.
                        break;
                    }
                }
                if (event.action == static_cast<int>(TouchAction::Jump)) {
                    if (event.down) jumpPulse->Press();
                } else if (event.action == static_cast<int>(TouchAction::Fire)) {
                    if (event.down) fireButton->Down(); else fireButton->Up();
                } else {
                    if (event.down && (event.action == static_cast<int>(TouchAction::Pause) ||
                                       event.action == static_cast<int>(TouchAction::Loadout))) {
                        jumpPulse->Cancel(); fireButton->Cancel();
                    }
                    router->Button(static_cast<TouchAction>(event.action), event.down);
                }
                break;
            case Type::Reset:
                if (quickGrenade->Active()) quickGrenade->Cancel(CurrentQuickClient(view) != nullptr);
                jumpPulse->Cancel();
                fireButton->Cancel();
                router->Reset();
                break;
            case Type::Palette:
                if (!quickGrenade->Active()) {
                    if (auto* client = dynamic_cast<client::Client*>(view.GetTouchGameView()))
                        if (client == event.eventOwner) client->AndroidSelectPalettePoint(event.x, event.y);
                }
                break;
            case Type::Chat:
                if (auto* client = dynamic_cast<client::Client*>(view.GetTouchGameView())) {
                    if (client != event.eventOwner || !client->SendAndroidChat(event.text, event.down))
                        RecordTouchChat("Message not sent: no active server connection.");
                }
                break;
        }
    }
    if (quickGrenade->Active()) quickGrenade->Step(GetQuickState(view));
}
void AdvanceTouchInputAfterFrame() {
    if (jumpPulse) jumpPulse->AfterFrame();
    if (fireButton) fireButton->AfterFrame();
}
void StopTouchInput(gui::View& view) {
    { std::lock_guard<std::mutex> lock(mutex); accepting = false; pending.clear(); messages.Reset(); hud.fill(0); hudOwner = nullptr; }
    if (quickGrenade) {
        quickGrenade->Cancel(CurrentQuickClient(view) != nullptr);
        quickGrenade.reset();
    }
    if (jumpPulse) { jumpPulse->Cancel(); jumpPulse.reset(); }
    if (fireButton) { fireButton->Cancel(); fireButton.reset(); }
    if (router) { router->Reset(); router.reset(); }
    quickClient = nullptr; quickWorld = nullptr; quickPlayer = nullptr;
}
} }

extern "C" JNIEXPORT jintArray JNICALL
Java_io_github_zerospades_mobile_GameActivity_nativeReadHud(JNIEnv* env, jclass) {
    std::array<jint, 14> snapshot;
    {
        std::lock_guard<std::mutex> lock(spades::android::mutex);
        snapshot = spades::android::hud;
    }
    jintArray result = env->NewIntArray(static_cast<jsize>(snapshot.size()));
    if (result) env->SetIntArrayRegion(result, 0, static_cast<jsize>(snapshot.size()), snapshot.data());
    return result;
}

// Byte arrays carry standard UTF-8 rather than JNI's modified UTF-8.
extern "C" JNIEXPORT void JNICALL
Java_io_github_zerospades_mobile_GameActivity_nativeSendChat(JNIEnv* env, jclass, jbyteArray bytes, jboolean global) {
    if (!bytes) return;
    const jsize size = env->GetArrayLength(bytes);
    if (size <= 0 || size > 800) return;
    std::string text(static_cast<size_t>(size), '\0');
    env->GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte*>(&text[0]));
    if (env->ExceptionCheck()) return;
    spades::android::Event event{spades::android::Type::Chat};
    event.text = std::move(text);
    event.down = global == JNI_TRUE;
    spades::android::Enqueue(std::move(event));
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_io_github_zerospades_mobile_GameActivity_nativeTakeChat(JNIEnv* env, jclass) {
    std::string result;
    {
        std::lock_guard<std::mutex> lock(spades::android::mutex);
        result = spades::android::messages.TakeChat();
    }
    jbyteArray bytes = env->NewByteArray(static_cast<jsize>(result.size()));
    if (bytes && !result.empty())
        env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(result.size()),
                                reinterpret_cast<const jbyte*>(result.data()));
    return bytes;
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_io_github_zerospades_mobile_GameActivity_nativeTakeKillfeed(JNIEnv* env, jclass) {
    std::string result;
    {
        std::lock_guard<std::mutex> lock(spades::android::mutex);
        if (!spades::android::messages.TakeKills(result)) return nullptr;
    }
    jbyteArray bytes = env->NewByteArray(static_cast<jsize>(result.size()));
    if (bytes && !result.empty())
        env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(result.size()),
                                reinterpret_cast<const jbyte*>(result.data()));
    return bytes;
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_zerospades_mobile_hud_NativeTouchSink_nativeMove(JNIEnv*, jclass, jfloat x, jfloat y) {
    spades::android::Enqueue({spades::android::Type::Move, x, y});
}
extern "C" JNIEXPORT void JNICALL
Java_io_github_zerospades_mobile_hud_NativeTouchSink_nativeLook(JNIEnv*, jclass, jfloat x, jfloat y) {
    spades::android::Enqueue({spades::android::Type::Look, x, y});
}
extern "C" JNIEXPORT void JNICALL
Java_io_github_zerospades_mobile_hud_NativeTouchSink_nativePalette(JNIEnv*, jclass, jfloat x, jfloat y) {
    spades::android::Enqueue({spades::android::Type::Palette, x, y});
}
extern "C" JNIEXPORT void JNICALL
Java_io_github_zerospades_mobile_hud_NativeTouchSink_nativeButton(JNIEnv*, jclass, jint action, jboolean down) {
    spades::android::Enqueue({spades::android::Type::Button, 0, 0, action, down == JNI_TRUE});
}
extern "C" JNIEXPORT void JNICALL
Java_io_github_zerospades_mobile_hud_NativeTouchSink_nativeReset(JNIEnv*, jclass) {
    spades::android::Enqueue({spades::android::Type::Reset});
}
