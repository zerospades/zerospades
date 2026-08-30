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

#include "SoftwareCursor.h"

#include <Client/IImage.h>
#include <Gui/OverlayPaint.h>
#include <Client/IRenderer.h>

namespace spades {
    namespace gui {
        SoftwareCursor::SoftwareCursor(client::IRenderer& r) : renderer(&r) {
            cursorImg = renderer->RegisterImage("Gfx/UI/Cursor.png");
            position = MakeVector2(renderer->ScreenWidth() * 0.5F, renderer->ScreenHeight() * 0.5F);
        }
        void SoftwareCursor::Accumulate(float dx, float dy) {
            position.x = Clamp(position.x + dx, 0.0F, renderer->ScreenWidth());
            position.y = Clamp(position.y + dy, 0.0F, renderer->ScreenHeight());
        }
        void SoftwareCursor::SetPosition(const Vector2& p) { position = p; }
        void SoftwareCursor::Draw() const {
            OverlayColorNP(*renderer, MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
            if (cursorImg)
                renderer->DrawImage(cursorImg.GetPointerOrNull(),
                                    MakeVector2(position.x - 8.0F, position.y - 8.0F));
        }
    } // namespace gui
} // namespace spades
