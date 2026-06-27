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

#include <map>
#include <string>
#include <Client/IImage.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanRenderer;
		class VulkanImageWrapper;

		class VulkanImageManager {
			VulkanRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;
			std::map<std::string, Handle<client::IImage>> images;
			Handle<client::IImage> whiteImage;

			Handle<client::IImage> CreateImage(const std::string& name);

		public:
			VulkanImageManager(VulkanRenderer& r, Handle<gui::SDLVulkanDevice> dev);
			~VulkanImageManager();

			Handle<client::IImage> RegisterImage(const std::string& name);
			Handle<client::IImage> GetWhiteImage();

			void ClearCache();
		};
	} // namespace draw
} // namespace spades
