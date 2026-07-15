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

#include "CrosshairRenderer.h"
#include <Client/IRenderer.h>
#include <Gui/UI/Widgets/DrawUtils.h>

namespace spades {
	namespace gui {
		namespace {
			void DrawTargetOutlineRect(client::IRenderer& r, int x, int y, int w, int h,
			                           const TargetParam& param) {
				int thickness = static_cast<int>(param.outlineThickness);
				ui::SetColorNP(r, param.outlineColor);
				if (param.useRoundedStyle) {
					r.DrawFilledRect(x, y - thickness, w, y);
					r.DrawFilledRect(x, h, w, h + thickness);
				} else {
					r.DrawFilledRect(x - thickness, y - thickness, w + thickness, y);
					r.DrawFilledRect(x - thickness, h, w + thickness, h + thickness);
				}
				r.DrawFilledRect(x - thickness, y, x, h);
				r.DrawFilledRect(w, y, w + thickness, h);
			}

			void DrawTargetRect(client::IRenderer& r, int x, int y, int w, int h,
			                    const TargetParam& param) {
				if (param.drawOutline)
					DrawTargetOutlineRect(r, x, y, w, h, param);
				ui::SetColorNP(r, param.lineColor);
				r.DrawFilledRect(x, y, w, h);
			}
		} // namespace

		void DrawTarget(client::IRenderer& r, Vector2 pos, const TargetParam& param) {
			int thickness = static_cast<int>(param.lineThickness);
			int x = static_cast<int>(pos.x) - thickness / 2;
			int y = static_cast<int>(pos.y) - thickness / 2;
			int w = x + thickness;
			int h = y + thickness;

			// draw target lines
			if (param.drawLines) {
				int lineGap = static_cast<int>(param.lineGap);

				// horizontal lines
				int lineLengthHorizontal = static_cast<int>(param.lineLength.x);
				int innerLeft = x - lineGap;
				int innerRight = w + lineGap;
				int outerLeft = innerLeft - lineLengthHorizontal;
				int outerRight = innerRight + lineLengthHorizontal;
				DrawTargetRect(r, outerLeft, y, innerLeft, h, param); // left
				DrawTargetRect(r, innerRight, y, outerRight, h, param); // right

				// vertical lines
				int lineLengthVertical = static_cast<int>(param.lineLength.y);
				int innerTop = y - lineGap;
				int innerBottom = h + lineGap;
				int outerTop = innerTop - lineLengthVertical;
				int outerBottom = innerBottom + lineLengthVertical;
				if (!param.useTStyle)
					DrawTargetRect(r, x, outerTop, w, innerTop, param); // top
				DrawTargetRect(r, x, innerBottom, w, outerBottom, param); // bottom
			}

			// draw center dot
			if (param.drawDot) {
				thickness = static_cast<int>(param.dotThickness);
				x = static_cast<int>(pos.x) - thickness / 2;
				y = static_cast<int>(pos.y) - thickness / 2;
				w = x + thickness;
				h = y + thickness;

				if (param.drawOutline)
					DrawTargetOutlineRect(r, x, y, w, h, param);
				ui::SetColorNP(r, param.dotColor);
				r.DrawFilledRect(x, y, w, h);
			}
		}
	} // namespace gui
} // namespace spades
