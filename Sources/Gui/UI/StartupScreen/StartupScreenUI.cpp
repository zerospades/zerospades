/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

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

#include "StartupMainMenu.h"
#include "StartupScreenUI.h"
#include <Client/Fonts.h>
#include <Client/IAudioDevice.h>
#include <Client/IImage.h>
#include <Client/IRenderer.h>
#include <Gui/StartupScreenHelper.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>

namespace spades {
	namespace gui {
		StartupScreenUI::StartupScreenUI(client::IRenderer* renderer,
		                                 client::IAudioDevice* audioDevice,
		                                 client::FontManager* fontManager,
		                                 StartupScreenHelper* helper)
		    : renderer(renderer),
		      audioDevice(audioDevice),
		      fontManager(fontManager),
		      helper(helper) {
			SetupRenderer();

			manager = Handle<ui::UIManager>::New(renderer, audioDevice);
			manager->GetRootElement().SetFont(&fontManager->GetGuiFont());
			Init();
		}

		StartupScreenUI::~StartupScreenUI() {}

		void StartupScreenUI::Init() {
			mainMenu = Handle<StartupScreenMainMenu>::New(this);
			mainMenu->SetBounds(manager->GetRootElement().GetBounds());
			manager->GetRootElement().AddChild(mainMenu.GetPointerOrNull());
		}

		void StartupScreenUI::Reload() {
			// Delete StartupScreenMainMenu
			ui::UIElement& root = manager->GetRootElement();
			if (!root.GetChildren().empty())
				root.RemoveChild(root.GetChildren()[0].GetPointerOrNull());

			// Reload entire the startup screen while
			// preserving the state as much as possible
			StartupScreenMainMenuState state = mainMenu->GetState();
			Init();
			mainMenu->SetState(state);
		}

		void StartupScreenUI::SetupRenderer() {
			if (manager)
				manager->KeyPanic();
		}

		void StartupScreenUI::MouseEvent(float x, float y) { manager->MouseEvent(x, y); }
		void StartupScreenUI::WheelEvent(float x, float y) { manager->WheelEvent(x, y); }
		void StartupScreenUI::KeyEvent(const std::string& key, bool down) {
			manager->KeyEvent(key, down);
		}
		void StartupScreenUI::TextInputEvent(const std::string& text) {
			manager->TextInputEvent(text);
		}
		void StartupScreenUI::TextEditingEvent(const std::string& text, int start, int len) {
			manager->TextEditingEvent(text, start, len);
		}

		bool StartupScreenUI::AcceptsTextInput() { return manager->AcceptsTextInput(); }
		AABB2 StartupScreenUI::GetTextInputRect() { return manager->GetTextInputRect(); }

		void StartupScreenUI::RunFrame(float dt) {
			ui::SetColorNP(*renderer, MakeVector4(0.0F, 0.0F, 0.0F, 1.0F));
			renderer->DrawImage(
			    nullptr, AABB2(0.0F, 0.0F, renderer->ScreenWidth(), renderer->ScreenHeight()));

			float fade = Clamp(manager->time * 2.0F, 0.0F, 1.0F);

			// draw title logo
			Handle<client::IImage> img = renderer->RegisterImage("Gfx/Title/LogoSmall.png");
			ui::SetColorNP(*renderer, MakeVector4(1.0F, 1.0F, 1.0F, 1.0F * fade));
			renderer->DrawImage(img, AABB2(10.0F - img->GetWidth() * (1.0F - fade), 2.0F,
			                               img->GetWidth(), img->GetHeight()));

			manager->RunFrame(dt);
			manager->Render();
		}

		void StartupScreenUI::RunFrameLate(float dt) {
			(void)dt;
			renderer->FrameDone();
			renderer->Flip();
		}

		void StartupScreenUI::Closing() { shouldExit = true; }
		bool StartupScreenUI::WantsToBeClosed() { return shouldExit; }
	} // namespace gui
} // namespace spades
