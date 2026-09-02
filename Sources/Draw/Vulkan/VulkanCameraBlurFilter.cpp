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

#include <algorithm>
#include <cmath>
#include <cstring>

#include "VulkanCameraBlurFilter.h"
#include "VulkanFramebufferManager.h"
#include "VulkanImage.h"
#include "VulkanRenderer.h"
#include "VulkanRenderPassUtils.h"
#include "VulkanTemporaryImagePool.h"
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Gui/SDLVulkanDevice.h>

namespace spades {
	namespace draw {

		namespace {
			struct CameraBlurPC {
				float reverseMatrix[16];
				float shutterTimeScale;
			};

			// Rotation-only "reverse" of the view difference matrix; straight
			// port of GLCameraBlurFilter's ReverseMatrix.
#define M(r, c) (d.m[(r) + (c)*4])
			Matrix4 ReverseMatrix(Matrix4 d) {
				return Matrix4(
				  M(1, 2) * M(2, 1) - M(1, 1) * M(2, 2), M(1, 0) * M(2, 2) - M(1, 2) * M(2, 0),
				  M(1, 1) * M(2, 0) - M(1, 0) * M(2, 1), 0, M(0, 1) * M(2, 2) - M(0, 2) * M(2, 1),
				  M(0, 2) * M(2, 0) - M(0, 0) * M(2, 2), M(0, 0) * M(2, 1) - M(0, 1) * M(2, 0), 0,
				  0, 0, 0, 0, M(0, 2) * M(1, 1) - M(0, 1) * M(1, 2),
				  M(0, 0) * M(1, 2) - M(0, 2) * M(1, 0), M(0, 1) * M(1, 0) - M(0, 0) * M(1, 1), 1);
			}
#undef M

			float MyACos(float v) { return v >= 1.0f ? 0.0f : acosf(v); }
		} // namespace

		VulkanCameraBlurFilter::VulkanCameraBlurFilter(VulkanRenderer& r)
		    : VulkanPostProcessFilter(r),
		      colorFormat(VK_FORMAT_UNDEFINED),
		      linearSampler(VK_NULL_HANDLE),
		      ppRenderPass(VK_NULL_HANDLE),
		      dualSamplerDSL(VK_NULL_HANDLE),
		      blurLayout(VK_NULL_HANDLE),
		      blurPipeline(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i)
				perFrameDescPool[i] = VK_NULL_HANDLE;

			prevMatrix = Matrix4::Identity();
			colorFormat = r.GetFramebufferManager()->GetMainColorFormat();

			InitRenderPass();
			InitDescriptorSetLayout();
			InitPipeline();
			InitDescriptorPools();
		}

		VulkanCameraBlurFilter::~VulkanCameraBlurFilter() {
			SPADES_MARK_FUNCTION();

			VkDevice dev = device->GetDevice();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				for (VkFramebuffer fb : perFrameFramebuffers[i])
					vkDestroyFramebuffer(dev, fb, nullptr);
				if (perFrameDescPool[i] != VK_NULL_HANDLE)
					vkDestroyDescriptorPool(dev, perFrameDescPool[i], nullptr);
			}

			if (blurPipeline != VK_NULL_HANDLE) vkDestroyPipeline(dev, blurPipeline, nullptr);
			if (blurLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, blurLayout, nullptr);
			if (dualSamplerDSL != VK_NULL_HANDLE)
				vkDestroyDescriptorSetLayout(dev, dualSamplerDSL, nullptr);
			if (linearSampler != VK_NULL_HANDLE) vkDestroySampler(dev, linearSampler, nullptr);
			if (ppRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(dev, ppRenderPass, nullptr);
		}

		void VulkanCameraBlurFilter::InitRenderPass() {
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
			si.maxAnisotropy = 1.0f;
			si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

			if (vkCreateSampler(dev, &si, nullptr, &linearSampler) != VK_SUCCESS)
				SPRaise("Failed to create camera blur sampler");
		}

