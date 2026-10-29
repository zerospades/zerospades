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

#include <vulkan/vulkan.h>
#include <string>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {

		class VulkanPipelineCache : public RefCountedObject {
		private:
			Handle<gui::SDLVulkanDevice> device;
			VkPipelineCache pipelineCache;
			std::string cachePath;

			bool LoadFromDisk();
			bool ValidateCacheHeader(const std::vector<uint8_t>& data);

		protected:
			~VulkanPipelineCache();

		public:
			VulkanPipelineCache(Handle<gui::SDLVulkanDevice> device);

			VkPipelineCache GetCache() const { return pipelineCache; }

			void SaveToDisk();
		};

	} // namespace draw
} // namespace spades
