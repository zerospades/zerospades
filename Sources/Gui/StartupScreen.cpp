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

namespace spades {
	namespace gui {
		StartupScreen::StartupScreen(Handle<client::IRenderer> r, Handle<client::IAudioDevice> a,
		                             StartupScreenHelper* helper,
		                             Handle<client::FontManager> _fontManager)
		    : renderer(std::move(r)),
		      audioDevice(std::move(a)),
		      fontManager(std::move(_fontManager)),
		      startRequested(false),
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
			return ui->GetTextInputRect();
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
			renderer->Init();

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

		void StartupScreen::Start() { startRequested = true; }

		void StartupScreen::Run() {
			SDL_InitSubSystem(SDL_INIT_VIDEO);

			auto* helper = new StartupScreenHelper();
			helper->ExamineSystem();

			class ConcreteRunner : public SDLRunner {
				Handle<StartupScreen> view;
				StartupScreenHelper* helper;

			protected:
				auto GetRendererType() -> RendererType override { return RendererType::SW; }
				client::IAudioDevice* CreateAudioDevice() override {
					return new audio::NullDevice();
				}
				View* CreateView(client::IRenderer* renderer, client::IAudioDevice* dev) override {
					auto fontManager = Handle<client::FontManager>::New(renderer);
					view = Handle<StartupScreen>::New(renderer, dev, helper, fontManager);
					return Handle<StartupScreen>{view}.Unmanage();
				}

			public:
				ConcreteRunner(StartupScreenHelper* helper) : helper(helper) {}
				bool RunAndGetStartFlag() {
					this->Run(800, 480);
					return view->startRequested;
				}
			};

			bool startFlag;
			{
				ConcreteRunner runner(helper);
				runner.SetHasSystemMenu(true);
				startFlag = runner.RunAndGetStartFlag();
			}

			if (startFlag)
				spades::StartMainScreen();
		}
	} // namespace gui
} // namespace spades
