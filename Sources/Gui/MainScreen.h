/*
 Copyright (c) 2013 yvt

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
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include "View.h"
#include <Client/IAudioDevice.h>
#include <Client/IRenderer.h>
#include <Core/RefCountedObject.h>
#include <Core/ServerAddress.h>

namespace spades {
	namespace client {
		class FontManager;
	}
	namespace gui {
		class MainScreenHelper;
		class MainScreenUI;
		class SoftwareCursor;
		class MainScreen : public View {
			friend class MainScreenHelper;
			Handle<client::IRenderer> renderer;
			Handle<client::IAudioDevice> audioDevice;
			Handle<View> subview;
			Handle<client::FontManager> fontManager;
			float timeToStartInitialization;

			Handle<MainScreenHelper> helper;
			Handle<MainScreenUI> ui;

			void DrawStartupScreen();
			void DoInit();
			/** Opens a model in the editor, reporting a failure the way the
			 *  main screen reports any other. */
			void OpenModelFile(const std::string& path);
			/** A model to open once the UI exists; empty once it has been. */
			std::string pendingModelPath;

			void RestoreRenderer();

			std::string Connect(const ServerAddress &host);
		std::string PlayDemo(const std::string &demoPath);
			std::string OpenKV6Editor(const std::string &path, bool isNew, SoftwareCursor* cursor = nullptr);

		protected:
			~MainScreen();

		public:
			/** `openModelPath`, when given, is a model to open in the editor as soon
			 *  as the screen is ready, instead of showing the menus. */
			MainScreen(Handle<client::IRenderer>, Handle<client::IAudioDevice>,
			           Handle<client::FontManager>,
			           const std::string& openModelPath = std::string());

			client::IRenderer *GetRenderer() { return &*renderer; }
			client::IAudioDevice *GetAudioDevice() { return &*audioDevice; }

			void MouseEvent(float x, float y) override;
			void KeyEvent(const std::string &, bool down) override;
			void TextInputEvent(const std::string &) override;
			void TextEditingEvent(const std::string &, int start, int len) override;
			bool AcceptsTextInput() override;
			AABB2 GetTextInputRect() override;
			void WheelEvent(float x, float y) override;
			bool NeedsAbsoluteMouseCoordinate() override;

			void RunFrame(float dt) override;
			void RunFrameLate(float dt) override;

			void Closing() override;
			bool WantsToBeClosed() override;

			bool ExecCommand(const Handle<ConsoleCommand> &) override;
			Handle<ConsoleCommandCandidateIterator>
			AutocompleteCommandName(const std::string &name) override;
		};
	} // namespace gui
} // namespace spades
