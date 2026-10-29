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

#include <string>

#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace client {
		class IImage;
	}
	namespace gui {
		/**
		 * An image to model from, laid flat in the document across one axis
		 * (see IVoxelEditContext::Reference): a front, a side or a top view.
		 *
		 * A setting of the editor, as the brush colour is: not saved with the
		 * document, and never an undo step. Coordinates are the document's
		 * voxels; the plane is across `axis`, at `depth` along it, and the
		 * image runs along the other two (see ReferenceAxes).
		 */
		struct ReferenceImage {
			std::string path; // where it was loaded from; empty when there is none
			Handle<client::IImage> image;
			int pixelWidth = 0, pixelHeight = 0;
			bool visible = true;
			// How many voxels the image's width spans: its scale.
			float size = 32.0F;
			// Where its middle is, along the plane's two directions.
			float across = 0.0F, along = 0.0F;
			// Where the plane is along its own axis.
			float depth = 0.0F;
			float opacity = 0.5F;
			// Mirrored left to right, for an image drawn from the other side.
			bool flipped = false;

			bool Loaded() const { return image && pixelWidth > 0 && pixelHeight > 0; }
			/** How many voxels its height spans, from its size and its shape. */
			float Height() const {
				return pixelWidth > 0 ? size * float(pixelHeight) / float(pixelWidth) : size;
			}
		};

		/**
		 * The directions an image across `axis` runs in: `u` along its width
		 * and `v` down its height. The world's Z points down, so an image's top
		 * is at the low end of Z, as a front or side view is seen; a top view
		 * (across Z) has X along its width and Y down it.
		 */
		inline void ReferenceAxes(int axis, int& u, int& v) {
			u = axis == 0 ? 1 : 0;
			v = axis == 2 ? 1 : 2;
		}
	} // namespace gui
} // namespace spades
