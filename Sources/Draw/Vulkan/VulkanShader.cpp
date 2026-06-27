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

#include "VulkanShader.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Core/Exception.h>

namespace spades {
	namespace draw {

		VulkanShader::VulkanShader(Handle<gui::SDLVulkanDevice> dev, Type t, const std::string& n)
		: device(std::move(dev)),
		  shaderModule(VK_NULL_HANDLE),
		  type(t),
		  name(n),
		  compiled(false) {
			SPADES_MARK_FUNCTION();
		}

		VulkanShader::~VulkanShader() {
			SPADES_MARK_FUNCTION();

			if (shaderModule != VK_NULL_HANDLE && device) {
				vkDestroyShaderModule(device->GetDevice(), shaderModule, nullptr);
			}
		}

		void VulkanShader::LoadSPIRV(const std::vector<uint32_t>& spirv) {
			SPADES_MARK_FUNCTION();

			if (compiled) {
				SPRaise("Shader '%s' already compiled", name.c_str());
			}

			spirvCode = spirv;

			VkShaderModuleCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			createInfo.codeSize = spirvCode.size() * sizeof(uint32_t);
			createInfo.pCode = spirvCode.data();

			VkResult result = vkCreateShaderModule(device->GetDevice(), &createInfo, nullptr, &shaderModule);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create shader module from SPIR-V (error code: %d)", result);
			}

			compiled = true;
			SPLog("Loaded Vulkan shader from SPIR-V: %s (%zu words)", name.c_str(), spirvCode.size());
		}

		VkShaderStageFlagBits VulkanShader::GetVkStage() const {
			switch (type) {
				case VertexShader: return VK_SHADER_STAGE_VERTEX_BIT;
				case FragmentShader: return VK_SHADER_STAGE_FRAGMENT_BIT;
				case GeometryShader: return VK_SHADER_STAGE_GEOMETRY_BIT;
				case ComputeShader: return VK_SHADER_STAGE_COMPUTE_BIT;
				default: return VK_SHADER_STAGE_VERTEX_BIT;
			}
		}

	} // namespace draw
} // namespace spades
