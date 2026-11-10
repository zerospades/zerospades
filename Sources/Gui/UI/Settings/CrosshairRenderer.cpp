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

#include <algorithm>
#include <cmath>

#include "CrosshairRenderer.h"
#include <Client/IRenderer.h>
#include <Gui/UI/Widgets/DrawUtils.h>

namespace spades {
	namespace gui {
		namespace {
			/**
			 * The renderer and the crosshair's grid of whole real (screen) pixels. The
			 * renderer takes UI coordinates, which a scaled UI (ScaledRenderer)
			 * multiplies back by the scale, so each rect lands exactly on the pixels
			 * computed here.
			 */
			struct PixelCanvas {
				client::IRenderer& r;
				float unit; // UI units per real pixel

				void Fill(int x0, int y0, int x1, int y1) const {
					r.DrawFilledRect(static_cast<float>(x0) * unit, static_cast<float>(y0) * unit,
					                 static_cast<float>(x1) * unit, static_cast<float>(y1) * unit);
				}
			};
			
			void DrawTargetOutlineRect(const PixelCanvas& c, int x, int y, int w, int h,
			                           const TargetParam& param) {
				int thickness = static_cast<int>(param.outlineThickness);
				ui::SetColorNP(c.r, param.outlineColor);
				if (param.useRoundedStyle) {
					c.Fill(x, y - thickness, w, y);
					c.Fill(x, h, w, h + thickness);
				} else {
					c.Fill(x - thickness, y - thickness, w + thickness, y);
					c.Fill(x - thickness, h, w + thickness, h + thickness);
				}
				c.Fill(x - thickness, y, x, h);
				c.Fill(w, y, w + thickness, h);
			}

			void DrawTargetRect(const PixelCanvas& c, int x, int y, int w, int h,
			                    const TargetParam& param) {
				if (param.drawOutline)
					DrawTargetOutlineRect(c, x, y, w, h, param);
				ui::SetColorNP(c.r, param.lineColor);
				c.Fill(x, y, w, h);
			}
		} // namespace

		void DrawTarget(client::IRenderer& r, Vector2 pos, const TargetParam& uiParam) {
			// A crosshair is pixel-exact: laid out on the UI's grid and then scaled by
			// a fractional UI scale, equal arms would come out 1 or 2 pixels thick
			// depending on where they fall. So scale each size once, round it to whole
			// real pixels, and lay the crosshair out on the real grid. At 1x this is
			// the plain UI layout.
			const float scale = r.ScreenPixelRatio();
			const PixelCanvas c{r, 1.0F / scale};
			auto px = [scale](float v) { return std::round(v * scale); };
			// Something set at least 1 UI pixel thick stays at least 1 real pixel thick.
			auto pxThick = [&](float v) { return v >= 1.0F ? std::max(1.0F, px(v)) : px(v); };

			TargetParam param = uiParam;
			param.lineGap = px(uiParam.lineGap);
			param.lineLength = MakeVector2(px(uiParam.lineLength.x), px(uiParam.lineLength.y));
			param.lineThickness = pxThick(uiParam.lineThickness);
			param.outlineThickness = pxThick(uiParam.outlineThickness);
			param.dotThickness = pxThick(uiParam.dotThickness);
			pos = pos * scale;

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
				DrawTargetRect(c, outerLeft, y, innerLeft, h, param); // left
				DrawTargetRect(c, innerRight, y, outerRight, h, param); // right

				// vertical lines
				int lineLengthVertical = static_cast<int>(param.lineLength.y);
				int innerTop = y - lineGap;
				int innerBottom = h + lineGap;
				int outerTop = innerTop - lineLengthVertical;
				int outerBottom = innerBottom + lineLengthVertical;
				if (!param.useTStyle)
					DrawTargetRect(c, x, outerTop, w, innerTop, param); // top
				DrawTargetRect(c, x, innerBottom, w, outerBottom, param); // bottom
			}

			// draw center dot
			if (param.drawDot) {
				thickness = static_cast<int>(param.dotThickness);
				x = static_cast<int>(pos.x) - thickness / 2;
				y = static_cast<int>(pos.y) - thickness / 2;
				w = x + thickness;
				h = y + thickness;

				if (param.drawOutline)
					DrawTargetOutlineRect(c, x, y, w, h, param);
				ui::SetColorNP(r, param.dotColor);
				c.Fill(x, y, w, h);
			}
		}
	} // namespace gui
} // namespace spades
