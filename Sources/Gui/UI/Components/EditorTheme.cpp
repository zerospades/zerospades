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

#include "EditorTheme.h"

#include <Gui/OverlayPaint.h>

namespace spades {
	namespace gui {
		namespace EditorTheme {
			Vector4 PanelFill() { return MakeVector4(0.11F, 0.115F, 0.13F, 0.97F); }
			Vector4 PanelBorder() { return MakeVector4(0.30F, 0.32F, 0.37F, 1.0F); }
			Vector4 PanelShadow() { return MakeVector4(0.0F, 0.0F, 0.0F, 0.35F); }
			Vector4 Separator() { return MakeVector4(0.26F, 0.28F, 0.32F, 1.0F); }

			Vector4 Text() { return MakeVector4(0.90F, 0.91F, 0.93F, 1.0F); }
			Vector4 TextDim() { return MakeVector4(0.62F, 0.64F, 0.69F, 1.0F); }
			Vector4 TextFaint() { return MakeVector4(0.47F, 0.49F, 0.54F, 1.0F); }
			Vector4 TextShadow() { return MakeVector4(0.0F, 0.0F, 0.0F, 0.9F); }

			Vector4 Highlight() { return MakeVector4(0.24F, 0.42F, 0.72F, 1.0F); }
			Vector4 Refused() { return MakeVector4(1.0F, 0.38F, 0.33F, 1.0F); }
			Vector4 DropTarget() { return MakeVector4(0.35F, 0.68F, 1.0F, 1.0F); }

			Vector4 FieldFill() { return MakeVector4(0.04F, 0.05F, 0.07F, 0.92F); }
			Vector4 TextSelection() { return MakeVector4(0.24F, 0.42F, 0.72F, 1.0F); }
			Vector4 Caret() { return MakeVector4(1.0F, 1.0F, 1.0F, 1.0F); }

			Vector4 Active() { return MakeVector4(1.0F, 0.65F, 0.2F, 1.0F); }
			Vector4 Selected() { return MakeVector4(0.95F, 0.52F, 0.18F, 1.0F); }

			void PaintPanel(client::IRenderer& renderer, float x, float y, float width,
			                float height) {
				OverlayColorNP(renderer, PanelShadow());
				OverlayFillRect(renderer, x + 3.0F, y + 4.0F, width, height);
				OverlayColorNP(renderer, PanelFill());
				OverlayFillRect(renderer, x, y, width, height);
				OverlayStrokeRect(renderer, x, y, width, height, 1.0F, PanelBorder());
			}
		} // namespace EditorTheme
	} // namespace gui
} // namespace spades
