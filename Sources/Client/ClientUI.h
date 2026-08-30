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

#pragma once

#include <utility>
#include <vector>

#include <Client/IAudioDevice.h>
#include <Client/IRenderer.h>
#include <Core/RefCountedObject.h>
#include <Gui/UI/Framework/IGameModeUI.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/FieldWithHistory.h>

namespace spades {
	namespace client {
		class FontManager;
		class Client;
		class ClientUIHelper;
		class ClientMenu;
		class ChatLogWindow;

		class ClientUI : public gui::IGameModeUI {
			friend class ClientUIHelper;
			friend class ClientMenu;
			friend class ChatLogWindow;
			friend class ClientChatWindow;

			Handle<IRenderer> renderer;
			Handle<IAudioDevice> audioDevice;
			Handle<FontManager> fontManager;
			Handle<ClientUIHelper> helper;

			Handle<gui::ui::UIManager> manager;
			Handle<gui::ui::UIElement> activeUI;

			Handle<ChatLogWindow> chatLogWindow;
			Handle<ClientMenu> clientMenu;

			// The chat log only exists inside `chatLogWindow`, which a screen resize
			// has to rebuild. Kept here so the rebuilt window can be replayed into.
			std::vector<std::pair<std::string, Vector4>> chatLogRecords;

			std::vector<gui::ui::CommandHistoryItem> chatHistory;

			bool shouldExit = false;
			float time = -1.0F;

			// weak reference
			Client* client;
			std::string ignoreInput;

			void SendChat(const std::string&, bool isGlobal);

			/** Rebuilds the in-game screens after the screen extent changed. */
			void ReloadScreens();

			void AlertNotice(const std::string&);
			void AlertWarning(const std::string&);
			void AlertError(const std::string&);

			void SetActiveUI(gui::ui::UIElement* value);

		protected:
			~ClientUI();

		public:
			ClientUI(IRenderer*, IAudioDevice*, FontManager* font, Client* client);
			void ClientDestroyed();

			client::IRenderer* GetRenderer() override { return &*renderer; }
			client::IAudioDevice* GetAudioDevice() override { return &*audioDevice; }
			FontManager& GetFontManager() override { return *fontManager; }
			ClientUIHelper& GetHelper() { return *helper; }
			gui::ui::UIManager& GetUIManager() override { return *manager; }
			std::vector<gui::ui::CommandHistoryItem>& GetChatHistory() { return chatHistory; }

			void MouseEvent(float x, float y) override;
			void WheelEvent(float x, float y) override;
			void KeyEvent(const std::string&, bool down) override;
			void TextInputEvent(const std::string&) override;
			void TextEditingEvent(const std::string&, int start, int len);
			bool AcceptsTextInput() override;
			AABB2 GetTextInputRect() override;
			bool NeedsAbsoluteMouseCoordinate() override { return true; }

			void RunFrame(float dt) override;
			void Closing() override;

			bool WantsClientToBeClosed();
			bool WantsToClose() override { return WantsClientToBeClosed(); }
			bool NeedsInput();

			void RecordChatLog(const std::string&, Vector4 col = {1, 1, 1, 0.8F});

			bool IsChatEnabled();

			void EnterClientMenu();
			void EnterGlobalChatWindow();
			void EnterTeamChatWindow();
			void EnterChatLogWindow();
			void CloseUI();

			// lm: so the chat does not have the initial chat key
			bool IsIgnored(const std::string& key);
			void SetIgnored(const std::string& key);
		};
	} // namespace client
} // namespace spades
