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

#include "VulkanShadowMapRenderer.h"
#include "VulkanSpirvCache.h"
#include "VulkanRenderer.h"
#include "VulkanMapRenderer.h"
#include "VulkanModelRenderer.h"
#include "VulkanImage.h"
#include "VulkanBuffer.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Settings.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Client/SceneDefinition.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

SPADES_SETTING(r_shadowMapSize);
SPADES_SETTING(r_modelShadows);

namespace spades {
	namespace draw {

		namespace {
			// std140-compatible UBO consumed by the lit shaders' EvaluteModelShadow().
			// mat4[N] are 16-byte aligned; enabled follows the array.
			struct SamplingUniforms {
				Matrix4 cascadeMatrix[VulkanShadowMapRenderer::NumSlices];
				int32_t enabled;
				int32_t pad[3];
			};
		} // namespace

		VulkanShadowMapRenderer::VulkanShadowMapRenderer(VulkanRenderer& r)
		: renderer(r),
		  device(r.GetDevice()),
		  renderPass(VK_NULL_HANDLE),
		  pipelineLayout(VK_NULL_HANDLE),
		  descriptorSetLayout(VK_NULL_HANDLE),
		  pipeline(VK_NULL_HANDLE),
		  descriptorPool(VK_NULL_HANDLE),
		  samplingSetLayout(VK_NULL_HANDLE),
		  samplingPool(VK_NULL_HANDLE),
		  samplingDescriptorSet(VK_NULL_HANDLE),
		  samplingSampler(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			textureSize = (int)r_shadowMapSize;
			if (textureSize < 128) textureSize = 128;
			if (textureSize > 4096) textureSize = 4096;

			SPLog("Creating shadow map renderer with size %dx%d", textureSize, textureSize);

			for (int i = 0; i < NumSlices; i++) {
				framebuffers[i] = VK_NULL_HANDLE;
				shadowMapImages[i] = nullptr;
				descriptorSets[i] = VK_NULL_HANDLE;
				uniformBuffers[i] = nullptr;
			}

			try {
				CreateRenderPass();
				CreateFramebuffers();
				CreatePipeline();
				CreateDescriptorSets();
				CreateSamplingResources();
			} catch (...) {
				DestroyResources();
				throw;
			}
		}

		VulkanShadowMapRenderer::~VulkanShadowMapRenderer() {
			SPADES_MARK_FUNCTION();
			DestroyResources();
		}

