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

#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
    namespace client {
        class IRenderer;
        class IImage;
    } // namespace client
    namespace gui {
        class SoftwareCursor {
        public:
            explicit SoftwareCursor(client::IRenderer& renderer);
            void Accumulate(float dx, float dy);
            void SetPosition(const Vector2& p);
            const Vector2& GetPosition() const { return position; }
            void Draw() const;
        private:
            Handle<client::IRenderer> renderer;
            Handle<client::IImage> cursorImg;
            Vector2 position;
        };
    } // namespace gui
} // namespace spades
