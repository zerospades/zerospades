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

#include <string>
#include <vector>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanShader;

		// Represents uniform buffer binding information
		struct VulkanUniformBlock {
			uint32_t binding;
			uint32_t size;
			VkShaderStageFlags stageFlags;
			std::string name;
		};

		// Represents texture/sampler binding information
		struct VulkanTextureBinding {
			uint32_t binding;
			VkShaderStageFlags stageFlags;
			std::string name;
		};

		class VulkanProgram : public RefCountedObject {
			Handle<gui::SDLVulkanDevice> device;
			std::string name;
			bool linked;

			// Attached shaders
			Handle<VulkanShader> vertexShader;
			Handle<VulkanShader> fragmentShader;
			Handle<VulkanShader> geometryShader;

			// Pipeline shader stage info
			std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

			// Descriptor set layout
			VkDescriptorSetLayout descriptorSetLayout;
			std::vector<VkDescriptorSetLayoutBinding> descriptorBindings;

			// Uniform blocks and texture bindings (from reflection)
			std::unordered_map<std::string, VulkanUniformBlock> uniformBlocks;
			std::unordered_map<std::string, VulkanTextureBinding> textureBindings;

			// Push constant ranges (from reflection)
			std::vector<VkPushConstantRange> pushConstantRanges;

			// Pipeline layout
			VkPipelineLayout pipelineLayout;

			void ReflectShaderResources();
			void CreateDescriptorSetLayout();
			void CreatePipelineLayout();

		protected:
			~VulkanProgram();

		public:
			VulkanProgram(Handle<gui::SDLVulkanDevice> device, const std::string& name = "(unnamed)");

			void AttachShader(Handle<VulkanShader> shader);

			// Link the program (creates descriptor set layout and pipeline layout)
			void Link();

			bool IsLinked() const { return linked; }
			const std::string& GetName() const { return name; }

			// Get shader stage create infos for pipeline creation
			const std::vector<VkPipelineShaderStageCreateInfo>& GetShaderStages() const { return shaderStages; }

			VkDescriptorSetLayout GetDescriptorSetLayout() const { return descriptorSetLayout; }
			VkPipelineLayout GetPipelineLayout() const { return pipelineLayout; }

			// Query uniform blocks and texture bindings
			const std::unordered_map<std::string, VulkanUniformBlock>& GetUniformBlocks() const { return uniformBlocks; }
			const std::unordered_map<std::string, VulkanTextureBinding>& GetTextureBindings() const { return textureBindings; }

			// Get specific uniform block by name
			const VulkanUniformBlock* GetUniformBlock(const std::string& name) const;
		};

	} // namespace draw
} // namespace spades