		void VulkanShadowMapRenderer::CreateRenderPass() {
			SPADES_MARK_FUNCTION();

			VkAttachmentDescription depthAttachment{};
			depthAttachment.format = VK_FORMAT_D32_SFLOAT;
			depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

			VkAttachmentReference depthAttachmentRef{};
			depthAttachmentRef.attachment = 0;
			depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass{};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 0;
			subpass.pDepthStencilAttachment = &depthAttachmentRef;

			VkSubpassDependency dependencies[2]{};
			// Incoming: serialize depth writes against the prior frame.
			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			dependencies[0].srcAccessMask = 0;
			dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			// Outgoing: make the written depth available to the lit shaders that sample
			// this map later in the frame (the layout transition alone doesn't make the
			// writes visible to FRAGMENT_SHADER reads on a TBDR GPU).
			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			VkRenderPassCreateInfo renderPassInfo{};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = 1;
			renderPassInfo.pAttachments = &depthAttachment;
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = 2;
			renderPassInfo.pDependencies = dependencies;

			if (vkCreateRenderPass(device->GetDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
				SPRaise("Failed to create shadow map render pass");
			}
		}

		void VulkanShadowMapRenderer::CreateFramebuffers() {
			SPADES_MARK_FUNCTION();

			for (int i = 0; i < NumSlices; i++) {
				shadowMapImages[i] = Handle<VulkanImage>::New(
					device,
					(uint32_t)textureSize, (uint32_t)textureSize,
					VK_FORMAT_D32_SFLOAT,
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
				);

				// VulkanImage's constructor builds a COLOR-aspect view by default; a
				// D32 depth image needs a DEPTH-aspect view to work as both a depth
				// attachment and a sampled texture (the framebuffer manager does the
				// same for its depth images).
				shadowMapImages[i]->CreateImageView(VK_IMAGE_ASPECT_DEPTH_BIT);

				VkImageView attachments[] = { shadowMapImages[i]->GetImageView() };

				VkFramebufferCreateInfo framebufferInfo{};
				framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				framebufferInfo.renderPass = renderPass;
				framebufferInfo.attachmentCount = 1;
				framebufferInfo.pAttachments = attachments;
				framebufferInfo.width = textureSize;
				framebufferInfo.height = textureSize;
				framebufferInfo.layers = 1;

				if (vkCreateFramebuffer(device->GetDevice(), &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS) {
					SPRaise("Failed to create shadow map framebuffer %d", i);
				}
			}
		}

		void VulkanShadowMapRenderer::CreatePipeline() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			// Load SPIR-V shaders
			auto LoadSPIRVFile = [](const char* filename) -> std::vector<uint32_t> {
				return SpirvCache::Load(filename);
			};

			std::vector<uint32_t> vertCode = LoadSPIRVFile("Shaders/Vulkan/ShadowMap.vert.spv");
			std::vector<uint32_t> fragCode = LoadSPIRVFile("Shaders/Vulkan/ShadowMap.frag.spv");

			// Create shader modules
			VkShaderModuleCreateInfo vertShaderModuleInfo{};
			vertShaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			vertShaderModuleInfo.codeSize = vertCode.size() * sizeof(uint32_t);
			vertShaderModuleInfo.pCode = vertCode.data();

			VkShaderModule vertShaderModule;
			if (vkCreateShaderModule(vkDevice, &vertShaderModuleInfo, nullptr, &vertShaderModule) != VK_SUCCESS) {
				SPRaise("Failed to create vertex shader module for shadow map");
			}

			VkShaderModuleCreateInfo fragShaderModuleInfo{};
			fragShaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			fragShaderModuleInfo.codeSize = fragCode.size() * sizeof(uint32_t);
			fragShaderModuleInfo.pCode = fragCode.data();

			VkShaderModule fragShaderModule;
			if (vkCreateShaderModule(vkDevice, &fragShaderModuleInfo, nullptr, &fragShaderModule) != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
				SPRaise("Failed to create fragment shader module for shadow map");
			}

			// Shader stages
			VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
			vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
			vertShaderStageInfo.module = vertShaderModule;
			vertShaderStageInfo.pName = "main";

			VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
			fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			fragShaderStageInfo.module = fragShaderModule;
			fragShaderStageInfo.pName = "main";

			VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

			// Create descriptor set layout
			VkDescriptorSetLayoutBinding uboLayoutBinding{};
			uboLayoutBinding.binding = 0;
			uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			uboLayoutBinding.descriptorCount = 1;
			uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			uboLayoutBinding.pImmutableSamplers = nullptr;

			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = 1;
			layoutInfo.pBindings = &uboLayoutBinding;

			if (vkCreateDescriptorSetLayout(vkDevice, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
				vkDestroyShaderModule(vkDevice, fragShaderModule, nullptr);
				SPRaise("Failed to create descriptor set layout for shadow map");
			}

			// Create pipeline layout with push constants for model origin
			VkPushConstantRange pushConstantRange{};
			pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			pushConstantRange.offset = 0;
			pushConstantRange.size = sizeof(Vector3); // modelOrigin (12 bytes)

			VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
			pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutInfo.setLayoutCount = 1;
			pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
			pipelineLayoutInfo.pushConstantRangeCount = 1;
			pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

			if (vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
				vkDestroyShaderModule(vkDevice, fragShaderModule, nullptr);
				SPRaise("Failed to create pipeline layout for shadow map");
			}

			// Vertex input - must match VulkanMapChunk::Vertex format
			// The map chunk vertex buffer has stride 20 bytes, with uint8 x,y,z at offset 0
			VkVertexInputBindingDescription bindingDescription{};
			bindingDescription.binding = 0;
			bindingDescription.stride = 20; // Same as VulkanMapChunk::Vertex
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			VkVertexInputAttributeDescription attributeDescription{};
			attributeDescription.binding = 0;
			attributeDescription.location = 0;
			attributeDescription.format = VK_FORMAT_R8G8B8_UINT; // uint8 x, y, z
			attributeDescription.offset = 0;

			VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
			vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputInfo.vertexBindingDescriptionCount = 1;
			vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
			vertexInputInfo.vertexAttributeDescriptionCount = 1;
			vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

			// Input assembly
			VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
			inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			inputAssembly.primitiveRestartEnable = VK_FALSE;

			// Viewport state (dynamic)
			VkPipelineViewportStateCreateInfo viewportState{};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.pViewports = nullptr;
			viewportState.scissorCount = 1;
			viewportState.pScissors = nullptr;

			// Rasterization
			VkPipelineRasterizationStateCreateInfo rasterizer{};
			rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizer.depthClampEnable = VK_FALSE;
			rasterizer.rasterizerDiscardEnable = VK_FALSE;
			rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
			rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rasterizer.depthBiasEnable = VK_TRUE;

			// Multisampling
			VkPipelineMultisampleStateCreateInfo multisampling{};
			multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampling.sampleShadingEnable = VK_FALSE;
			multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			// Depth stencil
			VkPipelineDepthStencilStateCreateInfo depthStencil{};
			depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencil.depthTestEnable = VK_TRUE;
			depthStencil.depthWriteEnable = VK_TRUE;
			depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
			depthStencil.depthBoundsTestEnable = VK_FALSE;
			depthStencil.stencilTestEnable = VK_FALSE;

			// Color blend (no color attachments for depth-only pass)
			VkPipelineColorBlendStateCreateInfo colorBlending{};
			colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlending.logicOpEnable = VK_FALSE;
			colorBlending.attachmentCount = 0;
			colorBlending.pAttachments = nullptr;

			// Dynamic state
			VkDynamicState dynamicStates[] = {
				VK_DYNAMIC_STATE_VIEWPORT,
				VK_DYNAMIC_STATE_SCISSOR,
				VK_DYNAMIC_STATE_DEPTH_BIAS
			};

			VkPipelineDynamicStateCreateInfo dynamicState{};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = 3;
			dynamicState.pDynamicStates = dynamicStates;

			// Create graphics pipeline
			VkGraphicsPipelineCreateInfo pipelineInfo{};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = 2;
			pipelineInfo.pStages = shaderStages;
			pipelineInfo.pVertexInputState = &vertexInputInfo;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterizer;
			pipelineInfo.pMultisampleState = &multisampling;
			pipelineInfo.pDepthStencilState = &depthStencil;
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.pDynamicState = &dynamicState;
			pipelineInfo.layout = pipelineLayout;
			pipelineInfo.renderPass = renderPass;
			pipelineInfo.subpass = 0;
			pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

			VkResult result = vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1, &pipelineInfo, nullptr, &pipeline);

			// Clean up shader modules
			vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
			vkDestroyShaderModule(vkDevice, fragShaderModule, nullptr);

			if (result != VK_SUCCESS) {
				SPRaise("Failed to create shadow map graphics pipeline");
			}

			SPLog("Shadow map pipeline created successfully");
		}

		void VulkanShadowMapRenderer::CreateDescriptorSets() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			// Create descriptor pool
			VkDescriptorPoolSize poolSize{};
			poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			poolSize.descriptorCount = NumSlices;

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes = &poolSize;
			poolInfo.maxSets = NumSlices;

			if (vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
				SPRaise("Failed to create shadow map descriptor pool");
			}

			// Create uniform buffers and descriptor sets for each cascade
			for (int i = 0; i < NumSlices; i++) {
				// Create uniform buffer
				uniformBuffers[i] = Handle<VulkanBuffer>::New(
					device,
					sizeof(Matrix4),
					VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
				);

				// Allocate descriptor set
				VkDescriptorSetAllocateInfo allocInfo{};
				allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				allocInfo.descriptorPool = descriptorPool;
				allocInfo.descriptorSetCount = 1;
				allocInfo.pSetLayouts = &descriptorSetLayout;

				if (vkAllocateDescriptorSets(vkDevice, &allocInfo, &descriptorSets[i]) != VK_SUCCESS) {
					SPRaise("Failed to allocate shadow map descriptor set %d", i);
				}

				// Update descriptor set
				VkDescriptorBufferInfo bufferInfo{};
				bufferInfo.buffer = uniformBuffers[i]->GetBuffer();
				bufferInfo.offset = 0;
				bufferInfo.range = sizeof(Matrix4);

				VkWriteDescriptorSet descriptorWrite{};
				descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				descriptorWrite.dstSet = descriptorSets[i];
				descriptorWrite.dstBinding = 0;
				descriptorWrite.dstArrayElement = 0;
				descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				descriptorWrite.descriptorCount = 1;
				descriptorWrite.pBufferInfo = &bufferInfo;

				vkUpdateDescriptorSets(vkDevice, 1, &descriptorWrite, 0, nullptr);
			}

			SPLog("Shadow map descriptor sets created successfully");
		}

