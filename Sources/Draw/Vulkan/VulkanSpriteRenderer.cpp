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

#include "VulkanSpriteRenderer.h"
#include "VulkanSpirvCache.h"
#include "VulkanRenderer.h"
#include "VulkanImage.h"
#include "VulkanBuffer.h"
#include "VulkanShader.h"
#include "VulkanFramebufferManager.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/Settings.h>
#include <algorithm>
#include <cstring>
#include <fstream>

SPADES_SETTING(r_softParticles);

namespace {
	// Push-constant block shared by Sprite.vert / .frag (non-soft particles).
	// Vulkan's std430 layout aligns each vec3 to 16 bytes, so the trailing floats
	// pad the vec3s up to match the shader exactly. Both the pipeline's
	// pushConstantRange size and the vkCmdPushConstants size use
	// sizeof(SpritePushConstants), so they can never disagree — a previous
	// undersized range (180 vs the 192 actually pushed) left fogColor/fogDistance
	// outside the declared range, which AMD/amdvlk drops (-> particles invisible
	// with dark tearing from undefined push data), while MoltenVK tolerated it.
	struct SpritePushConstants {
		spades::Matrix4 projectionViewMatrix;
		spades::Matrix4 viewMatrix;
		spades::Vector3 rightVector;       float padding1;
		spades::Vector3 upVector;          float padding2;
		spades::Vector3 viewOriginVector;  float padding3;
		spades::Vector3 fogColor;          float fogDistance;
	};

	// Push-constant block for SoftSprite.vert / .frag (soft particles). Same
	// std430-padding rule and the same sizeof()-for-both-sides discipline as
	// SpritePushConstants.
	struct SoftPushConstants {
		spades::Matrix4 projectionViewMatrix;
		spades::Vector3 rightVector;       float pad1;
		spades::Vector3 upVector;          float pad2;
		spades::Vector3 frontVector;       float pad3;
		spades::Vector3 viewOriginVector;  float pad4;
		spades::Vector3 fogColor;          float fogDistance;
		float zNear;
		float zFar;
	};
} // namespace

namespace spades {
	namespace draw {
		VulkanSpriteRenderer::VulkanSpriteRenderer(VulkanRenderer& r)
		    : renderer(r),
		      device(static_cast<gui::SDLVulkanDevice*>(r.GetDevice().Unmanage())),
		      lastImage(nullptr),
		      softParticles((int)r_softParticles != 0),
		      pipeline(VK_NULL_HANDLE),
		      pipelineLayout(VK_NULL_HANDLE),
		      descriptorSetLayout(VK_NULL_HANDLE),
		      depthDescriptorSetLayout(VK_NULL_HANDLE),
		      depthDescriptorSet(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			const auto& swapchainImageViews = device->GetSwapchainImageViews();
			perFrameDescriptorPools.resize(swapchainImageViews.size(), VK_NULL_HANDLE);
			perFrameBuffers.resize(swapchainImageViews.size());
			perFrameImages.resize(swapchainImageViews.size());

			CreatePipeline();
			CreateDescriptorSet();
		}

