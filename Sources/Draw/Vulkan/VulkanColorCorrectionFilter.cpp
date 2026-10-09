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

#include "VulkanColorCorrectionFilter.h"
#include "VulkanFramebufferManager.h"
#include "VulkanImage.h"
#include "VulkanRenderer.h"
#include "VulkanRenderPassUtils.h"
#include "VulkanTemporaryImagePool.h"
#include <Client/SceneDefinition.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/Math.h>
#include <Core/Settings.h>
#include <Gui/SDLVulkanDevice.h>
#include <algorithm>
#include <cmath>
#include <cstring>

SPADES_SETTING(r_colorCorrection);
SPADES_SETTING(r_saturation);
SPADES_SETTING(r_hdr);
SPADES_SETTING(r_bloom);
SPADES_SETTING(r_exposureValue);
SPADES_SETTING(r_sharpen);
SPADES_SETTING(r_fogShadow);
SPADES_SETTING(r_radiosity);

namespace spades {
	namespace draw {

		VulkanColorCorrectionFilter::VulkanColorCorrectionFilter(VulkanRenderer& r)
		    : VulkanPostProcessFilter(r),
		      colorFormat(VK_FORMAT_UNDEFINED),
		      linearSampler(VK_NULL_HANDLE),
		      ppRenderPass(VK_NULL_HANDLE),
		      singleSamplerDSL(VK_NULL_HANDLE),
		      dualSamplerDSL(VK_NULL_HANDLE),
		      layout(VK_NULL_HANDLE),
		      pipeline(VK_NULL_HANDLE),
		      blurLayout(VK_NULL_HANDLE),
		      blurPipeline(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i)
				perFrameDescPool[i] = VK_NULL_HANDLE;

			colorFormat = r.GetFramebufferManager()->GetMainColorFormat();

			InitRenderPass();
			InitDescriptorSetLayout();
			InitPipeline();
			InitDescriptorPools();
		}

		VulkanColorCorrectionFilter::~VulkanColorCorrectionFilter() {
			SPADES_MARK_FUNCTION();

			VkDevice dev = device->GetDevice();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				for (VkFramebuffer fb : perFrameFramebuffers[i])
					vkDestroyFramebuffer(dev, fb, nullptr);
				if (perFrameDescPool[i] != VK_NULL_HANDLE)
					vkDestroyDescriptorPool(dev, perFrameDescPool[i], nullptr);
			}

			if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(dev, pipeline, nullptr);
			if (layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, layout, nullptr);
			if (blurPipeline != VK_NULL_HANDLE) vkDestroyPipeline(dev, blurPipeline, nullptr);
			if (blurLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, blurLayout, nullptr);
			if (singleSamplerDSL != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, singleSamplerDSL, nullptr);
			if (dualSamplerDSL != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, dualSamplerDSL, nullptr);
			if (linearSampler != VK_NULL_HANDLE) vkDestroySampler(dev, linearSampler, nullptr);
			if (ppRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(dev, ppRenderPass, nullptr);
		}

		void VulkanColorCorrectionFilter::InitRenderPass() {
			VkDevice dev = device->GetDevice();

			VkSubpassDependency dep{};
			dep.srcSubpass = VK_SUBPASS_EXTERNAL;
			dep.dstSubpass = 0;
			dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			ppRenderPass = CreateSimpleColorRenderPass(
			    dev, colorFormat,
			    VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			    VK_IMAGE_LAYOUT_UNDEFINED,
			    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			    &dep);

			VkSamplerCreateInfo si{};
			si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			si.magFilter = VK_FILTER_LINEAR;
			si.minFilter = VK_FILTER_LINEAR;
			si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.anisotropyEnable = VK_FALSE;
			si.maxAnisotropy = 1.0f;
			si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			si.unnormalizedCoordinates = VK_FALSE;
			si.compareEnable = VK_FALSE;
			si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

			if (vkCreateSampler(dev, &si, nullptr, &linearSampler) != VK_SUCCESS)
				SPRaise("Failed to create ColorCorrection sampler");
		}

