/*
 Copyright (c) 2013 yvt
 Portion of the code is based on Serverbrowser.cpp.

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include <algorithm>

#include "ChatWindow.h"
#include "ClientUI.h"
#include "ClientUIHelper.h"
#include "IAudioChunk.h"
#include "NetClient.h"
#include "RocketEntity.h"
#include "World.h"
#include <Client/Client.h>
#include <Client/Fonts.h>
#include <Core/Exception.h>
#include <Core/Settings.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/InGame/ChatLogWindow.h>
#include <Gui/UI/InGame/ChatWindow.h>
#include <Gui/UI/InGame/ClientMenu.h>

// Defined in Client_Update.cpp; read by the "/rocket" chat command below.
SPADES_SETTING(cg_rpgRocket);

namespace spades {
	namespace client {
		ClientUI::ClientUI(IRenderer* r, IAudioDevice* a, FontManager* fontManager, Client* client)
		    : renderer(r), audioDevice(a), fontManager(fontManager), client(client) {
			SPADES_MARK_FUNCTION();
			if (!renderer)
				SPInvalidArgument("renderer");
			if (!audioDevice)
				SPInvalidArgument("audioDevice");

			helper = Handle<ClientUIHelper>::New(this);

			manager = Handle<gui::ui::UIManager>::New(renderer.GetPointerOrNull(),
			                                          audioDevice.GetPointerOrNull());
			manager->GetRootElement().SetFont(&fontManager->GetGuiFont());

			// These screens bake the screen extent into their layout, so a resize has
			// to rebuild them rather than just resize the root.
			manager->screenSizeChanged = [this] { ReloadScreens(); };

			// clientMenu is built lazily in EnterClientMenu() so that
			// helper->IsDemoMode() reflects the demo flag, which isn't set until
			// after Client::DoInit runs.
			chatLogWindow = Handle<ChatLogWindow>::New(this);
		}

		ClientUI::~ClientUI() {
			SPADES_MARK_FUNCTION();
			helper->ClientUIDestroyed();
		}

		void ClientUI::SendChat(const std::string& msg, bool isGlobal) {
			if (!client)
				return;

			// "/rocket": local, client-side only command. Spawns a purely
			// cosmetic test RocketEntity from the local player's eye and is
			// never sent to the server. Gated by cg_rpgRocket (DEFINE lives
			// in Client_Update.cpp); the RPG weapon will reuse this entity
			// once it is integrated into the game.
			if (msg == "/rocket") {
				if (!cg_rpgRocket) {
					RecordChatLog("/rocket is disabled (cg_rpgRocket is 0)",
					              Vector4(1.0F, 0.8F, 0.3F, 1.0F));
					return;
				}

				stmp::optional<Player&> maybePlayer;
				if (client->world)
					maybePlayer = client->world->GetLocalPlayer();

				if (maybePlayer && maybePlayer->IsAlive()) {
					Player& p = *maybePlayer;

					// spawn slightly in front of the eye so the rocket
					// doesn't start inside the player's own blocks
					Vector3 muzzle = p.GetEye() + p.GetFront() * 1.2F;

					// flight "woosh" (null chunk when muted: the loop
					// simply never starts)
					Handle<IAudioChunk> woosh;
					if (!client->IsMuted())
						woosh = client->audioDevice->RegisterSound(
						  "Sounds/Misc/woosh.opus");

					client->AddLocalEntity(stmp::make_unique<RocketEntity>(
					  *client, client->audioDevice.GetPointerOrNull(),
					  std::move(woosh), muzzle, p.GetFront()));

					RecordChatLog("rocket launched",
					              Vector4(0.6F, 1.0F, 0.6F, 1.0F));
				} else {
					RecordChatLog("rocket needs a living local player",
					              Vector4(1.0F, 0.5F, 0.5F, 1.0F));
				}
				return;
			}

			client->activeNet->SendChat(msg, isGlobal);
		}

		void ClientUI::AlertNotice(const std::string& msg) {
			if (!client)
				return;
			client->ShowAlert(msg, Client::AlertType::Notice);
		}

		void ClientUI::AlertWarning(const std::string& msg) {
			if (!client)
				return;
			client->ShowAlert(msg, Client::AlertType::Warning);
		}

		void ClientUI::AlertError(const std::string& msg) {
			if (!client)
				return;
			client->ShowAlert(msg, Client::AlertType::Error);
		}

		void ClientUI::ClientDestroyed() { client = NULL; }

		void ClientUI::SetActiveUI(gui::ui::UIElement* value) {
			// The chat panel belongs to the screen that is being replaced, so forget it before
			// that screen can go away. Entering a panel sets it again afterwards.
			chatPanel = nullptr;
			peeking = false;

			if (activeUI)
				manager->GetRootElement().RemoveChild(activeUI.GetPointerOrNull());
			activeUI = value;
			if (activeUI) {
				activeUI->SetBounds(manager->GetRootElement().GetBounds());
				manager->GetRootElement().AddChild(activeUI.GetPointerOrNull());
			}
			manager->KeyPanic();
		}

		void ClientUI::MouseEvent(float x, float y) { manager->MouseEvent(x, y); }
		void ClientUI::WheelEvent(float x, float y) { manager->WheelEvent(x, y); }
		void ClientUI::KeyEvent(const std::string& key, bool down) { manager->KeyEvent(key, down); }
		void ClientUI::TextInputEvent(const std::string& ch) { manager->TextInputEvent(ch); }
		void ClientUI::TextEditingEvent(const std::string& ch, int start, int len) {
			manager->TextEditingEvent(ch, start, len);
		}

		bool ClientUI::AcceptsTextInput() { return manager->AcceptsTextInput(); }
		AABB2 ClientUI::GetTextInputRect() { return manager->GetTextInputRect(); }

		void ClientUI::RunFrame(float dt) {
			SPADES_MARK_FUNCTION();
			if (time < 0.0F)
				time = 0.0F;

			manager->RunFrame(dt);
			if (activeUI) {
				if (peeking) {
					// the peek view has no mouse, so the UI cursor must stay hidden
					activeUI->Render();
				} else {
					manager->Render();
				}
			}

			time += std::min(dt, 0.05F);
		}

		void ClientUI::Closing() {}

		bool ClientUI::WantsClientToBeClosed() { return shouldExit; }
		bool ClientUI::NeedsInput() { return static_cast<bool>(activeUI) && !peeking; }

		void ClientUI::RecordChatLog(const std::string& formatted, Vector4 color) {
			// The log draws text alone, so the chat window's color and image bytes
			// would show up there as stray symbols.
			std::string msg = ChatWindow::StripFormatting(formatted);
			chatLogRecords.emplace_back(msg, color);
			chatLogWindow->Record(msg, color);
			if (chatPanel)
				chatPanel->Record(msg, color);
		}

		void ClientUI::ReloadScreens() {
			// A chat composer is open only while the player is typing; closing it is
			// better than leaving a misplaced one, and the partial message is not worth
			// carrying across a resize.
			CloseUI();

			// Rebuild through the constructors so the new layout is exactly what
			// entering the game at this extent would have produced. The menu is pure
			// chrome and comes back on next open; the log has to be replayed, since it
			// lives nowhere but in the window we just dropped.
			clientMenu = nullptr;
			chatLogWindow = Handle<ChatLogWindow>::New(this);
			for (const auto& record : chatLogRecords)
				chatLogWindow->Record(record.first, record.second);
		}

		bool ClientUI::IsChatEnabled() {
			return !helper->IsDemoMode() && helper->HasLocalPlayer();
		}

		void ClientUI::EnterClientMenu() {
			if (!clientMenu) {
				clientMenu = Handle<ClientMenu>::New(this);
				clientMenu->SetBounds(manager->GetRootElement().GetBounds());
			}
			SetActiveUI(clientMenu.GetPointerOrNull());
		}

		void ClientUI::EnterTeamChatWindow() {
			Handle<ClientChatWindow> wnd =
			    Handle<ClientChatWindow>::New(this, true, ChatWindowMode::Docked);
			SetActiveUI(wnd.GetPointerOrNull());
			chatPanel = wnd.GetPointerOrNull();
			manager->SetActiveElement(wnd->field);
		}

		void ClientUI::EnterGlobalChatWindow() {
			Handle<ClientChatWindow> wnd =
			    Handle<ClientChatWindow>::New(this, false, ChatWindowMode::Docked);
			SetActiveUI(wnd.GetPointerOrNull());
			chatPanel = wnd.GetPointerOrNull();
			manager->SetActiveElement(wnd->field);
		}

		void ClientUI::EnterChatPeek() {
			if (peeking)
				return;
			Handle<ClientChatWindow> wnd =
			    Handle<ClientChatWindow>::New(this, false, ChatWindowMode::Peek);
			SetActiveUI(wnd.GetPointerOrNull());
			chatPanel = wnd.GetPointerOrNull();
			peeking = true;
		}

		void ClientUI::ExitChatPeek() {
			// another screen may have replaced the peek view in the meantime
			if (peeking)
				SetActiveUI(nullptr);
		}

		void ClientUI::EnterChatLogWindow() {
			chatLogWindow->UpdateState(IsChatEnabled());
			chatLogWindow->ScrollToEnd();
			SetActiveUI(chatLogWindow.GetPointerOrNull());
		}

		void ClientUI::CloseUI() { SetActiveUI(nullptr); }

		bool ClientUI::IsIgnored(const std::string& key) {
			return !ignoreInput.empty() && EqualsIgnoringCase(ignoreInput, key);
		}
		void ClientUI::SetIgnored(const std::string& key) { ignoreInput = key; }
	} // namespace client
} // namespace spades
