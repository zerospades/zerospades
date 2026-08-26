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

#include <string>

#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace client {
		class IImage;
		class IRenderer;
	}
	namespace gui {
		/** Draws 16x16 country flag icons from the flag atlas. */
		class FlagIconRenderer : public RefCountedObject {
			Handle<client::IImage> atlas;
			client::IRenderer* renderer; // weak; owned by the screen

		protected:
			~FlagIconRenderer();

		public:
			FlagIconRenderer(client::IRenderer* renderer);

			/** Draws the flag for a two-letter country code centered at `pos`. */
			void DrawIcon(const std::string& name, Vector2 pos);
		};
	} // namespace gui
} // namespace spades
