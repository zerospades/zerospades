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
#include <Core/Exception.h>

namespace spades {
	namespace draw {

		// Fluent builder for Vulkan graphics pipelines.
		// Eliminates boilerplate code for post-processing filter pipelines.
		class VulkanPipelineBuilder {
		private:
			VkDevice device;
			VkPipelineCache pipelineCache;
			VkPipelineLayout pipelineLayout;
			VkRenderPass renderPass;

			std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

			VkVertexInputBindingDescription vertexBinding;
			VkVertexInputAttributeDescription vertexAttribute;
			VkPipelineVertexInputStateCreateInfo vertexInputInfo;
			VkPipelineInputAssemblyStateCreateInfo inputAssembly;
			VkPipelineViewportStateCreateInfo viewportState;
			VkPipelineRasterizationStateCreateInfo rasterizer;
			VkPipelineMultisampleStateCreateInfo multisampling;
			VkPipelineDepthStencilStateCreateInfo depthStencil;
			VkPipelineColorBlendAttachmentState colorBlendAttachment;
			VkPipelineColorBlendStateCreateInfo colorBlending;
			VkDynamicState dynamicStates[2];
			VkPipelineDynamicStateCreateInfo dynamicState;

			void InitializeDefaults() {
				// Vertex binding for 2D quad (position only)
				vertexBinding = {};
				vertexBinding.binding = 0;
				vertexBinding.stride = sizeof(float) * 2;
				vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

				// Vertex attribute (2D position)
				vertexAttribute = {};
				vertexAttribute.binding = 0;
				vertexAttribute.location = 0;
				vertexAttribute.format = VK_FORMAT_R32G32_SFLOAT;
				vertexAttribute.offset = 0;

				// Vertex input state
				vertexInputInfo = {};
				vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
				vertexInputInfo.vertexBindingDescriptionCount = 1;
				vertexInputInfo.pVertexBindingDescriptions = &vertexBinding;
				vertexInputInfo.vertexAttributeDescriptionCount = 1;
				vertexInputInfo.pVertexAttributeDescriptions = &vertexAttribute;

				// Input assembly (triangle list)
				inputAssembly = {};
				inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
				inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
				inputAssembly.primitiveRestartEnable = VK_FALSE;

				// Viewport state (dynamic)
				viewportState = {};
				viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
				viewportState.viewportCount = 1;
				viewportState.scissorCount = 1;

				// Rasterization state
				rasterizer = {};
				rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
				rasterizer.depthClampEnable = VK_FALSE;
				rasterizer.rasterizerDiscardEnable = VK_FALSE;
				rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
				rasterizer.lineWidth = 1.0f;
				rasterizer.cullMode = VK_CULL_MODE_NONE;
				rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
				rasterizer.depthBiasEnable = VK_FALSE;

				// Multisampling (disabled)
				multisampling = {};
				multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
				multisampling.sampleShadingEnable = VK_FALSE;
				multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

				// Depth stencil (disabled)
				depthStencil = {};
				depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
				depthStencil.depthTestEnable = VK_FALSE;
				depthStencil.depthWriteEnable = VK_FALSE;

				// Color blending (disabled by default)
				colorBlendAttachment = {};
				colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
				                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
				colorBlendAttachment.blendEnable = VK_FALSE;

				colorBlending = {};
				colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
				colorBlending.logicOpEnable = VK_FALSE;
				colorBlending.attachmentCount = 1;
				colorBlending.pAttachments = &colorBlendAttachment;

				// Dynamic states (viewport and scissor)
				dynamicStates[0] = VK_DYNAMIC_STATE_VIEWPORT;
				dynamicStates[1] = VK_DYNAMIC_STATE_SCISSOR;

				dynamicState = {};
				dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
				dynamicState.dynamicStateCount = 2;
				dynamicState.pDynamicStates = dynamicStates;
			}

		public:
			VulkanPipelineBuilder(VkDevice device, VkPipelineCache cache)
				: device(device),
				  pipelineCache(cache),
				  pipelineLayout(VK_NULL_HANDLE),
				  renderPass(VK_NULL_HANDLE) {
				InitializeDefaults();
			}

			VulkanPipelineBuilder& SetShaderStages(const std::vector<VkPipelineShaderStageCreateInfo>& stages) {
				shaderStages = stages;
				return *this;
			}

			VulkanPipelineBuilder& SetPipelineLayout(VkPipelineLayout layout) {
				pipelineLayout = layout;
				return *this;
			}

			VulkanPipelineBuilder& SetRenderPass(VkRenderPass pass) {
				renderPass = pass;
				return *this;
			}

			VulkanPipelineBuilder& SetBlending(bool enabled) {
				colorBlendAttachment.blendEnable = enabled ? VK_TRUE : VK_FALSE;
				return *this;
			}

			VulkanPipelineBuilder& SetAlphaBlending() {
				colorBlendAttachment.blendEnable = VK_TRUE;
				colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
				colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
				colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
				return *this;
			}

			VulkanPipelineBuilder& SetAdditiveBlending() {
				colorBlendAttachment.blendEnable = VK_TRUE;
				colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
				colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
				colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
				colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
				return *this;
			}

			VkPipeline Build() {
				if (shaderStages.empty()) {
					SPRaise("No shader stages set for pipeline");
				}
				if (pipelineLayout == VK_NULL_HANDLE) {
					SPRaise("No pipeline layout set for pipeline");
				}
				if (renderPass == VK_NULL_HANDLE) {
					SPRaise("No render pass set for pipeline");
				}

				VkGraphicsPipelineCreateInfo pipelineInfo = {};
				pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
				pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
				pipelineInfo.pStages = shaderStages.data();
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

				VkPipeline pipeline;
				if (vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
					SPRaise("Failed to create graphics pipeline");
				}

				return pipeline;
			}
		};

	} // namespace draw
} // namespace spades
