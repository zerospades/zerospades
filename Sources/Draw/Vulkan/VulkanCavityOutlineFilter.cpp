/*
 Copyright (c) 2026 Fran6nd

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

#include "VulkanCavityOutlineFilter.h"
#include "VulkanFramebufferManager.h"
#include "VulkanImage.h"
#include "VulkanRenderer.h"
#include "VulkanRenderPassUtils.h"
#include <Client/SceneDefinition.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/Math.h>
#include <Core/Settings.h>
#include <Gui/SDLVulkanDevice.h>
#include <algorithm>
#include <cstring>

DEFINE_SPADES_SETTING(r_outlinesStrength, "1");
DEFINE_SPADES_SETTING(r_outlinesDepthThreshold, "0.05");

namespace spades {
	namespace draw {

		// Push constants. Matches CavityOutline.vk.fs layout exactly.
		// 16 + 16 + 16 = 48 bytes (three vec4 slots).
		struct CavityPushConstants {
			float invViewport[4];           // [0..15]  xy = 1/screen, zw reserved
			float zNearFarFogStrength[4];   // [16..31] x=zNear, y=zFar, z=fogDistance, w=strength
			float thresholds[4];            // [32..47] x=relative depth threshold
		};
		static_assert(sizeof(CavityPushConstants) == 48, "CavityPushConstants must be 48 bytes");

		VulkanCavityOutlineFilter::VulkanCavityOutlineFilter(VulkanRenderer& r)
		    : VulkanPostProcessFilter(r),
		      colorFormat(VK_FORMAT_UNDEFINED),
		      colorSampler(VK_NULL_HANDLE),
		      ppRenderPass(VK_NULL_HANDLE),
		      dualSamplerDSL(VK_NULL_HANDLE),
		      cavityLayout(VK_NULL_HANDLE),
		      cavityPipeline(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i)
				perFrameDescPool[i] = VK_NULL_HANDLE;

			colorFormat = r.GetFramebufferManager()->GetMainColorFormat();

			InitRenderPass();
			InitDescriptorSetLayout();
			InitPipeline();
			InitDescriptorPools();
		}

		VulkanCavityOutlineFilter::~VulkanCavityOutlineFilter() {
			SPADES_MARK_FUNCTION();

			VkDevice dev = device->GetDevice();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				for (VkFramebuffer fb : perFrameFramebuffers[i])
					vkDestroyFramebuffer(dev, fb, nullptr);
				if (perFrameDescPool[i] != VK_NULL_HANDLE)
					vkDestroyDescriptorPool(dev, perFrameDescPool[i], nullptr);
			}

			if (cavityPipeline != VK_NULL_HANDLE) vkDestroyPipeline(dev, cavityPipeline, nullptr);
			if (cavityLayout   != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, cavityLayout, nullptr);
			if (dualSamplerDSL != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, dualSamplerDSL, nullptr);
			if (colorSampler   != VK_NULL_HANDLE) vkDestroySampler(dev, colorSampler, nullptr);
			if (ppRenderPass   != VK_NULL_HANDLE) vkDestroyRenderPass(dev, ppRenderPass, nullptr);
		}

		void VulkanCavityOutlineFilter::InitRenderPass() {
			VkDevice dev = device->GetDevice();

			VkSubpassDependency dep{};
			dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
			dep.dstSubpass    = 0;
			dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dep.dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			ppRenderPass = CreateSimpleColorRenderPass(
			    dev, colorFormat,
			    VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			    VK_IMAGE_LAYOUT_UNDEFINED,
			    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			    &dep);

			VkSamplerCreateInfo si{};
			si.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			si.magFilter               = VK_FILTER_LINEAR;
			si.minFilter               = VK_FILTER_LINEAR;
			si.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.anisotropyEnable        = VK_FALSE;
			si.maxAnisotropy           = 1.0f;
			si.borderColor             = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			si.unnormalizedCoordinates = VK_FALSE;
			si.compareEnable           = VK_FALSE;
			si.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;

			if (vkCreateSampler(dev, &si, nullptr, &colorSampler) != VK_SUCCESS)
				SPRaise("Failed to create cavity outline sampler");
		}

		void VulkanCavityOutlineFilter::InitDescriptorSetLayout() {
			VkDevice dev = device->GetDevice();

			VkDescriptorSetLayoutBinding bindings[2]{};
			for (int i = 0; i < 2; ++i) {
				bindings[i].binding         = static_cast<uint32_t>(i);
				bindings[i].descriptorCount = 1;
				bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
			}

			VkDescriptorSetLayoutCreateInfo info{};
			info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			info.bindingCount = 2;
			info.pBindings    = bindings;

			if (vkCreateDescriptorSetLayout(dev, &info, nullptr, &dualSamplerDSL) != VK_SUCCESS)
				SPRaise("Failed to create cavity outline descriptor set layout");
		}

		VkShaderModule VulkanCavityOutlineFilter::LoadSPIRV(const char* path) {
			std::string data = FileManager::ReadAllBytes(path);
			std::vector<uint32_t> code(data.size() / sizeof(uint32_t));
			std::memcpy(code.data(), data.data(), data.size());

			VkShaderModuleCreateInfo info{};
			info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			info.codeSize = data.size();
			info.pCode    = code.data();

			VkShaderModule mod;
			if (vkCreateShaderModule(device->GetDevice(), &info, nullptr, &mod) != VK_SUCCESS)
				SPRaise("Failed to create shader module: %s", path);
			return mod;
		}

		void VulkanCavityOutlineFilter::InitPipeline() {
			VkDevice         dev   = device->GetDevice();
			VkPipelineCache  cache = renderer.GetPipelineCache();

			// Reuse the shared fullscreen-triangle vertex shader; the cavity
			// fragment shader does all the per-pixel work.
			VkShaderModule vs = LoadSPIRV("Shaders/Vulkan/PostFilters/PassThrough.vk.vs.spv");
			VkShaderModule fs = LoadSPIRV("Shaders/Vulkan/PostFilters/CavityOutline.vk.fs.spv");

			VkPushConstantRange pcr{};
			pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			pcr.offset     = 0;
			pcr.size       = sizeof(CavityPushConstants);

			VkPipelineLayoutCreateInfo li{};
			li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			li.setLayoutCount         = 1;
			li.pSetLayouts            = &dualSamplerDSL;
			li.pushConstantRangeCount = 1;
			li.pPushConstantRanges    = &pcr;
			if (vkCreatePipelineLayout(dev, &li, nullptr, &cavityLayout) != VK_SUCCESS)
				SPRaise("Failed to create cavity outline pipeline layout");

			VkPipelineVertexInputStateCreateInfo vertexInput{};
			vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

			VkPipelineInputAssemblyStateCreateInfo ia{};
			ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			VkPipelineViewportStateCreateInfo vp{};
			vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			vp.viewportCount = 1;
			vp.scissorCount  = 1;

			VkPipelineRasterizationStateCreateInfo rs{};
			rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rs.polygonMode = VK_POLYGON_MODE_FILL;
			rs.cullMode    = VK_CULL_MODE_NONE;
			rs.lineWidth   = 1.0f;

			VkPipelineMultisampleStateCreateInfo ms{};
			ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			VkPipelineDepthStencilStateCreateInfo dss{};
			dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

			VkDynamicState dynArr[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
			VkPipelineDynamicStateCreateInfo dyn{};
			dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dyn.dynamicStateCount = 2;
			dyn.pDynamicStates    = dynArr;

			// The fragment shader composites the input colour with a black
			// overlay using its own alpha output, so disable hardware blending
			// and let the shader do the math. This keeps the filter chain's
			// ping-pong contract intact (each filter overwrites its output).
			VkPipelineColorBlendAttachmentState noBlend{};
			noBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			noBlend.blendEnable = VK_FALSE;

			VkPipelineColorBlendStateCreateInfo blend{};
			blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			blend.attachmentCount = 1;
			blend.pAttachments    = &noBlend;

			VkPipelineShaderStageCreateInfo stages[2]{};
			stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
			              VK_SHADER_STAGE_VERTEX_BIT,   vs, "main", nullptr};
			stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
			              VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};

			VkGraphicsPipelineCreateInfo pi{};
			pi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pi.stageCount          = 2;
			pi.pStages             = stages;
			pi.pVertexInputState   = &vertexInput;
			pi.pInputAssemblyState = &ia;
			pi.pViewportState      = &vp;
			pi.pRasterizationState = &rs;
			pi.pMultisampleState   = &ms;
			pi.pDepthStencilState  = &dss;
			pi.pColorBlendState    = &blend;
			pi.pDynamicState       = &dyn;
			pi.layout              = cavityLayout;
			pi.renderPass          = ppRenderPass;
			pi.subpass             = 0;

			if (vkCreateGraphicsPipelines(dev, cache, 1, &pi, nullptr, &cavityPipeline) != VK_SUCCESS)
				SPRaise("Failed to create cavity outline pipeline");

			vkDestroyShaderModule(dev, vs, nullptr);
			vkDestroyShaderModule(dev, fs, nullptr);
		}

		void VulkanCavityOutlineFilter::InitDescriptorPools() {
			VkDevice dev = device->GetDevice();

			VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4};
			VkDescriptorPoolCreateInfo info{};
			info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			info.poolSizeCount = 1;
			info.pPoolSizes    = &size;
			info.maxSets       = 2;

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				if (vkCreateDescriptorPool(dev, &info, nullptr, &perFrameDescPool[i]) != VK_SUCCESS)
					SPRaise("Failed to create cavity outline descriptor pool");
			}
		}

		VkFramebuffer VulkanCavityOutlineFilter::MakeFramebuffer(VulkanImage* image, int frameSlot) {
			VkImageView view = image->GetImageView();
			VkFramebufferCreateInfo fbInfo{};
			fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.renderPass      = ppRenderPass;
			fbInfo.attachmentCount = 1;
			fbInfo.pAttachments    = &view;
			fbInfo.width           = image->GetWidth();
			fbInfo.height          = image->GetHeight();
			fbInfo.layers          = 1;

			VkFramebuffer fb;
			if (vkCreateFramebuffer(device->GetDevice(), &fbInfo, nullptr, &fb) != VK_SUCCESS)
				SPRaise("Failed to create cavity outline framebuffer");
			perFrameFramebuffers[frameSlot].push_back(fb);
			return fb;
		}

		VkDescriptorSet VulkanCavityOutlineFilter::BindTextures(int         frameSlot,
		                                                        VkImageView colorView,
		                                                        VkImageView depthView,
		                                                        VkSampler   depthSampler) {
			VkDescriptorSetAllocateInfo ai{};
			ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			ai.descriptorPool     = perFrameDescPool[frameSlot];
			ai.descriptorSetCount = 1;
			ai.pSetLayouts        = &dualSamplerDSL;
			VkDescriptorSet set;
			if (vkAllocateDescriptorSets(device->GetDevice(), &ai, &set) != VK_SUCCESS)
				SPRaise("Failed to allocate cavity outline descriptor set");

			VkDescriptorImageInfo imgs[2]{
			    {colorSampler, colorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			    {depthSampler, depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			};
			VkWriteDescriptorSet writes[2]{};
			for (int i = 0; i < 2; ++i) {
				writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[i].dstSet          = set;
				writes[i].dstBinding      = static_cast<uint32_t>(i);
				writes[i].descriptorCount = 1;
				writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[i].pImageInfo      = &imgs[i];
			}
			vkUpdateDescriptorSets(device->GetDevice(), 2, writes, 0, nullptr);
			return set;
		}

		void VulkanCavityOutlineFilter::Filter(VkCommandBuffer cmd,
		                                       VulkanImage*    input,
		                                       VulkanImage*    output) {
			SPADES_MARK_FUNCTION();

			int frameSlot = static_cast<int>(renderer.GetCurrentFrameIndex());

			// Reclaim per-frame resources from the previous use of this slot.
			{
				VkDevice dev = device->GetDevice();
				for (VkFramebuffer fb : perFrameFramebuffers[frameSlot])
					vkDestroyFramebuffer(dev, fb, nullptr);
				perFrameFramebuffers[frameSlot].clear();
				vkResetDescriptorPool(dev, perFrameDescPool[frameSlot], 0);
			}

			const client::SceneDefinition& def = renderer.GetSceneDef();

			uint32_t rw = static_cast<uint32_t>(output->GetWidth());
			uint32_t rh = static_cast<uint32_t>(output->GetHeight());

			CavityPushConstants pc{};

			pc.invViewport[0] = 1.0F / static_cast<float>(rw);
			pc.invViewport[1] = 1.0F / static_cast<float>(rh);
			pc.invViewport[2] = 0.0F;
			pc.invViewport[3] = 0.0F;

			pc.zNearFarFogStrength[0] = def.zNear;
			pc.zNearFarFogStrength[1] = def.zFar;
			pc.zNearFarFogStrength[2] = renderer.GetFogDistance();
			// Edge strength: 1.0 = fully black at peak edge.
			pc.zNearFarFogStrength[3] =
			    std::max(0.0F, std::min(4.0F, (float)r_outlinesStrength));

			// Relative depth threshold. The Laplacian magnitude is normalised
			// by the centre tap's linearised depth, so this is "fraction of
			// view distance". 0.05 means: edges where the depth jump exceeds
			// 5% of how far the centre is from the camera. Tuned for voxel
			// scale (1 unit = 1 voxel cube) and typical viewing distances.
			pc.thresholds[0] =
			    std::max(0.001F, std::min(1.0F, (float)r_outlinesDepthThreshold));
			pc.thresholds[1] = 0.0F;
			pc.thresholds[2] = 0.0F;
			pc.thresholds[3] = 0.0F;

			Handle<VulkanImage> depthImg = renderer.GetFramebufferManager()->GetResolvedDepthImage();

			VkDescriptorSet ds = BindTextures(frameSlot,
			    input->GetImageView(),
			    depthImg->GetImageView(), depthImg->GetSampler());

			VkFramebuffer fb = MakeFramebuffer(output, frameSlot);

			VkClearValue cv{};
			cv.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

			VkRenderPassBeginInfo rpBegin{};
			rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			rpBegin.renderPass        = ppRenderPass;
			rpBegin.framebuffer       = fb;
			rpBegin.renderArea.extent = {rw, rh};
			rpBegin.clearValueCount   = 1;
			rpBegin.pClearValues      = &cv;

			vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport{0.0f, 0.0f, (float)rw, (float)rh, 0.0f, 1.0f};
			VkRect2D   scissor{{0, 0}, {rw, rh}};
			vkCmdSetViewport(cmd, 0, 1, &viewport);
			vkCmdSetScissor(cmd, 0, 1, &scissor);

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cavityPipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			                        cavityLayout, 0, 1, &ds, 0, nullptr);
			vkCmdPushConstants(cmd, cavityLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
			                   0, sizeof(pc), &pc);
			vkCmdDraw(cmd, 3, 1, 0, 0);

			vkCmdEndRenderPass(cmd);
		}

	} // namespace draw
} // namespace spades
