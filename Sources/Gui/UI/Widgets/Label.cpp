/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

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

#include "DrawUtils.h"
#include "Label.h"
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/UI/Framework/UIManager.h>

namespace spades {
	namespace gui {
		namespace ui {
			void Label::Render() {
				client::IRenderer& r = GetManager().GetRenderer();
				Vector2 pos = GetScreenPosition();
				Vector2 sz = size;

				if (backgroundColor.w > 0.0F) {
					SetColorNP(r, backgroundColor);
					r.DrawImage(nullptr, AABB2(pos.x, pos.y, sz.x, sz.y));
				}

				if (!text.empty()) {
					client::IFont* font = GetFont();
					if (!font)
						return;
					Vector2 textSize = font->Measure(text) * textScale;
					Vector2 textPos = pos + (sz - textSize) * alignment;
					font->Draw(text, textPos, textScale, IsEnabled() ? textColor : disabledTextColor);
				}
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
