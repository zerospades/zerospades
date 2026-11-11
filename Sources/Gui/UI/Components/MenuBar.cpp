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

#include "MenuBar.h"

#include <algorithm>

#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/OverlayPaint.h>

#include "EditorTheme.h"

namespace spades {
	namespace gui {
		namespace {
			// A title: its text, and the room each side of it.
			constexpr float kTitlePad = 10.0F;
			constexpr float kTitleScale = 0.9F;
			// A row of a dropped menu: its height, the room each side, and the
			// gap kept between a command and the key that does the same.
			constexpr float kRowH = 22.0F;
			constexpr float kRowPadX = 12.0F;
			constexpr float kKeyGap = 28.0F;
			constexpr float kRuleH = 9.0F;
			constexpr float kMenuPadY = 5.0F;
			constexpr float kMenuMinW = 150.0F;
			constexpr float kTextScale = 0.85F;
			// The tick a command that turns something on carries.
			constexpr float kTickSize = 3.0F;
		} // namespace

		MenuBar::MenuBar(IEditorMenuHost& host, client::IAudioDevice* audioDevice)
		    : host(host), sounds(audioDevice) {}

		float MenuBar::Layout(client::IFont& font, float x, float height) {
			// Asked for on the way to the first frame rather than in the
			// constructor: the bar is built as part of the editor's chrome,
			// before the editor its menus are about exists to be asked.
			if (!asked) {
				asked = true;
				menus = host.GetMenus();
			}
			barX = x;
			barH = height;
			titleX.clear();
			titleW.clear();
			float at = x;
			for (const EditorMenuGroup& menu : menus) {
				const float w = font.Measure(menu.title).x * kTitleScale + kTitlePad * 2.0F;
				titleX.push_back(at);
				titleW.push_back(w);
				at += w;
			}
			titleHover.resize(menus.size(), false);
			width = at - x;
			return width;
		}

		void MenuBar::Close() {
			open = -1;
			selected = -1;
		}

		void MenuBar::Open(int index) {
			if (index < 0 || index >= int(menus.size()))
				return;
			// Taken afresh: what a menu offers, and what of it can be done at
			// all, is as it stands the moment it is opened.
			menus = host.GetMenus();
			if (index >= int(menus.size())) {
				Close();
				return;
			}
			open = index;
			selected = -1;
		}

		int MenuBar::TitleAt(const Vector2& p) const {
			for (std::size_t i = 0; i < titleX.size(); i++) {
				if (OverlayInRect(p, titleX[i], 0.0F, titleW[i], barH))
					return int(i);
			}
			return -1;
		}

		int MenuBar::RowAt(const Vector2& p) const {
			if (!IsOpen() || !OverlayInRect(p, menuX, menuY, menuW, menuH))
				return -1;
			float y = menuY + kMenuPadY;
			const std::vector<EditorMenuItem>& items = menus[std::size_t(open)].items;
			for (std::size_t i = 0; i < items.size(); i++) {
				const float h = items[i].separator ? kRuleH : kRowH;
				if (!items[i].separator && p.y >= y && p.y < y + h)
					return int(i);
				y += h;
			}
			return -1;
		}

		bool MenuBar::Contains(const Vector2& p) const {
			if (TitleAt(p) >= 0)
				return true;
			return IsOpen() && OverlayInRect(p, menuX, menuY, menuW, menuH);
		}

		bool MenuBar::Click(const Vector2& p) {
			const int title = TitleAt(p);
			if (title >= 0) {
				// The title of the menu already showing closes it again, as a
				// menu bar does everywhere.
				if (open == title)
					Close();
				else
					Open(title);
				sounds.Activate();
				return true;
			}
			if (!IsOpen())
				return false;
			if (!OverlayInRect(p, menuX, menuY, menuW, menuH)) {
				Close(); // a press anywhere else puts the menu away
				return false;
			}
			const int row = RowAt(p);
			if (row >= 0 && menus[std::size_t(open)].items[std::size_t(row)].enabled) {
				sounds.Activate();
				Activate(row);
			}
			return true; // the press was the menu's, even on a gap or a greyed row
		}

		void MenuBar::MouseMove(const Vector2& p) {
			const int title = TitleAt(p);
			for (std::size_t i = 0; i < titleHover.size(); i++) {
				const bool hover = int(i) == title;
				titleHover[i] = sounds.HoverEdge(hover, titleHover[i]);
			}
			// With one menu down, moving along the titles walks between them.
			if (IsOpen() && title >= 0 && title != open) {
				Open(title);
				return;
			}
			if (IsOpen())
				selected = RowAt(p);
		}

		int MenuBar::Step(int from, int by) const {
			const std::vector<EditorMenuItem>& items = menus[std::size_t(open)].items;
			const int count = int(items.size());
			if (count == 0)
				return -1;
			// Past the rules and round the ends, so the keys reach every
			// command and stop on none of the gaps between them.
			for (int n = 1; n <= count; n++) {
				const int at = ((from + by * n) % count + count) % count;
				if (!items[std::size_t(at)].separator)
					return at;
			}
			return from;
		}

