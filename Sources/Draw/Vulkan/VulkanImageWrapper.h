/*
 Copyright (c) 2013 Fran6nd

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

#include <Client/IImage.h>
#include <Core/Math.h>

namespace spades {
	namespace draw {
		class VulkanImage;

		class VulkanImageWrapper : public client::IImage {
			Handle<VulkanImage> image;
			float width;
			float height;

		protected:
			~VulkanImageWrapper();

		public:
			VulkanImageWrapper(Handle<VulkanImage> img, float w, float h);

			void Update(Bitmap& bmp, int x, int y) override;

			float GetWidth() override { return width; }
			float GetHeight() override { return height; }

			VulkanImage* GetVulkanImage() { return image.GetPointerOrNull(); }
		};

	} // namespace draw
} // namespace spades
