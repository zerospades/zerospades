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

#include <algorithm>
#include <cmath>
#include <memory>

#include "CreateProfileScreen.h"
#include "MainMenu.h"
#include "MainScreenUI.h"
#include <Client/Fonts.h>
#include <Client/GameMap.h>
#include <Client/IAudioDevice.h>
#include <Client/IImage.h>
#include <Client/IRenderer.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/Settings.h>
#include <Gui/MainScreenHelper.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>

SPADES_SETTING(cg_playerName); // defined in Gui/MainScreen.cpp
DEFINE_SPADES_SETTING(cg_playerNameIsSet, "0");

namespace spades {
	namespace gui {
		MainScreenUI::MainScreenUI(client::IRenderer* renderer, client::IAudioDevice* audioDevice,
		                           client::FontManager* fontManager, MainScreenHelper* helper)
		    : renderer(renderer),
		      audioDevice(audioDevice),
		      fontManager(fontManager),
		      helper(helper) {
			SetupRenderer();

			manager = Handle<ui::UIManager>::New(renderer, audioDevice);
			manager->GetRootElement().SetFont(&fontManager->GetGuiFont());

			// The menu bakes the screen extent into its layout, so a resize has to
			// rebuild it rather than just resize the root.
			manager->screenSizeChanged = [this] { Reload(); };

			Init();

			// Let the new player choose their IGN
			std::string nameStr = cg_playerName;
			if (nameStr != "" && nameStr != "Deuce")
				cg_playerNameIsSet = 1;

			// prompt window if user has an empty name
			if (nameStr == "")
				cg_playerNameIsSet = 0;

			if (!static_cast<bool>(cg_playerNameIsSet)) {
				Handle<CreateProfileScreen> al =
				    Handle<CreateProfileScreen>::New(mainMenu.GetPointerOrNull(), fontManager);
				al->Run();
			}
		}

		MainScreenUI::~MainScreenUI() {}

		void MainScreenUI::Init() {
			mainMenu = Handle<MainScreenMainMenu>::New(this);
			mainMenu->SetBounds(manager->GetRootElement().GetBounds());
			manager->GetRootElement().AddChild(mainMenu.GetPointerOrNull());
		}

		void MainScreenUI::Reload() {
			// Detach every top-level element, not just the menu: modals attach
			// themselves next to their owner (`owner->GetParent()->AddChild`), so a
			// message box, the settings view or the profile prompt is a sibling of the
			// menu. Leaving one behind would orphan it over a menu it can no longer
			// re-enable. A dialog open across a resize is dismissed instead.
			ui::UIElement& root = manager->GetRootElement();
			while (!root.GetChildren().empty())
				root.RemoveChild(root.GetChildren()[0].GetPointerOrNull());

			// Rebuild through the constructor so the new layout is exactly what a fresh
			// launch at this extent would have produced, then restore what survives.
			MainScreenMainMenuState state = mainMenu->GetState();
			Init();
			mainMenu->SetState(state);
		}

		void MainScreenUI::SetupRenderer() {
			// load map
			std::unique_ptr<IStream> stream{FileManager::OpenForReading("Maps/Title.vxl")};
			titleMap.Set(client::GameMap::Load(stream.get()), false);
			renderer->SetGameMap(*titleMap);
			renderer->SetFogColor(MakeVector3(0.1F, 0.1F, 0.1F));
			renderer->SetFogDistance(128.0F);
			time = -1.0F;

			// returned from the client game, so reload the server list.
			if (mainMenu)
				mainMenu->LoadServerList();

			if (manager)
				manager->KeyPanic();
		}

		void MainScreenUI::MouseEvent(float x, float y) { manager->MouseEvent(x, y); }
		void MainScreenUI::WheelEvent(float x, float y) { manager->WheelEvent(x, y); }
		void MainScreenUI::KeyEvent(const std::string& key, bool down) {
			manager->KeyEvent(key, down);
		}
		void MainScreenUI::TextInputEvent(const std::string& text) {
			manager->TextInputEvent(text);
		}
		void MainScreenUI::TextEditingEvent(const std::string& text, int start, int len) {
			manager->TextEditingEvent(text, start, len);
		}

		bool MainScreenUI::AcceptsTextInput() { return manager->AcceptsTextInput(); }
		AABB2 MainScreenUI::GetTextInputRect() { return manager->GetTextInputRect(); }

		client::SceneDefinition MainScreenUI::SetupCamera(client::SceneDefinition sceneDef,
		                                                  Vector3 eye, Vector3 at, Vector3 up,
		                                                  float fov, float ratio) {
			Vector3 dir = (at - eye).Normalize();
			Vector3 side = Vector3::Cross(dir, up).Normalize();
			up = -Vector3::Cross(dir, side);
			sceneDef.viewOrigin = eye;
			sceneDef.viewAxis[0] = side;
			sceneDef.viewAxis[1] = up;
			sceneDef.viewAxis[2] = dir;
			sceneDef.fovY = fov * M_PI_F / 180.0F;
			sceneDef.fovX = 2.0F * std::atan(std::tan(sceneDef.fovY * 0.5F) * ratio);
			return sceneDef;
		}

		void MainScreenUI::RunFrame(float dt) {
			if (time < 0.0F)
				time = 0.0F;

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			client::SceneDefinition sceneDef;
			float cameraX = time;
			cameraX -= std::floor(cameraX / 512.0F) * 512.0F;
			cameraX = 512.0F - cameraX;

			Vector3 orig = MakeVector3(cameraX, 256.0F, 12.0F);
			Vector3 aimAt = MakeVector3(cameraX + 0.1F, 257.0F, 12.5F);
			Vector3 up = MakeVector3(0.0F, 0.0F, -1.0F);

			sceneDef = SetupCamera(sceneDef, orig, aimAt, up, 30.0F, sw / sh);
			sceneDef.zNear = 0.1F;
			sceneDef.zFar = 160.0F;
			sceneDef.time = static_cast<unsigned int>(time * 1000.0F);
			sceneDef.viewportWidth = static_cast<int>(sw);
			sceneDef.viewportHeight = static_cast<int>(sh);
			sceneDef.denyCameraBlur = true;
			sceneDef.depthOfFieldFocalLength = 100.0F;
			sceneDef.skipWorld = false;

			// fade the map
			float fade = Clamp((time - 1.0F) / 2.2F, 0.0F, 1.0F);
			sceneDef.globalBlur = Clamp((1.0F - (time - 1.0F) / 2.5F), 0.0F, 1.0F);
			if (!mainMenu->IsEnabled())
				sceneDef.globalBlur = std::max(sceneDef.globalBlur, 0.5F);

			renderer->StartScene(sceneDef);
			renderer->EndScene();

			// fade the map (draw)
			if (fade < 1.0F) {
				ui::SetColorNP(*renderer, MakeVector4(0.0F, 0.0F, 0.0F, 1.0F - fade));
				renderer->DrawImage(nullptr, AABB2(0.0F, 0.0F, sw, sh));
			}

			// draw title logo
			Handle<client::IImage> img = renderer->RegisterImage("Gfx/Title/Logo.png");
			ui::SetColorNP(*renderer, MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
			renderer->DrawImage(img, MakeVector2((sw - img->GetWidth()) * 0.5F, 64.0F));

			manager->RunFrame(dt);
			manager->Render();

			time += std::min(dt, 0.05F);
		}

		void MainScreenUI::Closing() { shouldExit = true; }

		bool MainScreenUI::WantsToBeClosed() { return shouldExit; }
	} // namespace gui
} // namespace spades
