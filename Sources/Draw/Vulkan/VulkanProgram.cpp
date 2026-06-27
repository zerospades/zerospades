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

#include "VulkanProgram.h"
#include "VulkanShader.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Core/Exception.h>

#ifdef HAVE_SPIRV_CROSS
#include <spirv_cross/spirv_cross.hpp>
#endif

namespace spades {
	namespace draw {

		VulkanProgram::VulkanProgram(Handle<gui::SDLVulkanDevice> dev, const std::string& n)
		: device(std::move(dev)),
		  name(n),
		  linked(false),
		  descriptorSetLayout(VK_NULL_HANDLE),
		  pipelineLayout(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();
		}

		VulkanProgram::~VulkanProgram() {
			SPADES_MARK_FUNCTION();

			if (device) {
				VkDevice vkDevice = device->GetDevice();

				if (pipelineLayout != VK_NULL_HANDLE) {
					vkDestroyPipelineLayout(vkDevice, pipelineLayout, nullptr);
				}

				if (descriptorSetLayout != VK_NULL_HANDLE) {
					vkDestroyDescriptorSetLayout(vkDevice, descriptorSetLayout, nullptr);
				}
			}
		}

		void VulkanProgram::AttachShader(Handle<VulkanShader> shader) {
			if (linked) {
				SPRaise("Cannot attach shader to already linked program '%s'", name.c_str());
			}

			if (!shader || !shader->IsCompiled()) {
				SPRaise("Cannot attach uncompiled shader to program '%s'", name.c_str());
			}

			switch (shader->GetType()) {
				case VulkanShader::VertexShader:
					vertexShader = shader;
					break;
				case VulkanShader::FragmentShader:
					fragmentShader = shader;
					break;
				case VulkanShader::GeometryShader:
					geometryShader = shader;
					break;
				default:
					SPRaise("Unsupported shader type for program '%s'", name.c_str());
			}
		}

		void VulkanProgram::ReflectShaderResources() {
#ifdef HAVE_SPIRV_CROSS
			uniformBlocks.clear();
			textureBindings.clear();
			pushConstantRanges.clear();

			// Reflect both vertex and fragment shaders
			std::vector<Handle<VulkanShader>> shadersToReflect;
			if (vertexShader) shadersToReflect.push_back(vertexShader);
			if (fragmentShader) shadersToReflect.push_back(fragmentShader);
			if (geometryShader) shadersToReflect.push_back(geometryShader);

			for (const auto& shader : shadersToReflect) {
				const std::vector<uint32_t>& spirv = shader->GetSPIRV();
				if (spirv.empty()) {
					continue;
				}

				spirv_cross::Compiler compiler(spirv);
				spirv_cross::ShaderResources resources = compiler.get_shader_resources();

				VkShaderStageFlags stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
				if (shader->GetType() == VulkanShader::FragmentShader) {
					stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
				} else if (shader->GetType() == VulkanShader::GeometryShader) {
					stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT;
				}

				// Reflect uniform buffers
				for (const auto& ubo : resources.uniform_buffers) {
					uint32_t binding = compiler.get_decoration(ubo.id, spv::DecorationBinding);
					uint32_t set = compiler.get_decoration(ubo.id, spv::DecorationDescriptorSet);

					if (set != 0) {
						SPLog("Warning: Shader '%s' uses descriptor set %d, only set 0 is supported",
						      name.c_str(), set);
						continue;
					}

					const spirv_cross::SPIRType& type = compiler.get_type(ubo.type_id);
					uint32_t size = (uint32_t)compiler.get_declared_struct_size(type);

					std::string blockName = ubo.name;
					auto it = uniformBlocks.find(blockName);
					if (it != uniformBlocks.end()) {
						// Merge stage flags if block already exists
						it->second.stageFlags |= stageFlags;
					} else {
						VulkanUniformBlock block;
						block.binding = binding;
						block.size = size;
						block.stageFlags = stageFlags;
						block.name = blockName;
						uniformBlocks[blockName] = block;
					}
				}

				// Reflect texture samplers
				for (const auto& sampler : resources.sampled_images) {
					uint32_t binding = compiler.get_decoration(sampler.id, spv::DecorationBinding);
					uint32_t set = compiler.get_decoration(sampler.id, spv::DecorationDescriptorSet);

					if (set != 0) {
						SPLog("Warning: Shader '%s' uses descriptor set %d, only set 0 is supported",
						      name.c_str(), set);
						continue;
					}

					std::string samplerName = sampler.name;
					auto it = textureBindings.find(samplerName);
					if (it != textureBindings.end()) {
						// Merge stage flags if sampler already exists
						it->second.stageFlags |= stageFlags;
					} else {
						VulkanTextureBinding tex;
						tex.binding = binding;
						tex.stageFlags = stageFlags;
						tex.name = samplerName;
						textureBindings[samplerName] = tex;
					}
				}

				// Reflect push constants
				for (const auto& pc : resources.push_constant_buffers) {
					const spirv_cross::SPIRType& type = compiler.get_type(pc.type_id);
					uint32_t size = (uint32_t)compiler.get_declared_struct_size(type);

					VkPushConstantRange range;
					range.stageFlags = stageFlags;
					range.offset = 0;
					range.size = size;

					// Check if we already have a push constant range with the same size
					bool found = false;
					for (auto& existing : pushConstantRanges) {
						if (existing.size == size && existing.offset == 0) {
							existing.stageFlags |= stageFlags;
							found = true;
							break;
						}
					}

					if (!found) {
						pushConstantRanges.push_back(range);
					}
				}
			}

			SPLog("Reflected program '%s': %zu uniform blocks, %zu textures, %zu push constant ranges",
			      name.c_str(), uniformBlocks.size(), textureBindings.size(), pushConstantRanges.size());
#else
			// Fallback: No reflection available
			SPLog("Shader reflection not available for program '%s' (SPIRV-Cross not found)", name.c_str());
#endif
		}