		void VulkanShadowMapRenderer::CreateSamplingResources() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			// Depth sampler. Border = opaque white so out-of-cascade lookups read the
			// far plane (1.0) and resolve to "lit". NEAREST: manual depth compare can't
			// linearly filter depth values.
			VkSamplerCreateInfo samplerInfo{};
			samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerInfo.magFilter = VK_FILTER_NEAREST;
			samplerInfo.minFilter = VK_FILTER_NEAREST;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			samplerInfo.maxLod = 0.0f;
			if (vkCreateSampler(vkDevice, &samplerInfo, nullptr, &samplingSampler) != VK_SUCCESS) {
				SPRaise("Failed to create shadow sampling sampler");
			}

			// Cascade-matrix UBO (host-visible, updated each frame). Start disabled.
			samplingUniformBuffer = Handle<VulkanBuffer>::New(
				device, sizeof(SamplingUniforms),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			{
				SamplingUniforms init{};
				init.enabled = 0;
				void* dst = samplingUniformBuffer->Map();
				memcpy(dst, &init, sizeof(init));
				samplingUniformBuffer->Unmap();
			}

			// Layout: binding 0 = UBO (read in VS + FS), bindings 1..N = depth maps (FS).
			std::vector<VkDescriptorSetLayoutBinding> bindings;
			VkDescriptorSetLayoutBinding uboBinding{};
			uboBinding.binding = 0;
			uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			uboBinding.descriptorCount = 1;
			uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings.push_back(uboBinding);
			for (int i = 0; i < NumSlices; i++) {
				VkDescriptorSetLayoutBinding texBinding{};
				texBinding.binding = (uint32_t)(1 + i);
				texBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				texBinding.descriptorCount = 1;
				texBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
				bindings.push_back(texBinding);
			}

			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = (uint32_t)bindings.size();
			layoutInfo.pBindings = bindings.data();
			if (vkCreateDescriptorSetLayout(vkDevice, &layoutInfo, nullptr, &samplingSetLayout) != VK_SUCCESS) {
				SPRaise("Failed to create shadow sampling descriptor set layout");
			}

			VkDescriptorPoolSize poolSizes[2]{};
			poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			poolSizes[0].descriptorCount = 1;
			poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSizes[1].descriptorCount = NumSlices;

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 2;
			poolInfo.pPoolSizes = poolSizes;
			poolInfo.maxSets = 1;
			if (vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &samplingPool) != VK_SUCCESS) {
				SPRaise("Failed to create shadow sampling descriptor pool");
			}

			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = samplingPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &samplingSetLayout;
			if (vkAllocateDescriptorSets(vkDevice, &allocInfo, &samplingDescriptorSet) != VK_SUCCESS) {
				SPRaise("Failed to allocate shadow sampling descriptor set");
			}

