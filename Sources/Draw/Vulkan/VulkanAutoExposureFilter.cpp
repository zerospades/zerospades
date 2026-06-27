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

#include "VulkanAutoExposureFilter.h"
#include "VulkanFramebufferManager.h"
#include "VulkanImage.h"
#include "VulkanRenderer.h"
#include "VulkanRenderPassUtils.h"
#include "VulkanShader.h"
#include "VulkanTemporaryImagePool.h"
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/Math.h>
#include <Core/Settings.h>
#include <Gui/SDLVulkanDevice.h>
#include <cmath>
#include <cstring>

SPADES_SETTING(r_hdrAutoExposureMin);
SPADES_SETTING(r_hdrAutoExposureMax);
SPADES_SETTING(r_hdrAutoExposureSpeed);
SPADES_SETTING(r_hdrGamma);

namespace spades {
	namespace draw {

		// ─────────────────────────────────────────────────────────────
		//  Construction / destruction
		// ─────────────────────────────────────────────────────────────

		VulkanAutoExposureFilter::VulkanAutoExposureFilter(VulkanRenderer& r)
		    : VulkanPostProcessFilter(r),
		      colorFormat(VK_FORMAT_UNDEFINED),
		      exposureImage(VK_NULL_HANDLE),
		      exposureAlloc(VK_NULL_HANDLE),
		      exposureImageView(VK_NULL_HANDLE),
		      exposureFramebuffer(VK_NULL_HANDLE),
		      linearSampler(VK_NULL_HANDLE),
		      ppRenderPass(VK_NULL_HANDLE),
		      gainFirstRenderPass(VK_NULL_HANDLE),
		      gainRenderPass(VK_NULL_HANDLE),
		      singleSamplerDSL(VK_NULL_HANDLE),
		      dualSamplerDSL(VK_NULL_HANDLE),
		      preprocessLayout(VK_NULL_HANDLE),
		      computeGainLayout(VK_NULL_HANDLE),
		      applyLayout(VK_NULL_HANDLE),
		      preprocessPipeline(VK_NULL_HANDLE),
		      downsamplePipeline(VK_NULL_HANDLE),
		      computeGainPipeline(VK_NULL_HANDLE),
		      applyPipeline(VK_NULL_HANDLE),
		      exposureInitialized(false) {
			SPADES_MARK_FUNCTION();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i)
				perFrameDescPool[i] = VK_NULL_HANDLE;

			colorFormat = r.GetFramebufferManager()->GetMainColorFormat();

			InitRenderPasses();
			InitDescriptorSetLayouts();
			InitPipelines();
			InitDescriptorPools();
			InitExposureImage();
		}

		VulkanAutoExposureFilter::~VulkanAutoExposureFilter() {
			SPADES_MARK_FUNCTION();

			VkDevice dev = device->GetDevice();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				for (VkFramebuffer fb : perFrameFramebuffers[i])
					vkDestroyFramebuffer(dev, fb, nullptr);
				if (perFrameDescPool[i] != VK_NULL_HANDLE)
					vkDestroyDescriptorPool(dev, perFrameDescPool[i], nullptr);
			}

			if (exposureFramebuffer != VK_NULL_HANDLE)
				vkDestroyFramebuffer(dev, exposureFramebuffer, nullptr);
			if (exposureImageView != VK_NULL_HANDLE)
				vkDestroyImageView(dev, exposureImageView, nullptr);
			if (exposureImage != VK_NULL_HANDLE)
				vmaDestroyImage(device->GetAllocator(), exposureImage, exposureAlloc);

			if (linearSampler != VK_NULL_HANDLE)
				vkDestroySampler(dev, linearSampler, nullptr);

			if (preprocessPipeline  != VK_NULL_HANDLE) vkDestroyPipeline(dev, preprocessPipeline,  nullptr);
			if (downsamplePipeline  != VK_NULL_HANDLE) vkDestroyPipeline(dev, downsamplePipeline,  nullptr);
			if (computeGainPipeline != VK_NULL_HANDLE) vkDestroyPipeline(dev, computeGainPipeline, nullptr);
			if (applyPipeline       != VK_NULL_HANDLE) vkDestroyPipeline(dev, applyPipeline,       nullptr);

			if (preprocessLayout   != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, preprocessLayout,   nullptr);
			if (computeGainLayout  != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, computeGainLayout,  nullptr);
			if (applyLayout        != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, applyLayout,        nullptr);

