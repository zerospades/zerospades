/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <Client/Fonts.h>
#include <Core/Math.h>
#include <Gui/UI/Components/IModalMenu.h>
#include <Gui/UI/Framework/UIManager.h>

namespace spades {
	namespace client {
		class IAudioDevice;
		class IRenderer;
	} // namespace client
	namespace gui {
		class SoftwareCursor;

		/**
		 * Runs retained `spades::ui` widgets inside a view that has no UI manager of
		 * its own and navigates with a software cursor (the KV6 editor, and any
		 * future editor built the same way).
		 *
		 * While a dialog is attached the host is "active": the owning view routes
		 * input here instead of acting on it, and lets the host draw the pointer,
		 * so hovering a text field shows the usual I-beam. It implements
		 * `IModalMenu`, so a view can treat it like its Esc menu.
		 */
		class UIOverlayHost : public IModalMenu {
		public:
			UIOverlayHost(client::IRenderer& renderer, client::IAudioDevice* audioDevice,
			              client::FontManager& fontManager, SoftwareCursor& cursor);
			~UIOverlayHost();

			ui::UIManager& GetUIManager() { return *manager; }

			/**
			 * Attaches `dialog` (sized to the screen) and starts routing input to it.
			 * Dialogs detach themselves when they close, which ends the takeover.
			 */
			void Show(ui::UIElement* dialog);

			// --- IModalMenu ---
			/** True while any dialog is attached. */
			bool IsActive() const override;
			/** Nothing to open on its own; `Show` starts a dialog. */
			void Open() override {}
			/** Detaches every dialog (e.g. when the document closes). */
			void Close() override;
			/** Consumes the event while active; returns false when idle. */
			bool KeyEvent(const std::string& key, bool down) override;
			void TextInputEvent(const std::string& text) override;
			bool AcceptsTextInput() const override;
			/** Draws the dialogs and the pointer. */
			void Draw() override;

			void TextEditingEvent(const std::string& text, int start, int len);
			AABB2 GetTextInputRect() const;
			/** Consumes the wheel while active. */
			bool WheelEvent(float x, float y);
			/** Keeps the widgets' pointer on the software cursor. Call once a frame,
			 *  after the cursor has moved. */
			void RunFrame(float dt);

		private:
			Handle<ui::UIManager> manager;
			Handle<client::FontManager> fontManager;
			SoftwareCursor& cursor;
		};
	} // namespace gui
} // namespace spades
