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

#include "VulkanDepthOfFieldFilter.h"
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
#include <cmath>
#include <cstring>

SPADES_SETTING(r_depthOfField);
SPADES_SETTING(r_depthOfFieldMaxCoc);

namespace spades {
	namespace draw {

		// CoC generation push constants (48 bytes, FS only).
		struct DoFCoCGenParams {
			float pixelShiftX, pixelShiftY;     // offset 0
			float zNear, zFar;                  // offset 8
			float depthScale;                   // offset 16
			float maxVignetteBlur;              // offset 20
			float vignetteScaleX, vignetteScaleY; // offset 24
			float globalBlur;                   // offset 32
			float nearBlur;                     // offset 36
			float farBlur;                      // offset 40 (already negated)
			float _pad;                         // offset 44
		};
		static_assert(sizeof(DoFCoCGenParams) == 48, "DoFCoCGenParams size mismatch");

		// ─────────────────────────────────────────────────────────────────────
		//  Construction / destruction
		// ─────────────────────────────────────────────────────────────────────

		VulkanDepthOfFieldFilter::VulkanDepthOfFieldFilter(VulkanRenderer& r)
		    : VulkanPostProcessFilter(r),
		      colorFormat(VK_FORMAT_UNDEFINED),
		      linearSampler(VK_NULL_HANDLE),
		      colorRenderPass(VK_NULL_HANDLE),
		      cocRenderPass(VK_NULL_HANDLE),
		      singleSamplerDSL(VK_NULL_HANDLE),
		      dualSamplerDSL(VK_NULL_HANDLE),
		      quadSamplerDSL(VK_NULL_HANDLE),
		      cocGenLayout(VK_NULL_HANDLE),
		      gauss1DLayout(VK_NULL_HANDLE),
		      dualLayout(VK_NULL_HANDLE),
		      blurLayout(VK_NULL_HANDLE),
		      doFMixLayout(VK_NULL_HANDLE),
		      cocGenPipeline(VK_NULL_HANDLE),
		      gauss1DPipeline(VK_NULL_HANDLE),
		      cocMixPipeline(VK_NULL_HANDLE),
		      blurPipeline(VK_NULL_HANDLE),
		      blur2Pipeline(VK_NULL_HANDLE),
		      addMixPipeline(VK_NULL_HANDLE),
		      doFMixPipeline(VK_NULL_HANDLE),
		      passthroughLayout(VK_NULL_HANDLE),
		      passthroughPipeline(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i)
				perFrameDescPool[i] = VK_NULL_HANDLE;

			colorFormat = r.GetFramebufferManager()->GetMainColorFormat();

			InitRenderPasses();
			InitDescriptorSetLayouts();
			InitPipelines();
			InitDescriptorPools();
		}

		VulkanDepthOfFieldFilter::~VulkanDepthOfFieldFilter() {
			SPADES_MARK_FUNCTION();
			VkDevice dev = device->GetDevice();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				for (VkFramebuffer fb : perFrameFramebuffers[i])
					vkDestroyFramebuffer(dev, fb, nullptr);
				if (perFrameDescPool[i] != VK_NULL_HANDLE)
					vkDestroyDescriptorPool(dev, perFrameDescPool[i], nullptr);
			}

			auto DestroyPipeline = [&](VkPipeline& p) {
				if (p != VK_NULL_HANDLE) { vkDestroyPipeline(dev, p, nullptr); p = VK_NULL_HANDLE; }
			};
			auto DestroyLayout = [&](VkPipelineLayout& l) {
				if (l != VK_NULL_HANDLE) { vkDestroyPipelineLayout(dev, l, nullptr); l = VK_NULL_HANDLE; }
			};
			auto DestroyDSL = [&](VkDescriptorSetLayout& d) {
				if (d != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(dev, d, nullptr); d = VK_NULL_HANDLE; }
			};

			DestroyPipeline(doFMixPipeline);
			DestroyPipeline(addMixPipeline);
			DestroyPipeline(blur2Pipeline);
			DestroyPipeline(blurPipeline);
			DestroyPipeline(cocMixPipeline);
			DestroyPipeline(gauss1DPipeline);
			DestroyPipeline(cocGenPipeline);
			DestroyPipeline(passthroughPipeline);