			if (singleSamplerDSL != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, singleSamplerDSL, nullptr);
			if (dualSamplerDSL   != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, dualSamplerDSL,   nullptr);

			if (gainRenderPass      != VK_NULL_HANDLE) vkDestroyRenderPass(dev, gainRenderPass,      nullptr);
			if (gainFirstRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(dev, gainFirstRenderPass, nullptr);
			if (ppRenderPass        != VK_NULL_HANDLE) vkDestroyRenderPass(dev, ppRenderPass,        nullptr);
		}

		// ─────────────────────────────────────────────────────────────
		//  Initialisation
		// ─────────────────────────────────────────────────────────────

		void VulkanAutoExposureFilter::InitRenderPasses() {
			VkDevice dev = device->GetDevice();

			// Dependency: wait for colour-attachment writes before fragment sampling.
			VkSubpassDependency dep{};
			dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
			dep.dstSubpass    = 0;
			dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dep.dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			// ppRenderPass: preprocess, downsample, apply — DONT_CARE, UNDEFINED → SHADER_READ_ONLY.
			ppRenderPass = CreateSimpleColorRenderPass(
			    dev, colorFormat,
			    VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			    VK_IMAGE_LAYOUT_UNDEFINED,
			    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			    &dep);

			// gainFirstRenderPass: first frame only — CLEAR (value supplied at begin),
			// UNDEFINED → SHADER_READ_ONLY.
			//
			// computeGainPipeline is created against this pass but also used with
			// gainRenderPass, so the two must stay compatible: same attachment
			// description and the SAME dependencies. Giving only one of them a
			// widened dependency makes every draw with that pipeline invalid.
			VkSubpassDependency gainDep = dep;
			gainDep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			gainDep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			gainDep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
			                        VK_ACCESS_SHADER_READ_BIT;
			gainDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
			                        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

			gainFirstRenderPass = CreateSimpleColorRenderPass(
			    dev, colorFormat,
			    VK_ATTACHMENT_LOAD_OP_CLEAR,
			    VK_IMAGE_LAYOUT_UNDEFINED,
			    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			    &gainDep);

			// gainRenderPass: subsequent frames — LOAD, COLOR_ATTACHMENT → SHADER_READ_ONLY.
			// Two dependencies: one incoming (wait for prior writes), one outgoing
			// (signal fragment-shader readers before the apply pass).
			{
				VkSubpassDependency deps[2];
				// Incoming: this pass LOADs the accumulator, which is a
				// COLOR_ATTACHMENT_READ at COLOR_ATTACHMENT_OUTPUT -- not the
				// FRAGMENT_SHADER/SHADER_READ the shared dependency describes.
				// The preceding barrier moves the image SHADER_READ_ONLY ->
				// COLOR_ATTACHMENT_OPTIMAL, a colour-attachment write, so both
				// sides have to be named here or the loadOp races that
				// transition.
				deps[0] = gainDep;

				deps[1].srcSubpass      = 0;
				deps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
				deps[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				deps[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				deps[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				deps[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;
				deps[1].dependencyFlags = 0;

				VkAttachmentDescription att{};
				att.format         = colorFormat;
				att.samples        = VK_SAMPLE_COUNT_1_BIT;
				att.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
				att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
				att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				att.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				att.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

				VkSubpassDescription subpass{};
				subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
				subpass.colorAttachmentCount = 1;
				subpass.pColorAttachments    = &ref;

				VkRenderPassCreateInfo rpInfo{};
				rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
				rpInfo.attachmentCount = 1;
				rpInfo.pAttachments    = &att;
				rpInfo.subpassCount    = 1;
				rpInfo.pSubpasses      = &subpass;
				rpInfo.dependencyCount = 2;
				rpInfo.pDependencies   = deps;

				if (vkCreateRenderPass(dev, &rpInfo, nullptr, &gainRenderPass) != VK_SUCCESS)
					SPRaise("Failed to create gain render pass");
			}
		}

		void VulkanAutoExposureFilter::InitDescriptorSetLayouts() {
			VkDevice dev = device->GetDevice();

			auto MakeDSL = [&](uint32_t bindingCount) {
				VkDescriptorSetLayoutBinding bindings[2]{};
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
		}

		VkShaderModule VulkanAutoExposureFilter::LoadSPIRV(const char* path,
		                                                    VkShaderStageFlagBits /*stage*/) {
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

		void VulkanAutoExposureFilter::InitPipelines() {
			VkDevice dev     = device->GetDevice();
			VkPipelineCache  cache = renderer.GetPipelineCache();

			VkShaderModule vs          = LoadSPIRV("Shaders/Vulkan/PostFilters/PassThrough.vk.vs.spv",          VK_SHADER_STAGE_VERTEX_BIT);
			VkShaderModule preprocessFS = LoadSPIRV("Shaders/Vulkan/PostFilters/AutoExposurePreprocess.vk.fs.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
			VkShaderModule passthroughFS= LoadSPIRV("Shaders/Vulkan/PostFilters/PassThrough.vk.fs.spv",            VK_SHADER_STAGE_FRAGMENT_BIT);
			VkShaderModule gainFS       = LoadSPIRV("Shaders/Vulkan/PostFilters/AutoExposure.vk.fs.spv",           VK_SHADER_STAGE_FRAGMENT_BIT);
			VkShaderModule applyFS      = LoadSPIRV("Shaders/Vulkan/PostFilters/AutoExposureApply.vk.fs.spv",      VK_SHADER_STAGE_FRAGMENT_BIT);

			// No vertex buffer; vertices are generated in the vertex shader.
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

			// Pipeline layouts.
			{
				VkPipelineLayoutCreateInfo li{};
				li.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				li.setLayoutCount = 1;
				li.pSetLayouts    = &singleSamplerDSL;
				if (vkCreatePipelineLayout(dev, &li, nullptr, &preprocessLayout) != VK_SUCCESS)
					SPRaise("Failed to create preprocess pipeline layout");
			}
			{
				VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 3};
				VkPipelineLayoutCreateInfo li{};
				li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				li.setLayoutCount         = 1;
				li.pSetLayouts            = &singleSamplerDSL;
				li.pushConstantRangeCount = 1;
				li.pPushConstantRanges    = &pcr;
				if (vkCreatePipelineLayout(dev, &li, nullptr, &computeGainLayout) != VK_SUCCESS)
					SPRaise("Failed to create compute-gain pipeline layout");
			}
			{
				// Apply pass needs a single-float push constant: invGamma =
				// 1.0 / r_hdrGamma, used to encode the linear HDR scene for
				// display (mirrors GL's GLNonlinearizeFilter, folded in here so
				// it is one less full-screen pass).
				VkPushConstantRange pcr{};
				pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
				pcr.offset     = 0;
				pcr.size       = sizeof(float);

				VkPipelineLayoutCreateInfo li{};
				li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				li.setLayoutCount         = 1;
				li.pSetLayouts            = &dualSamplerDSL;
				li.pushConstantRangeCount = 1;
				li.pPushConstantRanges    = &pcr;
				if (vkCreatePipelineLayout(dev, &li, nullptr, &applyLayout) != VK_SUCCESS)
					SPRaise("Failed to create apply pipeline layout");
			}

			// Helper: build one colour blend attachment.
			auto MakeBlendAtt = [](bool blend,
			                       VkBlendFactor srcC = VK_BLEND_FACTOR_ONE,
			                       VkBlendFactor dstC = VK_BLEND_FACTOR_ZERO,
			                       VkBlendFactor srcA = VK_BLEND_FACTOR_ONE,
			                       VkBlendFactor dstA = VK_BLEND_FACTOR_ZERO) {
				VkPipelineColorBlendAttachmentState a{};
				a.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
				                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
				a.blendEnable         = blend ? VK_TRUE : VK_FALSE;
				a.srcColorBlendFactor = srcC;
				a.dstColorBlendFactor = dstC;
				a.colorBlendOp        = VK_BLEND_OP_ADD;
				a.srcAlphaBlendFactor = srcA;
				a.dstAlphaBlendFactor = dstA;
				a.alphaBlendOp        = VK_BLEND_OP_ADD;
				return a;
			};

			// Helper: build one graphics pipeline.
			auto MakePipeline = [&](VkShaderModule fsModule, VkPipelineLayout layout,
			                         VkRenderPass rp,
			                         VkPipelineColorBlendAttachmentState blendAtt) {
				VkPipelineShaderStageCreateInfo stages[2]{};
				stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
				              VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
				stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
				              VK_SHADER_STAGE_FRAGMENT_BIT, fsModule, "main", nullptr};

				VkPipelineColorBlendStateCreateInfo blend{};
				blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
				blend.attachmentCount = 1;
				blend.pAttachments    = &blendAtt;

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
				pi.renderPass          = rp;
				pi.subpass             = 0;

				VkPipeline p;
				if (vkCreateGraphicsPipelines(dev, cache, 1, &pi, nullptr, &p) != VK_SUCCESS)
					SPRaise("Failed to create graphics pipeline");
				return p;
			};

			auto noBlend = MakeBlendAtt(false);
			auto srcAlpha = MakeBlendAtt(true,
			    VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			    VK_BLEND_FACTOR_ONE,       VK_BLEND_FACTOR_ZERO);

			preprocessPipeline  = MakePipeline(preprocessFS,  preprocessLayout,  ppRenderPass,        noBlend);
			downsamplePipeline  = MakePipeline(passthroughFS, preprocessLayout,  ppRenderPass,        noBlend);
			// computeGainPipeline is compatible with both gainFirstRenderPass and gainRenderPass
			// (same format and sample count); build against gainFirstRenderPass.
			computeGainPipeline = MakePipeline(gainFS,        computeGainLayout, gainFirstRenderPass, srcAlpha);
			applyPipeline       = MakePipeline(applyFS,       applyLayout,       ppRenderPass,        noBlend);

			vkDestroyShaderModule(dev, vs,           nullptr);
			vkDestroyShaderModule(dev, preprocessFS, nullptr);
			vkDestroyShaderModule(dev, passthroughFS,nullptr);
			vkDestroyShaderModule(dev, gainFS,       nullptr);
			vkDestroyShaderModule(dev, applyFS,      nullptr);
		}

		void VulkanAutoExposureFilter::InitDescriptorPools() {
			VkDevice dev = device->GetDevice();

			// Each frame slot needs enough sets for the full downsample chain
			// (~14 levels for 1920×1080) plus the apply pass.
			VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64};
			VkDescriptorPoolCreateInfo info{};
			info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			info.poolSizeCount = 1;
			info.pPoolSizes    = &size;
			info.maxSets       = 32;

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				if (vkCreateDescriptorPool(dev, &info, nullptr, &perFrameDescPool[i]) != VK_SUCCESS)
					SPRaise("Failed to create descriptor pool");
			}
		}

		void VulkanAutoExposureFilter::InitExposureImage() {
			VkDevice dev     = device->GetDevice();
			VmaAllocator vma = device->GetAllocator();

			VkImageCreateInfo imgInfo{};
			imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imgInfo.imageType     = VK_IMAGE_TYPE_2D;
			imgInfo.format        = colorFormat;
			imgInfo.extent        = {1, 1, 1};
			imgInfo.mipLevels     = 1;
			imgInfo.arrayLayers   = 1;
			imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
			imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
			imgInfo.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			VmaAllocationCreateInfo allocInfo{};
			allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

			if (vmaCreateImage(vma, &imgInfo, &allocInfo,
			                   &exposureImage, &exposureAlloc, nullptr) != VK_SUCCESS)
				SPRaise("Failed to allocate exposure accumulator image");

			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image            = exposureImage;
			viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format           = colorFormat;
			viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

			if (vkCreateImageView(dev, &viewInfo, nullptr, &exposureImageView) != VK_SUCCESS)
				SPRaise("Failed to create exposure image view");

			VkSamplerCreateInfo samplerInfo{};
			samplerInfo.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerInfo.magFilter               = VK_FILTER_LINEAR;
			samplerInfo.minFilter               = VK_FILTER_LINEAR;
			samplerInfo.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerInfo.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerInfo.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerInfo.anisotropyEnable        = VK_FALSE;
			samplerInfo.maxAnisotropy           = 1.0f;
			samplerInfo.borderColor             = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			samplerInfo.unnormalizedCoordinates = VK_FALSE;
			samplerInfo.compareEnable           = VK_FALSE;
			samplerInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;

			if (vkCreateSampler(dev, &samplerInfo, nullptr, &linearSampler) != VK_SUCCESS)
				SPRaise("Failed to create post-process sampler");

			// The framebuffer is compatible with both gain render passes because
			// they share the same format and sample count.
			VkFramebufferCreateInfo fbInfo{};
			fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.renderPass      = gainFirstRenderPass;
			fbInfo.attachmentCount = 1;
			fbInfo.pAttachments    = &exposureImageView;
			fbInfo.width           = 1;
			fbInfo.height          = 1;
			fbInfo.layers          = 1;

			if (vkCreateFramebuffer(dev, &fbInfo, nullptr, &exposureFramebuffer) != VK_SUCCESS)
				SPRaise("Failed to create exposure framebuffer");
		}

		// ─────────────────────────────────────────────────────────────
		//  Per-frame helpers
		// ─────────────────────────────────────────────────────────────

		VkFramebuffer VulkanAutoExposureFilter::MakeFramebuffer(VkRenderPass rp,
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
				SPRaise("Failed to create post-process framebuffer");
			perFrameFramebuffers[frameSlot].push_back(fb);
			return fb;
		}

		VkDescriptorSet VulkanAutoExposureFilter::BindTexture(int frameSlot,
		                                                       VkDescriptorSetLayout dsl,
		                                                       VkImageView view) {
			VkDescriptorSetAllocateInfo ai{};
			ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			ai.descriptorPool     = perFrameDescPool[frameSlot];
			ai.descriptorSetCount = 1;
			ai.pSetLayouts        = &dsl;
			VkDescriptorSet set;
			if (vkAllocateDescriptorSets(device->GetDevice(), &ai, &set) != VK_SUCCESS)
				SPRaise("Failed to allocate descriptor set");

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

		VkDescriptorSet VulkanAutoExposureFilter::BindTextures(int frameSlot,
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
				SPRaise("Failed to allocate descriptor set");

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

		void VulkanAutoExposureFilter::DrawFullscreen(VkCommandBuffer cmd,
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

		void VulkanAutoExposureFilter::Filter(VkCommandBuffer cmd,
		                                       VulkanImage* input,
		                                       VulkanImage* output,
		                                       float dt) {
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

			// ── 1. Downsample + preprocess ────────────────────────────────
			// First level: brightness extraction.  Subsequent levels: passthrough
			// (bilinear average via linear filtering on the smaller target).

			std::vector<Handle<VulkanImage>> tempImages;
			VulkanTemporaryImagePool* pool = renderer.GetTemporaryImagePool();

			VkImageView srcView = input->GetImageView();
			uint32_t    w       = static_cast<uint32_t>(renderer.GetRenderWidth());
			uint32_t    h       = static_cast<uint32_t>(renderer.GetRenderHeight());
			bool        first   = true;

			while (w > 1 || h > 1) {
				uint32_t nw = (w + 1) / 2;
				uint32_t nh = (h + 1) / 2;

				Handle<VulkanImage> dst = pool->Acquire(nw, nh, colorFormat);
				VkFramebuffer fb = MakeFramebuffer(ppRenderPass, dst.GetPointerOrNull(), frameSlot);
				VkDescriptorSet ds = BindTexture(frameSlot, singleSamplerDSL, srcView);

				DrawFullscreen(cmd, ppRenderPass, fb, nw, nh,
				               first ? preprocessPipeline : downsamplePipeline,
				               preprocessLayout, ds);

				srcView = dst->GetImageView();
				tempImages.push_back(dst);
				w = nw;  h = nh;  first = false;
			}

			// ── 2. Compute-gain pass ──────────────────────────────────────
			// Blend the 1×1 brightness into the persistent exposure accumulator
			// using temporal smoothing.  The blend rate is carried in alpha.

			float minExp = std::min(std::max((float)r_hdrAutoExposureMin, -10.0f), 10.0f);
			float maxExp = std::min(std::max((float)r_hdrAutoExposureMax, minExp),  10.0f);
			float speed  = std::max((float)r_hdrAutoExposureSpeed, 0.0f);
			float rate   = 1.0f - std::pow(0.01f, dt * speed);

			struct GainParams { float minGain, maxGain, rate; } gainPC{
			    std::pow(2.0f, minExp),
			    std::pow(2.0f, maxExp),
			    rate
			};

			VkDescriptorSet gainDS = BindTexture(frameSlot, singleSamplerDSL, srcView);

			if (!exposureInitialized) {
				// First frame: use gainFirstRenderPass (CLEAR to white, UNDEFINED initial).
				// Clear value (1,1,1,1) initialises the accumulator to unity gain,
				// matching the GL renderer's constructor clear.
				VkClearValue cv{};
				cv.color = {{1.0f, 1.0f, 1.0f, 1.0f}};

				VkRenderPassBeginInfo rpBegin{};
				rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				rpBegin.renderPass        = gainFirstRenderPass;
				rpBegin.framebuffer       = exposureFramebuffer;
				rpBegin.renderArea.extent = {1, 1};
				rpBegin.clearValueCount   = 1;
				rpBegin.pClearValues      = &cv;

				vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport vp{0, 0, 1, 1, 0, 1};
				VkRect2D   sc{{0, 0}, {1, 1}};
				vkCmdSetViewport(cmd, 0, 1, &vp);
				vkCmdSetScissor(cmd, 0, 1, &sc);
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, computeGainPipeline);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
				                        computeGainLayout, 0, 1, &gainDS, 0, nullptr);
				vkCmdPushConstants(cmd, computeGainLayout,
				                   VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(gainPC), &gainPC);
				vkCmdDraw(cmd, 3, 1, 0, 0);
				vkCmdEndRenderPass(cmd);

				exposureInitialized = true;
			} else {
				// Subsequent frames: transition SHADER_READ_ONLY → COLOR_ATTACHMENT,
				// then use gainRenderPass (LOAD) so blending accumulates the gain.
				VkImageMemoryBarrier bar{};
				bar.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				bar.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				bar.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				bar.image               = exposureImage;
				bar.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
				bar.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
				bar.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
				                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				vkCmdPipelineBarrier(cmd,
				    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				    0, 0, nullptr, 0, nullptr, 1, &bar);

				VkClearValue cv{};
				cv.color = {{0.0f, 0.0f, 0.0f, 0.0f}}; // unused (LOAD op)

				VkRenderPassBeginInfo rpBegin{};
				rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				rpBegin.renderPass        = gainRenderPass;
				rpBegin.framebuffer       = exposureFramebuffer;
				rpBegin.renderArea.extent = {1, 1};
				rpBegin.clearValueCount   = 1;
				rpBegin.pClearValues      = &cv;

				vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport vp{0, 0, 1, 1, 0, 1};
				VkRect2D   sc{{0, 0}, {1, 1}};
				vkCmdSetViewport(cmd, 0, 1, &vp);
				vkCmdSetScissor(cmd, 0, 1, &sc);
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, computeGainPipeline);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
				                        computeGainLayout, 0, 1, &gainDS, 0, nullptr);
				vkCmdPushConstants(cmd, computeGainLayout,
				                   VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(gainPC), &gainPC);
				vkCmdDraw(cmd, 3, 1, 0, 0);
				vkCmdEndRenderPass(cmd);
			}

			// After gainRenderPass the exposure image is in SHADER_READ_ONLY_OPTIMAL.

			// ── 3. Apply pass ─────────────────────────────────────────────
			// Multiply the scene colour by the accumulated gain.

			uint32_t rw = static_cast<uint32_t>(renderer.GetRenderWidth());
			uint32_t rh = static_cast<uint32_t>(renderer.GetRenderHeight());

			VkFramebuffer applyFB = MakeFramebuffer(ppRenderPass, output, frameSlot);
			VkDescriptorSet applyDS = BindTextures(frameSlot, dualSamplerDSL,
			                                       input->GetImageView(),
			                                       exposureImageView);

			// Inline apply draw so we can push the invGamma constant between
			// pipeline-bind and draw. invGamma = 1.0 / r_hdrGamma, clamped
			// to a safe range in case the cvar is set to an absurd value.
			{
				float gammaVal = static_cast<float>(r_hdrGamma);
				if (!(gammaVal > 0.01f && gammaVal < 100.0f))
					gammaVal = 2.2f;
				float invGamma = 1.0f / gammaVal;

				VkClearValue cv{};
				cv.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

				VkRenderPassBeginInfo rpBegin{};
				rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				rpBegin.renderPass        = ppRenderPass;
				rpBegin.framebuffer       = applyFB;
				rpBegin.renderArea.extent = {rw, rh};
				rpBegin.clearValueCount   = 1;
				rpBegin.pClearValues      = &cv;

				vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport viewport{0.0f, 0.0f, (float)rw, (float)rh, 0.0f, 1.0f};
				VkRect2D   scissor{{0, 0}, {rw, rh}};
				vkCmdSetViewport(cmd, 0, 1, &viewport);
				vkCmdSetScissor(cmd, 0, 1, &scissor);

				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, applyPipeline);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
				                        applyLayout, 0, 1, &applyDS, 0, nullptr);
				vkCmdPushConstants(cmd, applyLayout,
				                   VK_SHADER_STAGE_FRAGMENT_BIT, 0,
				                   sizeof(invGamma), &invGamma);
				vkCmdDraw(cmd, 3, 1, 0, 0);

				vkCmdEndRenderPass(cmd);
			}

			// Return temporary downsample images to the pool.
			for (auto& img : tempImages)
				pool->Return(img.GetPointerOrNull());
		}

	} // namespace draw
} // namespace spades
