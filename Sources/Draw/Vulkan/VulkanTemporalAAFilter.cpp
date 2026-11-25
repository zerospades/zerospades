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

#include "VulkanTemporalAAFilter.h"
#include "VulkanFramebufferManager.h"
#include "VulkanImage.h"
#include "VulkanPushConstants.h"
#include "VulkanRenderer.h"
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Gui/SDLVulkanDevice.h>
#include <cstring>

namespace spades {
	namespace draw {

		namespace {
			// TemporalAA.vk.fs's push constants
			struct TemporalAAPushConstants {
				// Rows x, y and w of the matrix taking this frame's texture coordinates
				// and depth to the last frame's
				float reprojection[3][4];
				// The same of the one taking them to the view-centric world
				float inverseViewProjection[3][4];
				// One over the image's size
				float inverseSize[2];
				// How far the fog reaches, as GL's filter takes it
				float fogDistance;
				float firstPersonDepthEnd;
				// 1 where the history starts over
				float reset;
			};

			void StoreRows(float (&out)[3][4], const Matrix4& m) {
				const int rows[3] = {0, 1, 3};
				for (int i = 0; i < 3; i++)
					for (int c = 0; c < 4; c++)
						out[i][c] = m.m[c * 4 + rows[i]];
			}

			/** A move between frames past which the last frame is no history of
			 * this one (a respawn, a spectator switching players), in blocks */
			constexpr float kCutDistance = 8.0F;
		} // namespace

		VulkanTemporalAAFilter::VulkanTemporalAAFilter(VulkanRenderer& r)
		    : VulkanPostProcessFilter(r),
		      colorFormat(r.GetFramebufferManager()->GetMainColorFormat()),
		      linearSampler(VK_NULL_HANDLE),
		      nearestSampler(VK_NULL_HANDLE),
		      resolvePass(VK_NULL_HANDLE),
		      setLayout(VK_NULL_HANDLE),
		      resolveLayout(VK_NULL_HANDLE),
		      resolvePipeline(VK_NULL_HANDLE),
		      prevMatrix(Matrix4::Identity()),
		      prevViewOrigin(MakeVector3(0.0F, 0.0F, 0.0F)) {
			SPADES_MARK_FUNCTION();

			for (int i = 0; i < MAX_FRAME_SLOTS; ++i)
				perFrameDescPool[i] = VK_NULL_HANDLE;

			InitRenderPass();
			InitDescriptorSetLayout();
			InitPipeline();
			InitDescriptorPools();
		}

		VulkanTemporalAAFilter::~VulkanTemporalAAFilter() {
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
			if (setLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, setLayout, nullptr);
			if (linearSampler != VK_NULL_HANDLE) vkDestroySampler(dev, linearSampler, nullptr);
			if (nearestSampler != VK_NULL_HANDLE) vkDestroySampler(dev, nearestSampler, nullptr);
			if (resolvePass != VK_NULL_HANDLE) vkDestroyRenderPass(dev, resolvePass, nullptr);
		}

