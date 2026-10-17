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

#include "VulkanScreenSpaceAmbientOcclusion.h"
#include "VulkanSettings.h"

#include <algorithm>
#include <cmath>

#include "VulkanFramebufferManager.h"
#include "VulkanImage.h"
#include "VulkanPushConstants.h"
#include "VulkanRenderer.h"
#include "VulkanSpirvCache.h"
#include <Client/SceneDefinition.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/Settings.h>
#include <Gui/SDLVulkanDevice.h>


namespace spades {
	namespace draw {
		namespace {
			// AmbientOcclusion/Evaluate.comp's push constants
			struct EvaluateParams {
				float zNearFar[2];
				float tanHalfFov[2];
				float sampleRadius;
				std::int32_t scale;
			};
			// AmbientOcclusion/Blur.comp's
			struct BlurParams {
				float zNearFar[2];
				std::int32_t axis[2];
				std::int32_t sourceScale;
			};

			constexpr std::uint32_t kGroupSize = 8; // both shaders' local size

			std::uint32_t GroupCount(std::uint32_t size) {
				return (size + kGroupSize - 1) / kGroupSize;
			}

			VkPipeline MakeComputePipeline(VkDevice device, VkPipelineCache cache,
			                               VkPipelineLayout layout, const char* path) {
				const std::vector<std::uint32_t> code = SpirvCache::Load(path);
				VkShaderModuleCreateInfo moduleInfo{};
				moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				moduleInfo.codeSize = code.size() * sizeof(std::uint32_t);
				moduleInfo.pCode = code.data();
				VkShaderModule module;
				if (vkCreateShaderModule(device, &moduleInfo, nullptr, &module) != VK_SUCCESS)
					SPRaise("Failed to create the shader module %s", path);

				VkComputePipelineCreateInfo pipelineInfo{};
				pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
				pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
				pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
				pipelineInfo.stage.module = module;
				pipelineInfo.stage.pName = "main";
				pipelineInfo.layout = layout;
				VkPipeline pipeline;
				const VkResult result =
				  vkCreateComputePipelines(device, cache, 1, &pipelineInfo, nullptr, &pipeline);
				vkDestroyShaderModule(device, module, nullptr);
				if (result != VK_SUCCESS)
					SPRaise("Failed to create the compute pipeline for %s", path);
				return pipeline;
			}

			VkDescriptorSetLayout MakeSetLayout(VkDevice device,
			                                    const std::vector<VkDescriptorType>& types) {
				std::vector<VkDescriptorSetLayoutBinding> bindings(types.size());
				for (std::uint32_t i = 0; i < types.size(); i++)
					bindings[i] = {i, types[i], 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
				VkDescriptorSetLayoutCreateInfo info{};
				info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
				info.bindingCount = static_cast<std::uint32_t>(bindings.size());
				info.pBindings = bindings.data();
				VkDescriptorSetLayout layout;
				if (vkCreateDescriptorSetLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
					SPRaise("Failed to create an ambient occlusion descriptor set layout");
				return layout;
			}

			VkPipelineLayout MakePipelineLayout(VkDevice device, VkDescriptorSetLayout setLayout,
			                                    const VkPushConstantRange& range) {
				VkPipelineLayoutCreateInfo info{};
				info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				info.setLayoutCount = 1;
				info.pSetLayouts = &setLayout;
				info.pushConstantRangeCount = 1;
				info.pPushConstantRanges = &range;
				VkPipelineLayout layout;
				if (vkCreatePipelineLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
					SPRaise("Failed to create an ambient occlusion pipeline layout");
				return layout;
			}

			// Orders the next dispatch after the previous one's writes, and its
			// writes after the previous one's reads
			void ComputeToCompute(VkCommandBuffer commandBuffer) {
				VkMemoryBarrier barrier{};
				barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
				barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
				vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0,
				                     nullptr, 0, nullptr);
			}
		} // namespace

		VulkanScreenSpaceAmbientOcclusion::VulkanScreenSpaceAmbientOcclusion(
		  VulkanRenderer& r, std::size_t framesInFlight)
		    : renderer(r), device(r.GetDevice()->GetDevice()) {
			SPADES_MARK_FUNCTION();

			VkSamplerCreateInfo samplerInfo{};
			samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerInfo.magFilter = VK_FILTER_NEAREST;
			samplerInfo.minFilter = VK_FILTER_NEAREST;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerInfo.maxAnisotropy = 1.0F;
			if (vkCreateSampler(device, &samplerInfo, nullptr, &depthSampler) != VK_SUCCESS)
				SPRaise("Failed to create the ambient occlusion depth sampler");

			CreatePipelines();
			CreateViews(framesInFlight);
			InitializeLayouts();
		}

		VulkanScreenSpaceAmbientOcclusion::~VulkanScreenSpaceAmbientOcclusion() {
			SPADES_MARK_FUNCTION();
			// The sets go with their pool, the images with their handles.
			if (pool != VK_NULL_HANDLE)
				vkDestroyDescriptorPool(device, pool, nullptr);
			for (VkPipeline pipeline : {evaluatePipeline, blurPipeline})
				if (pipeline != VK_NULL_HANDLE)
					vkDestroyPipeline(device, pipeline, nullptr);
			for (VkPipelineLayout layout : {evaluateLayout, blurLayout})
				if (layout != VK_NULL_HANDLE)
					vkDestroyPipelineLayout(device, layout, nullptr);
			for (VkDescriptorSetLayout layout : {evaluateSetLayout, blurSetLayout})
				if (layout != VK_NULL_HANDLE)
					vkDestroyDescriptorSetLayout(device, layout, nullptr);
			if (depthSampler != VK_NULL_HANDLE)
				vkDestroySampler(device, depthSampler, nullptr);
		}

		void VulkanScreenSpaceAmbientOcclusion::CreatePipelines() {
			evaluateSetLayout = MakeSetLayout(device, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			                                           VK_DESCRIPTOR_TYPE_STORAGE_IMAGE});
			blurSetLayout = MakeSetLayout(device, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			                                       VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			                                       VK_DESCRIPTOR_TYPE_STORAGE_IMAGE});
			evaluateLayout =
			  MakePipelineLayout(device, evaluateSetLayout,
			                     PushConstantRange<EvaluateParams>(VK_SHADER_STAGE_COMPUTE_BIT));
			blurLayout = MakePipelineLayout(
			  device, blurSetLayout, PushConstantRange<BlurParams>(VK_SHADER_STAGE_COMPUTE_BIT));

