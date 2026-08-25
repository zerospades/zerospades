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

#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace client {
		class IImage;
	}
	namespace gui {
		namespace ui {
			class UIManager;

			/** A mouse cursor image with a hot spot, rendered by `UIManager`. */
			class Cursor : public RefCountedObject {
				UIManager* manager; // weak; the manager owns the cursor
				Handle<client::IImage> image;
				Vector2 hotSpot;

			protected:
				~Cursor();

			public:
				Cursor(UIManager* manager, client::IImage* image, Vector2 hotSpot);

				void Render(Vector2 pos);
			};
		} // namespace ui
	} // namespace gui
} // namespace spades
