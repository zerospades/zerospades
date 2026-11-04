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

#include <functional>
#include <string>
#include <vector>

#include <Core/Math.h>
#include <Gui/UI/Framework/FeedbackSounds.h>

namespace spades {
	namespace client {
		class IAudioDevice;
		class IFont;
		class IRenderer;
	} // namespace client
	namespace gui {
		struct ContextMenuItem {
			std::string caption;
			std::function<void()> run;
			/** False draws it greyed out, and it does nothing. */
			bool enabled = true;
			/** A line above it, setting it apart from the items before. */
			bool separated = false;
		};

		/**
		 * A menu of commands opened at the cursor by a right press, over
		 * everything but modal dialogs. While it is open it owns the pointer
		 * and the keyboard: a click on an item runs it, a click anywhere else
		 * or Escape closes it, and the arrows and Enter choose from it.
		 *
		 * The host routes input to it first while it is open, and draws it
		 * last.
		 */
		class ContextMenu {
		public:
			ContextMenu(client::IRenderer& renderer, client::IFont& font,
			            client::IAudioDevice* audioDevice);

			/** Opens at `at` (kept on screen) with `items`. */
			void Open(const Vector2& at, std::vector<ContextMenuItem> items);
			void Close();
			bool IsOpen() const { return open; }

			/** A press of either button: one off the menu closes it. */
			void Press(const Vector2& at);
			/** A left release on the item the press was on runs it. */
			void Release(const Vector2& at);
			/** A key down while open: Up, Down, Enter, Escape; all are taken. */
			void Key(const std::string& key);

			void Draw(const Vector2& cursor);

		private:
			client::IRenderer& renderer;
			client::IFont& font;
			ui::FeedbackSounds sounds;
			std::vector<ContextMenuItem> items;
			bool open = false;
			float x = 0.0F, y = 0.0F, width = 0.0F, height = 0.0F;
			int highlighted = -1;
			int pressed = -1;
			Vector2 lastCursor = MakeVector2(-1.0F, -1.0F);

			float ItemTop(int index) const;
			int ItemAt(const Vector2& at) const;
			void Run(int index);
			void Step(int direction);
		};
	} // namespace gui
} // namespace spades
