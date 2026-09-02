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

#include "VulkanBloomFilter.h"
#include "VulkanBuffer.h"
#include "VulkanImageWrapper.h"
#include "VulkanFramebufferManager.h"
#include "VulkanImage.h"
#include "VulkanRenderer.h"
#include "VulkanRenderPassUtils.h"
#include "VulkanTemporaryImagePool.h"
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/Math.h>
#include <Gui/SDLVulkanDevice.h>
#include <cmath>
#include <cstring>

namespace spades {
	namespace draw {

		// ─────────────────────────────────────────────────────────────
		//  Construction / destruction
		// ─────────────────────────────────────────────────────────────

		VulkanBloomFilter::VulkanBloomFilter(VulkanRenderer& r)
		    : VulkanPostProcessFilter(r),
		      colorFormat(VK_FORMAT_UNDEFINED),
		      linearSampler(VK_NULL_HANDLE),
		      ppRenderPass(VK_NULL_HANDLE),
		      singleSamplerDSL(VK_NULL_HANDLE),
		      dualSamplerDSL(VK_NULL_HANDLE),
		      quadSamplerDSL(VK_NULL_HANDLE),
		      downsampleLayout(VK_NULL_HANDLE),
		      gaussLayout(VK_NULL_HANDLE),
		      upsampleLayout(VK_NULL_HANDLE),
		      compositeLayout(VK_NULL_HANDLE),
		      downsamplePipeline(VK_NULL_HANDLE),
		      gaussPipeline(VK_NULL_HANDLE),
		      upsamplePipeline(VK_NULL_HANDLE),
		      compositePipeline(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i)
				perFrameDescPool[i] = VK_NULL_HANDLE;

			colorFormat = r.GetFramebufferManager()->GetMainColorFormat();

			InitRenderPass();
			InitDescriptorSetLayouts();
			InitPipelines();
			InitDescriptorPools();

			// LensDust composite inputs (see GLLensDustFilter).
			dustImage = r.RegisterImage("Textures/LensDustTexture.jpg");

			noiseImage = Handle<VulkanImage>::New(
			    device, 128u, 128u, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
			    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			noiseImage->CreateSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST,
			                          VK_SAMPLER_ADDRESS_MODE_REPEAT);
			noiseStaging = Handle<VulkanBuffer>::New(
			    device, (VkDeviceSize)(128 * 128 * 4), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			noiseData.resize(128 * 128);
		}

		VulkanBloomFilter::~VulkanBloomFilter() {
			SPADES_MARK_FUNCTION();

			VkDevice dev = device->GetDevice();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				for (VkFramebuffer fb : perFrameFramebuffers[i])
					vkDestroyFramebuffer(dev, fb, nullptr);
				if (perFrameDescPool[i] != VK_NULL_HANDLE)
					vkDestroyDescriptorPool(dev, perFrameDescPool[i], nullptr);
			}

			if (compositePipeline  != VK_NULL_HANDLE) vkDestroyPipeline(dev, compositePipeline,  nullptr);
			if (upsamplePipeline   != VK_NULL_HANDLE) vkDestroyPipeline(dev, upsamplePipeline,   nullptr);
			if (gaussPipeline      != VK_NULL_HANDLE) vkDestroyPipeline(dev, gaussPipeline,      nullptr);
			if (downsamplePipeline != VK_NULL_HANDLE) vkDestroyPipeline(dev, downsamplePipeline, nullptr);

			if (compositeLayout  != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, compositeLayout,  nullptr);
			if (upsampleLayout   != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, upsampleLayout,   nullptr);
			if (gaussLayout      != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, gaussLayout,      nullptr);
			if (downsampleLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, downsampleLayout, nullptr);

			if (quadSamplerDSL   != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, quadSamplerDSL,   nullptr);
			if (dualSamplerDSL   != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, dualSamplerDSL,   nullptr);
			if (singleSamplerDSL != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, singleSamplerDSL, nullptr);