		void VulkanProgram::CreateDescriptorSetLayout() {
			descriptorBindings.clear();

			// Add uniform buffer bindings
			for (const auto& pair : uniformBlocks) {
				const auto& block = pair.second;

				VkDescriptorSetLayoutBinding binding{};
				binding.binding = block.binding;
				binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				binding.descriptorCount = 1;
				binding.stageFlags = block.stageFlags;
				binding.pImmutableSamplers = nullptr;

				descriptorBindings.push_back(binding);
			}

			// Add texture sampler bindings
			for (const auto& pair : textureBindings) {
				const auto& tex = pair.second;

				VkDescriptorSetLayoutBinding binding{};
				binding.binding = tex.binding;
				binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				binding.descriptorCount = 1;
				binding.stageFlags = tex.stageFlags;
				binding.pImmutableSamplers = nullptr;

				descriptorBindings.push_back(binding);
			}

			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = static_cast<uint32_t>(descriptorBindings.size());
			layoutInfo.pBindings = descriptorBindings.data();

			VkResult result = vkCreateDescriptorSetLayout(device->GetDevice(), &layoutInfo,
			                                              nullptr, &descriptorSetLayout);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create descriptor set layout for program '%s' (error code: %d)",
				        name.c_str(), result);
			}

			SPLog("Created descriptor set layout for program '%s' with %zu bindings",
			      name.c_str(), descriptorBindings.size());
		}

		void VulkanProgram::CreatePipelineLayout() {
			VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
			pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutInfo.setLayoutCount = 1;
			pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
			pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
			pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.empty() ? nullptr : pushConstantRanges.data();

			VkResult result = vkCreatePipelineLayout(device->GetDevice(), &pipelineLayoutInfo,
			                                         nullptr, &pipelineLayout);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create pipeline layout for program '%s' (error code: %d)",
				        name.c_str(), result);
			}

			SPLog("Created pipeline layout for program '%s' with %zu push constant ranges",
			      name.c_str(), pushConstantRanges.size());
		}

		void VulkanProgram::Link() {
			SPADES_MARK_FUNCTION();

			if (linked) {
				SPLog("Warning: Program '%s' already linked", name.c_str());
				return;
			}

			if (!vertexShader) {
				SPRaise("Program '%s' must have a vertex shader", name.c_str());
			}

			if (!fragmentShader) {
				SPRaise("Program '%s' must have a fragment shader", name.c_str());
			}

			// Build shader stage create infos
			shaderStages.clear();

			VkPipelineShaderStageCreateInfo vertStageInfo{};
			vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
			vertStageInfo.module = vertexShader->GetShaderModule();
			vertStageInfo.pName = "main";
			shaderStages.push_back(vertStageInfo);

			VkPipelineShaderStageCreateInfo fragStageInfo{};
			fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			fragStageInfo.module = fragmentShader->GetShaderModule();
			fragStageInfo.pName = "main";
			shaderStages.push_back(fragStageInfo);

			if (geometryShader) {
				VkPipelineShaderStageCreateInfo geomStageInfo{};
				geomStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
				geomStageInfo.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
				geomStageInfo.module = geometryShader->GetShaderModule();
				geomStageInfo.pName = "main";
				shaderStages.push_back(geomStageInfo);
			}

			// Reflect shader resources
			ReflectShaderResources();

			// Create descriptor set layout
			CreateDescriptorSetLayout();

			// Create pipeline layout
			CreatePipelineLayout();

			linked = true;
			SPLog("Linked Vulkan program: %s", name.c_str());
		}

		const VulkanUniformBlock* VulkanProgram::GetUniformBlock(const std::string& blockName) const {
			auto it = uniformBlocks.find(blockName);
			if (it != uniformBlocks.end()) {
				return &it->second;
			}
			return nullptr;
		}

	} // namespace draw
} // namespace spades