		bool MenuBar::KeyEvent(const std::string& key, bool down) {
			if (!IsOpen())
				return false;
			if (!down)
				return true; // the menu has the keyboard: releases go nowhere else
			if (key == "Escape") {
				Close();
			} else if (key == "Up") {
				selected = Step(selected < 0 ? 0 : selected, -1);
			} else if (key == "Down") {
				selected = Step(selected < 0 ? -1 : selected, 1);
			} else if (key == "Left") {
				Open((open + int(menus.size()) - 1) % int(menus.size()));
			} else if (key == "Right") {
				Open((open + 1) % int(menus.size()));
			} else if (key == "Enter" || key == "Keypad Enter") {
				if (selected >= 0 && menus[std::size_t(open)].items[std::size_t(selected)].enabled) {
					sounds.Activate();
					Activate(selected);
				}
			}
			return true;
		}

		void MenuBar::Activate(int index) {
			if (!IsOpen() || index < 0 || index >= int(menus[std::size_t(open)].items.size()))
				return;
			// The command may put a dialog of its own up, so the menu is away
			// before it runs.
			std::function<void()> run = menus[std::size_t(open)].items[std::size_t(index)].run;
			Close();
			if (run)
				run();
		}

		void MenuBar::LayoutOpenMenu(client::IFont& font) {
			const std::vector<EditorMenuItem>& items = menus[std::size_t(open)].items;
			float widest = kMenuMinW;
			float height = kMenuPadY * 2.0F;
			for (const EditorMenuItem& item : items) {
				height += item.separator ? kRuleH : kRowH;
				if (item.separator)
					continue;
				float w = font.Measure(item.caption).x * kTextScale + kRowPadX * 2.0F;
				if (!item.hotKey.empty())
					w += kKeyGap + font.Measure(item.hotKey).x * kTextScale;
				widest = std::max(widest, w);
			}
			menuX = titleX[std::size_t(open)];
			menuY = barH;
			menuW = widest;
			menuH = height;
		}

		void MenuBar::DrawTitles(client::IRenderer& renderer, client::FontManager& fontManager,
		                         const Vector2& cursor) {
			client::IFont& font = fontManager.GetSmallGuiFont();
			const int hover = TitleAt(cursor);
			for (std::size_t i = 0; i < menus.size(); i++) {
				const bool lit = int(i) == open;
				const bool hot = int(i) == hover;
				if (lit || hot) {
					OverlayColorNP(renderer, lit ? EditorTheme::PanelFill()
					                             : EditorTheme::Highlight());
					OverlayFillRect(renderer, titleX[i], 0.0F, titleW[i], barH);
				}
				const Vector2 size = font.Measure(menus[i].title) * kTitleScale;
				font.Draw(menus[i].title,
				          MakeVector2(titleX[i] + (titleW[i] - size.x) * 0.5F,
				                      (barH - size.y) * 0.5F),
				          kTitleScale,
				          lit || hot ? EditorTheme::Text() : EditorTheme::TextDim());
			}
		}

		void MenuBar::DrawOpenMenu(client::IRenderer& renderer, client::FontManager& fontManager,
		                           const Vector2& cursor) {
			if (!IsOpen())
				return;
			client::IFont& font = fontManager.GetSmallGuiFont();
			LayoutOpenMenu(font);
			// The pointer picks the row under it; the keys keep theirs where it
			// is not over the menu at all.
			const int under = RowAt(cursor);
			if (under >= 0)
				selected = under;

			EditorTheme::PaintPanel(renderer, menuX, menuY, menuW, menuH);
			const std::vector<EditorMenuItem>& items = menus[std::size_t(open)].items;
			float y = menuY + kMenuPadY;
			for (std::size_t i = 0; i < items.size(); i++) {
				const EditorMenuItem& item = items[i];
				if (item.separator) {
					OverlayColorNP(renderer, EditorTheme::Separator());
					OverlayFillRect(renderer, menuX + kRowPadX * 0.5F, y + (kRuleH - 1.0F) * 0.5F,
					                menuW - kRowPadX, 1.0F);
					y += kRuleH;
					continue;
				}
				if (int(i) == selected && item.enabled) {
					OverlayColorNP(renderer, EditorTheme::Highlight());
					OverlayFillRect(renderer, menuX + 2.0F, y, menuW - 4.0F, kRowH);
				}
				const float textH = font.Measure("Ag").y * kTextScale;
				const float textY = y + (kRowH - textH) * 0.5F;
				const Vector4 ink = item.enabled ? EditorTheme::Text() : EditorTheme::TextFaint();
				font.Draw(item.caption, MakeVector2(menuX + kRowPadX, textY), kTextScale, ink);
				if (!item.hotKey.empty()) {
					const float keyW = font.Measure(item.hotKey).x * kTextScale;
					font.Draw(item.hotKey, MakeVector2(menuX + menuW - kRowPadX - keyW, textY),
					          kTextScale,
					          item.enabled ? EditorTheme::TextDim() : EditorTheme::TextFaint());
				}
				if (item.checked) {
					// A square in the margin: what it turns on is on now.
					const float cx = menuX + kRowPadX * 0.5F - 1.0F;
					const float cy = y + kRowH * 0.5F;
					OverlayColorNP(renderer, ink);
					OverlayFillRect(renderer, cx - kTickSize * 0.5F, cy - kTickSize * 0.5F,
					                kTickSize, kTickSize);
				}
				y += kRowH;
			}
		}
	} // namespace gui
} // namespace spades