			// The depth images never change, so write the whole set once here; only the
			// UBO contents are updated per frame.
			VkDescriptorBufferInfo bufInfo{};
			bufInfo.buffer = samplingUniformBuffer->GetBuffer();
			bufInfo.offset = 0;
			bufInfo.range = sizeof(SamplingUniforms);

			std::vector<VkDescriptorImageInfo> imageInfos(NumSlices);
			std::vector<VkWriteDescriptorSet> writes;

			VkWriteDescriptorSet uboWrite{};
			uboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			uboWrite.dstSet = samplingDescriptorSet;
			uboWrite.dstBinding = 0;
			uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			uboWrite.descriptorCount = 1;
			uboWrite.pBufferInfo = &bufInfo;
			writes.push_back(uboWrite);

			for (int i = 0; i < NumSlices; i++) {
				imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
				imageInfos[i].imageView = shadowMapImages[i]->GetImageView();
				imageInfos[i].sampler = samplingSampler;

				VkWriteDescriptorSet texWrite{};
				texWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				texWrite.dstSet = samplingDescriptorSet;
				texWrite.dstBinding = (uint32_t)(1 + i);
				texWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				texWrite.descriptorCount = 1;
				texWrite.pImageInfo = &imageInfos[i];
				writes.push_back(texWrite);
			}