			VkPipelineCache cache = renderer.GetPipelineCache();
			evaluatePipeline = MakeComputePipeline(
			  device, cache, evaluateLayout, "Shaders/Vulkan/AmbientOcclusion/Evaluate.comp.spv");
			blurPipeline = MakeComputePipeline(device, cache, blurLayout,
			                                   "Shaders/Vulkan/AmbientOcclusion/Blur.comp.spv");
		}

		Handle<VulkanImage> VulkanScreenSpaceAmbientOcclusion::MakeImage(std::uint32_t width,
		                                                                 std::uint32_t height,
		                                                                 bool sampled) {
			VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT;
			if (sampled)
				usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
			Handle<VulkanImage> image =
			  Handle<VulkanImage>::New(renderer.GetDevice(), width, height, Format,
			                           VK_IMAGE_TILING_OPTIMAL, usage,
			                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			if (sampled)
				image->CreateSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST,
				                     VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
			return image;
		}

		void VulkanScreenSpaceAmbientOcclusion::CreateViews(std::size_t framesInFlight) {
			VulkanFramebufferManager& framebuffers = *renderer.GetFramebufferManager();
			const std::uint32_t width = static_cast<std::uint32_t>(renderer.GetRenderWidth());
			const std::uint32_t height = static_cast<std::uint32_t>(renderer.GetRenderHeight());

			ping = MakeImage(width, height, false);
			pong = MakeImage(width, height, false);

			// The main view at the render size, unless `r_ssao` 2 asks for less; the
			// mirror's at half, and blurred half as much, as GL's
			ViewResources& main = views[static_cast<std::size_t>(VulkanSceneView::View::Main)];
			main.present = true;
			main.depth = framebuffers.GetResolvedDepthImage().GetPointerOrNull();
			main.scale = (int)renderer.GetSettings().r_ssao >= 2 ? 2 : 1;

			ViewResources& mirror = views[static_cast<std::size_t>(VulkanSceneView::View::Mirror)];
			mirror.present = framebuffers.GetMirrorFramebuffer() != VK_NULL_HANDLE;
			if (mirror.present) {
				mirror.depth = framebuffers.GetMirrorResolvedDepthImage().GetPointerOrNull();
				mirror.scale = 2;
			}

			std::uint32_t setCount = 0;
			for (ViewResources& view : views) {
				if (!view.present)
					continue;
				SPAssert(view.depth);
				view.evaluated = MakeImage((width + view.scale - 1) / view.scale,
				                           (height + view.scale - 1) / view.scale, false);
				for (std::size_t slot = 0; slot < framesInFlight; slot++)
					view.outputs.push_back(MakeImage(width, height, true));
				// The evaluation and the blurs: four along alternate axes for the
				// main view, two for the mirror's
				const std::uint32_t blurs = &view == &main ? 4 : 2;
				setCount += static_cast<std::uint32_t>(framesInFlight) * (1 + blurs);
			}

			const VkDescriptorPoolSize poolSizes[2] = {
			  {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, setCount},
			  {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, setCount * 2}};
			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.maxSets = setCount;
			poolInfo.poolSizeCount = 2;
			poolInfo.pPoolSizes = poolSizes;
			if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
				SPRaise("Failed to create the ambient occlusion descriptor pool");

			for (ViewResources& view : views) {
				if (!view.present)
					continue;
				const bool isMain = &view == &main;
				view.passes.resize(framesInFlight);
				for (std::size_t slot = 0; slot < framesInFlight; slot++) {
					VulkanImage& output = *view.outputs[slot];
					std::vector<Pass>& passes = view.passes[slot];

					Pass evaluate;
					evaluate.set = MakeEvaluateSet(view);
					evaluate.target = view.evaluated.GetPointerOrNull();
					passes.push_back(evaluate);

					// As GL's: vertical first, from the evaluation (upsampling it
					// when at half), then horizontal, again for the main view
					auto blur = [&](VulkanImage& source, VulkanImage& target, int axis,
					                int sourceScale) {
						Pass pass;
						pass.set = MakeBlurSet(view, source, target);
						pass.target = &target;
						pass.axis = axis;
						pass.sourceScale = sourceScale;
						passes.push_back(pass);
					};
					if (isMain) {
						blur(*view.evaluated, *ping, 1, view.scale);
						blur(*ping, *pong, 0, 1);
						blur(*pong, *ping, 1, 1);
						blur(*ping, output, 0, 1);
					} else {
						blur(*view.evaluated, *ping, 1, view.scale);
						blur(*ping, output, 0, 1);
					}
				}
			}
		}

		VkDescriptorSet
		VulkanScreenSpaceAmbientOcclusion::MakeEvaluateSet(const ViewResources& view) {
			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = pool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &evaluateSetLayout;
			VkDescriptorSet set;
			if (vkAllocateDescriptorSets(device, &allocInfo, &set) != VK_SUCCESS)
				SPRaise("Failed to allocate an ambient occlusion descriptor set");

			const VkDescriptorImageInfo depth{depthSampler, view.depth->GetImageView(),
			                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
			const VkDescriptorImageInfo target{VK_NULL_HANDLE, view.evaluated->GetImageView(),
			                                   VK_IMAGE_LAYOUT_GENERAL};
			VkWriteDescriptorSet writes[2]{};
			writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
			             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &depth, nullptr, nullptr};
			writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
			             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &target, nullptr, nullptr};
			vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
			return set;
		}