		void VulkanCameraBlurFilter::InitDescriptorSetLayout() {
			VkDescriptorSetLayoutBinding b[2]{};
			for (int i = 0; i < 2; ++i) {
				b[i].binding = (uint32_t)i;
				b[i].descriptorCount = 1;
				b[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				b[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			}

			VkDescriptorSetLayoutCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			info.bindingCount = 2;
			info.pBindings = b;

			if (vkCreateDescriptorSetLayout(device->GetDevice(), &info, nullptr,
			                                &dualSamplerDSL) != VK_SUCCESS)
				SPRaise("Failed to create camera blur descriptor set layout");
		}

		VkShaderModule VulkanCameraBlurFilter::LoadSPIRV(const char* path) {
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

		void VulkanCameraBlurFilter::InitPipeline() {
			VkDevice dev = device->GetDevice();
			VkPipelineCache cache = renderer.GetPipelineCache();

			VkShaderModule vs = LoadSPIRV("Shaders/Vulkan/PostFilters/CameraBlur.vk.vs.spv");
			VkShaderModule fs = LoadSPIRV("Shaders/Vulkan/PostFilters/CameraBlur.vk.fs.spv");

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

			// One push-constant block shared by both stages (mat4 + float).
			VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
			                        sizeof(CameraBlurPC)};
			VkPipelineLayoutCreateInfo li{};
			li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			li.setLayoutCount = 1;
			li.pSetLayouts = &dualSamplerDSL;
			li.pushConstantRangeCount = 1;
			li.pPushConstantRanges = &pcr;
			if (vkCreatePipelineLayout(dev, &li, nullptr, &blurLayout) != VK_SUCCESS)
				SPRaise("Failed to create camera blur pipeline layout");

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
			pi.layout = blurLayout;
			pi.renderPass = ppRenderPass;
			pi.subpass = 0;

			if (vkCreateGraphicsPipelines(dev, cache, 1, &pi, nullptr, &blurPipeline) != VK_SUCCESS)
				SPRaise("Failed to create camera blur pipeline");

			vkDestroyShaderModule(dev, vs, nullptr);
			vkDestroyShaderModule(dev, fs, nullptr);
		}

		void VulkanCameraBlurFilter::InitDescriptorPools() {
			// Up to ~8 blur iterations per frame; 2 samplers per set.
			VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32};
			VkDescriptorPoolCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			info.poolSizeCount = 1;
			info.pPoolSizes = &size;
			info.maxSets = 16;

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i) {
				if (vkCreateDescriptorPool(device->GetDevice(), &info, nullptr,
				                           &perFrameDescPool[i]) != VK_SUCCESS)
					SPRaise("Failed to create camera blur descriptor pool");
			}
		}

		VkFramebuffer VulkanCameraBlurFilter::MakeFramebuffer(VulkanImage* image, int frameSlot) {
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
				SPRaise("Failed to create camera blur framebuffer");
			perFrameFramebuffers[frameSlot].push_back(fb);
			return fb;
		}

