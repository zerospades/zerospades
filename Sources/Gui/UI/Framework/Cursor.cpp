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

#include "Cursor.h"
#include <Client/IImage.h>
#include <Client/IRenderer.h>

namespace spades {
	namespace gui {
		namespace ui {
			namespace {
				const char* const kDefaultCursorImage = "Gfx/UI/Cursor.png";

				/** Hot spot of `kDefaultCursorImage`, in pixels from its top left corner. */
				const Vector2 kDefaultCursorHotSpot = MakeVector2(8.0F, 8.0F);

				/** Hue cycles per second of the cursor's rainbow tint. */
				constexpr float kCursorHueCycleSpeed = 0.1F;
			} // namespace

			Cursor::Cursor(client::IRenderer& renderer, client::IImage* image, Vector2 hotSpot)
			    : renderer(&renderer), image(image), hotSpot(hotSpot) {}

			Cursor::~Cursor() {}

			Handle<Cursor> Cursor::CreateDefault(client::IRenderer& renderer) {
				Handle<client::IImage> image = renderer.RegisterImage(kDefaultCursorImage);
				return Handle<Cursor>::New(renderer, image.GetPointerOrNull(),
				                           kDefaultCursorHotSpot);
			}

			void Cursor::Render(Vector2 pos, float time) const {
				if (!image)
					return;

				// slowly cycling rainbow tint
				Vector3 rgb = HSV2RGB(std::max(time, 0.0F) * kCursorHueCycleSpeed, 1.0F, 1.0F);
				renderer->SetColorAlphaPremultiplied(MakeVector4(rgb.x, rgb.y, rgb.z, 1.0F));
				renderer->DrawImage(image.GetPointerOrNull(),
				                    MakeVector2(pos.x - hotSpot.x, pos.y - hotSpot.y));
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
