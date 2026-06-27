/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

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

#include "VulkanDepthResolveFilter.h"
#include "VulkanFramebufferManager.h"
#include "VulkanImage.h"
#include "VulkanRenderer.h"
#include "VulkanRenderPassUtils.h"
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Gui/SDLVulkanDevice.h>
#include <cstring>

namespace spades {
	namespace draw {

		VulkanDepthResolveFilter::VulkanDepthResolveFilter(VulkanRenderer& r)
		    : VulkanPostProcessFilter(r),
		      depthSampler(VK_NULL_HANDLE),
		      ppRenderPass(VK_NULL_HANDLE),
		      msDepthDSL(VK_NULL_HANDLE),
		      resolveLayout(VK_NULL_HANDLE),
		      resolvePipeline(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				perFrameDescPool[i] = VK_NULL_HANDLE;
				slotResetFrame[i] = UINT32_MAX;
			}

			InitRenderPass();
			InitDescriptorSetLayout();
			InitPipeline();
			InitDescriptorPools();
		}

		VulkanDepthResolveFilter::~VulkanDepthResolveFilter() {
			SPADES_MARK_FUNCTION();

			VkDevice dev = device->GetDevice();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				for (VkFramebuffer fb : perFrameFramebuffers[i])
					vkDestroyFramebuffer(dev, fb, nullptr);
				if (perFrameDescPool[i] != VK_NULL_HANDLE)
					vkDestroyDescriptorPool(dev, perFrameDescPool[i], nullptr);
			}

			if (resolvePipeline != VK_NULL_HANDLE) vkDestroyPipeline(dev, resolvePipeline, nullptr);
			if (resolveLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, resolveLayout, nullptr);
			if (msDepthDSL != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, msDepthDSL, nullptr);
			if (depthSampler != VK_NULL_HANDLE) vkDestroySampler(dev, depthSampler, nullptr);
			if (ppRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(dev, ppRenderPass, nullptr);
		}

		void VulkanDepthResolveFilter::InitRenderPass() {
			VkDevice dev = device->GetDevice();

			VkSubpassDependency dep{};
			dep.srcSubpass = VK_SUBPASS_EXTERNAL;
			dep.dstSubpass = 0;
			dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			ppRenderPass = CreateSimpleColorRenderPass(
			    dev, kResolvedDepthFormat,
			    VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			    VK_IMAGE_LAYOUT_UNDEFINED,
			    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			    &dep);

			VkSamplerCreateInfo si{};
			si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			si.magFilter = VK_FILTER_NEAREST;
			si.minFilter = VK_FILTER_NEAREST;
			si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.anisotropyEnable = VK_FALSE;
			si.maxAnisotropy = 1.0f;
			si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			si.unnormalizedCoordinates = VK_FALSE;
			si.compareEnable = VK_FALSE;
			si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

			if (vkCreateSampler(dev, &si, nullptr, &depthSampler) != VK_SUCCESS)
				SPRaise("Failed to create depth-resolve sampler");
		}

		void VulkanDepthResolveFilter::InitDescriptorSetLayout() {
			VkDescriptorSetLayoutBinding b{};
			b.binding = 0;
			b.descriptorCount = 1;
			b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

			VkDescriptorSetLayoutCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			info.bindingCount = 1;
			info.pBindings = &b;

			if (vkCreateDescriptorSetLayout(device->GetDevice(), &info, nullptr, &msDepthDSL) != VK_SUCCESS)
				SPRaise("Failed to create depth-resolve descriptor set layout");
		}

		VkShaderModule VulkanDepthResolveFilter::LoadSPIRV(const char* path) {
			std::string data = FileManager::ReadAllBytes(path);
			std::vector<uint32_t> code(data.size() / sizeof(uint32_t));
			std::memcpy(code.data(), data.data(), data.size());

			VkShaderModuleCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			info.codeSize = data.size();
			info.pCode = code.data();

			VkShaderModule mod;
			if (vkCreateShaderModule(device->GetDevice(), &info, nullptr, &mod) != VK_SUCCESS)
				SPRaise("Failed to create shader module: %s", path);
			return mod;
		}

		void VulkanDepthResolveFilter::InitPipeline() {
			VkDevice dev = device->GetDevice();
			VkPipelineCache cache = renderer.GetPipelineCache();

			VkShaderModule vs = LoadSPIRV("Shaders/Vulkan/PostFilters/PassThrough.vk.vs.spv");
			VkShaderModule fs = LoadSPIRV("Shaders/Vulkan/PostFilters/DepthResolve.vk.fs.spv");

			VkPipelineVertexInputStateCreateInfo vertexInput{};
			vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

			VkPipelineInputAssemblyStateCreateInfo ia{};
			ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			VkPipelineViewportStateCreateInfo vp{};
			vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			vp.viewportCount = 1;
			vp.scissorCount = 1;

			VkPipelineRasterizationStateCreateInfo rs{};
			rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rs.polygonMode = VK_POLYGON_MODE_FILL;
			rs.cullMode = VK_CULL_MODE_NONE;
			rs.lineWidth = 1.0f;

			// The resolve target is single-sample even though the source is not.
			VkPipelineMultisampleStateCreateInfo ms{};
			ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			VkPipelineDepthStencilStateCreateInfo ds{};
			ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

			VkDynamicState dynArr[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
			VkPipelineDynamicStateCreateInfo dyn{};
			dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dyn.dynamicStateCount = 2;
			dyn.pDynamicStates = dynArr;

			VkPipelineColorBlendAttachmentState noBlend{};
			noBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;

			VkPipelineColorBlendStateCreateInfo blend{};
			blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			blend.attachmentCount = 1;
			blend.pAttachments = &noBlend;

			VkPipelineLayoutCreateInfo li{};
			li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			li.setLayoutCount = 1;
			li.pSetLayouts = &msDepthDSL;
			if (vkCreatePipelineLayout(dev, &li, nullptr, &resolveLayout) != VK_SUCCESS)
				SPRaise("Failed to create depth-resolve pipeline layout");

			VkPipelineShaderStageCreateInfo stages[2]{};
			stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
			             VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
			stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
			             VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};

			VkGraphicsPipelineCreateInfo pi{};
			pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pi.stageCount = 2;
			pi.pStages = stages;
			pi.pVertexInputState = &vertexInput;
			pi.pInputAssemblyState = &ia;
			pi.pViewportState = &vp;
			pi.pRasterizationState = &rs;
			pi.pMultisampleState = &ms;
			pi.pDepthStencilState = &ds;
			pi.pColorBlendState = &blend;
			pi.pDynamicState = &dyn;
			pi.layout = resolveLayout;
			pi.renderPass = ppRenderPass;
			pi.subpass = 0;

			if (vkCreateGraphicsPipelines(dev, cache, 1, &pi, nullptr, &resolvePipeline) != VK_SUCCESS)
				SPRaise("Failed to create depth-resolve pipeline");

			vkDestroyShaderModule(dev, vs, nullptr);
			vkDestroyShaderModule(dev, fs, nullptr);
		}