			if (linearSampler  != VK_NULL_HANDLE) vkDestroySampler(dev, linearSampler, nullptr);
			if (ppRenderPass   != VK_NULL_HANDLE) vkDestroyRenderPass(dev, ppRenderPass, nullptr);
		}

		// ─────────────────────────────────────────────────────────────
		//  Initialisation
		// ─────────────────────────────────────────────────────────────

		void VulkanBloomFilter::InitRenderPass() {
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

			if (vkCreateSampler(dev, &si, nullptr, &linearSampler) != VK_SUCCESS)
				SPRaise("Failed to create bloom sampler");
		}

		void VulkanBloomFilter::InitDescriptorSetLayouts() {
			VkDevice dev = device->GetDevice();

			auto MakeDSL = [&](uint32_t bindingCount) {
				VkDescriptorSetLayoutBinding bindings[4]{};
				for (uint32_t i = 0; i < bindingCount; ++i) {
					bindings[i].binding         = i;
					bindings[i].descriptorCount = 1;
					bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
				}
				VkDescriptorSetLayoutCreateInfo info{};
				info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
				info.bindingCount = bindingCount;
				info.pBindings    = bindings;
				VkDescriptorSetLayout dsl;
				if (vkCreateDescriptorSetLayout(dev, &info, nullptr, &dsl) != VK_SUCCESS)
					SPRaise("Failed to create descriptor set layout");
				return dsl;
			};

			singleSamplerDSL = MakeDSL(1);
			dualSamplerDSL   = MakeDSL(2);
			quadSamplerDSL   = MakeDSL(4);
		}