			vkUpdateDescriptorSets(vkDevice, (uint32_t)writes.size(), writes.data(), 0, nullptr);

			SPLog("Shadow sampling resources created successfully");
		}

		void VulkanShadowMapRenderer::DestroyResources() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			samplingUniformBuffer = nullptr;
			if (samplingPool != VK_NULL_HANDLE) {
				vkDestroyDescriptorPool(vkDevice, samplingPool, nullptr);
				samplingPool = VK_NULL_HANDLE;
				samplingDescriptorSet = VK_NULL_HANDLE;
			}
			if (samplingSetLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(vkDevice, samplingSetLayout, nullptr);
				samplingSetLayout = VK_NULL_HANDLE;
			}
			if (samplingSampler != VK_NULL_HANDLE) {
				vkDestroySampler(vkDevice, samplingSampler, nullptr);
				samplingSampler = VK_NULL_HANDLE;
			}

			if (descriptorPool != VK_NULL_HANDLE) {
				vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
				descriptorPool = VK_NULL_HANDLE;
			}

			for (int i = 0; i < NumSlices; i++) {
				uniformBuffers[i] = nullptr;
				descriptorSets[i] = VK_NULL_HANDLE;
			}

			if (pipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, pipeline, nullptr);
				pipeline = VK_NULL_HANDLE;
			}

			if (pipelineLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, pipelineLayout, nullptr);
				pipelineLayout = VK_NULL_HANDLE;
			}

			if (descriptorSetLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(vkDevice, descriptorSetLayout, nullptr);
				descriptorSetLayout = VK_NULL_HANDLE;
			}

			for (int i = 0; i < NumSlices; i++) {
				if (framebuffers[i] != VK_NULL_HANDLE) {
					vkDestroyFramebuffer(vkDevice, framebuffers[i], nullptr);
					framebuffers[i] = VK_NULL_HANDLE;
				}
				shadowMapImages[i] = nullptr;
			}

			if (renderPass != VK_NULL_HANDLE) {
				vkDestroyRenderPass(vkDevice, renderPass, nullptr);
				renderPass = VK_NULL_HANDLE;
			}
		}

		namespace {
			// Frustum-fit shadow ortho helpers, ported from GLBasicShadowMapRenderer.
			struct ShadowSegment {
				float low, high;
				bool empty;
				ShadowSegment() : low(0.0f), high(0.0f), empty(true) {}
				void operator+=(float v) {
					if (empty) {
						low = high = v;
						empty = false;
					} else {
						low = std::min(low, v);
						high = std::max(high, v);
					}
				}
			};

			Vector3 FrustumCoord(const client::SceneDefinition& def, float x, float y, float z) {
				x *= z;
				y *= z;
				return def.viewOrigin + def.viewAxis[0] * x + def.viewAxis[1] * y + def.viewAxis[2] * z;
			}

			// Where a sun ray through `base` (along `dir`) crosses the map's lower/upper
			// vertical planes, expressed as a lightDir-distance range.
			ShadowSegment ZRange(Vector3 base, Vector3 dir, const Plane3& plane1, const Plane3& plane2) {
				ShadowSegment seg;
				seg += plane1.GetDistanceTo(base) / Vector3::Dot(dir, plane1.n);
				seg += plane2.GetDistanceTo(base) / Vector3::Dot(dir, plane2.n);
				return seg;
			}
		} // namespace

		bool VulkanShadowMapRenderer::BuildMatrix(float near, float far) {
			SPADES_MARK_FUNCTION();

			// Sun-aligned cascaded ortho. We orient the box along the SUN direction
			// (not the camera) and fit the camera frustum slice [near,far] into it,
			// matching GLBasicShadowMapRenderer::BuildMatrix. Unlike GL (which emits
			// [-1,1] clip Z), the final remap leaves clip Z in [0,1] — Vulkan clips Z
			// to [0,1], so a GL-style [-1,1] mapping would discard the near half of
			// every cascade during the depth-only render. Local Z 0 = sun side, so a
			// LESS depth test keeps the surface closest to the sun (the occluder).
			Vector3 lightDir = renderer.GetSunDirection();
			Vector3 up = MakeVector3(0, 0, 1);
			Vector3 side = Vector3::Cross(up, lightDir).Normalize();
			up = Vector3::Cross(lightDir, side).Normalize();

			const client::SceneDefinition& def = renderer.GetSceneDef();
			float tanX = std::tan(def.fovX * 0.5f);
			float tanY = std::tan(def.fovY * 0.5f);

			Vector3 frustum[8];
			frustum[0] = FrustumCoord(def, tanX, tanY, near);
			frustum[1] = FrustumCoord(def, tanX, -tanY, near);
			frustum[2] = FrustumCoord(def, -tanX, tanY, near);
			frustum[3] = FrustumCoord(def, -tanX, -tanY, near);
			frustum[4] = FrustumCoord(def, tanX, tanY, far);
			frustum[5] = FrustumCoord(def, tanX, -tanY, far);
			frustum[6] = FrustumCoord(def, -tanX, tanY, far);
			frustum[7] = FrustumCoord(def, -tanX, -tanY, far);

			// XY bounds of the frustum in light space (side/up axes).
			float minX, maxX, minY, maxY;
			minX = maxX = Vector3::Dot(frustum[0], side);
			minY = maxY = Vector3::Dot(frustum[0], up);
			for (int i = 1; i < 8; i++) {
				float x = Vector3::Dot(frustum[i], side);
				float y = Vector3::Dot(frustum[i], up);
				minX = std::min(minX, x);
				maxX = std::max(maxX, x);
				minY = std::min(minY, y);
				maxY = std::max(maxY, y);
			}

			// Depth (lightDir) bounds: cover the frustum corners and the map's vertical
			// slab (z in [-4, 64]) so casters above/below the slice still register.
			ShadowSegment seg;
			Plane3 plane1(0, 0, 1, -4.0f);
			Plane3 plane2(0, 0, 1, 64.0f);
			const Vector3 corners[4] = {
				side * minX + up * minY, side * minX + up * maxY,
				side * maxX + up * minY, side * maxX + up * maxY,
			};
			for (const Vector3& c : corners) {
				ShadowSegment z = ZRange(c, lightDir, plane1, plane2);
				seg += z.low;
				seg += z.high;
			}
			for (int i = 0; i < 8; i++)
				seg += Vector3::Dot(frustum[i], lightDir);

			// Box: origin at (minX, minY, seg.low); axes span the fitted extents.
			Vector3 origin = side * minX + up * minY + lightDir * seg.low;
			Vector3 axis1 = side * (maxX - minX);
			Vector3 axis2 = up * (maxY - minY);
			Vector3 axis3 = lightDir * (seg.high - seg.low);

			// Bail out if the fitted box is degenerate, as GL does. A zero-length
			// axis makes 2/len infinite and InversedFast() non-finite, and a
			// non-finite matrix does not fail loudly: NaN compares false, so it
			// slips through both frustum culling here and the cascade bounds test
			// in the lit shaders, which then sample the shadow map at NaN
			// coordinates. The visible result is the whole frame flickering while
			// the camera looks along the sun axis.
			const float len1 = axis1.GetLength();
			const float len2 = axis2.GetLength();
			const float len3 = axis3.GetLength();
			if (!(len1 > 1.0E-6F && len2 > 1.0E-6F && len3 > 1.0E-6F))
				return false;

			obb = OBB3(Matrix4::FromAxis(axis1, axis2, axis3, origin));
			vpWidth = 2.0f / len1;
			vpHeight = 2.0f / len2;

			// world -> OBB-local [0,1]^3.
			matrix = obb.m.InversedFast();
			// XY: [0,1] -> [-1,1] (Vulkan clip XY). Z: flip to [0,1] with 0 = sun side,
			// because local-z increases TOWARD the sun (z is down in this engine, so
			// higher/closer-to-sun points have the larger lightDir projection). The
			// LESS depth test then keeps the occluder nearest the sun; without the flip
			// it kept the farthest surface and shadows landed on the sun side.
			matrix = Matrix4::Scale(2.0f, 2.0f, -1.0f) * matrix;
			matrix = Matrix4::Translate(-1.0f, -1.0f, 1.0f) * matrix;
			// Shrink XY slightly for edge padding; leave Z exact.
			matrix = Matrix4::Scale(0.98f, 0.98f, 1.0f) * matrix;
			return true;
		}

		void VulkanShadowMapRenderer::Render(VkCommandBuffer commandBuffer) {
			SPADES_MARK_FUNCTION();

			// Skip until the camera is initialized; a zero FOV degenerates the
			// frustum the cascades are fitted to (GLBasicShadowMapRenderer::Render
			// has the same guard).
			const client::SceneDefinition& sceneDef = renderer.GetSceneDef();
			if (sceneDef.fovX <= 0.0F || sceneDef.fovY <= 0.0F) {
				SetSamplingDisabled();
				return;
			}

			// Cascade split distances. These MUST match the thresholds the lit
			// shaders use to pick a cascade (BasicMap.frag / BasicModelVertexColor.frag
			// EvaluteModelShadow), otherwise a fragment can be shaded against a
			// cascade whose box it does not lie in. Same splits as
			// GLBasicShadowMapRenderer::Render.
			float cascadeDistances[] = { 12.0f, 40.0f, 150.0f };

			for (int i = 0; i < NumSlices; i++) {
				float near = (i == 0) ? 0.0f : cascadeDistances[i - 1];
				float far = cascadeDistances[i];

				// A degenerate cascade cannot be rendered or sampled. GL returns
				// out of its whole Render() here; do the same, and additionally
				// disable sampling so the lit shaders don't read this frame's
				// half-built cascade set.
				if (!BuildMatrix(near, far)) {
					SetSamplingDisabled();
					return;
				}
				matrices[i] = matrix;

				// Update uniform buffer with light space matrix
				void* data;
				data = uniformBuffers[i]->Map();
				memcpy(data, &matrix, sizeof(Matrix4));
				uniformBuffers[i]->Unmap();

				// Begin render pass for this slice
				VkRenderPassBeginInfo renderPassInfo{};
				renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				renderPassInfo.renderPass = renderPass;
				renderPassInfo.framebuffer = framebuffers[i];
				renderPassInfo.renderArea.offset = {0, 0};
				renderPassInfo.renderArea.extent = {(uint32_t)textureSize, (uint32_t)textureSize};

				VkClearValue clearValue{};
				clearValue.depthStencil = {1.0f, 0};
				renderPassInfo.clearValueCount = 1;
				renderPassInfo.pClearValues = &clearValue;

				vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

				// Set viewport and scissor for shadow map
				VkViewport viewport{};
				viewport.x = 0.0f;
				viewport.y = 0.0f;
				viewport.width = (float)textureSize;
				viewport.height = (float)textureSize;
				viewport.minDepth = 0.0f;
				viewport.maxDepth = 1.0f;
				vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

				VkRect2D scissor{};
				scissor.offset = {0, 0};
				scissor.extent = {(uint32_t)textureSize, (uint32_t)textureSize};
				vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

				// Bind the shadow map pipeline
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

				// Bind descriptor set with the light space matrix
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
					0, 1, &descriptorSets[i], 0, nullptr);

				// No depth bias: the cascade is models-only and only the ground samples
				// it (no model self-shadowing yet), so there's no acne to fight, and a
				// large bias would shift the stored depth — visible as a lateral shadow
				// offset along the slanted sun direction.
				vkCmdSetDepthBias(commandBuffer, 0.0f, 0.0f, 0.0f);

				// The cascade is models-only: terrain sun shadows come from the
				// separate 512² top-down mapShadowTexture, so this map need only carry
				// dynamic occluders. Sampling it as EvaluteModelShadow() then adds
				// model shadows on top of the terrain term with no double-counting.

				// Render model shadow pass. Models use their own shadow pipeline
				// (full per-instance transform), so they get this slice's light-space
				// matrix and the render pass for lazy pipeline creation. Gated on
				// r_modelShadows to match GL (GLShadowMapShader skips models when off).
				VulkanModelRenderer* modelRenderer = renderer.GetModelRenderer();
				if (modelRenderer && (int)r_modelShadows) {
					modelRenderer->RenderShadowMapPass(commandBuffer, matrices[i], renderPass);
				}

				vkCmdEndRenderPass(commandBuffer);
			}

			// Publish this frame's cascade matrices to the sampling UBO and mark it
			// enabled so the lit shaders sample model shadows. The depth images are
			// already in DEPTH_STENCIL_READ_ONLY_OPTIMAL (render-pass finalLayout).
			SamplingUniforms u{};
			for (int i = 0; i < NumSlices; i++)
				u.cascadeMatrix[i] = matrices[i];
			u.enabled = 1;
			void* dst = samplingUniformBuffer->Map();
			memcpy(dst, &u, sizeof(u));
			samplingUniformBuffer->Unmap();
		}

		void VulkanShadowMapRenderer::SetSamplingDisabled() {
			// Cascade not rendered this frame (r_fogShadow off): tell the lit shaders
			// to skip model-shadow sampling. Matrices are left stale but unread.
			int32_t disabled = 0;
			void* dst = samplingUniformBuffer->Map();
			memcpy(static_cast<char*>(dst) + offsetof(SamplingUniforms, enabled),
			       &disabled, sizeof(disabled));
			samplingUniformBuffer->Unmap();
		}

		bool VulkanShadowMapRenderer::Cull(const AABB3& box) {
			// Conservative bounding sphere test
			Vector3 center = (box.min + box.max) * 0.5f;
			float radius = (box.max - box.min).GetLength() * 0.5f;
			return SphereCull(center, radius);
		}

		bool VulkanShadowMapRenderer::SphereCull(const Vector3& center, float rad) {
			// Project sphere center to shadow map space
			Vector3 centerProj = (matrix * MakeVector4(center.x, center.y, center.z, 1.0f)).GetXYZ();

			// Check if sphere is outside the shadow map viewport
			if (centerProj.x + rad < -vpWidth * 0.5f || centerProj.x - rad > vpWidth * 0.5f) {
				return true;
			}
			if (centerProj.y + rad < -vpHeight * 0.5f || centerProj.y - rad > vpHeight * 0.5f) {
				return true;
			}

			return false;
		}
	}
}
