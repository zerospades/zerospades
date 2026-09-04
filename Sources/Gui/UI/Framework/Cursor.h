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
		class IRenderer;
	} // namespace client
	namespace gui {
		namespace ui {
			/**
			 * A mouse cursor image with a hot spot, and the single place where the
			 * cursor's look is defined. Every UI that draws a pointer — the UI
			 * framework and the in-game overlays alike — paints it through here so
			 * they cannot drift apart.
			 */
			class Cursor : public RefCountedObject {
				Handle<client::IRenderer> renderer;
				Handle<client::IImage> image;
				Vector2 hotSpot;

			protected:
				~Cursor();

			public:
				Cursor(client::IRenderer& renderer, client::IImage* image, Vector2 hotSpot);

				/** Creates the standard UI pointer. */
				static Handle<Cursor> CreateDefault(client::IRenderer& renderer);

				/**
				 * Draws the cursor at `pos`.
				 *
				 * `time` is the owner's seconds clock, and drives the shared hue cycle.
				 * It is passed in rather than kept here because cursors are short-lived
				 * — widgets build a fresh one on every `MouseEnter` — so an internal
				 * clock would restart the hue each time one is hovered.
				 */
				void Render(Vector2 pos, float time) const;
			};
		} // namespace ui
	} // namespace gui
} // namespace spades