		VulkanSpriteRenderer::~VulkanSpriteRenderer() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			for (auto pool : perFrameDescriptorPools) {
				if (pool != VK_NULL_HANDLE) {
					vkDestroyDescriptorPool(vkDevice, pool, nullptr);
				}
			}
			for (auto& frameImages : perFrameImages) {
				for (auto* img : frameImages) {
					img->Release();
				}
			}
			if (pipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, pipeline, nullptr);
			}
			if (pipelineLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, pipelineLayout, nullptr);
			}
			if (descriptorSetLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(vkDevice, descriptorSetLayout, nullptr);
			}
			if (depthDescriptorSetLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(vkDevice, depthDescriptorSetLayout, nullptr);
			}
		}

		void VulkanSpriteRenderer::CreatePipeline() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			auto LoadSPIRVFile = [](const char* filename) -> std::vector<uint32_t> {
				return SpirvCache::Load(filename);
			};

			const char* vertFile = softParticles ? "Shaders/Vulkan/SoftSprite.vert.spv"
			                                     : "Shaders/Vulkan/Sprite.vert.spv";
			const char* fragFile = softParticles ? "Shaders/Vulkan/SoftSprite.frag.spv"
			                                     : "Shaders/Vulkan/Sprite.frag.spv";
			const char* vertName = softParticles ? "SoftSprite.vert" : "Sprite.vert";
			const char* fragName = softParticles ? "SoftSprite.frag" : "Sprite.frag";

			std::vector<uint32_t> vertCode = LoadSPIRVFile(vertFile);
			std::vector<uint32_t> fragCode = LoadSPIRVFile(fragFile);

			Handle<VulkanShader> vertShader(new VulkanShader(device, VulkanShader::VertexShader, vertName), false);
			Handle<VulkanShader> fragShader(new VulkanShader(device, VulkanShader::FragmentShader, fragName), false);

			vertShader->LoadSPIRV(vertCode);
			fragShader->LoadSPIRV(fragCode);

			VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
			vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
			vertShaderStageInfo.module = vertShader->GetShaderModule();
			vertShaderStageInfo.pName = "main";

			VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
			fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			fragShaderStageInfo.module = fragShader->GetShaderModule();
			fragShaderStageInfo.pName = "main";

			VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

			VkVertexInputBindingDescription bindingDescription{};
			bindingDescription.binding = 0;
			bindingDescription.stride = sizeof(Vertex);
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			VkVertexInputAttributeDescription attributeDescriptions[4]{};
			attributeDescriptions[0].binding = 0;
			attributeDescriptions[0].location = 0;
			attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDescriptions[0].offset = offsetof(Vertex, x);

			attributeDescriptions[1].binding = 0;
			attributeDescriptions[1].location = 1;
			attributeDescriptions[1].format = VK_FORMAT_R32_SFLOAT;
			attributeDescriptions[1].offset = offsetof(Vertex, radius);

			attributeDescriptions[2].binding = 0;
			attributeDescriptions[2].location = 2;
			attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDescriptions[2].offset = offsetof(Vertex, sx);

			attributeDescriptions[3].binding = 0;
			attributeDescriptions[3].location = 3;
			attributeDescriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
			attributeDescriptions[3].offset = offsetof(Vertex, r);

			VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
			vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputInfo.vertexBindingDescriptionCount = 1;
			vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
			vertexInputInfo.vertexAttributeDescriptionCount = 4;
			vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

			VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
			inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			inputAssembly.primitiveRestartEnable = VK_FALSE;

			VkPipelineViewportStateCreateInfo viewportState{};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.pViewports = nullptr;
			viewportState.scissorCount = 1;
			viewportState.pScissors = nullptr;

			VkPipelineRasterizationStateCreateInfo rasterizer{};
			rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizer.depthClampEnable = VK_FALSE;
			rasterizer.rasterizerDiscardEnable = VK_FALSE;
			rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = VK_CULL_MODE_NONE; // Y-flip viewport inverts winding; disable culling like LongSpriteRenderer
			rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rasterizer.depthBiasEnable = VK_FALSE;

			VkPipelineMultisampleStateCreateInfo multisampling{};
			multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampling.sampleShadingEnable = VK_FALSE;
			// Match the scene render pass sample count (MSAA).
			multisampling.rasterizationSamples = device->GetSampleCount();

			VkPipelineDepthStencilStateCreateInfo depthStencil{};
			depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			if (softParticles) {
				// No depth attachment in sprite render pass; depth test done in shader
				depthStencil.depthTestEnable = VK_FALSE;
				depthStencil.depthWriteEnable = VK_FALSE;
			} else {
				depthStencil.depthTestEnable = VK_TRUE;
				depthStencil.depthWriteEnable = VK_FALSE;
				depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
			}
			depthStencil.depthBoundsTestEnable = VK_FALSE;
			depthStencil.stencilTestEnable = VK_FALSE;

			VkPipelineColorBlendAttachmentState colorBlendAttachment{};
			colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			colorBlendAttachment.blendEnable = VK_TRUE;
			colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
			colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
			colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

			VkPipelineColorBlendStateCreateInfo colorBlending{};
			colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlending.logicOpEnable = VK_FALSE;
			colorBlending.attachmentCount = 1;
			colorBlending.pAttachments = &colorBlendAttachment;

			VkDynamicState dynamicStates[] = {
				VK_DYNAMIC_STATE_VIEWPORT,
				VK_DYNAMIC_STATE_SCISSOR
			};

			VkPipelineDynamicStateCreateInfo dynamicState{};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = 2;
			dynamicState.pDynamicStates = dynamicStates;

			// Set 0: sprite texture (per batch)
			VkDescriptorSetLayoutBinding samplerLayoutBinding{};
			samplerLayoutBinding.binding = 0;
			samplerLayoutBinding.descriptorCount = 1;
			samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			samplerLayoutBinding.pImmutableSamplers = nullptr;
			samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = 1;
			layoutInfo.pBindings = &samplerLayoutBinding;

			VkResult result = vkCreateDescriptorSetLayout(vkDevice, &layoutInfo, nullptr, &descriptorSetLayout);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create descriptor set layout (error code: %d)", result);
			}

			VkDescriptorSetLayout setLayouts[2];
			uint32_t setLayoutCount = 1;
			setLayouts[0] = descriptorSetLayout;

			if (softParticles) {
				// Set 1: depth texture (per frame)
				VkDescriptorSetLayoutBinding depthBinding{};
				depthBinding.binding = 0;
				depthBinding.descriptorCount = 1;
				depthBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				depthBinding.pImmutableSamplers = nullptr;
				depthBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

				VkDescriptorSetLayoutCreateInfo depthLayoutInfo{};
				depthLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
				depthLayoutInfo.bindingCount = 1;
				depthLayoutInfo.pBindings = &depthBinding;

				result = vkCreateDescriptorSetLayout(vkDevice, &depthLayoutInfo, nullptr, &depthDescriptorSetLayout);
				if (result != VK_SUCCESS) {
					SPRaise("Failed to create depth descriptor set layout (error code: %d)", result);
				}

				setLayouts[1] = depthDescriptorSetLayout;
				setLayoutCount = 2;
			}

			VkPushConstantRange pushConstantRange{};
			pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			pushConstantRange.offset = 0;
			if (softParticles) {
				pushConstantRange.size = sizeof(SoftPushConstants);
			} else {
				pushConstantRange.size = sizeof(SpritePushConstants);
			}

			VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
			pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutInfo.setLayoutCount = setLayoutCount;
			pipelineLayoutInfo.pSetLayouts = setLayouts;
			pipelineLayoutInfo.pushConstantRangeCount = 1;
			pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

			result = vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create pipeline layout (error code: %d)", result);
			}

			VkRenderPass renderPassToUse;
			if (softParticles) {
				renderPassToUse = renderer.GetFramebufferManager()->GetSpriteRenderPass();
			} else {
				renderPassToUse = renderer.GetOffscreenRenderPass();
			}

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
			pipelineInfo.renderPass = renderPassToUse;
			pipelineInfo.subpass = 0;

			result = vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1, &pipelineInfo, nullptr, &pipeline);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create graphics pipeline (error code: %d)", result);
			}
		}

		void VulkanSpriteRenderer::CreateDescriptorSet() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			VkDescriptorPoolSize poolSize{};
			poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSize.descriptorCount = 1000;

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes = &poolSize;
			poolInfo.maxSets = 1000;
			poolInfo.flags = 0;

			for (size_t i = 0; i < perFrameDescriptorPools.size(); i++) {
				VkResult result = vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &perFrameDescriptorPools[i]);
				if (result != VK_SUCCESS) {
					SPRaise("Failed to create descriptor pool for frame %zu (error code: %d)", i, result);
				}
			}
		}

		void VulkanSpriteRenderer::Add(VulkanImage *img, Vector3 center, float rad, float ang,
		                               Vector4 color) {
			SPADES_MARK_FUNCTION_DEBUG();

			Sprite spr;
			spr.image = img;
			spr.center = center;
			spr.radius = rad;
			spr.angle = ang;
			spr.color = color;
			sprites.push_back(spr);
		}

		void VulkanSpriteRenderer::Clear() {
			SPADES_MARK_FUNCTION();
			sprites.clear();
			vertices.clear();
			indices.clear();
			lastImage = nullptr;
		}


		void VulkanSpriteRenderer::Flush(VkCommandBuffer commandBuffer, uint32_t frameIndex) {
			SPADES_MARK_FUNCTION();

			if (vertices.empty() || indices.empty())
				return;

			if (pipeline == VK_NULL_HANDLE)
				return;

			if (!lastImage)
				return;

			VkDevice vkDevice = device->GetDevice();

			size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
			Handle<VulkanBuffer> vertexBuffer(
				new VulkanBuffer(device, vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
				false);
			vertexBuffer->UpdateData(vertices.data(), vertexBufferSize);
			perFrameBuffers[frameIndex].push_back(vertexBuffer);

			size_t indexBufferSize = indices.size() * sizeof(uint32_t);
			Handle<VulkanBuffer> indexBuffer(
				new VulkanBuffer(device, indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
				false);
			indexBuffer->UpdateData(indices.data(), indexBufferSize);
			perFrameBuffers[frameIndex].push_back(indexBuffer);

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

			VkBuffer vb = vertexBuffer->GetBuffer();
			VkDeviceSize offsets[] = {0};
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, offsets);

			vkCmdBindIndexBuffer(commandBuffer, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

			// Allocate and bind sprite texture descriptor (set 0)
			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = perFrameDescriptorPools[frameIndex];
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &descriptorSetLayout;

			VkDescriptorSet descriptorSet;
			VkResult result = vkAllocateDescriptorSets(vkDevice, &allocInfo, &descriptorSet);
			if (result != VK_SUCCESS) {
				SPLog("Failed to allocate descriptor set (error code: %d)", result);
				vertices.clear();
				indices.clear();
				return;
			}

			VkImageView imageView = lastImage->GetImageView();
			VkSampler sampler = lastImage->GetSampler();

			if (imageView == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE) {
				SPLog("Warning: Invalid image view or sampler, skipping");
				vertices.clear();
				indices.clear();
				return;
			}

			VkDescriptorImageInfo imageInfo{};
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = imageView;
			imageInfo.sampler = sampler;

			VkWriteDescriptorSet descriptorWrite{};
			descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite.dstSet = descriptorSet;
			descriptorWrite.dstBinding = 0;
			descriptorWrite.dstArrayElement = 0;
			descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrite.descriptorCount = 1;
			descriptorWrite.pImageInfo = &imageInfo;

			vkUpdateDescriptorSets(vkDevice, 1, &descriptorWrite, 0, nullptr);

			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
			                        0, 1, &descriptorSet, 0, nullptr);

			// Bind depth descriptor (set 1) for soft particles
			if (softParticles && depthDescriptorSet != VK_NULL_HANDLE) {
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
				                        1, 1, &depthDescriptorSet, 0, nullptr);
			}

			const Matrix4& projViewMatrix = renderer.GetProjectionViewMatrix();
			Vector3 fogCol = renderer.GetFogColor();
			fogCol *= fogCol; // linearize
			float fogDist = renderer.GetFogDistance();
			const client::SceneDefinition& sceneDef = renderer.GetSceneDef();

			if (softParticles) {
				SoftPushConstants pushConstants;

				pushConstants.projectionViewMatrix = projViewMatrix;
				pushConstants.rightVector = sceneDef.viewAxis[0];
				pushConstants.pad1 = 0.0f;
				pushConstants.upVector = sceneDef.viewAxis[1];
				pushConstants.pad2 = 0.0f;
				pushConstants.frontVector = sceneDef.viewAxis[2];
				pushConstants.pad3 = 0.0f;
				pushConstants.viewOriginVector = sceneDef.viewOrigin;
				pushConstants.pad4 = 0.0f;
				pushConstants.fogColor = fogCol;
				pushConstants.fogDistance = fogDist;
				pushConstants.zNear = sceneDef.zNear;
				pushConstants.zFar = sceneDef.zFar;

				vkCmdPushConstants(commandBuffer, pipelineLayout,
				                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				                   0, sizeof(pushConstants), &pushConstants);
			} else {
				SpritePushConstants pushConstants;

				pushConstants.projectionViewMatrix = projViewMatrix;
				pushConstants.viewMatrix = Matrix4::Identity();
				pushConstants.rightVector = sceneDef.viewAxis[0];
				pushConstants.upVector = sceneDef.viewAxis[1];
				pushConstants.viewOriginVector = sceneDef.viewOrigin;
				pushConstants.fogColor = fogCol;
				pushConstants.fogDistance = fogDist;

				vkCmdPushConstants(commandBuffer, pipelineLayout,
				                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				                   0, sizeof(pushConstants), &pushConstants);
			}

			vkCmdDrawIndexed(commandBuffer, (uint32_t)indices.size(), 1, 0, 0, 0);

			lastImage->AddRef();
			perFrameImages[frameIndex].push_back(lastImage);

			vertices.clear();
			indices.clear();
		}

		void VulkanSpriteRenderer::Render(VkCommandBuffer commandBuffer, uint32_t frameIndex) {
			SPADES_MARK_FUNCTION();

			if (sprites.empty())
				return;

			VkDevice vkDevice = device->GetDevice();

			// Clear resources from this frame (GPU has finished with them due to fence wait)
			perFrameBuffers[frameIndex].clear();

			// Release images from previous use of this frame
			for (auto* img : perFrameImages[frameIndex]) {
				img->Release();
			}
			perFrameImages[frameIndex].clear();

			// Reset descriptor pool for this frame to free all descriptor sets
			vkResetDescriptorPool(vkDevice, perFrameDescriptorPools[frameIndex], 0);

			// Allocate depth descriptor set for soft particles
			if (softParticles && depthDescriptorSetLayout != VK_NULL_HANDLE) {
				VkDescriptorSetAllocateInfo depthAllocInfo{};
				depthAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				depthAllocInfo.descriptorPool = perFrameDescriptorPools[frameIndex];
				depthAllocInfo.descriptorSetCount = 1;
				depthAllocInfo.pSetLayouts = &depthDescriptorSetLayout;

				VkResult result = vkAllocateDescriptorSets(vkDevice, &depthAllocInfo, &depthDescriptorSet);
				if (result != VK_SUCCESS) {
					SPLog("Failed to allocate depth descriptor set (error code: %d)", result);
					depthDescriptorSet = VK_NULL_HANDLE;
				} else {
					// Resolved depth == raw depth at 1x; the single-sample R32F resolve
					// under MSAA (a multisampled depth attachment can't be sampled here).
					Handle<VulkanImage> depthImage = renderer.GetFramebufferManager()->GetResolvedDepthImage();
					VkDescriptorImageInfo depthImageInfo{};
					depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					depthImageInfo.imageView = depthImage->GetImageView();
					depthImageInfo.sampler = depthImage->GetSampler();

					VkWriteDescriptorSet depthWrite{};
					depthWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					depthWrite.dstSet = depthDescriptorSet;
					depthWrite.dstBinding = 0;
					depthWrite.dstArrayElement = 0;
					depthWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					depthWrite.descriptorCount = 1;
					depthWrite.pImageInfo = &depthImageInfo;

					vkUpdateDescriptorSets(vkDevice, 1, &depthWrite, 0, nullptr);
				}
			}

			// Sort sprites by image to minimize batch breaks and descriptor set allocations
			std::sort(sprites.begin(), sprites.end(),
			          [](const Sprite& a, const Sprite& b) { return a.image < b.image; });

			// Build billboard sprites as quads
			for (const auto& sprite : sprites) {
				if (sprite.image != lastImage) {
					// Flush previous batch
					Flush(commandBuffer, frameIndex);
					lastImage = sprite.image;
				}

				// Create billboard quad for sprite
				uint32_t baseIdx = (uint32_t)vertices.size();

				Vertex v;
				v.x = sprite.center.x;
				v.y = sprite.center.y;
				v.z = sprite.center.z;
				v.radius = sprite.radius;
				v.angle = sprite.angle;
				v.r = sprite.color.x;
				v.g = sprite.color.y;
				v.b = sprite.color.z;
				v.a = sprite.color.w;

				// Bottom-left
				v.sx = -1.0f;
				v.sy = -1.0f;
				vertices.push_back(v);

				// Bottom-right
				v.sx = 1.0f;
				v.sy = -1.0f;
				vertices.push_back(v);

				// Top-right
				v.sx = 1.0f;
				v.sy = 1.0f;
				vertices.push_back(v);

				// Top-left
				v.sx = -1.0f;
				v.sy = 1.0f;
				vertices.push_back(v);

				// Two triangles forming a quad
				indices.push_back(baseIdx + 0);
				indices.push_back(baseIdx + 1);
				indices.push_back(baseIdx + 2);
				indices.push_back(baseIdx + 0);
				indices.push_back(baseIdx + 2);
				indices.push_back(baseIdx + 3);
			}

			// Flush final batch
			Flush(commandBuffer, frameIndex);

			Clear();
		}

	} // namespace draw
} // namespace spades
