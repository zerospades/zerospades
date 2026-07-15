/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of OpenSpades.

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
#include "UIManager.h"
#include <Client/IImage.h>
#include <Client/IRenderer.h>

namespace spades {
	namespace gui {
		namespace ui {
			Cursor::Cursor(UIManager* manager, client::IImage* image, Vector2 hotSpot)
			    : manager(manager), image(image), hotSpot(hotSpot) {}

			Cursor::~Cursor() {}

			void Cursor::Render(Vector2 pos) {
				client::IRenderer& r = manager->GetRenderer();

				// slowly cycling rainbow tint, matching the original UI framework
				Vector3 rgb = HSV2RGB(std::max(manager->time * 0.1F, 0.0F), 1.0F, 1.0F);
				r.SetColorAlphaPremultiplied(MakeVector4(rgb.x, rgb.y, rgb.z, 1.0F));
				r.DrawImage(image.GetPointerOrNull(),
				            MakeVector2(pos.x - hotSpot.x, pos.y - hotSpot.y));
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
