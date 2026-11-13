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

#include <cstdint>
#include <string>

#include <Core/RefCountedObject.h>

namespace spades {
	class Bitmap;
	namespace gui {
		/**
		 * Lifts a subject off its background, from two pictures of the same
		 * scene taken on two different backgrounds.
		 *
		 * A pixel that is the same in both is the subject; one that differs as
		 * much as the backgrounds do is background; one in between is an edge,
		 * part of each. Nothing has to be known of how the scene was drawn or
		 * what was done to it afterwards, so the subject comes out as the
		 * screen shows it, whatever the renderer's filters made of it.
		 *
		 * The result is the subject over `background` (0x00BBGGRR), or with
		 * `transparent` over nothing, its edges keeping their coverage as
		 * alpha. `onFirst` and `onSecond` must be the same size.
		 */
		Handle<Bitmap> LiftSubject(Bitmap& onFirst, Bitmap& onSecond, std::uint32_t background,
		                           bool transparent);

		/** Saves `image` as the next free Screenshots/renderNNNN.png, which
		 *  keeps the alpha a screenshot's format may not; returns the name. */
		std::string SaveRenderImage(Bitmap& image);
	} // namespace gui
} // namespace spades
