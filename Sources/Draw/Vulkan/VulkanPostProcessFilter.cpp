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

#include "VulkanPostProcessFilter.h"
#include "VulkanRenderer.h"
#include <Gui/SDLVulkanDevice.h>

namespace spades {
	namespace draw {
		VulkanPostProcessFilter::VulkanPostProcessFilter(VulkanRenderer& r)
			: renderer(r),
			  device(r.GetDevice()),
			  pipeline(VK_NULL_HANDLE),
			  pipelineLayout(VK_NULL_HANDLE),
			  descriptorSetLayout(VK_NULL_HANDLE),
			  renderPass(VK_NULL_HANDLE) {
		}

		VulkanPostProcessFilter::~VulkanPostProcessFilter() {
			DestroyResources();
		}

		void VulkanPostProcessFilter::DestroyResources() {
			if (pipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device->GetDevice(), pipeline, nullptr);
				pipeline = VK_NULL_HANDLE;
			}
			if (pipelineLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device->GetDevice(), pipelineLayout, nullptr);
				pipelineLayout = VK_NULL_HANDLE;
			}
			if (descriptorSetLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device->GetDevice(), descriptorSetLayout, nullptr);
				descriptorSetLayout = VK_NULL_HANDLE;
			}
			if (renderPass != VK_NULL_HANDLE) {
				vkDestroyRenderPass(device->GetDevice(), renderPass, nullptr);
				renderPass = VK_NULL_HANDLE;
			}
		}
	}
}