		VkDescriptorSet VulkanCameraBlurFilter::BindTextures(int frameSlot, VkImageView color,
		                                                     VkImageView depth,
		                                                     VkSampler depthSampler) {
			VkDescriptorSetAllocateInfo ai{};
			ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			ai.descriptorPool = perFrameDescPool[frameSlot];
			ai.descriptorSetCount = 1;
			ai.pSetLayouts = &dualSamplerDSL;
			VkDescriptorSet set;
			if (vkAllocateDescriptorSets(device->GetDevice(), &ai, &set) != VK_SUCCESS)
				SPRaise("Failed to allocate camera blur descriptor set");

			VkDescriptorImageInfo imgs[2] = {
			    {linearSampler, color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			    {depthSampler, depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
			VkWriteDescriptorSet w[2]{};
			for (int i = 0; i < 2; ++i) {
				w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				w[i].dstSet = set;
				w[i].dstBinding = (uint32_t)i;
				w[i].descriptorCount = 1;
				w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				w[i].pImageInfo = &imgs[i];
			}
			vkUpdateDescriptorSets(device->GetDevice(), 2, w, 0, nullptr);
			return set;
		}

		bool VulkanCameraBlurFilter::Apply(VkCommandBuffer cmd, VulkanImage* input,
		                                   VulkanImage* output, float intensity,
		                                   float radialBlur) {
			SPADES_MARK_FUNCTION();

			// CPU-side setup is a straight port of GLCameraBlurFilter::Filter.
			if (radialBlur > 0.0f)
				radialBlur = 1.0f - radialBlur;
			else
				radialBlur = 1.0f;

			bool hasRadialBlur = radialBlur < 0.9999f;

			const client::SceneDefinition& def = renderer.GetSceneDef();
			Matrix4 newMatrix = Matrix4::Identity();
			Vector3 axes[] = {def.viewAxis[0], def.viewAxis[1], def.viewAxis[2]};
			axes[0] /= std::tan(def.fovX * 0.5f);
			axes[1] /= std::tan(def.fovY * 0.5f);
			newMatrix.m[0] = axes[0].x;
			newMatrix.m[1] = axes[1].x;
			newMatrix.m[2] = axes[2].x;
			newMatrix.m[4] = axes[0].y;
			newMatrix.m[5] = axes[1].y;
			newMatrix.m[6] = axes[2].y;
			newMatrix.m[8] = axes[0].z;
			newMatrix.m[9] = axes[1].z;
			newMatrix.m[10] = axes[2].z;

			Matrix4 inverseNewMatrix = newMatrix.Inversed();
			Matrix4 diffMatrix = prevMatrix * inverseNewMatrix;
			prevMatrix = newMatrix;
			Matrix4 reverseMatrix = ReverseMatrix(diffMatrix);

			if (diffMatrix.m[0] < 0.3f || diffMatrix.m[5] < 0.3f || diffMatrix.m[10] < 0.3f) {
				// camera cut; too much rotation to reproject
				if (hasRadialBlur)
					diffMatrix = Matrix4::Identity();
				else
					return false;
			}

			float movePixels = MyACos(diffMatrix.m[0]);
			float shutterTimeScale = intensity;
			movePixels = std::max(movePixels, MyACos(diffMatrix.m[5]));
			movePixels = std::max(movePixels, MyACos(diffMatrix.m[10]));
			movePixels = tanf(movePixels) / tanf(def.fovX * 0.5f);
			movePixels *= (float)renderer.GetRenderWidth() * 0.5f;
			movePixels *= shutterTimeScale;

			movePixels =
			    std::max(movePixels, (1.0f - radialBlur) * renderer.GetRenderWidth() * 0.5f);

			if (movePixels < 1.0f)
				return false;

			int levels = (int)ceilf(logf(movePixels) / logf(5.0f));
			if (levels <= 0)
				levels = 1;

			if (hasRadialBlur)
				radialBlur *= radialBlur;
			reverseMatrix = Matrix4::Scale(radialBlur, radialBlur, 1.0f) * reverseMatrix;

			int frameSlot = static_cast<int>(renderer.GetCurrentFrameIndex());

			{
				VkDevice dev = device->GetDevice();
				for (VkFramebuffer fb : perFrameFramebuffers[frameSlot])
					vkDestroyFramebuffer(dev, fb, nullptr);
				perFrameFramebuffers[frameSlot].clear();
				vkResetDescriptorPool(dev, perFrameDescPool[frameSlot], 0);
			}

			Handle<VulkanImage> depthImg =
			    renderer.GetFramebufferManager()->GetResolvedDepthImage();
			auto* pool = renderer.GetTemporaryImagePool();

			CameraBlurPC pc{};
			std::memcpy(pc.reverseMatrix, reverseMatrix.m, sizeof(pc.reverseMatrix));

			uint32_t w = static_cast<uint32_t>(output->GetWidth());
			uint32_t h = static_cast<uint32_t>(output->GetHeight());

			// Iterated smear; each pass shrinks the shutter by 5x. Ping-pong
			// through pool temporaries, final pass lands in `output`.
			std::vector<Handle<VulkanImage>> keepAlive;
			VulkanImage* src = input;
			for (int i = 0; i < levels; i++) {
				VulkanImage* dst;
				if (i == levels - 1) {
					dst = output;
				} else {
					Handle<VulkanImage> tmp = pool->Acquire(w, h, colorFormat);
					dst = tmp.GetPointerOrNull();
					keepAlive.push_back(std::move(tmp));
				}

				VkFramebuffer fb = MakeFramebuffer(dst, frameSlot);
				VkDescriptorSet ds = BindTextures(frameSlot, src->GetImageView(),
				                                  depthImg->GetImageView(),
				                                  depthImg->GetSampler());

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

				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blurLayout, 0, 1,
				                        &ds, 0, nullptr);

				pc.shutterTimeScale = shutterTimeScale;
				vkCmdPushConstants(cmd, blurLayout,
				                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
				                   sizeof(pc), &pc);

				vkCmdDraw(cmd, 3, 1, 0, 0);
				vkCmdEndRenderPass(cmd);

				shutterTimeScale /= 5.0f;
				src = dst;
			}

			return true;
		}

	} // namespace draw
} // namespace spades
