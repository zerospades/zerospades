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

#include "UI/KV6Editor/KV6EditorView.h"
#include "MainScreen.h"
#include "MainScreenHelper.h"
#include <Client/Client.h>
#include <Client/Fonts.h>
#include <Client/IAudioDevice.h>
#include <Client/IRenderer.h>
#include <Core/Exception.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Gui/UI/MainScreen/MainScreenUI.h>
#include <ScriptBindings/ScriptManager.h>

DEFINE_SPADES_SETTING(cg_playerName, "Deuce");

namespace spades {
	namespace gui {
		MainScreen::MainScreen(Handle<client::IRenderer> _renderer,
		                       Handle<client::IAudioDevice> _audioDevice,
		                       Handle<client::FontManager> _fontManager)
		    : renderer(std::move(_renderer)),
		      audioDevice(std::move(_audioDevice)),
		      fontManager(std::move(_fontManager)) {
			SPADES_MARK_FUNCTION();
			if (!renderer)
				SPInvalidArgument("renderer");
			if (!audioDevice)
				SPInvalidArgument("audioDevice");
			if (!fontManager)
				SPInvalidArgument("fontManager");

			helper = new MainScreenHelper(this);

			// first call to RunFrame tends to have larger dt value.
			// so this value is set in the first call.
			timeToStartInitialization = 1000.0F;
		}

		MainScreen::~MainScreen() {
			SPADES_MARK_FUNCTION();
			helper->MainScreenDestroyed();
		}

		// Restores renderer's state (game map, fog color)
		// after returning from the game client.
		void MainScreen::RestoreRenderer() {
			if (ui)
				ui->SetupRenderer();
		}

		std::string MainScreen::OpenKV6Editor(const std::string& path, bool isNew,
		                                      SoftwareCursor* cursor) {
			try {
				subview = Handle<KV6EditorView>::New(&*renderer, &*audioDevice, &*fontManager,
				                                     cursor, path, isNew)
				            .Cast<View>();
			} catch (const std::exception& ex) {
				SPLog("[!] Error while opening the KV6 editor: %s", ex.what());
				return ex.what();
			}
			return "";
		}

		bool MainScreen::NeedsAbsoluteMouseCoordinate() {
			SPADES_MARK_FUNCTION();
			if (subview)
				return subview->NeedsAbsoluteMouseCoordinate();

			return true;
		}

		void MainScreen::MouseEvent(float x, float y) {
			SPADES_MARK_FUNCTION();
			if (subview) {
				subview->MouseEvent(x, y);
				return;
			}
			if (!ui)
				return;

			ui->MouseEvent(x, y);
		}

		void MainScreen::WheelEvent(float x, float y) {
			SPADES_MARK_FUNCTION();
			if (subview) {
				subview->WheelEvent(x, y);
				return;
			}
			if (!ui)
				return;

			ui->WheelEvent(x, y);
		}

		void MainScreen::KeyEvent(const std::string& key, bool down) {
			SPADES_MARK_FUNCTION();
			if (subview) {
				subview->KeyEvent(key, down);
				return;
			}
			if (!ui)
				return;
			ui->KeyEvent(key, down);
		}

		void MainScreen::TextInputEvent(const std::string& ch) {
			SPADES_MARK_FUNCTION();
			if (subview) {
				subview->TextInputEvent(ch);
				return;
			}
			if (!ui)
				return;
			ui->TextInputEvent(ch);
		}

		void MainScreen::TextEditingEvent(const std::string& ch, int start, int len) {
			SPADES_MARK_FUNCTION();
			if (subview) {
				subview->TextEditingEvent(ch, start, len);
				return;
			}
			if (!ui)
				return;
			ui->TextEditingEvent(ch, start, len);
		}

		bool MainScreen::AcceptsTextInput() {
			SPADES_MARK_FUNCTION();
			if (subview)
				return subview->AcceptsTextInput();
			if (!ui)
				return false;
			return ui->AcceptsTextInput();
		}

		AABB2 MainScreen::GetTextInputRect() {
			SPADES_MARK_FUNCTION();
			if (subview)
				return subview->GetTextInputRect();
			if (!ui)
				return AABB2();
			return ui->GetTextInputRect();
		}

		bool MainScreen::WantsToBeClosed() {
			SPADES_MARK_FUNCTION();
			if (!ui)
				return false;
			return ui->WantsToBeClosed();
		}

		void MainScreen::DrawStartupScreen() {
			SPADES_MARK_FUNCTION();
			Handle<client::IImage> img;
			Vector2 pos, size;

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			img = renderer->RegisterImage("Gfx/White.tga");
			renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 1));
			renderer->DrawImage(img, AABB2(0, 0, sw, sh));

