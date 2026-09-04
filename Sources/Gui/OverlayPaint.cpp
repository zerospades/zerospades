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

#include "OverlayPaint.h"

#include <Client/IImage.h>
#include <Client/IRenderer.h>

namespace spades {
    namespace gui {
        void OverlayColorNP(client::IRenderer& renderer, const Vector4& c) {
            renderer.SetColorAlphaPremultiplied(MakeVector4(c.x * c.w, c.y * c.w, c.z * c.w, c.w));
        }
        void OverlayFillRect(client::IRenderer& renderer, float x, float y, float w, float h) {
            renderer.DrawImage((client::IImage*)NULL, AABB2(x, y, w, h));
        }
        void OverlayStrokeRect(client::IRenderer& renderer, float x, float y, float w, float h,
                               float t, const Vector4& c) {
            OverlayColorNP(renderer, c);
            OverlayFillRect(renderer, x, y, w, t);
            OverlayFillRect(renderer, x, y + h - t, w, t);
            OverlayFillRect(renderer, x, y, t, h);
            OverlayFillRect(renderer, x + w - t, y, t, h);
        }
        bool OverlayInRect(const Vector2& p, float x, float y, float w, float h) {
            return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
        }
    } // namespace gui
} // namespace spades
