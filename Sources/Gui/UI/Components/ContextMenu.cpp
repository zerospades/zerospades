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

#include "ContextMenu.h"

#include <algorithm>

#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/OverlayPaint.h>

#include "EditorTheme.h"

namespace spades {
	namespace gui {
		namespace {
			constexpr float kItemH = 24.0F;
			constexpr float kSeparatorH = 9.0F; // the gap a separator line sits in
			constexpr float kPadX = 14.0F;
			constexpr float kCheckW = 16.0F;    // the check mark's column, left of captions
			constexpr float kShortcutGap = 28.0F;
			constexpr float kArrowW = 18.0F;    // a submenu's arrow, right of the caption
			constexpr float kPadY = 4.0F;
			constexpr float kMinWidth = 150.0F;
			constexpr float kTextScale = 0.9F;
			constexpr float kScreenMargin = 4.0F;
			constexpr float kSubmenuOverlap = 2.0F; // a submenu over its menu's edge
		} // namespace

		ContextMenu::ContextMenu(client::IRenderer& renderer, client::IFont& font,
		                         client::IAudioDevice* audioDevice)
		    : renderer(renderer), font(font), sounds(audioDevice) {}

		ContextMenu::Level ContextMenu::Lay(std::vector<ContextMenuItem> items, const Vector2& at,
		                                    float leftOf) const {
			Level level;
			level.items = std::move(items);
			level.width = kMinWidth;
			level.height = kPadY * 2.0F;
			for (const ContextMenuItem& item : level.items) {
				float w = kPadX * 2.0F + kCheckW + font.Measure(item.caption).x * kTextScale;
				if (!item.shortcut.empty())
					w += kShortcutGap + font.Measure(item.shortcut).x * kTextScale;
				if (!item.submenu.empty())
					w += kArrowW;
				level.width = std::max(level.width, w);
				level.height += kItemH + (item.separated ? kSeparatorH : 0.0F);
			}
			// Down and right of `at`, as far as the screen allows; a submenu that
			// would run off the right goes on the left of its menu instead.
			const float sw = renderer.ScreenWidth(), sh = renderer.ScreenHeight();
			float x = at.x;
			if (leftOf >= 0.0F && x + level.width > sw - kScreenMargin)
				x = leftOf - level.width;
			level.x = std::max(kScreenMargin, std::min(x, sw - level.width - kScreenMargin));
			level.y = std::max(kScreenMargin, std::min(at.y, sh - level.height - kScreenMargin));
			return level;
		}

		void ContextMenu::Open(const Vector2& at, std::vector<ContextMenuItem> newItems,
		                       const void* owner) {
			levels.clear();
			openedBy = owner;
			pressedLevel = pressed = -1;
			lastCursor = at;
			if (!newItems.empty())
				levels.push_back(Lay(std::move(newItems), at, -1.0F));
		}

		void ContextMenu::OpenBelow(const AABB2& anchor, std::vector<ContextMenuItem> newItems,
		                            const void* owner) {
			Open(MakeVector2(anchor.GetMinX(), anchor.GetMaxY() + 2.0F), std::move(newItems), owner);
			if (IsOpen() && levels[0].width < anchor.GetWidth()) {
				Level& root = levels[0];
				root.width = anchor.GetWidth();
				root.x = std::min(root.x, renderer.ScreenWidth() - root.width - kScreenMargin);
			}
		}

		void ContextMenu::Close() {
			levels.clear();
			pressedLevel = pressed = -1;
		}

		float ContextMenu::ItemTop(const Level& level, int index) const {
			float top = level.y + kPadY;
			for (int i = 0; i <= index; i++) {
				if (level.items[std::size_t(i)].separated)
					top += kSeparatorH;
				if (i < index)
					top += kItemH;
			}
			return top;
		}