		VkShaderModule VulkanBloomFilter::LoadSPIRV(const char* path) {
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

		void VulkanBloomFilter::InitPipelines() {
			VkDevice dev        = device->GetDevice();
			VkPipelineCache cache = renderer.GetPipelineCache();

			VkShaderModule vs           = LoadSPIRV("Shaders/Vulkan/PostFilters/PassThrough.vk.vs.spv");
			VkShaderModule passthroughFS = LoadSPIRV("Shaders/Vulkan/PostFilters/PassThrough.vk.fs.spv");
			VkShaderModule gaussFS      = LoadSPIRV("Shaders/Vulkan/PostFilters/Gauss1DRGBA.vk.fs.spv");
			VkShaderModule upsampleFS   = LoadSPIRV("Shaders/Vulkan/PostFilters/BloomUpsample.vk.fs.spv");
			VkShaderModule compositeFS  = LoadSPIRV("Shaders/Vulkan/PostFilters/BloomComposite.vk.fs.spv");

			// Fixed pipeline state ─────────────────────────────────

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

			VkPipelineDepthStencilStateCreateInfo ds{};
			ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

			VkDynamicState dynArr[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
			VkPipelineDynamicStateCreateInfo dyn{};
			dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dyn.dynamicStateCount = 2;
			dyn.pDynamicStates    = dynArr;

			VkPipelineColorBlendAttachmentState noBlend{};
			noBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

			// Pipeline layouts ─────────────────────────────────────
			{
				VkPipelineLayoutCreateInfo li{};
				li.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				li.setLayoutCount = 1;
				li.pSetLayouts    = &singleSamplerDSL;
				if (vkCreatePipelineLayout(dev, &li, nullptr, &downsampleLayout) != VK_SUCCESS)
					SPRaise("Failed to create downsample pipeline layout");
			}
			{
				VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 2};
				VkPipelineLayoutCreateInfo li{};
				li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				li.setLayoutCount         = 1;
				li.pSetLayouts            = &singleSamplerDSL;
				li.pushConstantRangeCount = 1;
				li.pPushConstantRanges    = &pcr;
				if (vkCreatePipelineLayout(dev, &li, nullptr, &gaussLayout) != VK_SUCCESS)
					SPRaise("Failed to create gauss pipeline layout");
			}
			{
				VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float)};
				VkPipelineLayoutCreateInfo li{};
				li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				li.setLayoutCount         = 1;
				li.pSetLayouts            = &dualSamplerDSL;
				li.pushConstantRangeCount = 1;
				li.pPushConstantRanges    = &pcr;
				if (vkCreatePipelineLayout(dev, &li, nullptr, &upsampleLayout) != VK_SUCCESS)
					SPRaise("Failed to create upsample pipeline layout");
			}
			{
				VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 4};
				VkPipelineLayoutCreateInfo li{};
				li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				li.setLayoutCount         = 1;
				li.pSetLayouts            = &quadSamplerDSL;
				li.pushConstantRangeCount = 1;
				li.pPushConstantRanges    = &pcr;
				if (vkCreatePipelineLayout(dev, &li, nullptr, &compositeLayout) != VK_SUCCESS)
					SPRaise("Failed to create composite pipeline layout");
			}

			// Helper: build a graphics pipeline ───────────────────
			auto MakePipeline = [&](VkShaderModule fsModule, VkPipelineLayout layout) {
				VkPipelineShaderStageCreateInfo stages[2]{};
				stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
				              VK_SHADER_STAGE_VERTEX_BIT,   vs,       "main", nullptr};
				stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
				              VK_SHADER_STAGE_FRAGMENT_BIT, fsModule, "main", nullptr};

				VkPipelineColorBlendStateCreateInfo blend{};
				blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
				blend.attachmentCount = 1;
				blend.pAttachments    = &noBlend;

				VkGraphicsPipelineCreateInfo pi{};
				pi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
				pi.stageCount          = 2;
				pi.pStages             = stages;
				pi.pVertexInputState   = &vertexInput;
				pi.pInputAssemblyState = &ia;
				pi.pViewportState      = &vp;
				pi.pRasterizationState = &rs;
				pi.pMultisampleState   = &ms;
				pi.pDepthStencilState  = &ds;
				pi.pColorBlendState    = &blend;
				pi.pDynamicState       = &dyn;
				pi.layout              = layout;
				pi.renderPass          = ppRenderPass;
				pi.subpass             = 0;

				VkPipeline p;
				if (vkCreateGraphicsPipelines(dev, cache, 1, &pi, nullptr, &p) != VK_SUCCESS)
					SPRaise("Failed to create bloom pipeline");
				return p;
			};

			downsamplePipeline = MakePipeline(passthroughFS, downsampleLayout);
			gaussPipeline      = MakePipeline(gaussFS,       gaussLayout);
			upsamplePipeline   = MakePipeline(upsampleFS,    upsampleLayout);
			compositePipeline  = MakePipeline(compositeFS,   compositeLayout);

			vkDestroyShaderModule(dev, vs,           nullptr);
			vkDestroyShaderModule(dev, passthroughFS,nullptr);
			vkDestroyShaderModule(dev, gaussFS,      nullptr);
			vkDestroyShaderModule(dev, upsampleFS,   nullptr);
			vkDestroyShaderModule(dev, compositeFS,  nullptr);
		}

		void VulkanBloomFilter::InitDescriptorPools() {
			VkDevice dev = device->GetDevice();

			// 6 downsample + 5 upsample + 1 composite + padding = ~32 sets per frame.
			VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64};
			VkDescriptorPoolCreateInfo info{};
			info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			info.poolSizeCount = 1;
			info.pPoolSizes    = &size;
			info.maxSets       = 32;

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				if (vkCreateDescriptorPool(dev, &info, nullptr, &perFrameDescPool[i]) != VK_SUCCESS)
					SPRaise("Failed to create bloom descriptor pool");
			}
		}

		// ─────────────────────────────────────────────────────────────
		//  Per-frame helpers
		// ─────────────────────────────────────────────────────────────

		VkFramebuffer VulkanBloomFilter::MakeFramebuffer(VkRenderPass rp,
		                                                  VulkanImage* image,
		                                                  int frameSlot) {
			VkImageView view = image->GetImageView();
			VkFramebufferCreateInfo fbInfo{};
			fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.renderPass      = rp;
			fbInfo.attachmentCount = 1;
			fbInfo.pAttachments    = &view;
			fbInfo.width           = image->GetWidth();
			fbInfo.height          = image->GetHeight();
			fbInfo.layers          = 1;

			VkFramebuffer fb;
			if (vkCreateFramebuffer(device->GetDevice(), &fbInfo, nullptr, &fb) != VK_SUCCESS)
				SPRaise("Failed to create bloom framebuffer");
			perFrameFramebuffers[frameSlot].push_back(fb);
			return fb;
		}

		VkDescriptorSet VulkanBloomFilter::BindTexture(int frameSlot,
		                                                VkDescriptorSetLayout dsl,
		                                                VkImageView view) {
			VkDescriptorSetAllocateInfo ai{};
			ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			ai.descriptorPool     = perFrameDescPool[frameSlot];
			ai.descriptorSetCount = 1;
			ai.pSetLayouts        = &dsl;
			VkDescriptorSet set;
			if (vkAllocateDescriptorSets(device->GetDevice(), &ai, &set) != VK_SUCCESS)
				SPRaise("Failed to allocate bloom descriptor set");

			VkDescriptorImageInfo img{linearSampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
			VkWriteDescriptorSet  w{};
			w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			w.dstSet          = set;
			w.dstBinding      = 0;
			w.descriptorCount = 1;
			w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			w.pImageInfo      = &img;
			vkUpdateDescriptorSets(device->GetDevice(), 1, &w, 0, nullptr);
			return set;
		}

		VkDescriptorSet VulkanBloomFilter::BindTextures(int frameSlot,
		                                                 VkDescriptorSetLayout dsl,
		                                                 VkImageView view0,
		                                                 VkImageView view1) {
			VkDescriptorSetAllocateInfo ai{};
			ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			ai.descriptorPool     = perFrameDescPool[frameSlot];
			ai.descriptorSetCount = 1;
			ai.pSetLayouts        = &dsl;
			VkDescriptorSet set;
			if (vkAllocateDescriptorSets(device->GetDevice(), &ai, &set) != VK_SUCCESS)
				SPRaise("Failed to allocate bloom descriptor set");

			VkDescriptorImageInfo imgs[2]{
			    {linearSampler, view0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			    {linearSampler, view1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
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

		VkDescriptorSet VulkanBloomFilter::BindTextures4(int frameSlot,
		                                                  const VkImageView* views,
		                                                  const VkSampler* samplers) {
			VkDescriptorSetAllocateInfo ai{};
			ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			ai.descriptorPool     = perFrameDescPool[frameSlot];
			ai.descriptorSetCount = 1;
			ai.pSetLayouts        = &quadSamplerDSL;
			VkDescriptorSet set;
			if (vkAllocateDescriptorSets(device->GetDevice(), &ai, &set) != VK_SUCCESS)
				SPRaise("Failed to allocate bloom descriptor set");

			VkDescriptorImageInfo imgs[4];
			VkWriteDescriptorSet  writes[4]{};
			for (int i = 0; i < 4; ++i) {
				imgs[i] = {samplers[i] != VK_NULL_HANDLE ? samplers[i] : linearSampler,
				           views[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
				writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[i].dstSet          = set;
				writes[i].dstBinding      = static_cast<uint32_t>(i);
				writes[i].descriptorCount = 1;
				writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[i].pImageInfo      = &imgs[i];
			}
			vkUpdateDescriptorSets(device->GetDevice(), 4, writes, 0, nullptr);
			return set;
		}

		void VulkanBloomFilter::UpdateNoise(VkCommandBuffer cmd) {
			// Fresh 128x128 random grain every frame, like GL's UpdateNoise.
			for (size_t i = 0; i < noiseData.size(); i++)
				noiseData[i] = static_cast<uint32_t>(SampleRandom());
			noiseStaging->UpdateData(noiseData.data(), noiseData.size() * sizeof(uint32_t));

			noiseImage->TransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			    VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
			noiseImage->CopyFromBuffer(cmd, noiseStaging->GetBuffer());
			noiseImage->TransitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			    VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
		}

		void VulkanBloomFilter::DrawFullscreen(VkCommandBuffer cmd,
		                                        VkRenderPass rp,
		                                        VkFramebuffer fb,
		                                        uint32_t width, uint32_t height,
		                                        VkPipeline pipeline,
		                                        VkPipelineLayout layout,
		                                        VkDescriptorSet ds) {
			VkClearValue cv{};
			cv.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

			VkRenderPassBeginInfo rpBegin{};
			rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			rpBegin.renderPass        = rp;
			rpBegin.framebuffer       = fb;
			rpBegin.renderArea.extent = {width, height};
			rpBegin.clearValueCount   = 1;
			rpBegin.pClearValues      = &cv;

			vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport{0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
			VkRect2D   scissor{{0, 0}, {width, height}};
			vkCmdSetViewport(cmd, 0, 1, &viewport);
			vkCmdSetScissor(cmd, 0, 1, &scissor);

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			                        layout, 0, 1, &ds, 0, nullptr);
			vkCmdDraw(cmd, 3, 1, 0, 0);

			vkCmdEndRenderPass(cmd);
		}

		// ─────────────────────────────────────────────────────────────
		//  Filter()
		// ─────────────────────────────────────────────────────────────

		void VulkanBloomFilter::Filter(VkCommandBuffer cmd,
		                                VulkanImage* input,
		                                VulkanImage* output) {
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

			UpdateNoise(cmd);

			VulkanTemporaryImagePool* pool = renderer.GetTemporaryImagePool();

			// ── 1. Downsample 6 levels ────────────────────────────────
			// levels[i] holds the temp image at half^(i+1) of the input resolution.

			static constexpr int NUM_LEVELS = 6;
			std::vector<Handle<VulkanImage>> levels;
			levels.reserve(NUM_LEVELS);

			uint32_t prevW = static_cast<uint32_t>(input->GetWidth());
			uint32_t prevH = static_cast<uint32_t>(input->GetHeight());
			VkImageView srcView = input->GetImageView();

			// Superseded gauss ping images; returned after recording (see below).
			std::vector<Handle<VulkanImage>> gaussTrash;
			gaussTrash.reserve(NUM_LEVELS);

			for (int i = 0; i < NUM_LEVELS; ++i) {
				uint32_t nw = (prevW + 1) / 2;
				uint32_t nh = (prevH + 1) / 2;

				Handle<VulkanImage> dst = pool->Acquire(nw, nh, colorFormat);
				VkFramebuffer fb        = MakeFramebuffer(ppRenderPass, dst.GetPointerOrNull(), frameSlot);
				VkDescriptorSet ds      = BindTexture(frameSlot, singleSamplerDSL, srcView);

				DrawFullscreen(cmd, ppRenderPass, fb, nw, nh,
				               downsamplePipeline, downsampleLayout, ds);

				// GL parity: GLLensDustFilter runs a Gauss1D H+V blur on
				// every downsample level; without it the bloom is noticeably
				// harder-edged than GL.
				for (int pass = 0; pass < 2; ++pass) {
					float unitShift[2] = {pass == 0 ? 1.0f / (float)nw : 0.0f,
					                      pass == 0 ? 0.0f : 1.0f / (float)nh};
					vkCmdPushConstants(cmd, gaussLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
					                   0, sizeof(unitShift), unitShift);

					Handle<VulkanImage> blurred = pool->Acquire(nw, nh, colorFormat);
					VkFramebuffer bfb   = MakeFramebuffer(ppRenderPass,
					                                      blurred.GetPointerOrNull(), frameSlot);
					VkDescriptorSet bds = BindTexture(frameSlot, singleSamplerDSL,
					                                  dst->GetImageView());
					DrawFullscreen(cmd, ppRenderPass, bfb, nw, nh,
					               gaussPipeline, gaussLayout, bds);

					gaussTrash.push_back(std::move(dst));
					dst = std::move(blurred);
				}

				srcView = dst->GetImageView();
				levels.push_back(std::move(dst));
				prevW = nw;  prevH = nh;
			}

			// ── 2. Upsample composite (smallest → largest) ────────────
			// For each step, blend the smaller level into the larger using:
			//   outColor = mix(large, small, alpha), alpha = sqrt(cnt/(cnt+1))
			//
			// Superseded images are collected and returned only after all command
			// recording is done, preventing the pool from reusing an image still
			// referenced in the command buffer.

			std::vector<Handle<VulkanImage>> toReturn;
			toReturn.reserve(NUM_LEVELS - 1);

			for (int i = NUM_LEVELS - 1; i >= 1; --i) {
				int   cnt   = NUM_LEVELS - i;
				float alpha = std::sqrt((float)cnt / (float)(cnt + 1));

				VulkanImage* large = levels[i - 1].GetPointerOrNull();
				VulkanImage* small = levels[i].GetPointerOrNull();

				uint32_t nw = static_cast<uint32_t>(large->GetWidth());
				uint32_t nh = static_cast<uint32_t>(large->GetHeight());

				// Push constant before the draw inside DrawFullscreen.
				vkCmdPushConstants(cmd, upsampleLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
				                   0, sizeof(float), &alpha);

				Handle<VulkanImage> blended = pool->Acquire(nw, nh, colorFormat);
				VkFramebuffer fb   = MakeFramebuffer(ppRenderPass, blended.GetPointerOrNull(), frameSlot);
				VkDescriptorSet ds = BindTextures(frameSlot, dualSamplerDSL,
				                                  large->GetImageView(),
				                                  small->GetImageView());

				DrawFullscreen(cmd, ppRenderPass, fb, nw, nh,
				               upsamplePipeline, upsampleLayout, ds);

				toReturn.push_back(std::move(levels[i - 1]));
				levels[i - 1] = std::move(blended);
			}

			// ── 3. Final composite: scene * 0.8 + bloom * 0.2 ────────

			uint32_t rw = static_cast<uint32_t>(output->GetWidth());
			uint32_t rh = static_cast<uint32_t>(output->GetHeight());

			VulkanImage* dust = nullptr;
			if (auto* wrapper = dynamic_cast<VulkanImageWrapper*>(dustImage.GetPointerOrNull()))
				dust = wrapper->GetVulkanImage();

			// GL falls back to plain input if anything is missing; here the
			// dust texture ships with the game, so treat absence as fatal-ish
			// and just skip the dust term by binding the bloom texture twice.
			VkImageView views[4] = {
			    input->GetImageView(), levels[0]->GetImageView(),
			    dust ? dust->GetImageView() : levels[0]->GetImageView(),
			    noiseImage->GetImageView()};
			VkSampler samplers[4] = {linearSampler, linearSampler,
			                         dust ? dust->GetSampler() : linearSampler,
			                         noiseImage->GetSampler()};

			VkFramebuffer fb   = MakeFramebuffer(ppRenderPass, output, frameSlot);
			VkDescriptorSet ds = BindTextures4(frameSlot, views, samplers);

			// noiseTexCoordFactor, exactly as GLLensDustFilter computes it.
			float facX = renderer.GetRenderWidth() / 128.0f;
			float facY = renderer.GetRenderHeight() / 128.0f;
			float noiseFactor[4] = {facX, facY, facX / 128.0f, facY / 128.0f};
			vkCmdPushConstants(cmd, compositeLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
			                   0, sizeof(noiseFactor), noiseFactor);

			DrawFullscreen(cmd, ppRenderPass, fb, rw, rh,
			               compositePipeline, compositeLayout, ds);

			// Return all temporary images (deferred to avoid pool reuse hazards).
			for (auto& img : gaussTrash)
				pool->Return(img.GetPointerOrNull());
			for (auto& img : toReturn)
				pool->Return(img.GetPointerOrNull());
			for (auto& img : levels)
				pool->Return(img.GetPointerOrNull());
		}

	} // namespace draw
} // namespace spades