			img = renderer->RegisterImage("Gfx/Title/Logo.png");
			size = {img->GetWidth(), img->GetHeight()};
			size *= std::min(1.0F, sw / size.x);
			size *= std::min(1.0F, sh / size.y);
			pos = (MakeVector2(sw, sh) - size) * 0.5F;
			renderer->SetColorAlphaPremultiplied(MakeVector4(1, 1, 1, 1));
			renderer->DrawImage(img, AABB2(pos.x, pos.y, size.x, size.y));

			std::string str = _Tr("MainScreen", "NOW LOADING");
			client::IFont& font = fontManager->GetGuiFont();
			size = font.Measure(str);
			pos = MakeVector2(sw - 16.0F, sh - 16.0F);
			pos -= size;
			font.DrawShadow(str, pos, 1.0F, MakeVector4(1, 1, 1, 1), MakeVector4(0, 0, 0, 0.5));
		}

		void MainScreen::RunFrame(float dt) {
			SPADES_MARK_FUNCTION();
			if (subview) {
				try {
					subview->RunFrame(dt);
					return;
				} catch (const std::exception& ex) {
					SPLog("[!] Error while running a game client: %s", ex.what());
					subview->Closing();
					subview = NULL;
					RestoreRenderer();
					helper->errorMessage = ex.what();
				}
			}

			if (timeToStartInitialization > 100.0F)
				timeToStartInitialization = 0.2F;
			if (timeToStartInitialization > 0.0F) {
				DrawStartupScreen();

				timeToStartInitialization -= dt;
				if (timeToStartInitialization <= 0.0F) {
					DoInit(); // do init
				} else {
					return;
				}
			}

			helper->Update();

			// `DoInit` either sets `ui` or throws, so this should not fire. Guard it
			// anyway, so that an init path which ever leaves `ui` unset stops here
			// instead of dereferencing null.
			if (!ui)
				return;

			ui->RunFrame(dt);
		}

		// Runs after the runner has presented the frame: this is where the game client
		// is torn down, so the renderer state it leaves behind is never swapped in.
		void MainScreen::RunFrameLate(float dt) {
			SPADES_MARK_FUNCTION();
			if (!subview)
				return;

			try {
				subview->RunFrameLate(dt);
				if (!subview->WantsToBeClosed())
					return;

				subview->Closing();
				subview = NULL;
				RestoreRenderer();
			} catch (const std::exception& ex) {
				SPLog("[!] Error while running a game client: %s", ex.what());
				subview->Closing();
				subview = NULL;
				RestoreRenderer();
				helper->errorMessage = ex.what();
			}
		}

		void MainScreen::DoInit() {
			SPADES_MARK_FUNCTION();
			try {
				renderer->Init();

				ui = Handle<MainScreenUI>::New(renderer.GetPointerOrNull(),
				                               audioDevice.GetPointerOrNull(),
				                               fontManager.GetPointerOrNull(),
				                               helper.GetPointerOrNull());
			} catch (const std::exception& ex) {
				// Let the failure reach the top-level handler, but name the phase in
				// SystemMessages.log first: a failure here leaves the startup screen as
				// the last presented frame, which is otherwise hard to tell apart from
				// a hang.
				SPLog("[!] Failed to initialize the main screen UI: %s", ex.what());
				throw;
			}
		}

		void MainScreen::Closing() {
			SPADES_MARK_FUNCTION();
			if (subview) {
				subview->Closing();
				subview = NULL;
			}

			if (!ui)
				return;

			ui->Closing();
		}

		bool MainScreen::ExecCommand(const Handle<ConsoleCommand>& cmd) {
			SPADES_MARK_FUNCTION();
			if (subview)
				return subview->ExecCommand(cmd);

			return View::ExecCommand(cmd);
		}

		Handle<ConsoleCommandCandidateIterator>
		MainScreen::AutocompleteCommandName(const std::string& name) {
			SPADES_MARK_FUNCTION();
			if (subview) {
				return subview->AutocompleteCommandName(name);
			}
			return View::AutocompleteCommandName(name);
		}

		std::string MainScreen::Connect(const ServerAddress& host) {
			try {
				subview = Handle<client::Client>::New(&*renderer, &*audioDevice, host, fontManager)
				            .Cast<View>();
			} catch (const std::exception& ex) {
				SPLog("[!] Error while initializing a game client: %s", ex.what());
				return ex.what();
			}
			return "";
		}

		std::string MainScreen::PlayDemo(const std::string& demoPath) {
			try {
				subview = Handle<client::Client>::New(&*renderer, &*audioDevice,
				              ServerAddress(), fontManager, demoPath)
				            .Cast<View>();
			} catch (const std::exception& ex) {
				SPLog("[!] Error starting demo playback: %s", ex.what());
				return ex.what();
			}
			return "";
		}
	} // namespace gui
} // namespace spades
