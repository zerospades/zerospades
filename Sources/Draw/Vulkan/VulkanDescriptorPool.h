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
#include <Core/RefCountedObject.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {

		class VulkanDescriptorPool : public RefCountedObject {
			Handle<gui::SDLVulkanDevice> device;
			VkDescriptorPool descriptorPool;

			uint32_t maxSets;

		protected:
			~VulkanDescriptorPool();

		public:
			VulkanDescriptorPool(Handle<gui::SDLVulkanDevice> device, uint32_t maxSets = 1000);

			VkDescriptorPool GetPool() const { return descriptorPool; }

			// Allocate descriptor set
			VkDescriptorSet AllocateDescriptorSet(VkDescriptorSetLayout layout);

			// Reset the pool (frees all allocated sets)
			void Reset();
		};

	} // namespace draw
} // namespace spades
