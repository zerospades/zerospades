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

#include "VulkanPipeline.h"
#include "VulkanProgram.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Core/Exception.h>

namespace spades {
	namespace draw {

		VulkanPipeline::VulkanPipeline(Handle<gui::SDLVulkanDevice> dev, VulkanProgram* program,
		                               const VulkanPipelineConfig& config, VkRenderPass rp,
		                               VkPipelineCache cache)
		: device(std::move(dev)),
		  pipeline(VK_NULL_HANDLE),
		  pipelineLayout(VK_NULL_HANDLE),
		  renderPass(rp) {

			SPADES_MARK_FUNCTION();

			if (!program || !program->IsLinked()) {
				SPRaise("Cannot create pipeline with unlinked program");
			}

			pipelineLayout = program->GetPipelineLayout();

			// Vertex input state
			VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
			vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(config.vertexBindings.size());
			vertexInputInfo.pVertexBindingDescriptions = config.vertexBindings.data();
			vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(config.vertexAttributes.size());
			vertexInputInfo.pVertexAttributeDescriptions = config.vertexAttributes.data();

			// Input assembly state
			VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
			inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssembly.topology = config.topology;
			inputAssembly.primitiveRestartEnable = config.primitiveRestartEnable;

			// Viewport state (we'll use dynamic viewport/scissor)
			VkViewport viewport{};
			viewport.x = 0.0f;
			viewport.y = 0.0f;
			viewport.width = 800.0f;
			viewport.height = 600.0f;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;

			VkRect2D scissor{};
			scissor.offset = {0, 0};
			scissor.extent = {800, 600};

			VkPipelineViewportStateCreateInfo viewportState{};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.pViewports = &viewport;
			viewportState.scissorCount = 1;
			viewportState.pScissors = &scissor;

			// Rasterization state
			VkPipelineRasterizationStateCreateInfo rasterizer{};
			rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizer.depthClampEnable = VK_FALSE;
			rasterizer.rasterizerDiscardEnable = VK_FALSE;
			rasterizer.polygonMode = config.polygonMode;
			rasterizer.lineWidth = config.lineWidth;
			rasterizer.cullMode = config.cullMode;
			rasterizer.frontFace = config.frontFace;
			rasterizer.depthBiasEnable = config.depthBiasEnable;

			// Multisampling state
			VkPipelineMultisampleStateCreateInfo multisampling{};
			multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampling.sampleShadingEnable = config.sampleShadingEnable;
			multisampling.rasterizationSamples = config.rasterizationSamples;

			// Depth and stencil state
			VkPipelineDepthStencilStateCreateInfo depthStencil{};
			depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencil.depthTestEnable = config.depthTestEnable;
			depthStencil.depthWriteEnable = config.depthWriteEnable;
			depthStencil.depthCompareOp = config.depthCompareOp;
			depthStencil.depthBoundsTestEnable = VK_FALSE;
			depthStencil.stencilTestEnable = VK_FALSE;

			// Color blend attachment state
			VkPipelineColorBlendAttachmentState colorBlendAttachment{};
			colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			colorBlendAttachment.blendEnable = config.blendEnable;
			colorBlendAttachment.srcColorBlendFactor = config.srcColorBlendFactor;
			colorBlendAttachment.dstColorBlendFactor = config.dstColorBlendFactor;
			colorBlendAttachment.colorBlendOp = config.colorBlendOp;
			colorBlendAttachment.srcAlphaBlendFactor = config.srcAlphaBlendFactor;
			colorBlendAttachment.dstAlphaBlendFactor = config.dstAlphaBlendFactor;
			colorBlendAttachment.alphaBlendOp = config.alphaBlendOp;

			VkPipelineColorBlendStateCreateInfo colorBlending{};
			colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlending.logicOpEnable = VK_FALSE;
			colorBlending.logicOp = VK_LOGIC_OP_COPY;
			colorBlending.attachmentCount = 1;
			colorBlending.pAttachments = &colorBlendAttachment;

			// Dynamic state
			std::vector<VkDynamicState> dynamicStates;
			if (config.dynamicViewport) dynamicStates.push_back(VK_DYNAMIC_STATE_VIEWPORT);
			if (config.dynamicScissor) dynamicStates.push_back(VK_DYNAMIC_STATE_SCISSOR);

			VkPipelineDynamicStateCreateInfo dynamicState{};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
			dynamicState.pDynamicStates = dynamicStates.data();

			// Create graphics pipeline
			VkGraphicsPipelineCreateInfo pipelineInfo{};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = static_cast<uint32_t>(program->GetShaderStages().size());
			pipelineInfo.pStages = program->GetShaderStages().data();
			pipelineInfo.pVertexInputState = &vertexInputInfo;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterizer;
			pipelineInfo.pMultisampleState = &multisampling;
			pipelineInfo.pDepthStencilState = &depthStencil;
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.pDynamicState = dynamicStates.empty() ? nullptr : &dynamicState;
			pipelineInfo.layout = pipelineLayout;
			pipelineInfo.renderPass = renderPass;
			pipelineInfo.subpass = 0;
			pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

			VkResult result = vkCreateGraphicsPipelines(device->GetDevice(), cache, 1,
			                                            &pipelineInfo, nullptr, &pipeline);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create graphics pipeline (error code: %d)", result);
			}

			SPLog("Created Vulkan graphics pipeline");
		}

		VulkanPipeline::~VulkanPipeline() {
			SPADES_MARK_FUNCTION();

			if (pipeline != VK_NULL_HANDLE && device) {
				vkDestroyPipeline(device->GetDevice(), pipeline, nullptr);
			}
		}

	} // namespace draw
} // namespace spades
