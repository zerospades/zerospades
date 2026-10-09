/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "VulkanGlareRenderer.h"

#include <array>
#include <cmath>

#include "VulkanFramebufferManager.h"
#include "VulkanImage.h"
#include "VulkanRenderer.h"
#include "VulkanSpirvCache.h"
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Gui/SDLVulkanDevice.h>

namespace spades {
	namespace draw {
		namespace {
			/** `Glare.vert` and `Glare.frag`'s push constants. */
			struct GlarePushConstants {
				float drawRange[4];
				float color[3];
				float firstPersonDepthEnd;
				float sourceCoord[2];
				float sourceSpread[2];
				float sourceDepth;
				float zNear;
				float zFar;
				float outputIsLinear;
			};
			static_assert(sizeof(GlarePushConstants) <= 128,
			              "push constants past 128 bytes are not portable");

			/** The radius of a glaring lamp's lens, in blocks: how much of the scene
			 * around it is looked at to tell whether it is in sight. */
			constexpr float kSourceRadius = 0.1F;

			/** How far nearer than the lamp something has to be drawn to hide it, in
			 * blocks: the headlamp it shines from is drawn about as far as it. */
			constexpr float kSourceOcclusionMargin = 0.25F;

			/** The most glares a frame draws; a pool holds a set for each. */
			constexpr std::uint32_t kMaxGlaresPerFrame = 256;

			bool IsSrgbFormat(VkFormat format) {
				switch (format) {
					case VK_FORMAT_B8G8R8A8_SRGB:
					case VK_FORMAT_R8G8B8A8_SRGB:
					case VK_FORMAT_A8B8G8R8_SRGB_PACK32: return true;
					default: return false;
				}
			}

			VkShaderModule CreateModule(VkDevice device, const char* path) {
				const std::vector<std::uint32_t> code = SpirvCache::Load(path);
				VkShaderModuleCreateInfo info{};
				info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				info.codeSize = code.size() * sizeof(std::uint32_t);
				info.pCode = code.data();
				VkShaderModule module;
				if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS)
					SPRaise("Failed to create shader module: %s", path);
				return module;
			}
		} // namespace

