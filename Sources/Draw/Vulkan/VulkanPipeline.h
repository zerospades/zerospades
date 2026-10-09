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

#include <vector>
#include <vulkan/vulkan.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanProgram;

		// Pipeline configuration
		struct VulkanPipelineConfig {
			// Vertex input
			std::vector<VkVertexInputBindingDescription> vertexBindings;
			std::vector<VkVertexInputAttributeDescription> vertexAttributes;

			// Input assembly
			VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			VkBool32 primitiveRestartEnable = VK_FALSE;

			// Viewport and scissor (dynamic by default)
			bool dynamicViewport = true;
			bool dynamicScissor = true;

			// Rasterization
			VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
			VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
			VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			VkBool32 depthBiasEnable = VK_FALSE;
			float lineWidth = 1.0f;

			// Depth and stencil
			VkBool32 depthTestEnable = VK_TRUE;
			VkBool32 depthWriteEnable = VK_TRUE;
			VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;

			// Multisampling
			VkSampleCountFlagBits rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			VkBool32 sampleShadingEnable = VK_FALSE;

			// Color blending
			VkBool32 blendEnable = VK_FALSE;
			VkBlendFactor srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
			VkBlendFactor dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
			VkBlendOp colorBlendOp = VK_BLEND_OP_ADD;
			VkBlendFactor srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			VkBlendFactor dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			VkBlendOp alphaBlendOp = VK_BLEND_OP_ADD;
		};

		class VulkanPipeline : public RefCountedObject {
			Handle<gui::SDLVulkanDevice> device;
			VkPipeline pipeline;
			VkPipelineLayout pipelineLayout;
			VkRenderPass renderPass;

		protected:
			~VulkanPipeline();

		public:
			VulkanPipeline(Handle<gui::SDLVulkanDevice> device, VulkanProgram* program,
			               const VulkanPipelineConfig& config, VkRenderPass renderPass,
			               VkPipelineCache pipelineCache = VK_NULL_HANDLE);

			VkPipeline GetPipeline() const { return pipeline; }
			VkPipelineLayout GetPipelineLayout() const { return pipelineLayout; }
		};

	} // namespace draw
} // namespace spades