		void VulkanColorCorrectionFilter::InitDescriptorSetLayout() {
			VkDescriptorSetLayoutBinding bindings[2]{};
			for (uint32_t i = 0; i < 2; ++i) {
				bindings[i].binding = i;
				bindings[i].descriptorCount = 1;
				bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			}

			VkDescriptorSetLayoutCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			info.bindingCount = 1;
			info.pBindings = bindings;
			if (vkCreateDescriptorSetLayout(device->GetDevice(), &info, nullptr, &singleSamplerDSL) != VK_SUCCESS)
				SPRaise("Failed to create ColorCorrection descriptor set layout");

			info.bindingCount = 2;
			if (vkCreateDescriptorSetLayout(device->GetDevice(), &info, nullptr, &dualSamplerDSL) != VK_SUCCESS)
				SPRaise("Failed to create ColorCorrection descriptor set layout");
		}

		VkShaderModule VulkanColorCorrectionFilter::LoadSPIRV(const char* path) {
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

		VkPipeline VulkanColorCorrectionFilter::BuildPipeline(const char* fragmentShader,
		                                                      VkPipelineLayout pipelineLayout) {
			VkDevice dev = device->GetDevice();
			VkPipelineCache cache = renderer.GetPipelineCache();

			VkShaderModule vs = LoadSPIRV("Shaders/Vulkan/PostFilters/PassThrough.vk.vs.spv");
			VkShaderModule fs = LoadSPIRV(fragmentShader);

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
			noBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

			VkPipelineColorBlendStateCreateInfo blend{};
			blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			blend.attachmentCount = 1;
			blend.pAttachments = &noBlend;

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
			pi.layout = pipelineLayout;
			pi.renderPass = ppRenderPass;
			pi.subpass = 0;

			VkPipeline built = VK_NULL_HANDLE;
			const VkResult result = vkCreateGraphicsPipelines(dev, cache, 1, &pi, nullptr, &built);
			vkDestroyShaderModule(dev, vs, nullptr);
			vkDestroyShaderModule(dev, fs, nullptr);
			if (result != VK_SUCCESS)
				SPRaise("Failed to create the ColorCorrection pipeline of %s", fragmentShader);
			return built;
		}

		void VulkanColorCorrectionFilter::InitPipeline() {
			VkDevice dev = device->GetDevice();

			// Three vec4s: the tint and the enhancement, the saturation, HDR and the
			// sharpening, and the blur's shift
			VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 12};
			VkPipelineLayoutCreateInfo li{};
			li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			li.setLayoutCount = 1;
			li.pSetLayouts = &dualSamplerDSL;
			li.pushConstantRangeCount = 1;
			li.pPushConstantRanges = &pcr;
			if (vkCreatePipelineLayout(dev, &li, nullptr, &layout) != VK_SUCCESS)
				SPRaise("Failed to create ColorCorrection pipeline layout");
			pipeline = BuildPipeline("Shaders/Vulkan/PostFilters/ColorCorrection.vk.fs.spv", layout);

			// The blur's step, and whether the input is sRGB-decoded
			VkPushConstantRange blurRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 3};
			li.pSetLayouts = &singleSamplerDSL;
			li.pPushConstantRanges = &blurRange;
			if (vkCreatePipelineLayout(dev, &li, nullptr, &blurLayout) != VK_SUCCESS)
				SPRaise("Failed to create ColorCorrection blur pipeline layout");
			blurPipeline =
			  BuildPipeline("Shaders/Vulkan/PostFilters/ColorCorrectionBlur.vk.fs.spv", blurLayout);
		}

		void VulkanColorCorrectionFilter::InitDescriptorPools() {
			// A frame's sets: the blur's input, and the input with its blur
			VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8};
			VkDescriptorPoolCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			info.poolSizeCount = 1;
			info.pPoolSizes = &size;
			info.maxSets = 4;

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				if (vkCreateDescriptorPool(device->GetDevice(), &info, nullptr, &perFrameDescPool[i]) != VK_SUCCESS)
					SPRaise("Failed to create ColorCorrection descriptor pool");
			}
		}

		VkFramebuffer VulkanColorCorrectionFilter::MakeFramebuffer(VulkanImage* image, int frameSlot) {
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
				SPRaise("Failed to create ColorCorrection framebuffer");
			perFrameFramebuffers[frameSlot].push_back(fb);
			return fb;
		}

		VkDescriptorSet VulkanColorCorrectionFilter::BindTexture(int frameSlot, VkImageView view) {
			VkDescriptorSetAllocateInfo ai{};
			ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			ai.descriptorPool = perFrameDescPool[frameSlot];
			ai.descriptorSetCount = 1;
			ai.pSetLayouts = &singleSamplerDSL;
			VkDescriptorSet set;
			if (vkAllocateDescriptorSets(device->GetDevice(), &ai, &set) != VK_SUCCESS)
				SPRaise("Failed to allocate ColorCorrection descriptor set");

			VkDescriptorImageInfo img{linearSampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
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

		VkDescriptorSet VulkanColorCorrectionFilter::BindTextures(int frameSlot, VkImageView view,
		                                                          VkImageView blurred) {
			VkDescriptorSetAllocateInfo ai{};
			ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			ai.descriptorPool = perFrameDescPool[frameSlot];
			ai.descriptorSetCount = 1;
			ai.pSetLayouts = &dualSamplerDSL;
			VkDescriptorSet set;
			if (vkAllocateDescriptorSets(device->GetDevice(), &ai, &set) != VK_SUCCESS)
				SPRaise("Failed to allocate ColorCorrection descriptor set");

			const VkDescriptorImageInfo images[2] = {
			  {linearSampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			  {linearSampler, blurred, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
			VkWriteDescriptorSet writes[2]{};
			for (uint32_t i = 0; i < 2; ++i) {
				writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[i].dstSet = set;
				writes[i].dstBinding = i;
				writes[i].descriptorCount = 1;
				writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[i].pImageInfo = &images[i];
			}
			vkUpdateDescriptorSets(device->GetDevice(), 2, writes, 0, nullptr);
			return set;
		}

		void VulkanColorCorrectionFilter::Filter(VkCommandBuffer cmd,
		                                         VulkanImage* input,
		                                         VulkanImage* output) {
			SPADES_MARK_FUNCTION();

			int frameSlot = static_cast<int>(renderer.GetCurrentFrameIndex());

			{
				VkDevice dev = device->GetDevice();
				for (VkFramebuffer fb : perFrameFramebuffers[frameSlot])
					vkDestroyFramebuffer(dev, fb, nullptr);
				perFrameFramebuffers[frameSlot].clear();
				vkResetDescriptorPool(dev, perFrameDescPool[frameSlot], 0);
			}

			// Compute tint and saturation per-frame to track live fog colour.
			// Mirrors GLRenderer.cpp:1040-1059.
			//
			// fogColor is used HERE in encoded (perceptual) space exactly like
			// GL. Squaring it ("linearize") makes a bluish fog much darker,
			// which inverts to a stronger warm bias in `tint`, and the whole
			// scene ends up shifted toward red/purple. Match GL: no linearize.
			//
			// As GL, the tint follows the fog colour slowly, so it doesn't jump when
			// the fog changes; it starts from the colour set.
			const Vector3 sceneFogColor = renderer.GetFogColor();
			if (smoothedFogColor.x < 0.0F)
				smoothedFogColor = renderer.GetFullDaylightFogColor();
			Vector3 fogCol = smoothedFogColor;

			Vector3 tint = fogCol + MakeVector3(0.5f, 0.5f, 0.5f);
			tint = MakeVector3(1.0f, 1.0f, 1.0f) / tint;
			tint = Mix(tint, MakeVector3(1.0f, 1.0f, 1.0f), 0.2f);
			float tmin = std::min(std::min(tint.x, tint.y), tint.z);
			if (tmin > 1e-4f)
				tint *= 1.0f / tmin;

			float exposure = std::pow(2.0f, (float)r_exposureValue * 0.5f);
			tint *= exposure;

			// Saturation matches GL: per-scene def.saturation × r_saturation,
			// with a coefficient that depends on HDR/Bloom (see
			// GLColorCorrectionFilter.cpp:114-133).
			const client::SceneDefinition& def = renderer.GetSceneDef();
			float satCvar = (float)r_saturation * def.saturation;
			float enhancement;
			float saturation;
			if ((int)r_hdr) {
				if ((int)r_bloom) {
					saturation  = 0.8f * satCvar;
					enhancement = 0.1f;
				} else {
					saturation  = 0.9f * satCvar;
					enhancement = 0.0f;
				}
			} else {
				if ((int)r_bloom) {
					saturation  = 0.85f * satCvar;
					enhancement = 0.7f;
				} else {
					saturation  = 1.0f * satCvar;
					enhancement = 0.3f;
				}
			}

			// satAndHdr.y is the runtime gate for the ACES branch in the
			// shader (ACES is calibrated for HDR; running it on a non-HDR
			// [0, 1] image shifts blues to purple).
			float useHdr = (int)r_hdr ? 1.0f : 0.0f;

			// The sharpening, as GLColorCorrectionFilter works it out: it recovers the
			// contrast the fog takes away, so it grows with the fog's luminance as the
			// scene shows it (none at night), and r_sharpen scales it. GL raises it to
			// 1.5 at least under temporal AA, which this renderer doesn't have.
			float fogLuminance = (sceneFogColor.x + sceneFogColor.y + sceneFogColor.z) / 3.0F;
			if ((int)r_fogShadow == 2 && (int)r_radiosity != 0) {
				// Fog2 adds a GI factor, so the fog receives some light even if the
				// fog color is set to dark.
				fogLuminance = fogLuminance * 0.9F + 0.2F;
			}
			const float sharpening = std::sqrt(fogLuminance) * 2.7F;
			const float sharpeningFinalGain = Clamp((float)r_sharpen, 0.0F, 1.0F);

			uint32_t w = static_cast<uint32_t>(output->GetWidth());
			uint32_t h = static_cast<uint32_t>(output->GetHeight());

			float pc[12] = {
			    tint.x, tint.y, tint.z, enhancement,
			    saturation, useHdr, sharpening, sharpeningFinalGain,
			    1.0F / (float)input->GetHeight(), 0.0F, 0.0F, 0.0F
			};

			// The tint eases towards the scene's fog, as GL's.
			smoothedFogColor = Mix(smoothedFogColor, sceneFogColor, 0.002F);

			// The horizontal half of the sharpening's blur, in its own pass
			Handle<VulkanImage> blurred;
			VulkanTemporaryImagePool* pool = renderer.GetTemporaryImagePool();
			if (sharpeningFinalGain > 0.0F && pool) {
				blurred = pool->Acquire(input->GetWidth(), input->GetHeight(), colorFormat);

				VkRenderPassBeginInfo blurBegin{};
				blurBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				blurBegin.renderPass = ppRenderPass;
				blurBegin.framebuffer = MakeFramebuffer(blurred.GetPointerOrNull(), frameSlot);
				blurBegin.renderArea.extent = {blurred->GetWidth(), blurred->GetHeight()};
				vkCmdBeginRenderPass(cmd, &blurBegin, VK_SUBPASS_CONTENTS_INLINE);

				const VkViewport blurViewport{0.0f, 0.0f, (float)blurred->GetWidth(),
				                              (float)blurred->GetHeight(), 0.0f, 1.0f};
				const VkRect2D blurScissor{{0, 0}, {blurred->GetWidth(), blurred->GetHeight()}};
				vkCmdSetViewport(cmd, 0, 1, &blurViewport);
				vkCmdSetScissor(cmd, 0, 1, &blurScissor);

				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline);
				const VkDescriptorSet blurSet = BindTexture(frameSlot, input->GetImageView());
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blurLayout, 0, 1,
				                        &blurSet, 0, nullptr);
				const float blurParams[3] = {1.0F / (float)input->GetWidth(), 0.0F, useHdr};
				vkCmdPushConstants(cmd, blurLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
				                   sizeof(blurParams), blurParams);
				vkCmdDraw(cmd, 3, 1, 0, 0);
				vkCmdEndRenderPass(cmd);
			} else {
				// Nothing to sharpen: the shader leaves the blur unread.
				pc[7] = 0.0F;
			}

			VkFramebuffer fb = MakeFramebuffer(output, frameSlot);
			VkDescriptorSet dsSet =
			  BindTextures(frameSlot, input->GetImageView(),
			               blurred ? blurred->GetImageView() : input->GetImageView());

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

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			                        layout, 0, 1, &dsSet, 0, nullptr);
			vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT,
			                   0, sizeof(pc), pc);

			vkCmdDraw(cmd, 3, 1, 0, 0);

			vkCmdEndRenderPass(cmd);
		}

	} // namespace draw
} // namespace spades
