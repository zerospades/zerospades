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

#include "StartupScreen.h"

#include "StartupScreenHelper.h"
#include <Audio/NullDevice.h>
#include <Client/Client.h>
#include <Client/Fonts.h>
#include <Core/Exception.h>
#include <Core/Settings.h>
#include <Gui/Main.h>
#include <Gui/SDLRunner.h>
#include <Gui/UI/StartupScreen/StartupScreenUI.h>
#ifdef __ANDROID__
#include "StartupScaledRenderer.h"
#endif

namespace spades {
	namespace gui {
		StartupScreen::StartupScreen(Handle<client::IRenderer> r, Handle<client::IAudioDevice> a,
		                             StartupScreenHelper* helper,
		                             Handle<client::FontManager> _fontManager, bool* startResult)
		    : renderer(std::move(r)),
		      audioDevice(std::move(a)),
		      fontManager(std::move(_fontManager)),
		      startRequested(startResult),
		      helper(helper) {
			SPADES_MARK_FUNCTION();
			if (!renderer)
				SPInvalidArgument("renderer");
			if (!audioDevice)
				SPInvalidArgument("audioDevice");
			if (!fontManager)
				SPInvalidArgument("fontManager");

			helper->BindStartupScreen(this);

			DoInit();
		}

		StartupScreen::~StartupScreen() {
			SPADES_MARK_FUNCTION();
			helper->StartupScreenDestroyed();
		}

		// Restores renderer's state (game map, fog color)
		// after returning from the game client.
		void StartupScreen::RestoreRenderer() {
			if (ui)
				ui->SetupRenderer();
		}

		bool StartupScreen::NeedsAbsoluteMouseCoordinate() {
			SPADES_MARK_FUNCTION();
			return true;
		}

		void StartupScreen::MouseEvent(float x, float y) {
			SPADES_MARK_FUNCTION();
			if (!ui)
				return;
#ifdef __ANDROID__
			if (auto* scaled = dynamic_cast<StartupScaledRenderer*>(renderer.GetPointerOrNull())) {
				const float scale = scaled->GetScale();
				x /= scale;
				y /= scale;
			}
#endif
			ui->MouseEvent(x, y);
		}

		void StartupScreen::WheelEvent(float x, float y) {
			SPADES_MARK_FUNCTION();
			if (!ui)
				return;
			ui->WheelEvent(x, y);
		}

		void StartupScreen::KeyEvent(const std::string& key, bool down) {
			SPADES_MARK_FUNCTION();
			if (!ui)
				return;
			ui->KeyEvent(key, down);
		}

		void StartupScreen::TextInputEvent(const std::string& ch) {
			SPADES_MARK_FUNCTION();
			if (!ui)
				return;
			ui->TextInputEvent(ch);
		}

		void StartupScreen::TextEditingEvent(const std::string& ch, int start, int len) {
			SPADES_MARK_FUNCTION();
			if (!ui)
				return;
			ui->TextEditingEvent(ch, start, len);
		}

		bool StartupScreen::AcceptsTextInput() {
			SPADES_MARK_FUNCTION();
			if (!ui)
				return false;
			return ui->AcceptsTextInput();
		}

		AABB2 StartupScreen::GetTextInputRect() {
			SPADES_MARK_FUNCTION();
			if (!ui)
				return AABB2();
			auto rect = ui->GetTextInputRect();
#ifdef __ANDROID__
			if (auto* scaled = dynamic_cast<StartupScaledRenderer*>(renderer.GetPointerOrNull())) {
				const float scale = scaled->GetScale();
				return AABB2(rect.min * scale, rect.max * scale);
			}
#endif
			return rect;
		}

		bool StartupScreen::WantsToBeClosed() {
			SPADES_MARK_FUNCTION();
			if (!ui)
				return false;
			return ui->WantsToBeClosed();
		}

		void StartupScreen::RunFrame(float dt) {
			SPADES_MARK_FUNCTION();
			if (!ui)
				return;
			ui->RunFrame(dt);
		}

		void StartupScreen::DoInit() {
			SPADES_MARK_FUNCTION();
#ifndef __ANDROID__
			renderer->Init();
#else
			// This screen only draws 2D UI. Do not preload the game world,
			// shaders and water twice before the user can choose settings.
#endif

			ui = Handle<StartupScreenUI>::New(renderer.GetPointerOrNull(),
			                                  audioDevice.GetPointerOrNull(),
			                                  fontManager.GetPointerOrNull(),
			                                  helper.GetPointerOrNull());
		}

		void StartupScreen::Closing() {
			SPADES_MARK_FUNCTION();
			if (!ui)
				return;
			ui->Closing();
		}

		void StartupScreen::Start() { *startRequested = true; }

		void StartupScreen::Run() {
			SDL_InitSubSystem(SDL_INIT_VIDEO);

			auto helper = Handle<StartupScreenHelper>::New();
			helper->ExamineSystem();

			class ConcreteRunner : public SDLRunner {
				bool startRequested = false;
				StartupScreenHelper* helper;

			protected:
				auto GetRendererType() -> RendererType override {
#ifdef __ANDROID__
					// SDL on Android owns one native surface; use the same GLES
					// renderer as the game instead of assuming a desktop SW surface.
					return RendererType::GL;
#else
					return RendererType::SW;
#endif
				}
				client::IAudioDevice* CreateAudioDevice() override {
					return new audio::NullDevice();
				}
				View* CreateView(client::IRenderer* renderer, client::IAudioDevice* dev) override {
					Handle<client::IRenderer> uiRenderer(renderer);
#ifdef __ANDROID__
					uiRenderer = Handle<StartupScaledRenderer>::New(renderer).Cast<client::IRenderer>();
#endif
					auto fontManager = Handle<client::FontManager>::New(uiRenderer.GetPointerOrNull());
					// Release the view while the renderer/context is still alive;
					// the old runner-held Handle outlived SDL_Quit().
					return Handle<StartupScreen>::New(uiRenderer, dev, helper, fontManager,
					                                  &startRequested).Unmanage();
				}

			public:
				ConcreteRunner(StartupScreenHelper* helper) : helper(helper) {}
				bool RunAndGetStartFlag() {
					this->Run(800, 480);
					return startRequested;
				}
			};

			bool startFlag;
			{
				ConcreteRunner runner(helper.GetPointerOrNull());
				runner.SetHasSystemMenu(true);
				startFlag = runner.RunAndGetStartFlag();
			}

			if (startFlag)
				spades::StartMainScreen();
		}
	} // namespace gui
} // namespace spades
