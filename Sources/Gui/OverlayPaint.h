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

#pragma once

#include <cstddef>

#include <Core/Math.h>

namespace spades {
    namespace client {
        class IRenderer;
    } // namespace client
    namespace gui {
        void OverlayColorNP(client::IRenderer& renderer, const Vector4& c);
        void OverlayFillRect(client::IRenderer& renderer, float x, float y, float w, float h);
        void OverlayStrokeRect(client::IRenderer& renderer, float x, float y, float w, float h,
                               float thickness, const Vector4& c);
        /** Fills a convex polygon (either winding) with an anti-aliased edge: a
         *  fringe one framebuffer pixel wide straddles the outline, fading from
         *  the colour to transparent. `c` is not alpha premultiplied. */
        void OverlayFillConvexPolygon(client::IRenderer& renderer, const Vector2* points,
                                      std::size_t count, const Vector4& c);
        /** An anti-aliased straight line `width` 2D units thick. Lines thinner
         *  than a framebuffer pixel are drawn a pixel wide and proportionally
         *  fainter, so they keep their weight instead of breaking up. */
        void OverlayStrokeLine(client::IRenderer& renderer, const Vector2& a, const Vector2& b,
                               float width, const Vector4& c);
        /** Anti-aliased connected segments `width` 2D units thick, mitred at the
         *  joints so a translucent stroke covers each pixel once; `closed` links
         *  the last point back to the first. Thin strokes behave as in
         *  OverlayStrokeLine. */
        void OverlayStrokePolyline(client::IRenderer& renderer, const Vector2* points,
                                   std::size_t count, float width, const Vector4& c,
                                   bool closed);
        /** An anti-aliased filled circle. `c` is not alpha premultiplied. */
        void OverlayFillCircle(client::IRenderer& renderer, const Vector2& center, float radius,
                               const Vector4& c);
        /** An anti-aliased ring of stroke `width` centred on `radius`. */
        void OverlayStrokeCircle(client::IRenderer& renderer, const Vector2& center,
                                 float radius, float width, const Vector4& c);
        bool OverlayInRect(const Vector2& p, float x, float y, float w, float h);
    } // namespace gui
} // namespace spades
