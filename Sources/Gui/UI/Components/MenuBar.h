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

#include <string>
#include <vector>

#include <Core/Math.h>
#include <Gui/UI/Framework/FeedbackSounds.h>

#include "MenuBarItems.h"

namespace spades {
	namespace client {
		class FontManager;
		class IAudioDevice;
		class IFont;
		class IRenderer;
	} // namespace client
	namespace gui {
		/**
		 * The editor's menu bar: File, Edit and View along its ribbon, each
		 * dropping a list of commands with the keys that do the same.
		 *
		 * The commands live here rather than behind Escape because a modal list
		 * covering the model is a list that has to be dismissed before the
		 * model can be seen again, and because a menu bar does not grow down
		 * the screen as the editor learns new commands.
		 *
		 * Its host supplies the menus, rebuilt every time one is opened, so
		 * what is offered and what is greyed out are always current. The bar
		 * holds no editor policy: it draws what it is given and runs what is
		 * clicked.
		 *
		 * It is drawn in two parts, because a dropped menu hangs over the bars
		 * below it: the titles belong in the ribbon, with everything else, and
		 * the open menu belongs over the lot.
		 */
		class MenuBar {
		public:
			MenuBar(IMenuBarHost& host, client::IAudioDevice* audioDevice = nullptr);

			/** Places the titles from `x` along a ribbon `height` tall, and
			 *  answers how wide they came to: what the ribbon may use next. */
			float Layout(client::IFont& font, float x, float height);
			float Width() const { return width; }

			bool IsOpen() const { return open >= 0; }
			void Close();

			/** Acts on a press at `p`; false when it was on nothing of ours. */
			bool Click(const Vector2& p);
			/** The pointer moved: a title hovered while a menu is open opens
			 *  that one instead, as a menu bar does everywhere. */
			void MouseMove(const Vector2& p);
			/** A key while a menu is open, which then owns the keyboard; false
			 *  when none is open. */
			bool KeyEvent(const std::string& key, bool down);

			/** The titles, in the ribbon. */
			void DrawTitles(client::IRenderer& renderer, client::FontManager& fontManager,
			                const Vector2& cursor);
			/** The open menu, which hangs over the bars and the view: drawn
			 *  after them, though it answers for its presses before them. */
			void DrawOpenMenu(client::IRenderer& renderer, client::FontManager& fontManager,
			                  const Vector2& cursor);

		private:
			IMenuBarHost& host;
			ui::FeedbackSounds sounds;

			std::vector<MenuBarGroup> menus; // as the host last gave them
			bool asked = false;                 // whether it has ever been asked
			std::vector<float> titleX, titleW;  // where Layout put each title
			float barH = 0.0F, width = 0.0F;
			std::vector<bool> titleHover;       // for the hover sound

			// How large each menu's panel comes out, measured with the titles
			// every frame. A menu's panel is hit-tested the moment it is
			// clicked, which may be before it has ever been drawn -- hovering
			// from one title to the next opens a menu between two frames --
			// so its size may not be worked out while drawing it.
			std::vector<float> panelW, panelH;

			int open = -1;      // the menu showing, or -1
			// Whether the press that opened it was on its own title. Walking
			// along the bar opens each menu in turn as the pointer crosses it,
			// and the press that follows is the one that meant to open the
			// menu already showing -- it must not be read as the press that
			// closes it again.
			bool openedByPress = false;
			int selected = -1;  // the row under the pointer or the keys
			float menuX = 0.0F, menuY = 0.0F, menuW = 0.0F, menuH = 0.0F;

			/** Takes the menus from the host and opens `index`. */
			void Open(int index);
			/** Which title `p` is on, or -1. */
			int TitleAt(const Vector2& p) const;
			/** Which row of the open menu `p` is on, or -1 (a rule is none). */
			int RowAt(const Vector2& p) const;
			/** Puts the open menu's panel under its title, at the size measured
			 *  for it. */
			void PlaceOpenMenu();
			/** Runs row `index` of the open menu and closes. */
			void Activate(int index);
			/** The row `by` steps from `selected`, past the rules. */
			int Step(int from, int by) const;
		};
	} // namespace gui
} // namespace spades