			DestroyLayout(doFMixLayout);
			DestroyLayout(blurLayout);
			DestroyLayout(dualLayout);
			DestroyLayout(gauss1DLayout);
			DestroyLayout(cocGenLayout);
			DestroyLayout(passthroughLayout);

			DestroyDSL(quadSamplerDSL);
			DestroyDSL(dualSamplerDSL);
			DestroyDSL(singleSamplerDSL);

			if (linearSampler   != VK_NULL_HANDLE) vkDestroySampler(dev, linearSampler, nullptr);
			if (colorRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(dev, colorRenderPass, nullptr);
			if (cocRenderPass   != VK_NULL_HANDLE) vkDestroyRenderPass(dev, cocRenderPass, nullptr);
		}

		// ─────────────────────────────────────────────────────────────────────
		//  Initialisation
		// ─────────────────────────────────────────────────────────────────────

		void VulkanDepthOfFieldFilter::InitRenderPasses() {
			VkDevice dev = device->GetDevice();

			VkSubpassDependency dep{};
			dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
			dep.dstSubpass    = 0;
			dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dep.dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			colorRenderPass = CreateSimpleColorRenderPass(
			    dev, colorFormat,
			    VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			    VK_IMAGE_LAYOUT_UNDEFINED,
			    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			    &dep);

			cocRenderPass = CreateSimpleColorRenderPass(
			    dev, VK_FORMAT_R8_UNORM,
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
				SPRaise("Failed to create DoF sampler");
		}

		void VulkanDepthOfFieldFilter::InitDescriptorSetLayouts() {
			VkDevice dev = device->GetDevice();

			auto MakeDSL = [&](uint32_t n) {
				VkDescriptorSetLayoutBinding bindings[4]{};
				for (uint32_t i = 0; i < n; ++i) {
					bindings[i].binding         = i;
					bindings[i].descriptorCount = 1;
					bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
				}
				VkDescriptorSetLayoutCreateInfo info{};
				info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
				info.bindingCount = n;
				info.pBindings    = bindings;
				VkDescriptorSetLayout dsl;
				if (vkCreateDescriptorSetLayout(dev, &info, nullptr, &dsl) != VK_SUCCESS)
					SPRaise("Failed to create DoF descriptor set layout");
				return dsl;
			};

			singleSamplerDSL = MakeDSL(1);
			dualSamplerDSL   = MakeDSL(2);
			quadSamplerDSL   = MakeDSL(4);
		}

		VkShaderModule VulkanDepthOfFieldFilter::LoadSPIRV(const char* path) {
			std::string data = FileManager::ReadAllBytes(path);
			std::vector<uint32_t> code(data.size() / sizeof(uint32_t));
			std::memcpy(code.data(), data.data(), data.size());

			VkShaderModuleCreateInfo info{};
			info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			info.codeSize = data.size();
			info.pCode    = code.data();

			VkShaderModule mod;
			if (vkCreateShaderModule(device->GetDevice(), &info, nullptr, &mod) != VK_SUCCESS)
				SPRaise("Failed to create DoF shader module: %s", path);
			return mod;
		}

		void VulkanDepthOfFieldFilter::InitPipelines() {
			VkDevice dev        = device->GetDevice();
			VkPipelineCache cache = renderer.GetPipelineCache();

			VkShaderModule vs           = LoadSPIRV("Shaders/Vulkan/PostFilters/PassThrough.vk.vs.spv");
			VkShaderModule passthroughFS = LoadSPIRV("Shaders/Vulkan/PostFilters/PassThrough.vk.fs.spv");
			VkShaderModule cocGenFS     = LoadSPIRV("Shaders/Vulkan/PostFilters/DoFCoCGen.vk.fs.spv");
			VkShaderModule gauss1DFS    = LoadSPIRV("Shaders/Vulkan/PostFilters/Gauss1D.vk.fs.spv");
			VkShaderModule cocMixFS     = LoadSPIRV("Shaders/Vulkan/PostFilters/DoFCoCMix.vk.fs.spv");
			VkShaderModule blurFS       = LoadSPIRV("Shaders/Vulkan/PostFilters/DoFBlur.vk.fs.spv");
			VkShaderModule blur2FS      = LoadSPIRV("Shaders/Vulkan/PostFilters/DoFBlur2.vk.fs.spv");
			VkShaderModule addMixFS     = LoadSPIRV("Shaders/Vulkan/PostFilters/DoFAddMix.vk.fs.spv");
			VkShaderModule doFMixFS     = LoadSPIRV("Shaders/Vulkan/PostFilters/DoFMix.vk.fs.spv");

			// ── Fixed pipeline state ────────────────────────────────────────
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

			VkPipelineColorBlendAttachmentState noBlend{};
			noBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

			VkPipelineColorBlendStateCreateInfo blend{};
			blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			blend.attachmentCount = 1;
			blend.pAttachments    = &noBlend;

			// ── Pipeline layouts ────────────────────────────────────────────
			{
				VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DoFCoCGenParams)};
				VkPipelineLayoutCreateInfo li{};
				li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				li.setLayoutCount         = 1;
				li.pSetLayouts            = &singleSamplerDSL;
				li.pushConstantRangeCount = 1;
				li.pPushConstantRanges    = &pcr;
				if (vkCreatePipelineLayout(dev, &li, nullptr, &cocGenLayout) != VK_SUCCESS)
					SPRaise("Failed to create DoF cocGen layout");
			}
			{
				VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 2 * sizeof(float)}; // vec2 unitShift
				VkPipelineLayoutCreateInfo li{};
				li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				li.setLayoutCount         = 1;
				li.pSetLayouts            = &singleSamplerDSL;
				li.pushConstantRangeCount = 1;
				li.pPushConstantRanges    = &pcr;
				if (vkCreatePipelineLayout(dev, &li, nullptr, &gauss1DLayout) != VK_SUCCESS)
					SPRaise("Failed to create DoF gauss1D layout");
			}
			{
				VkPipelineLayoutCreateInfo li{};
				li.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				li.setLayoutCount = 1;
				li.pSetLayouts    = &dualSamplerDSL;
				if (vkCreatePipelineLayout(dev, &li, nullptr, &dualLayout) != VK_SUCCESS)
					SPRaise("Failed to create DoF dual layout");
			}
			{
				VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 2 * sizeof(float)}; // vec2 offset
				VkPipelineLayoutCreateInfo li{};
				li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				li.setLayoutCount         = 1;
				li.pSetLayouts            = &dualSamplerDSL;
				li.pushConstantRangeCount = 1;
				li.pPushConstantRanges    = &pcr;
				if (vkCreatePipelineLayout(dev, &li, nullptr, &blurLayout) != VK_SUCCESS)
					SPRaise("Failed to create DoF blur layout");
			}
			{
				VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int)}; // int blurredOnly
				VkPipelineLayoutCreateInfo li{};
				li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				li.setLayoutCount         = 1;
				li.pSetLayouts            = &quadSamplerDSL;
				li.pushConstantRangeCount = 1;
				li.pPushConstantRanges    = &pcr;
				if (vkCreatePipelineLayout(dev, &li, nullptr, &doFMixLayout) != VK_SUCCESS)
					SPRaise("Failed to create DoF mix layout");
			}
			{
				VkPipelineLayoutCreateInfo li{};
				li.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				li.setLayoutCount = 1;
				li.pSetLayouts    = &singleSamplerDSL;
				if (vkCreatePipelineLayout(dev, &li, nullptr, &passthroughLayout) != VK_SUCCESS)
					SPRaise("Failed to create DoF passthrough layout");
			}

			// ── Helper: create one graphics pipeline ────────────────────────
			auto MakePipeline = [&](VkShaderModule fsModule, VkPipelineLayout layout,
			                         VkRenderPass rp) -> VkPipeline {
				VkPipelineShaderStageCreateInfo stages[2]{};
				stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
				              VK_SHADER_STAGE_VERTEX_BIT,   vs,       "main", nullptr};
				stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
				              VK_SHADER_STAGE_FRAGMENT_BIT, fsModule, "main", nullptr};

				VkPipelineColorBlendStateCreateInfo localBlend = blend;

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
				pi.pColorBlendState    = &localBlend;
				pi.pDynamicState       = &dyn;
				pi.layout              = layout;
				pi.renderPass          = rp;
				pi.subpass             = 0;

				VkPipeline p;
				if (vkCreateGraphicsPipelines(dev, cache, 1, &pi, nullptr, &p) != VK_SUCCESS)
					SPRaise("Failed to create DoF pipeline");
				return p;
			};

			cocGenPipeline      = MakePipeline(cocGenFS,      cocGenLayout,      cocRenderPass);
			gauss1DPipeline     = MakePipeline(gauss1DFS,     gauss1DLayout,     cocRenderPass);
			cocMixPipeline      = MakePipeline(cocMixFS,      dualLayout,        cocRenderPass);
			blurPipeline        = MakePipeline(blurFS,        blurLayout,        colorRenderPass);
			blur2Pipeline       = MakePipeline(blur2FS,       blurLayout,        colorRenderPass);
			addMixPipeline      = MakePipeline(addMixFS,      dualLayout,        colorRenderPass);
			doFMixPipeline      = MakePipeline(doFMixFS,      doFMixLayout,      colorRenderPass);
			passthroughPipeline = MakePipeline(passthroughFS, passthroughLayout, colorRenderPass);

			vkDestroyShaderModule(dev, vs,            nullptr);
			vkDestroyShaderModule(dev, passthroughFS, nullptr);
			vkDestroyShaderModule(dev, cocGenFS,      nullptr);
			vkDestroyShaderModule(dev, gauss1DFS,     nullptr);
			vkDestroyShaderModule(dev, cocMixFS,      nullptr);
			vkDestroyShaderModule(dev, blurFS,        nullptr);
			vkDestroyShaderModule(dev, blur2FS,       nullptr);
			vkDestroyShaderModule(dev, addMixFS,      nullptr);
			vkDestroyShaderModule(dev, doFMixFS,      nullptr);
		}

		void VulkanDepthOfFieldFilter::InitDescriptorPools() {
			VkDevice dev = device->GetDevice();

			// Generous per-frame budget: up to 64 descriptor sets,
			// up to 256 combined-image-sampler bindings.
			VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256};
			VkDescriptorPoolCreateInfo info{};
			info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			info.poolSizeCount = 1;
			info.pPoolSizes    = &size;
			info.maxSets       = 64;

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				if (vkCreateDescriptorPool(dev, &info, nullptr, &perFrameDescPool[i]) != VK_SUCCESS)
					SPRaise("Failed to create DoF descriptor pool");
			}
		}

		// ─────────────────────────────────────────────────────────────────────
		//  Per-frame helpers
		// ─────────────────────────────────────────────────────────────────────

		VkFramebuffer VulkanDepthOfFieldFilter::MakeFramebuffer(VkRenderPass rp, VulkanImage* img,
		                                                         int frameSlot) {
			VkImageView view = img->GetImageView();
			VkFramebufferCreateInfo fbInfo{};
			fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.renderPass      = rp;
			fbInfo.attachmentCount = 1;
			fbInfo.pAttachments    = &view;
			fbInfo.width           = img->GetWidth();
			fbInfo.height          = img->GetHeight();
			fbInfo.layers          = 1;

			VkFramebuffer fb;
			if (vkCreateFramebuffer(device->GetDevice(), &fbInfo, nullptr, &fb) != VK_SUCCESS)
				SPRaise("Failed to create DoF framebuffer");
			perFrameFramebuffers[frameSlot].push_back(fb);
			return fb;
		}

		VkDescriptorSet VulkanDepthOfFieldFilter::BindImages(int frameSlot,
		                                                      VkDescriptorSetLayout dsl,
		                                                      const VkImageView* views,
		                                                      const VkSampler* samplers,
		                                                      uint32_t count) {
			VkDescriptorSetAllocateInfo ai{};
			ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			ai.descriptorPool     = perFrameDescPool[frameSlot];
			ai.descriptorSetCount = 1;
			ai.pSetLayouts        = &dsl;
			VkDescriptorSet set;
			if (vkAllocateDescriptorSets(device->GetDevice(), &ai, &set) != VK_SUCCESS)
				SPRaise("Failed to allocate DoF descriptor set");

			std::vector<VkDescriptorImageInfo> imgInfos(count);
			std::vector<VkWriteDescriptorSet>  writes(count);
			for (uint32_t i = 0; i < count; ++i) {
				imgInfos[i] = {samplers[i], views[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
				writes[i]   = {};
				writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[i].dstSet          = set;
				writes[i].dstBinding      = i;
				writes[i].descriptorCount = 1;
				writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[i].pImageInfo      = &imgInfos[i];
			}
			vkUpdateDescriptorSets(device->GetDevice(), count, writes.data(), 0, nullptr);
			return set;
		}

		void VulkanDepthOfFieldFilter::DrawFullscreen(VkCommandBuffer cmd, VkRenderPass rp,
		                                               VkFramebuffer fb,
		                                               uint32_t w, uint32_t h,
		                                               VkPipeline pipeline, VkPipelineLayout layout,
		                                               VkDescriptorSet ds) {
			VkClearValue cv{};
			cv.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

			VkRenderPassBeginInfo rpBegin{};
			rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			rpBegin.renderPass        = rp;
			rpBegin.framebuffer       = fb;
			rpBegin.renderArea.extent = {w, h};
			rpBegin.clearValueCount   = 1;
			rpBegin.pClearValues      = &cv;

			vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport{0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f};
			VkRect2D   scissor{{0, 0}, {w, h}};
			vkCmdSetViewport(cmd, 0, 1, &viewport);
			vkCmdSetScissor(cmd, 0, 1, &scissor);

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			                        layout, 0, 1, &ds, 0, nullptr);
			vkCmdDraw(cmd, 3, 1, 0, 0);

			vkCmdEndRenderPass(cmd);
		}

		// ─────────────────────────────────────────────────────────────────────
		//  Algorithm helpers
		// ─────────────────────────────────────────────────────────────────────

		bool VulkanDepthOfFieldFilter::HighQuality() const {
			return (int)r_depthOfField >= 2;
		}

		Handle<VulkanImage> VulkanDepthOfFieldFilter::BlurCoC(
		    VkCommandBuffer cmd, VulkanImage* coc, float spread,
		    int frameSlot, std::vector<Handle<VulkanImage>>& deferred) {
			auto* pool = renderer.GetTemporaryImagePool();
			uint32_t w = coc->GetWidth();
			uint32_t h = coc->GetHeight();

			// X-direction pass.
			Handle<VulkanImage> tempX = pool->Acquire(w, h, VK_FORMAT_R8_UNORM);
			{
				VkImageView  v = coc->GetImageView();
				VkSampler    s = linearSampler;
				VkDescriptorSet ds = BindImages(frameSlot, singleSamplerDSL, &v, &s, 1);
				VkFramebuffer   fb = MakeFramebuffer(cocRenderPass, tempX.GetPointerOrNull(), frameSlot);
				float unitShift[2] = {spread / (float)w, 0.0f};
				vkCmdPushConstants(cmd, gauss1DLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
				                   0, sizeof(unitShift), unitShift);
				DrawFullscreen(cmd, cocRenderPass, fb, w, h, gauss1DPipeline, gauss1DLayout, ds);
			}

			// Y-direction pass.
			Handle<VulkanImage> result = pool->Acquire(w, h, VK_FORMAT_R8_UNORM);
			{
				VkImageView  v = tempX->GetImageView();
				VkSampler    s = linearSampler;
				VkDescriptorSet ds = BindImages(frameSlot, singleSamplerDSL, &v, &s, 1);
				VkFramebuffer   fb = MakeFramebuffer(cocRenderPass, result.GetPointerOrNull(), frameSlot);
				float unitShift[2] = {0.0f, spread / (float)h};
				vkCmdPushConstants(cmd, gauss1DLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
				                   0, sizeof(unitShift), unitShift);
				DrawFullscreen(cmd, cocRenderPass, fb, w, h, gauss1DPipeline, gauss1DLayout, ds);
			}

			deferred.push_back(std::move(tempX));
			return result;
		}

		Handle<VulkanImage> VulkanDepthOfFieldFilter::DoBlur(
		    VkCommandBuffer cmd, VulkanImage* buffer, VulkanImage* coc,
		    float offsetPixX, float offsetPixY,
		    int frameSlot, bool highQuality,
		    std::vector<Handle<VulkanImage>>& deferred) {
			auto* pool = renderer.GetTemporaryImagePool();
			uint32_t w = buffer->GetWidth();
			uint32_t h = buffer->GetHeight();

			float len = sqrtf(offsetPixX * offsetPixX + offsetPixY * offsetPixY);

			VkPipeline usePipeline = highQuality ? blur2Pipeline : blurPipeline;

			Handle<VulkanImage> current;
			VulkanImage* src = buffer;

			while (len > 0.5f) {
				Handle<VulkanImage> dst = pool->Acquire(w, h, colorFormat);

				VkImageView  views[2]   = {src->GetImageView(), coc->GetImageView()};
				VkSampler    smpls[2]   = {linearSampler, linearSampler};
				VkDescriptorSet ds      = BindImages(frameSlot, dualSamplerDSL, views, smpls, 2);
				VkFramebuffer   fb      = MakeFramebuffer(colorRenderPass, dst.GetPointerOrNull(), frameSlot);

				float texOffset[2] = {offsetPixX / (float)w, offsetPixY / (float)h};
				vkCmdPushConstants(cmd, blurLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
				                   0, sizeof(texOffset), texOffset);
				DrawFullscreen(cmd, colorRenderPass, fb, w, h, usePipeline, blurLayout, ds);

				// Previous source is no longer needed: defer return (but not the original buffer).
				if (current) deferred.push_back(std::move(current));
				current = std::move(dst);
				src = current.GetPointerOrNull();

				offsetPixX *= 0.125f;
				offsetPixY *= 0.125f;
				len        *= 0.125f;
			}

			// If no iterations ran (len <= 0.5 on entry), return the original buffer as-is.
			if (!current) {
				// Passthrough: copy buffer to a new temp image so the caller gets ownership.
				Handle<VulkanImage> dst = pool->Acquire(w, h, colorFormat);
				VkImageView  v = buffer->GetImageView();
				VkSampler    s = linearSampler;
				VkDescriptorSet ds = BindImages(frameSlot, singleSamplerDSL, &v, &s, 1);
				VkFramebuffer   fb = MakeFramebuffer(colorRenderPass, dst.GetPointerOrNull(), frameSlot);
				DrawFullscreen(cmd, colorRenderPass, fb, w, h, passthroughPipeline, passthroughLayout, ds);
				return dst;
			}

			return current;
		}

		// ─────────────────────────────────────────────────────────────────────
		//  Filter()
		// ─────────────────────────────────────────────────────────────────────

		void VulkanDepthOfFieldFilter::Filter(VkCommandBuffer cmd,
		                                       VulkanImage* input, VulkanImage* output) {
			SPADES_MARK_FUNCTION();

			int frameSlot = static_cast<int>(renderer.GetCurrentFrameIndex());

			// Reclaim per-frame resources.
			{
				VkDevice dev = device->GetDevice();
				for (VkFramebuffer fb : perFrameFramebuffers[frameSlot])
					vkDestroyFramebuffer(dev, fb, nullptr);
				perFrameFramebuffers[frameSlot].clear();
				vkResetDescriptorPool(dev, perFrameDescPool[frameSlot], 0);
			}

			const client::SceneDefinition& def = renderer.GetSceneDef();
			float blurDepthRange = def.depthOfFieldFocalLength;
			float vignetteBlur   = def.blurVignette;
			float globalBlur     = std::min(def.globalBlur * 3.0f, 1.0f);
			float nearBlur       = def.depthOfFieldNearBlurStrength;
			float farBlur        = def.depthOfFieldFarBlurStrength;

			bool  highQ = HighQuality();
			int   w     = input->GetWidth();
			int   h     = input->GetHeight();
			int   w2    = highQ ? w : (w + 3) / 4;
			int   h2    = highQ ? h : (h + 3) / 4;

			auto* pool = renderer.GetTemporaryImagePool();
			std::vector<Handle<VulkanImage>> deferred;

			// ── 1. Generate CoC ───────────────────────────────────────────────

			Handle<VulkanImage> depthImg = renderer.GetFramebufferManager()->GetResolvedDepthImage();

			DoFCoCGenParams cp{};
			cp.pixelShiftX    = 1.0f / (float)w;
			cp.pixelShiftY    = 1.0f / (float)h;
			cp.zNear          = def.zNear;
			cp.zFar           = def.zFar;
			cp.depthScale     = (blurDepthRange > 0.0f) ? (1.0f / blurDepthRange) : 1.0f;
			cp.maxVignetteBlur = sinf(std::max(def.fovX, def.fovY) * 0.5f) * vignetteBlur;
			if (h > w) {
				cp.vignetteScaleX = 2.0f * (float)w / (float)h;
				cp.vignetteScaleY = 2.0f;
			} else {
				cp.vignetteScaleX = 2.0f;
				cp.vignetteScaleY = 2.0f * (float)h / (float)w;
			}
			cp.globalBlur = globalBlur;
			cp.nearBlur   = nearBlur;
			cp.farBlur    = -farBlur; // negated to match GL convention
			cp._pad       = 0.0f;

			Handle<VulkanImage> coc = pool->Acquire(w2, h2, VK_FORMAT_R8_UNORM);
			{
				VkImageView  v = depthImg->GetImageView();
				VkSampler    s = depthImg->GetSampler();
				VkDescriptorSet ds = BindImages(frameSlot, singleSamplerDSL, &v, &s, 1);
				VkFramebuffer   fb = MakeFramebuffer(cocRenderPass, coc.GetPointerOrNull(), frameSlot);
				vkCmdPushConstants(cmd, cocGenLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
				                   0, sizeof(cp), &cp);
				DrawFullscreen(cmd, cocRenderPass, fb, w2, h2, cocGenPipeline, cocGenLayout, ds);
			}

			// ── 2. CoC refinement (low quality path only) ─────────────────────

			Handle<VulkanImage> finalCoc;

			if (highQ) {
				finalCoc = std::move(coc);
			} else {
				// BlurCoC with spread = 1.0.
				Handle<VulkanImage> cocBlur = BlurCoC(cmd, coc.GetPointerOrNull(), 1.0f,
				                                       frameSlot, deferred);

				// CoCMix: blend coc + cocBlur → coc2.
				Handle<VulkanImage> coc2 = pool->Acquire(w2, h2, VK_FORMAT_R8_UNORM);
				{
					VkImageView  views[2]  = {coc->GetImageView(), cocBlur->GetImageView()};
					VkSampler    smpls[2]  = {linearSampler, linearSampler};
					VkDescriptorSet ds     = BindImages(frameSlot, dualSamplerDSL, views, smpls, 2);
					VkFramebuffer   fb     = MakeFramebuffer(cocRenderPass, coc2.GetPointerOrNull(),
					                                         frameSlot);
					DrawFullscreen(cmd, cocRenderPass, fb, w2, h2, cocMixPipeline, dualLayout, ds);
				}
				deferred.push_back(std::move(cocBlur));
				deferred.push_back(std::move(coc));

				// BlurCoC with spread = 0.5 → finalCoc.
				finalCoc = BlurCoC(cmd, coc2.GetPointerOrNull(), 0.5f, frameSlot, deferred);
				deferred.push_back(std::move(coc2));
			}

			// ── 3. Reduce colour resolution (low quality path only) ────────────

			VulkanImage* lowbuf = input;
			Handle<VulkanImage> lowbufOwned;
			int divide = 1;

			if (!highQ) {
				int siz = std::max(w, h);
				while (siz >= 768) {
					divide <<= 1;
					siz    >>= 1;
					uint32_t nw = lowbuf->GetWidth()  / 2;
					uint32_t nh = lowbuf->GetHeight() / 2;
					Handle<VulkanImage> next = pool->Acquire(nw, nh, colorFormat);
					{
						VkImageView  v = lowbuf->GetImageView();
						VkSampler    s = linearSampler;
						VkDescriptorSet ds = BindImages(frameSlot, singleSamplerDSL, &v, &s, 1);
						VkFramebuffer   fb = MakeFramebuffer(colorRenderPass, next.GetPointerOrNull(),
						                                      frameSlot);
						DrawFullscreen(cmd, colorRenderPass, fb, nw, nh,
						               passthroughPipeline, passthroughLayout, ds);
					}
					if (lowbufOwned) deferred.push_back(std::move(lowbufOwned));
					lowbufOwned = std::move(next);
					lowbuf = lowbufOwned.GetPointerOrNull();
				}
			}

			// ── 4. Scatter blur in three directions ───────────────────────────

			float maxCocFrac = Clamp((float)r_depthOfFieldMaxCoc, 0.001f, 0.2f);
			float maxCocPx   = (float)std::max(w, h) * maxCocFrac;
			maxCocPx *= 0.7f + vignetteBlur * 0.5f;
			maxCocPx *= 1.0f + 3.0f * globalBlur;
			if (!highQ) maxCocPx /= (float)divide;

			const float cos60 = cosf(static_cast<float>(M_PI) / 3.0f);
			const float sin60 = sinf(static_cast<float>(M_PI) / 3.0f);

			VulkanImage* cocPtr = finalCoc.GetPointerOrNull();

			Handle<VulkanImage> buf1 = DoBlur(cmd, lowbuf, cocPtr,
			                                   0.0f, -maxCocPx,
			                                   frameSlot, highQ, deferred);
			Handle<VulkanImage> buf2 = DoBlur(cmd, lowbuf, cocPtr,
			                                   -sin60 * maxCocPx, cos60 * maxCocPx,
			                                   frameSlot, highQ, deferred);

			// AddMix(buf1, buf2) → mixBuf.
			uint32_t lowW = lowbuf->GetWidth();
			uint32_t lowH = lowbuf->GetHeight();
			Handle<VulkanImage> mixBuf = pool->Acquire(lowW, lowH, colorFormat);
			{
				VkImageView  views[2]  = {buf1->GetImageView(), buf2->GetImageView()};
				VkSampler    smpls[2]  = {linearSampler, linearSampler};
				VkDescriptorSet ds     = BindImages(frameSlot, dualSamplerDSL, views, smpls, 2);
				VkFramebuffer   fb     = MakeFramebuffer(colorRenderPass, mixBuf.GetPointerOrNull(),
				                                          frameSlot);
				DrawFullscreen(cmd, colorRenderPass, fb, lowW, lowH, addMixPipeline, dualLayout, ds);
			}
			deferred.push_back(std::move(buf2));

			Handle<VulkanImage> buf1b = DoBlur(cmd, buf1.GetPointerOrNull(), cocPtr,
			                                    -sin60 * maxCocPx, cos60 * maxCocPx,
			                                    frameSlot, highQ, deferred);
			deferred.push_back(std::move(buf1));

			Handle<VulkanImage> buf2b = DoBlur(cmd, mixBuf.GetPointerOrNull(), cocPtr,
			                                    sin60 * maxCocPx, cos60 * maxCocPx,
			                                    frameSlot, highQ, deferred);
			deferred.push_back(std::move(mixBuf));

			// ── 5. Final composite → output ────────────────────────────────────

			{
				// Bindings order must match DoFMix.vk.fs:
				//   0 = cocTexture, 1 = blurTexture2, 2 = blurTexture1, 3 = mainTexture
				VkImageView  views[4]  = {
				    finalCoc->GetImageView(),
				    buf2b->GetImageView(),
				    buf1b->GetImageView(),
				    input->GetImageView(),
				};
				VkSampler    smpls[4]  = {linearSampler, linearSampler, linearSampler, linearSampler};
				VkDescriptorSet ds     = BindImages(frameSlot, quadSamplerDSL, views, smpls, 4);
				VkFramebuffer   fb     = MakeFramebuffer(colorRenderPass, output, frameSlot);

				int blurredOnly = highQ ? 1 : 0;
				vkCmdPushConstants(cmd, doFMixLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
				                   0, sizeof(blurredOnly), &blurredOnly);
				DrawFullscreen(cmd, colorRenderPass, fb,
				               static_cast<uint32_t>(output->GetWidth()),
				               static_cast<uint32_t>(output->GetHeight()),
				               doFMixPipeline, doFMixLayout, ds);
			}

			// ── 6. Return all temporary images ────────────────────────────────

			if (lowbufOwned) deferred.push_back(std::move(lowbufOwned));
			deferred.push_back(std::move(finalCoc));
			deferred.push_back(std::move(buf1b));
			deferred.push_back(std::move(buf2b));

			for (auto& img : deferred)
				pool->Return(img.GetPointerOrNull());
		}

	} // namespace draw
} // namespace spades
