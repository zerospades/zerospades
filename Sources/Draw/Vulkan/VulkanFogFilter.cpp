/*
 Copyright (c) 2021 Fran6nd

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

#include "VulkanFogFilter.h"
#include "VulkanAmbientShadowRenderer.h"
#include "VulkanFramebufferManager.h"
#include "VulkanImage.h"
#include "VulkanMapShadowRenderer.h"
#include "VulkanRadiosityRenderer.h"
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

SPADES_SETTING(r_fogShadow);
SPADES_SETTING(r_radiosity);

namespace spades {
	namespace draw {

		// Push constants for Fog2.vk.vs + Fog2.vk.fs (total = 144 bytes).
		struct Fog2PushConstants {
			float viewProjInv[16];      // [0..63]   mat4 viewProjectionMatrixInv
			float viewOriginFogDist[4]; // [64..79]  xyz=viewOrigin, w=fogDistance
			float sunlightScale[4];     // [80..95]  xyz scale, w = sunDir.x
			float ambientScale[4];      // [96..111] xyz scale, w = sunDir.y
			float radiosityScale[4];    // [112..127] xyz scale, w = sunDir.z
			float ditherFrame[4];       // [128..143] xy=per-frame noise seed
		};
		static_assert(sizeof(Fog2PushConstants) == 144, "Fog2PushConstants must be 144 bytes");

		// Push constants for Fog.vk.vs + Fog.vk.fs (total = 96 bytes).
		struct FogClassicPushConstants {
			float viewOriginPad[4];   // [0..15]  xyz = viewOrigin
			float viewAxisUp[4];      // [16..31] xyz
			float viewAxisSide[4];    // [32..47] xyz
			float viewAxisFront[4];   // [48..63] xyz
			float fovZNearFar[4];     // [64..79] xy = (tan(fovX/2), -tan(fovY/2)),
			                          //           y is pre-negated to match the
			                          //           negative-height viewport used in
			                          //           the scene pass.
			                          //           z = zNear, w = zFar
			float fogColorDist[4];    // [80..95] xyz = fogColor (linear), w = fogDistance
		};
		static_assert(sizeof(FogClassicPushConstants) == 96, "FogClassicPushConstants must be 96 bytes");

		// ─────────────────────────────────────────────────────────────────────────
		//  Construction / destruction
		// ─────────────────────────────────────────────────────────────────────────

		VulkanFogFilter::VulkanFogFilter(VulkanRenderer& r)
		    : VulkanPostProcessFilter(r),
		      colorFormat(VK_FORMAT_UNDEFINED),
		      colorSampler(VK_NULL_HANDLE),
		      ppRenderPass(VK_NULL_HANDLE),
		      triSamplerDSL(VK_NULL_HANDLE),
		      fog2DSL(VK_NULL_HANDLE),
		      fogLayout(VK_NULL_HANDLE),
		      fogPipeline(VK_NULL_HANDLE),
		      fogClassicLayout(VK_NULL_HANDLE),
		      fogClassicPipeline(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i)
				perFrameDescPool[i] = VK_NULL_HANDLE;

			colorFormat = r.GetFramebufferManager()->GetMainColorFormat();

			InitRenderPass();
			InitDescriptorSetLayout();
			InitPipeline();
			InitDescriptorPools();
		}

		VulkanFogFilter::~VulkanFogFilter() {
			SPADES_MARK_FUNCTION();

			VkDevice dev = device->GetDevice();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				for (VkFramebuffer fb : perFrameFramebuffers[i])
					vkDestroyFramebuffer(dev, fb, nullptr);
				if (perFrameDescPool[i] != VK_NULL_HANDLE)
					vkDestroyDescriptorPool(dev, perFrameDescPool[i], nullptr);
			}

			if (fogClassicPipeline != VK_NULL_HANDLE) vkDestroyPipeline(dev, fogClassicPipeline, nullptr);
			if (fogClassicLayout   != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, fogClassicLayout, nullptr);
			if (fogPipeline   != VK_NULL_HANDLE) vkDestroyPipeline(dev, fogPipeline, nullptr);
			if (fogLayout     != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, fogLayout, nullptr);
			if (fog2DSL       != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, fog2DSL, nullptr);
			if (triSamplerDSL != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, triSamplerDSL, nullptr);
			if (colorSampler  != VK_NULL_HANDLE) vkDestroySampler(dev, colorSampler, nullptr);
			if (ppRenderPass  != VK_NULL_HANDLE) vkDestroyRenderPass(dev, ppRenderPass, nullptr);
		}

		// ─────────────────────────────────────────────────────────────────────────
		//  Initialisation
		// ─────────────────────────────────────────────────────────────────────────

		void VulkanFogFilter::InitRenderPass() {
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
				SPRaise("Failed to create fog filter sampler");
		}

		void VulkanFogFilter::InitDescriptorSetLayout() {
			VkDevice dev = device->GetDevice();

			// Fog1 layout: 4 bindings (color, depth, fine shadow, coarse shadow).
			VkDescriptorSetLayoutBinding bindings[4]{};
			for (int i = 0; i < 4; ++i) {
				bindings[i].binding         = static_cast<uint32_t>(i);
				bindings[i].descriptorCount = 1;
				bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
			}

			VkDescriptorSetLayoutCreateInfo info{};
			info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			info.bindingCount = 4;
			info.pBindings    = bindings;

			if (vkCreateDescriptorSetLayout(dev, &info, nullptr, &triSamplerDSL) != VK_SUCCESS)
				SPRaise("Failed to create fog filter descriptor set layout");

			// Fog2 layout: 8 bindings.
			//   0 colorTexture        (sampler2D)
			//   1 depthTexture        (sampler2D)
			//   2 shadowMapTexture    (sampler2D)
			//   3 ambientShadowTexture (sampler3D)
			//   4 radiosityTextureFlat (sampler3D)
			//   5 radiosityTextureX    (sampler3D)
			//   6 radiosityTextureY    (sampler3D)
			//   7 radiosityTextureZ    (sampler3D)
			VkDescriptorSetLayoutBinding bindings2[8]{};
			for (int i = 0; i < 8; ++i) {
				bindings2[i].binding         = static_cast<uint32_t>(i);
				bindings2[i].descriptorCount = 1;
				bindings2[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				bindings2[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
			}

			VkDescriptorSetLayoutCreateInfo info2{};
			info2.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			info2.bindingCount = 8;
			info2.pBindings    = bindings2;

			if (vkCreateDescriptorSetLayout(dev, &info2, nullptr, &fog2DSL) != VK_SUCCESS)
				SPRaise("Failed to create fog2 descriptor set layout");
		}

		VkShaderModule VulkanFogFilter::LoadSPIRV(const char* path) {
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

		void VulkanFogFilter::InitPipeline() {
			VkDevice        dev   = device->GetDevice();
			VkPipelineCache cache = renderer.GetPipelineCache();

			// Shared fixed pipeline state — every field below is identical between the
			// two variants, so build it once and reuse it.
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

			// Build one pipeline + matching pipeline layout for a (vs, fs, pcSize, dsl) tuple.
			auto buildVariant = [&](const char* vsPath, const char* fsPath, uint32_t pcSize,
			                        VkDescriptorSetLayout dsl,
			                        VkPipelineLayout* outLayout, VkPipeline* outPipeline,
			                        const char* errLabel) {
				VkShaderModule vs = LoadSPIRV(vsPath);
				VkShaderModule fs = LoadSPIRV(fsPath);

				VkPushConstantRange pcr{};
				pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
				pcr.offset     = 0;
				pcr.size       = pcSize;

				VkPipelineLayoutCreateInfo li{};
				li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				li.setLayoutCount         = 1;
				li.pSetLayouts            = &dsl;
				li.pushConstantRangeCount = 1;
				li.pPushConstantRanges    = &pcr;
				if (vkCreatePipelineLayout(dev, &li, nullptr, outLayout) != VK_SUCCESS)
					SPRaise("Failed to create %s pipeline layout", errLabel);

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
				pi.layout              = *outLayout;
				pi.renderPass          = ppRenderPass;
				pi.subpass             = 0;

				if (vkCreateGraphicsPipelines(dev, cache, 1, &pi, nullptr, outPipeline) != VK_SUCCESS)
					SPRaise("Failed to create %s pipeline", errLabel);

				vkDestroyShaderModule(dev, vs, nullptr);
				vkDestroyShaderModule(dev, fs, nullptr);
			};

			buildVariant("Shaders/Vulkan/PostFilters/Fog2.vk.vs.spv",
			             "Shaders/Vulkan/PostFilters/Fog2.vk.fs.spv",
			             sizeof(Fog2PushConstants), fog2DSL,
			             &fogLayout, &fogPipeline, "fog2");

			buildVariant("Shaders/Vulkan/PostFilters/Fog.vk.vs.spv",
			             "Shaders/Vulkan/PostFilters/Fog.vk.fs.spv",
			             sizeof(FogClassicPushConstants), triSamplerDSL,
			             &fogClassicLayout, &fogClassicPipeline, "fog");
		}

		void VulkanFogFilter::InitDescriptorPools() {
			VkDevice dev = device->GetDevice();

			// Up to one set per frame slot; Fog2 needs 8 samplers, Fog1 needs 3.
			// Budget 16 samplers (2× Fog2 worst case) per pool and 4 sets total.
			VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16};
			VkDescriptorPoolCreateInfo info{};
			info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			info.poolSizeCount = 1;
			info.pPoolSizes    = &size;
			info.maxSets       = 4;

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				if (vkCreateDescriptorPool(dev, &info, nullptr, &perFrameDescPool[i]) != VK_SUCCESS)
					SPRaise("Failed to create fog descriptor pool");
			}
		}

		// ─────────────────────────────────────────────────────────────────────────
		//  Per-frame helpers
		// ─────────────────────────────────────────────────────────────────────────

		VkFramebuffer VulkanFogFilter::MakeFramebuffer(VulkanImage* image, int frameSlot) {
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
				SPRaise("Failed to create fog framebuffer");
			perFrameFramebuffers[frameSlot].push_back(fb);
			return fb;
		}

		VkDescriptorSet VulkanFogFilter::BindTextures(int           frameSlot,
		                                               VkImageView   colorView,
		                                               VkImageView   depthView,
		                                               VkSampler     depthSampler,
		                                               VkImageView   shadowView,
		                                               VkSampler     shadowSampler,
		                                               VkImageView   coarseShadowView,
		                                               VkSampler     coarseShadowSampler) {
			VkDescriptorSetAllocateInfo ai{};
			ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			ai.descriptorPool     = perFrameDescPool[frameSlot];
			ai.descriptorSetCount = 1;
			ai.pSetLayouts        = &triSamplerDSL;
			VkDescriptorSet set;
			if (vkAllocateDescriptorSets(device->GetDevice(), &ai, &set) != VK_SUCCESS)
				SPRaise("Failed to allocate fog descriptor set");

			VkDescriptorImageInfo imgs[4]{
			    {colorSampler,         colorView,        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			    {depthSampler,         depthView,        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			    {shadowSampler,        shadowView,       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			    {coarseShadowSampler,  coarseShadowView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			};
			VkWriteDescriptorSet writes[4]{};
			for (int i = 0; i < 4; ++i) {
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

		VkDescriptorSet VulkanFogFilter::BindTexturesFog2(int           frameSlot,
		                                                  VkImageView   colorView,
		                                                  VkImageView   depthView,
		                                                  VkSampler     depthSampler,
		                                                  VkImageView   shadowView,
		                                                  VkSampler     shadowSampler,
		                                                  VkImageView   aoView,
		                                                  VkSampler     aoSampler,
		                                                  VkImageView   radFlat,
		                                                  VkImageView   radX,
		                                                  VkImageView   radY,
		                                                  VkImageView   radZ,
		                                                  VkSampler     radSampler) {
			VkDescriptorSetAllocateInfo ai{};
			ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			ai.descriptorPool     = perFrameDescPool[frameSlot];
			ai.descriptorSetCount = 1;
			ai.pSetLayouts        = &fog2DSL;
			VkDescriptorSet set;
			if (vkAllocateDescriptorSets(device->GetDevice(), &ai, &set) != VK_SUCCESS)
				SPRaise("Failed to allocate fog2 descriptor set");

			VkDescriptorImageInfo imgs[8]{
			    {colorSampler,  colorView,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			    {depthSampler,  depthView,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			    {shadowSampler, shadowView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			    {aoSampler,     aoView,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			    {radSampler,    radFlat,    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			    {radSampler,    radX,       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			    {radSampler,    radY,       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			    {radSampler,    radZ,       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			};
			VkWriteDescriptorSet writes[8]{};
			for (int i = 0; i < 8; ++i) {
				writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[i].dstSet          = set;
				writes[i].dstBinding      = static_cast<uint32_t>(i);
				writes[i].descriptorCount = 1;
				writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[i].pImageInfo      = &imgs[i];
			}
			vkUpdateDescriptorSets(device->GetDevice(), 8, writes, 0, nullptr);
			return set;
		}

		// ─────────────────────────────────────────────────────────────────────────
		//  Filter()
		// ─────────────────────────────────────────────────────────────────────────

		void VulkanFogFilter::Filter(VkCommandBuffer cmd,
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

			// ── Pick variant ──────────────────────────────────────────────────────
			// Matches GLSettings::ShouldUseFogFilter2():
			//   Fog2 (smoother 16-step + radiosity in-scatter) requires both
			//   r_fogShadow == 2 AND r_radiosity != 0. Otherwise fall back to
			//   Fog (DDA-style sharp shadow shafts), which is also what GL
			//   uses when r_fogShadow == 1 or when r_radiosity is off.
			const bool useFog2    = ((int)r_fogShadow == 2 && (int)r_radiosity != 0);
			const bool useClassic = !useFog2;

			const client::SceneDefinition& def = renderer.GetSceneDef();

			Vector3 fogCol = renderer.GetFogColor();
			fogCol *= fogCol; // linearise (match GL renderer)

			// ── Build push constants ──────────────────────────────────────────────

			Fog2PushConstants        pc2{};
			FogClassicPushConstants  pc1{};
			const void*  pcData = nullptr;
			uint32_t     pcSize = 0;

			if (useClassic) {
				// Fog (Fog1) — per-vertex shadow ray origin/direction in shadow space.
				pc1.viewOriginPad[0] = def.viewOrigin.x;
				pc1.viewOriginPad[1] = def.viewOrigin.y;
				pc1.viewOriginPad[2] = def.viewOrigin.z;

				pc1.viewAxisUp[0]    = def.viewAxis[1].x;
				pc1.viewAxisUp[1]    = def.viewAxis[1].y;
				pc1.viewAxisUp[2]    = def.viewAxis[1].z;

				pc1.viewAxisSide[0]  = def.viewAxis[0].x;
				pc1.viewAxisSide[1]  = def.viewAxis[0].y;
				pc1.viewAxisSide[2]  = def.viewAxis[0].z;

				pc1.viewAxisFront[0] = def.viewAxis[2].x;
				pc1.viewAxisFront[1] = def.viewAxis[2].y;
				pc1.viewAxisFront[2] = def.viewAxis[2].z;

				// Sun direction packed into the free .w slots (see Fog.vk.fs).
				Vector3 sunDir1 = renderer.GetSunDirection();
				pc1.viewOriginPad[3] = sunDir1.x;
				pc1.viewAxisUp[3]    = sunDir1.y;
				pc1.viewAxisSide[3]  = sunDir1.z;

				// tan(fov/2). Y is pre-negated to flip the per-pixel view-ray
				// direction vertically, matching the negative-height viewport
				// used during the scene pass (the texture stores top-of-view at
				// row 0; without this flip, the shadow shafts march in the wrong
				// vertical band).
				pc1.fovZNearFar[0] =  tanf(def.fovX * 0.5F);
				pc1.fovZNearFar[1] = -tanf(def.fovY * 0.5F);
				pc1.fovZNearFar[2] = def.zNear;
				pc1.fovZNearFar[3] = def.zFar;

				pc1.fogColorDist[0] = fogCol.x;
				pc1.fogColorDist[1] = fogCol.y;
				pc1.fogColorDist[2] = fogCol.z;
				pc1.fogColorDist[3] = renderer.GetFogDistance();

				pcData = &pc1;
				pcSize = sizeof(pc1);
			} else {
				// Fog2 — view-projection inverse + sunlight/ambient transmission scales.
				Matrix4 viewMat = def.ToViewMatrix();
				viewMat.m[12]   = 0.0F;
				viewMat.m[13]   = 0.0F;
				viewMat.m[14]   = 0.0F;

				Matrix4 projMat = renderer.GetProjectionMatrix(); // Vulkan: z ∈ [0,1]
				Matrix4 vp      = projMat * viewMat;

				// Map Vulkan clip space (x,y ∈ [-1,1], z ∈ [0,1]) to UV+depth ([0,1]³).
				// The scene is rasterised with a negative-height viewport so the
				// offscreen color/depth textures store row 0 at clip_y = +1.
				// Flip Y here so vpInv produces the correct world ray for each
				// (u, v) texel.
				//   u = (x+1)*0.5,  v = (1-y)*0.5,  depth = z
				vp = Matrix4::Translate(1.0F, -1.0F, 0.0F) * vp;
				vp = Matrix4::Scale(0.5F, -0.5F, 1.0F)     * vp;

				Matrix4 vpInv = vp.Inversed();

				constexpr float sunlightBrightness   = 0.6F;
				constexpr float ambientBrightness    = 1.0F;
				constexpr float radiosityBrightness  = 1.0F;
				constexpr float radiosityOffset      = 0.2F;

				auto fogTransmission1 = [&](float f) {
					return f / (sunlightBrightness + ambientBrightness * f + 1.0e-6F);
				};
				Vector3 ft{fogTransmission1(fogCol.x),
				           fogTransmission1(fogCol.y),
				           fogTransmission1(fogCol.z)};

				std::memcpy(pc2.viewProjInv, vpInv.m, sizeof(pc2.viewProjInv));

				pc2.viewOriginFogDist[0] = def.viewOrigin.x;
				pc2.viewOriginFogDist[1] = def.viewOrigin.y;
				pc2.viewOriginFogDist[2] = def.viewOrigin.z;
				pc2.viewOriginFogDist[3] = renderer.GetFogDistance();

				pc2.sunlightScale[0] = ft.x * sunlightBrightness;
				pc2.sunlightScale[1] = ft.y * sunlightBrightness;
				pc2.sunlightScale[2] = ft.z * sunlightBrightness;

				pc2.ambientScale[0] = ft.x * fogCol.x * ambientBrightness;
				pc2.ambientScale[1] = ft.y * fogCol.y * ambientBrightness;
				pc2.ambientScale[2] = ft.z * fogCol.z * ambientBrightness;

				// Matches GLFogFilter2: radiosityScale = ft * 1.0 + 0.2
				pc2.radiosityScale[0] = ft.x * radiosityBrightness + radiosityOffset;
				pc2.radiosityScale[1] = ft.y * radiosityBrightness + radiosityOffset;
				pc2.radiosityScale[2] = ft.z * radiosityBrightness + radiosityOffset;

				// Sun direction packed into the free .w slots (see Fog2.vk.fs).
				Vector3 sunDir = renderer.GetSunDirection();
				pc2.sunlightScale[3]  = sunDir.x;
				pc2.ambientScale[3]   = sunDir.y;
				pc2.radiosityScale[3] = sunDir.z;

				std::uint32_t frame = frameCounter++ % 4;
				pc2.ditherFrame[0] = (float)(frame & 1) * 0.5F;
				pc2.ditherFrame[1] = (float)((frame >> 1) & 1) * 0.5F;

				pcData = &pc2;
				pcSize = sizeof(pc2);
			}

			// ── Gather image views ────────────────────────────────────────────────

			Handle<VulkanImage> depthImg  = renderer.GetFramebufferManager()->GetResolvedDepthImage();
			VulkanImage*        shadowImg       = renderer.GetMapShadowRenderer()->GetShadowImage();
			VulkanImage*        coarseShadowImg = renderer.GetMapShadowRenderer()->GetCoarseShadowImage();

			VkDescriptorSet ds;
			if (useClassic) {
				ds = BindTextures(frameSlot,
				    input->GetImageView(),
				    depthImg->GetImageView(), depthImg->GetSampler(),
				    shadowImg->GetImageView(), shadowImg->GetSampler(),
				    coarseShadowImg->GetImageView(), coarseShadowImg->GetSampler());
			} else {
				// Fog2 also samples per-block AO and radiosity 3D textures so it
				// can integrate atmospheric indirect light along the view ray
				// (matches GLFogFilter2). Both subsystems may be null very early
				// during map load — fall back to the 3-binding path in that case.
				VulkanAmbientShadowRenderer* aoR  = renderer.GetAmbientShadowRenderer();
				VulkanRadiosityRenderer*     radR = renderer.GetRadiosityRenderer();
				if (aoR && radR) {
					ds = BindTexturesFog2(frameSlot,
					    input->GetImageView(),
					    depthImg->GetImageView(), depthImg->GetSampler(),
					    shadowImg->GetImageView(), shadowImg->GetSampler(),
					    aoR->GetImageView(),       aoR->GetSampler(),
					    radR->GetImageViewFlat(),
					    radR->GetImageViewX(),
					    radR->GetImageViewY(),
					    radR->GetImageViewZ(),
					    radR->GetSampler());
				} else {
					// Filter is technically unsafe to run without the extra
					// textures bound (Fog2 pipeline expects 8 bindings). Skip
					// the post-pass entirely; the world still renders, just
					// without atmospheric scattering this frame.
					return;
				}
			}

			// ── Record draw ───────────────────────────────────────────────────────

			uint32_t rw = static_cast<uint32_t>(output->GetWidth());
			uint32_t rh = static_cast<uint32_t>(output->GetHeight());

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

			VkPipeline       pipeline = useClassic ? fogClassicPipeline : fogPipeline;
			VkPipelineLayout layout   = useClassic ? fogClassicLayout   : fogLayout;

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			                        layout, 0, 1, &ds, 0, nullptr);
			vkCmdPushConstants(cmd, layout,
			                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			                   0, pcSize, pcData);
			vkCmdDraw(cmd, 3, 1, 0, 0);

			vkCmdEndRenderPass(cmd);
		}

	} // namespace draw
} // namespace spades
