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
			constexpr float kPadY = 4.0F;
			constexpr float kMinWidth = 150.0F;
			constexpr float kTextScale = 0.9F;
			constexpr float kScreenMargin = 4.0F;
		} // namespace

		ContextMenu::ContextMenu(client::IRenderer& renderer, client::IFont& font,
		                         client::IAudioDevice* audioDevice)
		    : renderer(renderer), font(font), sounds(audioDevice) {}

		void ContextMenu::Open(const Vector2& at, std::vector<ContextMenuItem> newItems,
		                       const void* owner) {
			items = std::move(newItems);
			open = !items.empty();
			openedBy = owner;
			highlighted = -1;
			pressed = -1;
			width = kMinWidth;
			height = kPadY * 2.0F;
			for (const ContextMenuItem& item : items) {
				float w = kPadX * 2.0F + kCheckW + font.Measure(item.caption).x * kTextScale;
				if (!item.shortcut.empty())
					w += kShortcutGap + font.Measure(item.shortcut).x * kTextScale;
				width = std::max(width, w);
				height += kItemH + (item.separated ? kSeparatorH : 0.0F);
			}
			// Down and right of the cursor, as far as the screen allows.
			const float sw = renderer.ScreenWidth(), sh = renderer.ScreenHeight();
			x = std::max(kScreenMargin, std::min(at.x, sw - width - kScreenMargin));
			y = std::max(kScreenMargin, std::min(at.y, sh - height - kScreenMargin));
			lastCursor = at;
		}

		void ContextMenu::OpenBelow(const AABB2& anchor, std::vector<ContextMenuItem> newItems,
		                            const void* owner) {
			Open(MakeVector2(anchor.GetMinX(), anchor.GetMaxY() + 2.0F), std::move(newItems), owner);
			if (open && width < anchor.GetWidth()) {
				width = anchor.GetWidth();
				x = std::min(x, renderer.ScreenWidth() - width - kScreenMargin);
			}
		}

		void ContextMenu::Close() {
			open = false;
			pressed = -1;
		}

		float ContextMenu::ItemTop(int index) const {
			float top = y + kPadY;
			for (int i = 0; i <= index; i++) {
				if (items[std::size_t(i)].separated)
					top += kSeparatorH;
				if (i < index)
					top += kItemH;
			}
			return top;
		}

		int ContextMenu::ItemAt(const Vector2& at) const {
			if (!open || at.x < x || at.x >= x + width)
				return -1;
			for (int i = 0; i < int(items.size()); i++) {
				const float top = ItemTop(i);
				if (at.y >= top && at.y < top + kItemH)
					return i;
			}
			return -1;
		}

		void ContextMenu::Press(const Vector2& at) {
			if (!open)
				return;
			if (!OverlayInRect(at, x, y, width, height)) {
				Close();
				return;
			}
			pressed = ItemAt(at);
		}

		void ContextMenu::Release(const Vector2& at) {
			const int was = pressed;
			pressed = -1;
			if (open && was >= 0 && ItemAt(at) == was)
				Run(was);
		}

		void ContextMenu::Run(int index) {
			if (index < 0 || index >= int(items.size()) || !items[std::size_t(index)].enabled)
				return;
			// Closed first: the command may open something of its own.
			std::function<void()> run = items[std::size_t(index)].run;
			Close();
			sounds.Activate();
			if (run)
				run();
		}

		void ContextMenu::Step(int direction) {
			const int count = int(items.size());
			for (int n = 1; n <= count; n++) {
				const int i = ((highlighted < 0 ? (direction > 0 ? -1 : 0) : highlighted) +
				               direction * n + count * 2) %
				              count;
				if (items[std::size_t(i)].enabled) {
					highlighted = i;
					sounds.Hover();
					return;
				}
			}
		}

		void ContextMenu::Key(const std::string& key) {
			if (!open)
				return;
			if (key == "Escape")
				Close();
			else if (key == "Up")
				Step(-1);
			else if (key == "Down")
				Step(1);
			else if (key == "Enter" || key == "Return")
				Run(highlighted);
		}

		void ContextMenu::Draw(const Vector2& cursor) {
			if (!open)
				return;
			// The pointer chooses while it moves; the keys, while it rests.
			if (cursor.x != lastCursor.x || cursor.y != lastCursor.y) {
				lastCursor = cursor;
				const int over = ItemAt(cursor);
				const int now = over >= 0 && items[std::size_t(over)].enabled ? over : -1;
				if (now >= 0 && now != highlighted)
					sounds.Hover();
				highlighted = now;
			}

			EditorTheme::PaintPanel(renderer, x, y, width, height);

			const float textH = font.Measure("Ag").y * kTextScale;
			for (int i = 0; i < int(items.size()); i++) {
				const ContextMenuItem& item = items[std::size_t(i)];
				const float top = ItemTop(i);
				if (item.separated) {
					OverlayColorNP(renderer, EditorTheme::Separator());
					OverlayFillRect(renderer, x + 6.0F, top - kSeparatorH * 0.5F - 0.5F,
					                width - 12.0F, 1.0F);
				}
				if (i == highlighted) {
					OverlayColorNP(renderer, EditorTheme::Highlight());
					OverlayFillRect(renderer, x + 3.0F, top, width - 6.0F, kItemH);
				}
				const Vector4 color = item.enabled ? EditorTheme::Text() : EditorTheme::TextFaint();
				const float midY = top + kItemH * 0.5F;
				if (item.checked) {
					const Vector2 tick[3] = {MakeVector2(x + kPadX - 6.0F, midY),
					                         MakeVector2(x + kPadX - 2.5F, midY + 3.5F),
					                         MakeVector2(x + kPadX + 4.0F, midY - 4.0F)};
					OverlayStrokePolyline(renderer, tick, 3, 1.6F, color, false);
				}
				const float textY = top + (kItemH - textH) * 0.5F;
				font.Draw(item.caption, MakeVector2(x + kPadX + kCheckW - 6.0F, textY), kTextScale,
				          color);
				if (!item.shortcut.empty()) {
					const float sw = font.Measure(item.shortcut).x * kTextScale;
					font.Draw(item.shortcut, MakeVector2(x + width - kPadX - sw, textY), kTextScale,
					          i == highlighted ? EditorTheme::Text() : EditorTheme::TextDim());
				}
			}
		}
	} // namespace gui
} // namespace spades