		VkDescriptorSet VulkanScreenSpaceAmbientOcclusion::MakeBlurSet(const ViewResources& view,
		                                                               VulkanImage& source,
		                                                               VulkanImage& target) {
			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = pool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &blurSetLayout;
			VkDescriptorSet set;
			if (vkAllocateDescriptorSets(device, &allocInfo, &set) != VK_SUCCESS)
				SPRaise("Failed to allocate an ambient occlusion descriptor set");

			const VkDescriptorImageInfo depth{depthSampler, view.depth->GetImageView(),
			                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
			const VkDescriptorImageInfo sourceInfo{VK_NULL_HANDLE, source.GetImageView(),
			                                       VK_IMAGE_LAYOUT_GENERAL};
			const VkDescriptorImageInfo targetInfo{VK_NULL_HANDLE, target.GetImageView(),
			                                       VK_IMAGE_LAYOUT_GENERAL};
			VkWriteDescriptorSet writes[3]{};
			writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
			             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &depth, nullptr, nullptr};
			writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
			             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &sourceInfo, nullptr, nullptr};
			writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1,
			             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &targetInfo, nullptr, nullptr};
			vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
			return set;
		}

		void VulkanScreenSpaceAmbientOcclusion::InitializeLayouts() {
			// The intermediates live in GENERAL; the outputs rest in the layout the
			// lit passes read them in, from before their first frame
			std::vector<VkImageMemoryBarrier> barriers;
			auto transition = [&](VulkanImage& image, VkImageLayout layout,
			                      VkAccessFlags access) {
				VkImageMemoryBarrier barrier{};
				barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				barrier.newLayout = layout;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.image = image.GetImage();
				barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
				barrier.dstAccessMask = access;
				barriers.push_back(barrier);
			};
			transition(*ping, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT);
			transition(*pong, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT);
			for (ViewResources& view : views) {
				if (!view.present)
					continue;
				transition(*view.evaluated, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT);
				for (Handle<VulkanImage>& output : view.outputs)
					transition(*output, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					           VK_ACCESS_SHADER_READ_BIT);
			}

			gui::SDLVulkanDevice& vulkanDevice = *renderer.GetDevice();
			VkCommandBufferAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandPool = vulkanDevice.GetCommandPool();
			allocInfo.commandBufferCount = 1;
			VkCommandBuffer commandBuffer;
			if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS)
				SPRaise("Failed to allocate the ambient occlusion layout command buffer");
			VkCommandBufferBeginInfo beginInfo{};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			vkBeginCommandBuffer(commandBuffer, &beginInfo);
			vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
			                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                     0, 0, nullptr, 0, nullptr, static_cast<std::uint32_t>(barriers.size()),
			                     barriers.data());
			vkEndCommandBuffer(commandBuffer);
			VkSubmitInfo submitInfo{};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffer;
			vkQueueSubmit(vulkanDevice.GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
			vkQueueWaitIdle(vulkanDevice.GetGraphicsQueue());
			vkFreeCommandBuffers(device, vulkanDevice.GetCommandPool(), 1, &commandBuffer);
		}

		bool VulkanScreenSpaceAmbientOcclusion::HasView(VulkanSceneView::View view) const {
			return views[static_cast<std::size_t>(view)].present;
		}

		VulkanImage& VulkanScreenSpaceAmbientOcclusion::GetOutput(std::size_t frameSlot,
		                                                          VulkanSceneView::View view) {
			ViewResources& resources = views[static_cast<std::size_t>(view)];
			SPAssert(resources.present && frameSlot < resources.outputs.size());
			return *resources.outputs[frameSlot];
		}

		void VulkanScreenSpaceAmbientOcclusion::Render(VkCommandBuffer commandBuffer,
		                                               std::size_t frameSlot,
		                                               VulkanSceneView::View view,
		                                               const client::SceneDefinition& sceneDef) {
			SPADES_MARK_FUNCTION();

			ViewResources& resources = views[static_cast<std::size_t>(view)];
			SPAssert(resources.present && frameSlot < resources.passes.size());
			VulkanImage& output = *resources.outputs[frameSlot];

			// The intermediates were last written and read by an earlier view or
			// frame; the output was last read by this frame slot's lit pass, and
			// its contents go
			ComputeToCompute(commandBuffer);
			{
				VkImageMemoryBarrier toGeneral{};
				toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				toGeneral.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
				toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toGeneral.image = output.GetImage();
				toGeneral.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
				toGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
				                     nullptr, 1, &toGeneral);
			}

			const float zNearFar[2] = {sceneDef.zNear, sceneDef.zFar};
			const std::vector<Pass>& passes = resources.passes[frameSlot];

			// The evaluation
			{
				const Pass& pass = passes.front();
				const std::uint32_t width = pass.target->GetWidth();
				const std::uint32_t height = pass.target->GetHeight();
				// GL's kernel: 0.18% of the target's smaller side, a texel at least
				const float kernel =
				  std::max(1.0F, static_cast<float>(std::min(width, height)) * 0.0018F);
				EvaluateParams params{};
				params.zNearFar[0] = zNearFar[0];
				params.zNearFar[1] = zNearFar[1];
				params.tanHalfFov[0] = std::tan(sceneDef.fovX * 0.5F);
				params.tanHalfFov[1] = std::tan(sceneDef.fovY * 0.5F);
				params.sampleRadius = kernel * static_cast<float>(resources.scale);
				params.scale = resources.scale;

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, evaluatePipeline);
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
				                        evaluateLayout, 0, 1, &pass.set, 0, nullptr);
				PushConstants(commandBuffer, evaluateLayout, VK_SHADER_STAGE_COMPUTE_BIT, params);
				vkCmdDispatch(commandBuffer, GroupCount(width), GroupCount(height), 1);
			}

			// The blurs, each after the one before
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, blurPipeline);
			for (std::size_t i = 1; i < passes.size(); i++) {
				const Pass& pass = passes[i];
				ComputeToCompute(commandBuffer);
				BlurParams params{};
				params.zNearFar[0] = zNearFar[0];
				params.zNearFar[1] = zNearFar[1];
				params.axis[0] = pass.axis == 0 ? 1 : 0;
				params.axis[1] = pass.axis == 0 ? 0 : 1;
				params.sourceScale = pass.sourceScale;
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, blurLayout,
				                        0, 1, &pass.set, 0, nullptr);
				PushConstants(commandBuffer, blurLayout, VK_SHADER_STAGE_COMPUTE_BIT, params);
				vkCmdDispatch(commandBuffer, GroupCount(pass.target->GetWidth()),
				              GroupCount(pass.target->GetHeight()), 1);
			}

			// For the lit pass to read
			VkImageMemoryBarrier toRead{};
			toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			toRead.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toRead.image = output.GetImage();
			toRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			toRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
			                     1, &toRead);
		}
	} // namespace draw
} // namespace spades