		void VulkanDepthResolveFilter::InitDescriptorPools() {
			VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4};
			VkDescriptorPoolCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			info.poolSizeCount = 1;
			info.pPoolSizes = &size;
			info.maxSets = 4;

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				if (vkCreateDescriptorPool(device->GetDevice(), &info, nullptr, &perFrameDescPool[i]) != VK_SUCCESS)
					SPRaise("Failed to create depth-resolve descriptor pool");
			}
		}

		VkFramebuffer VulkanDepthResolveFilter::MakeFramebuffer(VulkanImage* image, int frameSlot) {
			VkImageView view = image->GetImageView();
			VkFramebufferCreateInfo fbInfo{};
			fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.renderPass = ppRenderPass;
			fbInfo.attachmentCount = 1;
			fbInfo.pAttachments = &view;
			fbInfo.width = image->GetWidth();
			fbInfo.height = image->GetHeight();
			fbInfo.layers = 1;

			VkFramebuffer fb;
			if (vkCreateFramebuffer(device->GetDevice(), &fbInfo, nullptr, &fb) != VK_SUCCESS)
				SPRaise("Failed to create depth-resolve framebuffer");
			perFrameFramebuffers[frameSlot].push_back(fb);
			return fb;
		}

		VkDescriptorSet VulkanDepthResolveFilter::BindDepth(int frameSlot, VkImageView view) {
			VkDescriptorSetAllocateInfo ai{};
			ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			ai.descriptorPool = perFrameDescPool[frameSlot];
			ai.descriptorSetCount = 1;
			ai.pSetLayouts = &msDepthDSL;
			VkDescriptorSet set;
			if (vkAllocateDescriptorSets(device->GetDevice(), &ai, &set) != VK_SUCCESS)
				SPRaise("Failed to allocate depth-resolve descriptor set");

			VkDescriptorImageInfo img{depthSampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
			VkWriteDescriptorSet w{};
			w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			w.dstSet = set;
			w.dstBinding = 0;
			w.descriptorCount = 1;
			w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			w.pImageInfo = &img;
			vkUpdateDescriptorSets(device->GetDevice(), 1, &w, 0, nullptr);
			return set;
		}

		void VulkanDepthResolveFilter::Resolve(VkCommandBuffer cmd,
		                                       VulkanImage* msaaDepth,
		                                       VulkanImage* output) {
			SPADES_MARK_FUNCTION();

			int frameSlot = static_cast<int>(renderer.GetCurrentFrameIndex());

			// Reset this slot's pool/framebuffers only on its first Resolve() this
			// frame; subsequent resolves in the same frame (e.g. mirror depth then
			// scene depth) allocate additional sets without freeing the earlier ones,
			// which are still referenced by the in-flight command buffer.
			std::uint32_t frame = renderer.GetFrameNumber();
			if (slotResetFrame[frameSlot] != frame) {
				VkDevice dev = device->GetDevice();
				for (VkFramebuffer fb : perFrameFramebuffers[frameSlot])
					vkDestroyFramebuffer(dev, fb, nullptr);
				perFrameFramebuffers[frameSlot].clear();
				vkResetDescriptorPool(dev, perFrameDescPool[frameSlot], 0);
				slotResetFrame[frameSlot] = frame;
			}

			uint32_t w = static_cast<uint32_t>(output->GetWidth());
			uint32_t h = static_cast<uint32_t>(output->GetHeight());

			VkFramebuffer fb = MakeFramebuffer(output, frameSlot);
			VkDescriptorSet ds = BindDepth(frameSlot, msaaDepth->GetImageView());

			VkRenderPassBeginInfo rpBegin{};
			rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			rpBegin.renderPass = ppRenderPass;
			rpBegin.framebuffer = fb;
			rpBegin.renderArea.extent = {w, h};

			vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport{0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f};
			VkRect2D scissor{{0, 0}, {w, h}};
			vkCmdSetViewport(cmd, 0, 1, &viewport);
			vkCmdSetScissor(cmd, 0, 1, &scissor);

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, resolvePipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			                        resolveLayout, 0, 1, &ds, 0, nullptr);

			vkCmdDraw(cmd, 3, 1, 0, 0);

			vkCmdEndRenderPass(cmd);
		}

	} // namespace draw
} // namespace spades