		int ContextMenu::ItemAt(const Level& level, const Vector2& at) const {
			if (at.x < level.x || at.x >= level.x + level.width)
				return -1;
			for (int i = 0; i < int(level.items.size()); i++) {
				const float top = ItemTop(level, i);
				if (at.y >= top && at.y < top + kItemH)
					return i;
			}
			return -1;
		}

		int ContextMenu::LevelAt(const Vector2& at) const {
			for (int d = int(levels.size()) - 1; d >= 0; d--) {
				const Level& l = levels[std::size_t(d)];
				if (OverlayInRect(at, l.x, l.y, l.width, l.height))
					return d;
			}
			return -1;
		}

		void ContextMenu::OpenSubmenu(int depth, int index) {
			levels.resize(std::size_t(depth) + 1);
			Level& parent = levels[std::size_t(depth)];
			parent.highlighted = index;
			const ContextMenuItem& item = parent.items[std::size_t(index)];
			const Vector2 at = MakeVector2(parent.x + parent.width - kSubmenuOverlap,
			                               ItemTop(parent, index) - kPadY);
			Level sub = Lay(item.submenu, at, parent.x + kSubmenuOverlap);
			sub.from = index;
			levels.push_back(std::move(sub));
		}

		void ContextMenu::Press(const Vector2& at) {
			if (!IsOpen())
				return;
			const int depth = LevelAt(at);
			if (depth < 0) {
				Close();
				return;
			}
			pressedLevel = depth;
			pressed = ItemAt(levels[std::size_t(depth)], at);
		}

		void ContextMenu::Release(const Vector2& at) {
			const int wasLevel = pressedLevel, was = pressed;
			pressedLevel = pressed = -1;
			if (!IsOpen() || was < 0 || LevelAt(at) != wasLevel)
				return;
			if (ItemAt(levels[std::size_t(wasLevel)], at) == was)
				Run(wasLevel, was, false);
		}

		void ContextMenu::Run(int depth, int index, bool byKey) {
			if (depth < 0 || depth >= int(levels.size()))
				return;
			const Level& level = levels[std::size_t(depth)];
			if (index < 0 || index >= int(level.items.size()) ||
			    !level.items[std::size_t(index)].enabled)
				return;
			if (!level.items[std::size_t(index)].submenu.empty()) {
				// Already open from the pointer resting on it, it stays as it is.
				const bool open = int(levels.size()) > depth + 1 &&
				                  levels[std::size_t(depth) + 1].from == index;
				if (!open)
					OpenSubmenu(depth, index);
				if (byKey)
					Step(1);
				return;
			}
			// Closed first: the command may open something of its own.
			std::function<void()> run = level.items[std::size_t(index)].run;
			Close();
			sounds.Activate();
			if (run)
				run();
		}

		void ContextMenu::Step(int direction) {
			Level& level = levels.back();
			const int count = int(level.items.size());
			for (int n = 1; n <= count; n++) {
				const int i =
				  ((level.highlighted < 0 ? (direction > 0 ? -1 : 0) : level.highlighted) +
				   direction * n + count * 2) %
				  count;
				if (level.items[std::size_t(i)].enabled) {
					level.highlighted = i;
					sounds.Hover();
					return;
				}
			}
		}

		void ContextMenu::Key(const std::string& key) {
			if (!IsOpen())
				return;
			const int deepest = int(levels.size()) - 1;
			if (key == "Escape" || key == "Left") {
				if (deepest > 0)
					levels.pop_back(); // back to the menu it came from
				else if (key == "Escape")
					Close();
			} else if (key == "Up") {
				Step(-1);
			} else if (key == "Down") {
				Step(1);
			} else if (key == "Right") {
				const Level& level = levels.back();
				if (level.highlighted >= 0 &&
				    !level.items[std::size_t(level.highlighted)].submenu.empty())
					Run(deepest, level.highlighted, true);
			} else if (key == "Enter" || key == "Return") {
				Run(deepest, levels.back().highlighted, true);
			}
		}

