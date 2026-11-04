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

namespace spades {
	namespace gui {
		namespace {
			constexpr float kItemH = 24.0F;
			constexpr float kSeparatorH = 9.0F; // the gap a separator line sits in
			constexpr float kPadX = 14.0F;
			constexpr float kPadY = 4.0F;
			constexpr float kMinWidth = 150.0F;
			constexpr float kTextScale = 0.9F;
			constexpr float kScreenMargin = 4.0F;

			const Vector4 kBackground = MakeVector4(0.11F, 0.115F, 0.13F, 0.97F);
			const Vector4 kBorder = MakeVector4(0.30F, 0.32F, 0.37F, 1.0F);
			const Vector4 kDropShadow = MakeVector4(0.0F, 0.0F, 0.0F, 0.35F);
			const Vector4 kHighlight = MakeVector4(0.24F, 0.42F, 0.72F, 1.0F);
			const Vector4 kText = MakeVector4(0.90F, 0.91F, 0.93F, 1.0F);
			const Vector4 kDisabledText = MakeVector4(0.50F, 0.52F, 0.56F, 1.0F);
			const Vector4 kSeparator = MakeVector4(0.26F, 0.28F, 0.32F, 1.0F);
		} // namespace

		ContextMenu::ContextMenu(client::IRenderer& renderer, client::IFont& font,
		                         client::IAudioDevice* audioDevice)
		    : renderer(renderer), font(font), sounds(audioDevice) {}

		void ContextMenu::Open(const Vector2& at, std::vector<ContextMenuItem> newItems) {
			items = std::move(newItems);
			open = !items.empty();
			highlighted = -1;
			pressed = -1;
			width = kMinWidth;
			height = kPadY * 2.0F;
			for (const ContextMenuItem& item : items) {
				width = std::max(width, font.Measure(item.caption).x * kTextScale + kPadX * 2.0F);
				height += kItemH + (item.separated ? kSeparatorH : 0.0F);
			}
			// Down and right of the cursor, as far as the screen allows.
			const float sw = renderer.ScreenWidth(), sh = renderer.ScreenHeight();
			x = std::max(kScreenMargin, std::min(at.x, sw - width - kScreenMargin));
			y = std::max(kScreenMargin, std::min(at.y, sh - height - kScreenMargin));
			lastCursor = at;
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

			OverlayColorNP(renderer, kDropShadow);
			OverlayFillRect(renderer, x + 3.0F, y + 4.0F, width, height);
			OverlayColorNP(renderer, kBackground);
			OverlayFillRect(renderer, x, y, width, height);
			OverlayStrokeRect(renderer, x, y, width, height, 1.0F, kBorder);

			const float textH = font.Measure("Ag").y * kTextScale;
			for (int i = 0; i < int(items.size()); i++) {
				const ContextMenuItem& item = items[std::size_t(i)];
				const float top = ItemTop(i);
				if (item.separated) {
					OverlayColorNP(renderer, kSeparator);
					OverlayFillRect(renderer, x + 6.0F, top - kSeparatorH * 0.5F - 0.5F,
					                width - 12.0F, 1.0F);
				}
				if (i == highlighted) {
					OverlayColorNP(renderer, kHighlight);
					OverlayFillRect(renderer, x + 3.0F, top, width - 6.0F, kItemH);
				}
				font.Draw(item.caption, MakeVector2(x + kPadX, top + (kItemH - textH) * 0.5F),
				          kTextScale, item.enabled ? kText : kDisabledText);
			}
		}
	} // namespace gui
} // namespace spades
