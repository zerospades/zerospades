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

#include "VulkanImageRenderer.h"
#include "VulkanSpirvCache.h"
#include "VulkanRenderer.h"
#include "VulkanImage.h"
#include "VulkanBuffer.h"
#include "VulkanShader.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <cmath>
#include <cstring>
#include <fstream>

namespace spades {
	namespace draw {
		VulkanImageRenderer::VulkanImageRenderer(VulkanRenderer& r)
		    : renderer(r),
		      device(static_cast<gui::SDLVulkanDevice*>(r.GetDevice().Unmanage())),
		      image(nullptr),
		      pipeline(VK_NULL_HANDLE),
		      pipelineLayout(VK_NULL_HANDLE),
		      descriptorSetLayout(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			// Initialize per-frame buffer storage for each swapchain image
			const auto& swapchainImageViews = device->GetSwapchainImageViews();
			perFrameBuffers.resize(swapchainImageViews.size());
			perFrameImages.resize(swapchainImageViews.size());
			perFrameDescriptorPools.resize(swapchainImageViews.size(), VK_NULL_HANDLE);

			CreatePipeline();
			CreateDescriptorSet();
		}

		VulkanImageRenderer::~VulkanImageRenderer() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			for (auto pool : perFrameDescriptorPools) {
				if (pool != VK_NULL_HANDLE) {
					vkDestroyDescriptorPool(vkDevice, pool, nullptr);
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

			if (image) {
				image->Release();
			}
		}

		void VulkanImageRenderer::CreatePipeline() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			// Load SPIR-V shaders
			auto LoadSPIRVFile = [](const char* filename) -> std::vector<uint32_t> {
				return SpirvCache::Load(filename);
			};

			std::vector<uint32_t> vertCode = LoadSPIRVFile("Shaders/Vulkan/BasicImage.vert.spv");
			std::vector<uint32_t> fragCode = LoadSPIRVFile("Shaders/Vulkan/BasicImage.frag.spv");

			Handle<VulkanShader> vertShader(new VulkanShader(device, VulkanShader::VertexShader, "BasicImage.vert"), false);
			Handle<VulkanShader> fragShader(new VulkanShader(device, VulkanShader::FragmentShader, "BasicImage.frag"), false);

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

			// Vertex input
			VkVertexInputBindingDescription bindingDescription{};
			bindingDescription.binding = 0;
			bindingDescription.stride = sizeof(ImageVertex);
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			VkVertexInputAttributeDescription attributeDescriptions[3]{};
			attributeDescriptions[0].binding = 0;
			attributeDescriptions[0].location = 0;
			attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
			attributeDescriptions[0].offset = offsetof(ImageVertex, x);

			attributeDescriptions[1].binding = 0;
			attributeDescriptions[1].location = 1;
			attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
			attributeDescriptions[1].offset = offsetof(ImageVertex, u);

			attributeDescriptions[2].binding = 0;
			attributeDescriptions[2].location = 2;
			attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
			attributeDescriptions[2].offset = offsetof(ImageVertex, r);

			VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
			vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputInfo.vertexBindingDescriptionCount = 1;
			vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
			vertexInputInfo.vertexAttributeDescriptionCount = 3;
			vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

			VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
			inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			inputAssembly.primitiveRestartEnable = VK_FALSE;

			// Viewport and scissor will be set dynamically
			VkPipelineViewportStateCreateInfo viewportState{};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.pViewports = nullptr; // Dynamic state
			viewportState.scissorCount = 1;
			viewportState.pScissors = nullptr; // Dynamic state

			VkPipelineRasterizationStateCreateInfo rasterizer{};
			rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizer.depthClampEnable = VK_FALSE;
			rasterizer.rasterizerDiscardEnable = VK_FALSE;
			rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = VK_CULL_MODE_NONE;
			rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rasterizer.depthBiasEnable = VK_FALSE;

			VkPipelineMultisampleStateCreateInfo multisampling{};
			multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampling.sampleShadingEnable = VK_FALSE;
			multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

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

			// Enable dynamic state for viewport and scissor
			VkDynamicState dynamicStates[] = {
				VK_DYNAMIC_STATE_VIEWPORT,
				VK_DYNAMIC_STATE_SCISSOR
			};

			VkPipelineDynamicStateCreateInfo dynamicState{};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = 2;
			dynamicState.pDynamicStates = dynamicStates;

			// Create descriptor set layout
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

			// Push constant range for screen size, texture size and the circular
			// clip. Sized from the same struct the push uses, so the two can
			// never drift apart.
			VkPushConstantRange pushConstantRange{};
			pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			pushConstantRange.offset = 0;
			pushConstantRange.size = sizeof(PushConstants);

			VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
			pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutInfo.setLayoutCount = 1;
			pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
			pipelineLayoutInfo.pushConstantRangeCount = 1;
			pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

			result = vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create pipeline layout (error code: %d)", result);
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
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.pDynamicState = &dynamicState;
			pipelineInfo.layout = pipelineLayout;
			pipelineInfo.renderPass = renderer.GetRenderPass();
			pipelineInfo.subpass = 0;

			result = vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1, &pipelineInfo, nullptr, &pipeline);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create graphics pipeline (error code: %d)", result);
			}

		}

		void VulkanImageRenderer::CreateDescriptorSet() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			VkDescriptorPoolSize poolSize{};
			poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSize.descriptorCount = 1000; // Support up to 1000 image batches per frame (increased from 100)

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes = &poolSize;
			poolInfo.maxSets = 1000; // Match descriptor count (increased from 100)
			poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // Allow freeing individual sets

			// Create one descriptor pool per swapchain image
			for (size_t i = 0; i < perFrameDescriptorPools.size(); i++) {
				VkResult result = vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &perFrameDescriptorPools[i]);
				if (result != VK_SUCCESS) {
					SPRaise("Failed to create descriptor pool for frame %zu (error code: %d)", i, result);
				}
			}

			SPLog("Created %zu descriptor pools (one per swapchain image)", perFrameDescriptorPools.size());
		}

		VkRect2D VulkanImageRenderer::FullScissor() const {
			VkRect2D r{};
			r.offset = {0, 0};
			r.extent = device->GetSwapchainExtent();
			return r;
		}

		void VulkanImageRenderer::FinishCurrentBatch() {
			if (!image || vertices.empty())
				return;

			Batch batch;
			batch.image = image;
			batch.vertices = std::move(vertices);
			batch.indices = std::move(indices);
			batch.clip = currentClip;
			batches.push_back(std::move(batch));

			// The batch takes over the reference 'image' was holding, so no
			// AddRef/Release here.
			vertices.clear();
			indices.clear();
			image = nullptr;
		}

		void VulkanImageRenderer::SetClipCircle(const Vector2& center, float radius) {
			SPADES_MARK_FUNCTION();

			// Everything queued so far keeps the previous clip.
			FinishCurrentBatch();
			currentClip.circleCenter = center;
			currentClip.circleRadius = radius;
		}

		void VulkanImageRenderer::ClearClipCircle() {
			SPADES_MARK_FUNCTION();

			FinishCurrentBatch();
			currentClip.circleRadius = 0.0f;
		}

		void VulkanImageRenderer::SetClipRect(const AABB2& rect) {
			SPADES_MARK_FUNCTION();

			FinishCurrentBatch();

			// Screen coordinates map 1:1 onto framebuffer pixels here: the
			// vertex shader divides by the swapchain extent and the viewport is
			// Y-flipped, so screen (0, 0) is the top-left pixel, same as the
			// scissor's origin. Clamp to the surface, as Vulkan rejects a
			// scissor reaching outside the framebuffer.
			const VkRect2D full = FullScissor();
			const float maxX = static_cast<float>(full.extent.width);
			const float maxY = static_cast<float>(full.extent.height);

			float x0 = Clamp(floorf(rect.GetMinX()), 0.0f, maxX);
			float y0 = Clamp(floorf(rect.GetMinY()), 0.0f, maxY);
			float x1 = Clamp(ceilf(rect.GetMaxX()), x0, maxX);
			float y1 = Clamp(ceilf(rect.GetMaxY()), y0, maxY);

			VkRect2D scissor{};
			scissor.offset = {static_cast<int32_t>(x0), static_cast<int32_t>(y0)};
			scissor.extent = {static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0)};
			currentClip.scissor = scissor;
			currentClip.hasScissor = true;
		}

		void VulkanImageRenderer::ClearClipRect() {
			SPADES_MARK_FUNCTION();

			FinishCurrentBatch();
			currentClip.hasScissor = false;
		}

		void VulkanImageRenderer::SetImage(VulkanImage* img) {
			if (img == image)
				return;

			// When image changes, save the current batch before switching
			// This mimics the GL renderer which calls Flush() immediately here
			FinishCurrentBatch();

			if (image) {
				image->Release();
			}

			image = img;

			if (image) {
				image->AddRef();
			}
		}

		void VulkanImageRenderer::Add(float dx1, float dy1, float dx2, float dy2, float dx3, float dy3,
		                              float dx4, float dy4, float sx1, float sy1, float sx2, float sy2,
		                              float sx3, float sy3, float sx4, float sy4, float r, float g,
		                              float b, float a) {
			ImageVertex v;
			v.r = r;
			v.g = g;
			v.b = b;
			v.a = a;

			uint32_t idx = static_cast<uint32_t>(vertices.size());

			v.x = dx1;
			v.y = dy1;
			v.u = sx1;
			v.v = sy1;
			vertices.push_back(v);

			v.x = dx2;
			v.y = dy2;
			v.u = sx2;
			v.v = sy2;
			vertices.push_back(v);

			v.x = dx3;
			v.y = dy3;
			v.u = sx3;
			v.v = sy3;
			vertices.push_back(v);

			v.x = dx4;
			v.y = dy4;
			v.u = sx4;
			v.v = sy4;
			vertices.push_back(v);

			indices.push_back(idx);
			indices.push_back(idx + 1);
			indices.push_back(idx + 2);
			indices.push_back(idx);
			indices.push_back(idx + 2);
			indices.push_back(idx + 3);
		}

		void VulkanImageRenderer::AddTriangle(float dx1, float dy1, float dx2, float dy2, float dx3,
		                                      float dy3, float r, float g, float b, float a) {
			ImageVertex v;
			v.r = r;
			v.g = g;
			v.b = b;
			v.a = a;

			uint32_t idx = static_cast<uint32_t>(vertices.size());

			v.x = dx1;
			v.y = dy1;
			v.u = 0.0f;
			v.v = 0.0f;
			vertices.push_back(v);

			v.x = dx2;
			v.y = dy2;
			vertices.push_back(v);

			v.x = dx3;
			v.y = dy3;
			vertices.push_back(v);

			indices.push_back(idx);
			indices.push_back(idx + 1);
			indices.push_back(idx + 2);
		}

		void VulkanImageRenderer::AddGradient(float dx1, float dy1, float dx2, float dy2, float dx3,
		                                      float dy3, float dx4, float dy4, float sx1, float sy1,
		                                      float sx2, float sy2, float sx3, float sy3, float sx4,
		                                      float sy4, float r0, float g0, float b0, float a0,
		                                      float r1, float g1, float b1, float a1,
		                                      bool horizontal) {
			uint32_t idx = static_cast<uint32_t>(vertices.size());
			ImageVertex v;

			// The two colours are assigned per corner rather than interpolated here;
			// the rasterizer does the blend. Which corners take colour1 is the only
			// difference between the two directions.
			if (horizontal) {
				// left verts = colour0, right verts = colour1
				v.x = dx1;
				v.y = dy1;
				v.u = sx1;
				v.v = sy1;
				v.r = r0;
				v.g = g0;
				v.b = b0;
				v.a = a0;
				vertices.push_back(v);

				v.x = dx2;
				v.y = dy2;
				v.u = sx2;
				v.v = sy2;
				v.r = r1;
				v.g = g1;
				v.b = b1;
				v.a = a1;
				vertices.push_back(v);

				v.x = dx3;
				v.y = dy3;
				v.u = sx3;
				v.v = sy3;
				vertices.push_back(v);

				v.x = dx4;
				v.y = dy4;
				v.u = sx4;
				v.v = sy4;
				v.r = r0;
				v.g = g0;
				v.b = b0;
				v.a = a0;
				vertices.push_back(v);
			} else {
				// top verts = colour0, bottom verts = colour1
				v.x = dx1;
				v.y = dy1;
				v.u = sx1;
				v.v = sy1;
				v.r = r0;
				v.g = g0;
				v.b = b0;
				v.a = a0;
				vertices.push_back(v);

				v.x = dx2;
				v.y = dy2;
				v.u = sx2;
				v.v = sy2;
				vertices.push_back(v);

				v.x = dx3;
				v.y = dy3;
				v.u = sx3;
				v.v = sy3;
				v.r = r1;
				v.g = g1;
				v.b = b1;
				v.a = a1;
				vertices.push_back(v);

				v.x = dx4;
				v.y = dy4;
				v.u = sx4;
				v.v = sy4;
				vertices.push_back(v);
			}

			indices.push_back(idx);
			indices.push_back(idx + 1);
			indices.push_back(idx + 2);
			indices.push_back(idx);
			indices.push_back(idx + 2);
			indices.push_back(idx + 3);
		}

	void VulkanImageRenderer::Flush(VkCommandBuffer commandBuffer, uint32_t frameIndex) {
		SPADES_MARK_FUNCTION();

		// Add current vertices as final batch if not empty
		FinishCurrentBatch();

		// Every batch carries its own clip now, so the pending state can be
		// dropped. Doing it here rather than at the end covers the early
		// returns below: an unbalanced Begin...() must never leak into the next
		// frame and blank out the whole UI.
		currentClip = BatchClip{};

		if (batches.empty()) {
			vertices.clear();
			indices.clear();
			return;
		}


		VkDevice vkDevice = device->GetDevice();

		// Clear resources from this specific frame (safe now that GPU has finished with them)
		// Each frame has its own resource set to avoid use-after-free with in-flight command buffers
		perFrameBuffers[frameIndex].clear();

		// Release images from the previous use of this frame
		for (auto* img : perFrameImages[frameIndex]) {
			img->Release();
		}
		perFrameImages[frameIndex].clear();

		// Reset descriptor pool for THIS frame to free descriptor sets
		// Each frame has its own pool to avoid freeing descriptors that are still in use by other frames
		vkResetDescriptorPool(vkDevice, perFrameDescriptorPools[frameIndex], 0);

		// Consolidate all vertices and indices into single buffers to reduce memory allocations
		size_t totalVertexCount = 0;
		size_t totalIndexCount = 0;
		for (const auto& batch : batches) {
			totalVertexCount += batch.vertices.size();
			totalIndexCount += batch.indices.size();
		}

		// SPLog("[VulkanImageRenderer::Flush] Rendering %zu batches with %zu vertices and %zu indices",
		// 	batches.size(), totalVertexCount, totalIndexCount);

		if (totalVertexCount == 0 || totalIndexCount == 0) {
			// Nothing to render
			for (auto& batch : batches) {
				batch.image->Release();
			}
			batches.clear();
			return;
		}

		// Create consolidated vertex buffer
		std::vector<ImageVertex> allVertices;
		allVertices.reserve(totalVertexCount);
		std::vector<uint32_t> allIndices;
		allIndices.reserve(totalIndexCount);

		// Track offset for each batch
		struct BatchDrawInfo {
			VulkanImage* image;
			uint32_t indexOffset;
			uint32_t indexCount;
			uint32_t vertexOffset;
			BatchClip clip;
		};
		std::vector<BatchDrawInfo> drawInfos;
		drawInfos.reserve(batches.size());

		uint32_t currentVertexOffset = 0;
		uint32_t currentIndexOffset = 0;

		for (auto& batch : batches) {
			BatchDrawInfo info;
			info.image = batch.image;
			info.indexOffset = currentIndexOffset;
			info.indexCount = static_cast<uint32_t>(batch.indices.size());
			info.vertexOffset = currentVertexOffset;
			info.clip = batch.clip;

			// Copy vertices
			allVertices.insert(allVertices.end(), batch.vertices.begin(), batch.vertices.end());

			// Copy indices (no adjustment needed since we use vertexOffset in draw call)
			allIndices.insert(allIndices.end(), batch.indices.begin(), batch.indices.end());

			currentVertexOffset += static_cast<uint32_t>(batch.vertices.size());
			currentIndexOffset += static_cast<uint32_t>(batch.indices.size());

			drawInfos.push_back(info);
		}

		// Create single vertex buffer for all batches
		VkDeviceSize totalVertexBufferSize = sizeof(ImageVertex) * allVertices.size();
		Handle<VulkanBuffer> consolidatedVertexBuffer(
			new VulkanBuffer(device, totalVertexBufferSize,
			                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
			false);
		consolidatedVertexBuffer->UpdateData(allVertices.data(), totalVertexBufferSize);
		perFrameBuffers[frameIndex].push_back(consolidatedVertexBuffer);

		// Create single index buffer for all batches
		VkDeviceSize totalIndexBufferSize = sizeof(uint32_t) * allIndices.size();
		Handle<VulkanBuffer> consolidatedIndexBuffer(
			new VulkanBuffer(device, totalIndexBufferSize,
			                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
			false);
		consolidatedIndexBuffer->UpdateData(allIndices.data(), totalIndexBufferSize);
		perFrameBuffers[frameIndex].push_back(consolidatedIndexBuffer);

		// Bind pipeline once (all batches use the same pipeline)
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		// Use the actual swapchain extent as the authoritative UI surface size.
		// renderer.ScreenWidth()/ScreenHeight() may be stale after a swapchain resize
		// and may differ from the physical pixel size on HiDPI displays.
		VkExtent2D uiExtent = device->GetSwapchainExtent();
		float invUIWidth  =  2.0f / static_cast<float>(uiExtent.width);
		float invUIHeight = -2.0f / static_cast<float>(uiExtent.height);

		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = static_cast<float>(uiExtent.height);
		viewport.width = static_cast<float>(uiExtent.width);
		viewport.height = -static_cast<float>(uiExtent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

		VkRect2D fullScissor{};
		fullScissor.offset = {0, 0};
		fullScissor.extent = uiExtent;
		vkCmdSetScissor(commandBuffer, 0, 1, &fullScissor);

		// Bind the consolidated buffers once
		VkBuffer vertexBuffers[] = {consolidatedVertexBuffer->GetBuffer()};
		VkDeviceSize offsets[] = {0};
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(commandBuffer, consolidatedIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

		// Keep track of images used in this frame so we can release them later
		// Images must stay alive until GPU finishes executing the command buffer
		std::vector<VulkanImage*> frameImages;
		frameImages.reserve(drawInfos.size());

		// Render each batch using offsets into the consolidated buffers
		for (size_t i = 0; i < drawInfos.size(); i++) {
			const auto& info = drawInfos[i];

			if (!info.image) {
				SPLog("Warning: Batch %zu has null image pointer, skipping", i);
				continue;
			}

			// Track this image for later release
			// Don't AddRef - we're just tracking the pointer, batch already holds the reference
			frameImages.push_back(info.image);

			// Allocate descriptor set for this batch from this frame's pool
			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = perFrameDescriptorPools[frameIndex];
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &descriptorSetLayout;

			VkDescriptorSet batchDescriptorSet;
			VkResult result = vkAllocateDescriptorSets(vkDevice, &allocInfo, &batchDescriptorSet);
			if (result != VK_SUCCESS) {
				SPLog("Failed to allocate descriptor set for batch %zu/%zu (error code: %d). Consider increasing descriptor pool size.",
					i + 1, drawInfos.size(), result);
				// Skip this batch - better to render partial UI than crash
				continue;
			}

			// Update descriptor set with batch image
			VkImageView imageView = info.image->GetImageView();
			VkSampler sampler = info.image->GetSampler();

			if (imageView == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE) {
				SPLog("Warning: Batch %zu has invalid image view (%p) or sampler (%p), skipping",
					i, imageView, sampler);
				continue;
			}

			VkDescriptorImageInfo imageInfo{};
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = imageView;
			imageInfo.sampler = sampler;

			VkWriteDescriptorSet descriptorWrite{};
			descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite.dstSet = batchDescriptorSet;
			descriptorWrite.dstBinding = 0;
			descriptorWrite.dstArrayElement = 0;
			descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrite.descriptorCount = 1;
			descriptorWrite.pImageInfo = &imageInfo;

			vkUpdateDescriptorSets(vkDevice, 1, &descriptorWrite, 0, nullptr);

			// Rectangular clip. Scissor is dynamic state, so this costs nothing
			// but a command and cannot invalidate the pipeline.
			const VkRect2D& batchScissor = info.clip.hasScissor ? info.clip.scissor : fullScissor;
			vkCmdSetScissor(commandBuffer, 0, 1, &batchScissor);

			// Push constants for screen size, texture size and circular clip
			PushConstants pushConstants{};
			pushConstants.invScreenSizeFactored[0] = invUIWidth;
			pushConstants.invScreenSizeFactored[1] = invUIHeight;
			pushConstants.invTextureSize[0] = 1.0f / static_cast<float>(info.image->GetWidth());
			pushConstants.invTextureSize[1] = 1.0f / static_cast<float>(info.image->GetHeight());
			pushConstants.clipCircleCenter[0] = info.clip.circleCenter.x;
			pushConstants.clipCircleCenter[1] = info.clip.circleCenter.y;
			pushConstants.clipCircleRadius = info.clip.circleRadius;

			vkCmdPushConstants(commandBuffer, pipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof(pushConstants), &pushConstants);

			// Bind descriptor set
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
			                        0, 1, &batchDescriptorSet, 0, nullptr);

			// Draw this batch using offsets
			vkCmdDrawIndexed(commandBuffer, info.indexCount, 1, info.indexOffset, info.vertexOffset, 0);
		}

		// Store image references for this frame
		// Images will be kept alive until the next time this frame index is used
		// (at which point the GPU will have finished with them)
		perFrameImages[frameIndex] = std::move(frameImages);

		// Clear all batches for next frame
		batches.clear();
		vertices.clear();
		indices.clear();

		if (image) {
			image->Release();
			image = nullptr;
		}
	}
	} // namespace draw
} // namespace spades
