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

#include "DialogChrome.h"

#include <algorithm>

#include "DrawUtils.h"
#include "Label.h"
#include <Client/IRenderer.h>
#include <Gui/UI/Framework/UIManager.h>

namespace spades {
	namespace gui {
		using ui::Label;
		using ui::SetColorNP;
		using ui::UIElement;
		using ui::UIManager;

		namespace {
			// How far the band reaches past the contents, above and below, and
			// where the hairline marking its edge is drawn.
			const float kBandAbove = 13.0F;
			const float kBandBelow = 14.0F;
			// The contents never run to the very edge of a narrow screen.
			const float kScreenMargin = 16.0F;
			// The band reads as a surface over the view rather than a hole in it:
			// what is behind stays faintly visible, which a live preview needs.
			const float kBandOpacity = 0.75F;
			const float kDimOpacity = 0.7F;
		} // namespace

		AABB2 DialogChrome::Attach(UIElement& dialog, float contentsWidth, float contentsHeight,
		                           bool dim) {
			UIManager& manager = dialog.GetManager();
			float sw = manager.screenWidth;
			float sh = manager.screenHeight;
			float w = std::min(contentsWidth, sw - kScreenMargin);
			float x = (sw - w) * 0.5F;
			float y = (sh - contentsHeight) * 0.5F;

			if (dim) {
				Handle<Label> overlay = Handle<Label>::New(&manager);
				overlay->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, kDimOpacity);
				overlay->SetBounds(AABB2(0.0F, 0.0F, sw, sh));
				dialog.AddChild(overlay.GetPointerOrNull());
			}
			{
				// Full width, whatever the contents are: the band is the dialog's
				// surface, and the contents sit on it.
				Handle<Label> band = Handle<Label>::New(&manager);
				band->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, kBandOpacity);
				band->SetBounds(AABB2(0.0F, y - kBandAbove, dialog.size.x,
				                      contentsHeight + kBandAbove + kBandBelow));
				dialog.AddChild(band.GetPointerOrNull());
			}
			return AABB2(x, y, w, contentsHeight);
		}

		void DialogChrome::DrawEdges(const UIElement& dialog, const AABB2& contents) {
			client::IRenderer& r = dialog.GetManager().GetRenderer();
			Vector2 pos = dialog.GetScreenPosition();

			SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.07F));
			r.DrawImage(nullptr,
			            AABB2(pos.x, pos.y + contents.min.y - kBandBelow, dialog.size.x, 1.0F));
			r.DrawImage(nullptr,
			            AABB2(pos.x, pos.y + contents.max.y + kBandBelow, dialog.size.x, 1.0F));
		}

		AABB2 DialogChrome::ButtonSlot(const AABB2& contents, int slot) {
			return AABB2(contents.max.x - kButtonWidth -
			               float(slot) * (kButtonWidth + kButtonGap),
			             contents.max.y - kButtonHeight, kButtonWidth, kButtonHeight);
		}
	} // namespace gui
} // namespace spades
