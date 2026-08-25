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

#pragma once

#include <Client/IRenderer.h>
#include <Core/Math.h>

namespace spades {
	namespace gui {
		namespace ui {
			/**
			 * Sets the renderer draw color from a non-premultiplied-alpha value.
			 *
			 * The AngelScript UI set colors through the renderer's `ColorNP` property,
			 * which premultiplied the color before drawing. `IRenderer` only exposes the
			 * premultiplied setter, so we premultiply here to match the original look.
			 */
			inline void SetColorNP(client::IRenderer& r, const Vector4& c) {
				r.SetColorAlphaPremultiplied(MakeVector4(c.x * c.w, c.y * c.w, c.z * c.w, c.w));
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