		VulkanGlareRenderer::VulkanGlareRenderer(VulkanRenderer& renderer,
		                                         VkRenderPass swapchainRenderPass,
		                                         std::size_t framesInFlight)
		    : renderer(renderer), device(renderer.GetDevice()->GetDevice()) {
			SPADES_MARK_FUNCTION();

			// The glare's image, and the scene's depth, which the vertex shader reads
			// around the lamp too
			std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
			for (std::uint32_t i = 0; i < bindings.size(); i++) {
				bindings[i].binding = i;
				bindings[i].descriptorCount = 1;
				bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			}
			bindings[1].stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
			layoutInfo.pBindings = bindings.data();
			if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &setLayout) != VK_SUCCESS)
				SPRaise("Failed to create the glare descriptor set layout");

			VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			                              kMaxGlaresPerFrame * 2};
			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.maxSets = kMaxGlaresPerFrame;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes = &poolSize;
			pools.resize(framesInFlight, VK_NULL_HANDLE);
			for (VkDescriptorPool& pool : pools)
				if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
					SPRaise("Failed to create a glare descriptor pool");

			CreatePipeline(swapchainRenderPass);
		}

		VulkanGlareRenderer::~VulkanGlareRenderer() {
			SPADES_MARK_FUNCTION();

			if (pipeline != VK_NULL_HANDLE)
				vkDestroyPipeline(device, pipeline, nullptr);
			if (pipelineLayout != VK_NULL_HANDLE)
				vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			for (VkDescriptorPool pool : pools)
				if (pool != VK_NULL_HANDLE)
					vkDestroyDescriptorPool(device, pool, nullptr);
			if (setLayout != VK_NULL_HANDLE)
				vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
		}

		void VulkanGlareRenderer::CreatePipeline(VkRenderPass swapchainRenderPass) {
			VkPushConstantRange pushRange{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			                              0, sizeof(GlarePushConstants)};
			VkPipelineLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layoutInfo.setLayoutCount = 1;
			layoutInfo.pSetLayouts = &setLayout;
			layoutInfo.pushConstantRangeCount = 1;
			layoutInfo.pPushConstantRanges = &pushRange;
			if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
				SPRaise("Failed to create the glare pipeline layout");

			VkShaderModule vertexModule = CreateModule(device, "Shaders/Vulkan/Glare.vert.spv");
			VkShaderModule fragmentModule = CreateModule(device, "Shaders/Vulkan/Glare.frag.spv");

			std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
			stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			stages[0].module = vertexModule;
			stages[0].pName = "main";
			stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[1].module = fragmentModule;
			stages[1].pName = "main";

			VkPipelineVertexInputStateCreateInfo vertexInput{};
			vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

			VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
			inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

			VkPipelineViewportStateCreateInfo viewportState{};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.scissorCount = 1;

			VkPipelineRasterizationStateCreateInfo rasterization{};
			rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterization.polygonMode = VK_POLYGON_MODE_FILL;
			rasterization.cullMode = VK_CULL_MODE_NONE;
			rasterization.lineWidth = 1.0F;

			VkPipelineMultisampleStateCreateInfo multisample{};
			multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			// Added onto the frame: what is underneath stays.
			VkPipelineColorBlendAttachmentState blendAttachment{};
			blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			blendAttachment.blendEnable = VK_TRUE;
			blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
			blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
			blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
			blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

			VkPipelineColorBlendStateCreateInfo blend{};
			blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			blend.attachmentCount = 1;
			blend.pAttachments = &blendAttachment;

			std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT,
			                                            VK_DYNAMIC_STATE_SCISSOR};
			VkPipelineDynamicStateCreateInfo dynamic{};
			dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
			dynamic.pDynamicStates = dynamicStates.data();

			VkGraphicsPipelineCreateInfo pipelineInfo{};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
			pipelineInfo.pStages = stages.data();
			pipelineInfo.pVertexInputState = &vertexInput;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterization;
			pipelineInfo.pMultisampleState = &multisample;
			pipelineInfo.pColorBlendState = &blend;
			pipelineInfo.pDynamicState = &dynamic;
			pipelineInfo.layout = pipelineLayout;
			pipelineInfo.renderPass = swapchainRenderPass;
			pipelineInfo.subpass = 0;

			const VkResult result = vkCreateGraphicsPipelines(device, renderer.GetPipelineCache(), 1,
			                                                  &pipelineInfo, nullptr, &pipeline);
			vkDestroyShaderModule(device, vertexModule, nullptr);
			vkDestroyShaderModule(device, fragmentModule, nullptr);
			if (result != VK_SUCCESS)
				SPRaise("Failed to create the glare pipeline (error code: %d)", result);
		}

		void VulkanGlareRenderer::Clear() { glares.clear(); }

		void VulkanGlareRenderer::Add(VulkanImage& image, const client::GlareParam& param) {
			Glare glare;
			glare.image = image;
			glare.param = param;
			glares.push_back(std::move(glare));
		}

		void VulkanGlareRenderer::Render(VkCommandBuffer commandBuffer, std::size_t frameSlot) {
			SPADES_MARK_FUNCTION();

			VkDescriptorPool pool = pools[frameSlot];
			vkResetDescriptorPool(device, pool, 0);

			if (glares.empty())
				return;

			Handle<VulkanImage> depth = renderer.GetFramebufferManager()->GetResolvedDepthImage();
			if (!depth)
				return;

			const client::SceneDefinition& def = renderer.GetSceneDef();
			const Matrix4& projectionView = renderer.GetProjectionViewMatrix();

			// From the 2D units the glares are sized in to normalized device ones
			const float ndcPerUnitX = 2.0F / renderer.ScreenWidth();
			const float ndcPerUnitY = 2.0F / renderer.ScreenHeight();

			const VkExtent2D extent = renderer.GetDevice()->GetSwapchainExtent();
			const VkViewport viewport{0.0F, 0.0F, (float)extent.width, (float)extent.height,
			                          0.0F, 1.0F};
			const VkRect2D scissor{{0, 0}, extent};

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			const float tanHalfX = std::tan(def.fovX * 0.5F);
			const float tanHalfY = std::tan(def.fovY * 0.5F);

			GlarePushConstants push{};
			push.firstPersonDepthEnd = VulkanRenderer::kFirstPersonDepthEnd;
			push.zNear = def.zNear;
			push.zFar = def.zFar;
			push.outputIsLinear =
			  IsSrgbFormat(renderer.GetDevice()->GetSwapchainImageFormat()) ? 1.0F : 0.0F;

			std::uint32_t numDrawn = 0;
			for (const Glare& glare : glares) {
				if (numDrawn == kMaxGlaresPerFrame)
					break;

				const client::GlareParam& param = glare.param;

				// `w` is how far ahead of the camera the light is.
				const Vector4 clip = projectionView * param.origin;
				if (clip.w <= def.zNear)
					continue;
				const float centreX = clip.x / clip.w;
				const float centreY = clip.y / clip.w;

				VulkanImage* image = glare.image.GetPointerOrNull();

				VkDescriptorSetAllocateInfo allocInfo{};
				allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				allocInfo.descriptorPool = pool;
				allocInfo.descriptorSetCount = 1;
				allocInfo.pSetLayouts = &setLayout;
				VkDescriptorSet set;
				if (vkAllocateDescriptorSets(device, &allocInfo, &set) != VK_SUCCESS)
					SPRaise("Failed to allocate a glare descriptor set");

				const std::array<VkDescriptorImageInfo, 2> images{
				  VkDescriptorImageInfo{image->GetSampler(), image->GetImageView(),
				                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
				  VkDescriptorImageInfo{depth->GetSampler(), depth->GetImageView(),
				                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
				std::array<VkWriteDescriptorSet, 2> writes{};
				for (std::uint32_t i = 0; i < writes.size(); i++) {
					writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					writes[i].dstSet = set;
					writes[i].dstBinding = i;
					writes[i].descriptorCount = 1;
					writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					writes[i].pImageInfo = &images[i];
				}
				vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()),
				                       writes.data(), 0, nullptr);

				const float halfWidth = param.radius * ndcPerUnitX;
				const float halfHeight = param.radius * ndcPerUnitY;
				push.drawRange[0] = centreX - halfWidth;
				push.drawRange[1] = centreY - halfHeight;
				push.drawRange[2] = centreX + halfWidth;
				push.drawRange[3] = centreY + halfHeight;
				push.color[0] = param.color.x;
				push.color[1] = param.color.y;
				push.color[2] = param.color.z;

				// The lamp in the scene's depth, which runs from the top down, and how
				// far its lens spreads over it
				push.sourceCoord[0] = 0.5F + centreX * 0.5F;
				push.sourceCoord[1] = 0.5F - centreY * 0.5F;
				push.sourceSpread[0] = kSourceRadius / (clip.w * tanHalfX) * 0.5F;
				push.sourceSpread[1] = kSourceRadius / (clip.w * tanHalfY) * 0.5F;
				push.sourceDepth = clip.w - kSourceOcclusionMargin;

				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				                        pipelineLayout, 0, 1, &set, 0, nullptr);
				vkCmdPushConstants(commandBuffer, pipelineLayout,
				                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
				                   sizeof(push), &push);
				vkCmdDraw(commandBuffer, 4, 1, 0, 0);
				numDrawn++;
			}
		}
	} // namespace draw
} // namespace spades