		void VulkanTemporalAAFilter::InitRenderPass() {
			VkDevice dev = device->GetDevice();

			// The chain's output and the history the next frame reads, both filled
			// whole and sampled afterwards
			VkAttachmentDescription attachments[2]{};
			const VkFormat formats[2] = {colorFormat, kHistoryFormat};
			for (int i = 0; i < 2; i++) {
				attachments[i].format = formats[i];
				attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
				attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				attachments[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				attachments[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}

			VkAttachmentReference refs[2] = {
			  {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
			  {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
			VkSubpassDescription subpass{};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 2;
			subpass.pColorAttachments = refs;

			// As CreateSimpleColorRenderPass's: what the inputs were written by is
			// visible to this pass's reads; this pass's writes wait for the earlier
			// reads of its targets (the last frame's of this history image) and
			// writes; and its writes are visible to whoever samples them.
			VkSubpassDependency deps[3]{};
			deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			deps[0].dstSubpass = 0;
			deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			                       VK_PIPELINE_STAGE_TRANSFER_BIT;
			deps[0].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
			                        VK_ACCESS_TRANSFER_WRITE_BIT;
			deps[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			deps[1].srcSubpass = VK_SUBPASS_EXTERNAL;
			deps[1].dstSubpass = 0;
			deps[1].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			                       VK_PIPELINE_STAGE_TRANSFER_BIT;
			deps[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			deps[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
			                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
			                        VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
			deps[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
			                        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

			deps[2].srcSubpass = 0;
			deps[2].dstSubpass = VK_SUBPASS_EXTERNAL;
			deps[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			deps[2].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			                       VK_PIPELINE_STAGE_TRANSFER_BIT;
			deps[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			deps[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;

			VkRenderPassCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			info.attachmentCount = 2;
			info.pAttachments = attachments;
			info.subpassCount = 1;
			info.pSubpasses = &subpass;
			info.dependencyCount = 3;
			info.pDependencies = deps;
			if (vkCreateRenderPass(dev, &info, nullptr, &resolvePass) != VK_SUCCESS)
				SPRaise("Failed to create the temporal AA render pass");

			VkSamplerCreateInfo si{};
			si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			si.magFilter = VK_FILTER_LINEAR;
			si.minFilter = VK_FILTER_LINEAR;
			si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			si.maxAnisotropy = 1.0F;
			si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			if (vkCreateSampler(dev, &si, nullptr, &linearSampler) != VK_SUCCESS)
				SPRaise("Failed to create the temporal AA sampler");
			si.magFilter = VK_FILTER_NEAREST;
			si.minFilter = VK_FILTER_NEAREST;
			if (vkCreateSampler(dev, &si, nullptr, &nearestSampler) != VK_SUCCESS)
				SPRaise("Failed to create the temporal AA depth sampler");
		}

		void VulkanTemporalAAFilter::InitDescriptorSetLayout() {
			// The input, the history, the depth
			VkDescriptorSetLayoutBinding b[3]{};
			for (uint32_t i = 0; i < 3; i++) {
				b[i].binding = i;
				b[i].descriptorCount = 1;
				b[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				b[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			}
			VkDescriptorSetLayoutCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			info.bindingCount = 3;
			info.pBindings = b;
			if (vkCreateDescriptorSetLayout(device->GetDevice(), &info, nullptr, &setLayout) !=
			    VK_SUCCESS)
				SPRaise("Failed to create the temporal AA descriptor set layout");
		}

		VkShaderModule VulkanTemporalAAFilter::LoadSPIRV(const char* path) {
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

		void VulkanTemporalAAFilter::InitPipeline() {
			VkDevice dev = device->GetDevice();

			VkShaderModule vs = LoadSPIRV("Shaders/Vulkan/PostFilters/PassThrough.vk.vs.spv");
			VkShaderModule fs = LoadSPIRV("Shaders/Vulkan/PostFilters/TemporalAA.vk.fs.spv");

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
			rs.lineWidth = 1.0F;
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

			VkPipelineColorBlendAttachmentState noBlend[2]{};
			for (auto& b : noBlend)
				b.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
				                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			VkPipelineColorBlendStateCreateInfo blend{};
			blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			blend.attachmentCount = 2;
			blend.pAttachments = noBlend;

			const VkPushConstantRange pcr =
			  PushConstantRange<TemporalAAPushConstants>(VK_SHADER_STAGE_FRAGMENT_BIT);
			VkPipelineLayoutCreateInfo li{};
			li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			li.setLayoutCount = 1;
			li.pSetLayouts = &setLayout;
			li.pushConstantRangeCount = 1;
			li.pPushConstantRanges = &pcr;
			if (vkCreatePipelineLayout(dev, &li, nullptr, &resolveLayout) != VK_SUCCESS)
				SPRaise("Failed to create the temporal AA pipeline layout");

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
			pi.renderPass = resolvePass;
			pi.subpass = 0;
			if (vkCreateGraphicsPipelines(dev, renderer.GetPipelineCache(), 1, &pi, nullptr,
			                              &resolvePipeline) != VK_SUCCESS)
				SPRaise("Failed to create the temporal AA pipeline");

			vkDestroyShaderModule(dev, vs, nullptr);
			vkDestroyShaderModule(dev, fs, nullptr);
		}

		void VulkanTemporalAAFilter::InitDescriptorPools() {
			VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3};
			VkDescriptorPoolCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			info.poolSizeCount = 1;
			info.pPoolSizes = &size;
			info.maxSets = 1;
			for (int i = 0; i < MAX_FRAME_SLOTS; ++i)
				if (vkCreateDescriptorPool(device->GetDevice(), &info, nullptr,
				                           &perFrameDescPool[i]) != VK_SUCCESS)
					SPRaise("Failed to create the temporal AA descriptor pool");
		}

		void VulkanTemporalAAFilter::PrepareHistory(VkCommandBuffer cmd, uint32_t width,
		                                            uint32_t height) {
			if (history[0] && history[0]->GetWidth() == width &&
			    history[0]->GetHeight() == height)
				return;

			// A new size comes only with new render targets, which are made with the
			// device idle: nothing in flight still reads the old history.
			for (Handle<VulkanImage>& image : history) {
				image = Handle<VulkanImage>::New(
				  device, width, height, kHistoryFormat, VK_IMAGE_TILING_OPTIMAL,
				  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				image->CreateImageView(VK_IMAGE_ASPECT_COLOR_BIT);
				// In the layout the resolve samples it in, before any frame wrote it
				image->TransitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0,
				                        VK_ACCESS_SHADER_READ_BIT,
				                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
			}
			historyValid = false;
		}

		void VulkanTemporalAAFilter::Filter(VkCommandBuffer cmd, VulkanImage* input,
		                                    VulkanImage* output) {
			SPADES_MARK_FUNCTION();

			const int frameSlot = static_cast<int>(renderer.GetCurrentFrameIndex());
			VkDevice dev = device->GetDevice();
			for (VkFramebuffer fb : perFrameFramebuffers[frameSlot])
				vkDestroyFramebuffer(dev, fb, nullptr);
			perFrameFramebuffers[frameSlot].clear();
			vkResetDescriptorPool(dev, perFrameDescPool[frameSlot], 0);

			const uint32_t w = static_cast<uint32_t>(output->GetWidth());
			const uint32_t h = static_cast<uint32_t>(output->GetHeight());
			PrepareHistory(cmd, w, h);

			// The view-centric view-projection onto the images' coordinates: x right
			// and y down in [0, 1], the depth as it is stored. Unjittered, as GL's.
			const client::SceneDefinition& def = renderer.GetSceneDef();
			Matrix4 view = def.ToViewMatrix();
			view.m[12] = 0.0F;
			view.m[13] = 0.0F;
			view.m[14] = 0.0F;
			const Matrix4 newMatrix = Matrix4::Translate(0.5F, 0.5F, 0.0F) *
			                          Matrix4::Scale(0.5F, -0.5F, 1.0F) *
			                          def.ToVulkanProjectionMatrix() * view;
			const Matrix4 inverseNewMatrix = newMatrix.Inversed();

			// The last frame's history serves only the frame right after it, seen
			// from about the same place
			const std::uint32_t frame = renderer.GetFrameNumber();
			const bool reset = !historyValid || frame != lastFrame + 1 ||
			                   (def.viewOrigin - prevViewOrigin).GetLength() > kCutDistance;

			// The camera's move is kept apart from the matrices, as GL does: folded
			// into them, it costs visible precision
			const Matrix4 reprojection =
			  prevMatrix * Matrix4::Translate(def.viewOrigin - prevViewOrigin) * inverseNewMatrix;
			prevMatrix = newMatrix;
			prevViewOrigin = def.viewOrigin;
			lastFrame = frame;
			historyValid = true;

			const int written = 1 - historyWritten;
			VulkanImage& target = *history[written];
			VulkanImage& previous = *history[historyWritten];
			historyWritten = written;

			VkImageView attachmentViews[2] = {output->GetImageView(), target.GetImageView()};
			VkFramebufferCreateInfo fbInfo{};
			fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.renderPass = resolvePass;
			fbInfo.attachmentCount = 2;
			fbInfo.pAttachments = attachmentViews;
			fbInfo.width = w;
			fbInfo.height = h;
			fbInfo.layers = 1;
			VkFramebuffer fb;
			if (vkCreateFramebuffer(dev, &fbInfo, nullptr, &fb) != VK_SUCCESS)
				SPRaise("Failed to create the temporal AA framebuffer");
			perFrameFramebuffers[frameSlot].push_back(fb);

			VkDescriptorSetAllocateInfo ai{};
			ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			ai.descriptorPool = perFrameDescPool[frameSlot];
			ai.descriptorSetCount = 1;
			ai.pSetLayouts = &setLayout;
			VkDescriptorSet set;
			if (vkAllocateDescriptorSets(dev, &ai, &set) != VK_SUCCESS)
				SPRaise("Failed to allocate the temporal AA descriptor set");

			Handle<VulkanImage> depth = renderer.GetFramebufferManager()->GetResolvedDepthImage();
			VkDescriptorImageInfo images[3] = {
			  {linearSampler, input->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			  {linearSampler, previous.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			  {nearestSampler, depth->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
			VkWriteDescriptorSet writes[3]{};
			for (uint32_t i = 0; i < 3; i++) {
				writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[i].dstSet = set;
				writes[i].dstBinding = i;
				writes[i].descriptorCount = 1;
				writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[i].pImageInfo = &images[i];
			}
			vkUpdateDescriptorSets(dev, 3, writes, 0, nullptr);

			TemporalAAPushConstants pc{};
			StoreRows(pc.reprojection, reprojection);
			StoreRows(pc.inverseViewProjection, inverseNewMatrix);
			pc.inverseSize[0] = 1.0F / (float)w;
			pc.inverseSize[1] = 1.0F / (float)h;
			pc.fogDistance = 128.0F;
			pc.firstPersonDepthEnd = VulkanRenderer::kFirstPersonDepthEnd;
			pc.reset = reset ? 1.0F : 0.0F;

			VkRenderPassBeginInfo rpBegin{};
			rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			rpBegin.renderPass = resolvePass;
			rpBegin.framebuffer = fb;
			rpBegin.renderArea.extent = {w, h};
			vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport{0.0F, 0.0F, (float)w, (float)h, 0.0F, 1.0F};
			VkRect2D scissor{{0, 0}, {w, h}};
			vkCmdSetViewport(cmd, 0, 1, &viewport);
			vkCmdSetScissor(cmd, 0, 1, &scissor);
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, resolvePipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, resolveLayout, 0, 1,
			                        &set, 0, nullptr);
			PushConstants(cmd, resolveLayout, VK_SHADER_STAGE_FRAGMENT_BIT, pc);
			vkCmdDraw(cmd, 3, 1, 0, 0);
			vkCmdEndRenderPass(cmd);
		}

	} // namespace draw
} // namespace spades