		void ContextMenu::Draw(const Vector2& cursor) {
			if (!IsOpen())
				return;
			// The pointer chooses while it moves; the keys, while it rests.
			if (cursor.x != lastCursor.x || cursor.y != lastCursor.y) {
				lastCursor = cursor;
				const int depth = LevelAt(cursor);
				if (depth >= 0) {
					Level& level = levels[std::size_t(depth)];
					const int over = ItemAt(level, cursor);
					const int now = over >= 0 && level.items[std::size_t(over)].enabled ? over : -1;
					if (now >= 0 && now != level.highlighted)
						sounds.Hover();
					if (now >= 0 && !level.items[std::size_t(now)].submenu.empty()) {
						// Onto an item with a submenu: it opens beside it.
						// (`level` is not used past here: opening one moves the levels.)
						if (int(levels.size()) <= depth + 1 ||
						    levels[std::size_t(depth) + 1].from != now)
							OpenSubmenu(depth, now);
						levels[std::size_t(depth)].highlighted = now;
					} else if (now >= 0) {
						levels.resize(std::size_t(depth) + 1); // another item: what it opened goes
						levels.back().highlighted = now;
					} else if (int(levels.size()) == depth + 1) {
						level.highlighted = -1;
					}
				} else {
					// Off every level: the item that opened a submenu stays lit.
					levels.back().highlighted = -1;
				}
			}

			const float textH = font.Measure("Ag").y * kTextScale;
			for (std::size_t d = 0; d < levels.size(); d++) {
				const Level& level = levels[d];
				EditorTheme::PaintPanel(renderer, level.x, level.y, level.width, level.height);
				for (int i = 0; i < int(level.items.size()); i++) {
					const ContextMenuItem& item = level.items[std::size_t(i)];
					const float top = ItemTop(level, i);
					const float x = level.x, width = level.width;
					if (item.separated) {
						OverlayColorNP(renderer, EditorTheme::Separator());
						OverlayFillRect(renderer, x + 6.0F, top - kSeparatorH * 0.5F - 0.5F,
						                width - 12.0F, 1.0F);
					}
					const bool lit = i == level.highlighted;
					if (lit) {
						OverlayColorNP(renderer, EditorTheme::Highlight());
						OverlayFillRect(renderer, x + 3.0F, top, width - 6.0F, kItemH);
					}
					const Vector4 color =
					  item.enabled ? EditorTheme::Text() : EditorTheme::TextFaint();
					const float midY = top + kItemH * 0.5F;
					if (item.checked) {
						const Vector2 tick[3] = {MakeVector2(x + kPadX - 6.0F, midY),
						                         MakeVector2(x + kPadX - 2.5F, midY + 3.5F),
						                         MakeVector2(x + kPadX + 4.0F, midY - 4.0F)};
						OverlayStrokePolyline(renderer, tick, 3, 1.6F, color, false);
					}
					const float textY = top + (kItemH - textH) * 0.5F;
					font.Draw(item.caption, MakeVector2(x + kPadX + kCheckW - 6.0F, textY),
					          kTextScale, color);
					if (!item.submenu.empty()) {
						const float ax = x + width - kPadX;
						const Vector2 arrow[3] = {MakeVector2(ax - 4.0F, midY - 4.5F),
						                          MakeVector2(ax + 1.0F, midY),
						                          MakeVector2(ax - 4.0F, midY + 4.5F)};
						OverlayFillConvexPolygon(renderer, arrow, 3,
						                         lit ? EditorTheme::Text() : EditorTheme::TextDim());
					} else if (!item.shortcut.empty()) {
						const float sw = font.Measure(item.shortcut).x * kTextScale;
						font.Draw(item.shortcut, MakeVector2(x + width - kPadX - sw, textY),
						          kTextScale, lit ? EditorTheme::Text() : EditorTheme::TextDim());
					}
				}
			}
		}
	} // namespace gui
} // namespace spades
