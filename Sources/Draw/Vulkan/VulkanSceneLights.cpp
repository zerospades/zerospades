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

#include "VulkanSceneLights.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "VulkanBuffer.h"
#include "VulkanDynamicLight.h"
#include "VulkanImage.h"
#include "VulkanImageWrapper.h"
#include "VulkanMapOccupancy.h"
#include "VulkanRenderer.h"
#include "VulkanSpirvCache.h"
#include <Client/SceneDefinition.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Gui/SDLVulkanDevice.h>

namespace spades {
	namespace draw {
		namespace {
			/** `DynamicLight` of `SceneLight/Table.glsl`, std430 */
			struct GpuLight {
				float originReach[4];
				float colorReachInversed[4];
				float spotMatrix[16];
				float linearDirectionLength[4];
				float kind[4];
				float viewSphere[4];
				float traceRight[4];
				float traceUp[4];
				float traceForward[4];
			};
			static_assert(sizeof(GpuLight) == 192, "GpuLight must match DynamicLight");

			/** `DynamicLightFrame` of `SceneLight/Table.glsl`, std140 */
			struct GpuFrame {
				float eyeNear[4];
				float right[4];
				float up[4];
				float forward[4];
				float tangents[2];
				float mapOcclusion;
				float firstPersonDepthEnd;
				std::uint32_t counts[4];
				std::uint32_t occlusionTileLights[VulkanSceneLights::MaxOcclusionTiles];
			};
			static_assert(sizeof(GpuFrame) == 96 + 4 * VulkanSceneLights::MaxOcclusionTiles,
			              "GpuFrame must match DynamicLightFrame");

			/** `SceneSunSky` of `SceneLight/SunSky.glsl`, std140 */
			struct GpuSunSky {
				float sunlight;
				float daylight;
				float pad[2];
				float skyLight[4];
				float ambientLight[3];
				float pad2; // to the block's std140 size, a multiple of 16
			};
			static_assert(sizeof(GpuSunSky) == 48, "GpuSunSky must match SceneSunSky");

			/**
			 * The widest a texel of a light's occlusion map may get at its reach, in
			 * blocks: its width is how far off an edge of the beam's shadow can land.
			 */
			constexpr float kMaxOcclusionTexelWidth = 1.5F;

			/** `kind.x` of a light, `DYNAMIC_LIGHT_POINT`, `_LINEAR` or `_SPOT` */
			float KindOf(client::DynamicLightType type) {
				switch (type) {
					case client::DynamicLightTypeLinear: return 1.0F;
					case client::DynamicLightTypeSpotlight: return 2.0F;
					case client::DynamicLightTypePoint: break;
				}
				return 0.0F;
			}

			constexpr std::uint32_t kClusterCount =
			  VulkanSceneLights::ClustersX * VulkanSceneLights::ClustersY *
			  VulkanSceneLights::ClustersZ;
			constexpr std::uint32_t kMaskWords = VulkanSceneLights::MaxLights / 32;

			void Store(float (&out)[4], const Vector3& v, float w) {
				out[0] = v.x;
				out[1] = v.y;
				out[2] = v.z;
				out[3] = w;
			}
		} // namespace

		VulkanSceneLights::VulkanSceneLights(VulkanRenderer& renderer,
		                                                       std::size_t framesInFlight)
		    : renderer(renderer), device(renderer.GetDevice()->GetDevice()) {
			SPADES_MARK_FUNCTION();

			const VkShaderStageFlags stages =
			  VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			std::array<VkDescriptorSetLayoutBinding, 7> bindings{};
			bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, stages, nullptr};
			bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages, nullptr};
			bindings[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages, nullptr};
			bindings[3] = {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MaxImages,
			               VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
			bindings[4] = {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, stages, nullptr};
			bindings[5] = {5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, stages, nullptr};
			bindings[6] = {6, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
			               nullptr};

			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
			layoutInfo.pBindings = bindings.data();
			if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &setLayout) != VK_SUCCESS)
				SPRaise("Failed to create the dynamic light descriptor set layout");

			VkDescriptorSetLayoutCreateInfo emptyInfo{};
			emptyInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			if (vkCreateDescriptorSetLayout(device, &emptyInfo, nullptr, &emptySetLayout) !=
			    VK_SUCCESS)
				SPRaise("Failed to create the empty descriptor set layout");

			const std::uint32_t slotCount = static_cast<std::uint32_t>(framesInFlight);
			std::array<VkDescriptorPoolSize, 4> poolSizes{{
			  {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, slotCount * 2},
			  {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, slotCount * 2},
			  {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, slotCount * (MaxImages + 1)},
			  {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, slotCount},
			}};
			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.maxSets = slotCount;
			poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
			poolInfo.pPoolSizes = poolSizes.data();
			if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
				SPRaise("Failed to create the dynamic light descriptor pool");

			noOccupancy.reset(new VulkanMapOccupancy(renderer));

			slots.resize(framesInFlight);
			for (Slot& slot : slots)
				CreateSlot(slot);

			CreatePipelines();
		}

		VulkanSceneLights::~VulkanSceneLights() {
			SPADES_MARK_FUNCTION();

			for (Slot& slot : slots) {
				if (slot.occlusionAtlasView != VK_NULL_HANDLE)
					vkDestroyImageView(device, slot.occlusionAtlasView, nullptr);
				if (slot.occlusionAtlas != VK_NULL_HANDLE)
					vmaDestroyImage(renderer.GetDevice()->GetAllocator(), slot.occlusionAtlas,
					                slot.occlusionAtlasAllocation);
			}
			if (occlusionPipeline != VK_NULL_HANDLE)
				vkDestroyPipeline(device, occlusionPipeline, nullptr);
			if (pipeline != VK_NULL_HANDLE)
				vkDestroyPipeline(device, pipeline, nullptr);
			if (pipelineLayout != VK_NULL_HANDLE)
				vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			// The sets go with their pool.
			if (pool != VK_NULL_HANDLE)
				vkDestroyDescriptorPool(device, pool, nullptr);
			if (emptySetLayout != VK_NULL_HANDLE)
				vkDestroyDescriptorSetLayout(device, emptySetLayout, nullptr);
			if (setLayout != VK_NULL_HANDLE)
				vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
		}

		void VulkanSceneLights::CreateSlot(Slot& slot) {
			Handle<gui::SDLVulkanDevice> vulkanDevice = renderer.GetDevice();
			const VkMemoryPropertyFlags hostVisible =
			  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

			// Written by the CPU every frame and mapped for good
			slot.frame = Handle<VulkanBuffer>::New(vulkanDevice, sizeof(GpuFrame),
			                                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, hostVisible);
			slot.sunSky = Handle<VulkanBuffer>::New(vulkanDevice, sizeof(GpuSunSky),
			                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, hostVisible);
			slot.lights = Handle<VulkanBuffer>::New(vulkanDevice, sizeof(GpuLight) * MaxLights,
			                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, hostVisible);
			// Written by the binning pass, read by the fragments
			slot.clusters = Handle<VulkanBuffer>::New(
			  vulkanDevice, sizeof(std::uint32_t) * kMaskWords * kClusterCount,
			  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = pool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &setLayout;
			if (vkAllocateDescriptorSets(device, &allocInfo, &slot.set) != VK_SUCCESS)
				SPRaise("Failed to allocate a dynamic light descriptor set");

			struct BufferBinding {
				std::uint32_t binding;
				VkDescriptorType type;
				VulkanBuffer* buffer;
			};
			const std::array<BufferBinding, 4> bufferBindings{{
			  {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, slot.frame.GetPointerOrNull()},
			  {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, slot.lights.GetPointerOrNull()},
			  {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, slot.clusters.GetPointerOrNull()},
			  {6, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, slot.sunSky.GetPointerOrNull()},
			}};
			std::array<VkDescriptorBufferInfo, bufferBindings.size()> buffers{};
			std::array<VkWriteDescriptorSet, bufferBindings.size()> writes{};
			for (std::size_t i = 0; i < writes.size(); i++) {
				buffers[i] = {bufferBindings[i].buffer->GetBuffer(), 0, VK_WHOLE_SIZE};
				writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[i].dstSet = slot.set;
				writes[i].dstBinding = bufferBindings[i].binding;
				writes[i].descriptorCount = 1;
				writes[i].descriptorType = bufferBindings[i].type;
				writes[i].pBufferInfo = &buffers[i];
			}
			vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()),
			                       writes.data(), 0, nullptr);

			// A frame without spotlights still reads a valid image in each place, and
			// one without a map a valid occupancy.
			BindImages(slot, {});
			BindOccupancy(slot, *noOccupancy);
			CreateOcclusionAtlas(slot);

			// Nothing is lit until the first update says otherwise.
			const GpuFrame empty{};
			std::memcpy(slot.frame->Map(), &empty, sizeof(empty));
			const GpuSunSky dark{};
			std::memcpy(slot.sunSky->Map(), &dark, sizeof(dark));
		}

		void VulkanSceneLights::CreatePipelines() {
			VkPipelineLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layoutInfo.setLayoutCount = 1;
			layoutInfo.pSetLayouts = &setLayout;
			if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
				SPRaise("Failed to create the dynamic light compute pipeline layout");

			pipeline = CreateComputePipeline("Shaders/Vulkan/SceneLight/Cluster.comp.spv");
			occlusionPipeline =
			  CreateComputePipeline("Shaders/Vulkan/SceneLight/OcclusionMap.comp.spv");
		}

		VkPipeline VulkanSceneLights::CreateComputePipeline(const char* shaderPath) {
			const std::vector<std::uint32_t> code = SpirvCache::Load(shaderPath);
			VkShaderModuleCreateInfo moduleInfo{};
			moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			moduleInfo.codeSize = code.size() * sizeof(std::uint32_t);
			moduleInfo.pCode = code.data();
			VkShaderModule module;
			if (vkCreateShaderModule(device, &moduleInfo, nullptr, &module) != VK_SUCCESS)
				SPRaise("Failed to create the shader module %s", shaderPath);

			VkComputePipelineCreateInfo pipelineInfo{};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
			pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
			pipelineInfo.stage.module = module;
			pipelineInfo.stage.pName = "main";
			pipelineInfo.layout = pipelineLayout;
			VkPipeline created = VK_NULL_HANDLE;
			const VkResult result = vkCreateComputePipelines(device, renderer.GetPipelineCache(), 1,
			                                                 &pipelineInfo, nullptr, &created);
			vkDestroyShaderModule(device, module, nullptr);
			if (result != VK_SUCCESS)
				SPRaise("Failed to create the compute pipeline of %s (error code: %d)", shaderPath,
				        result);
			return created;
		}

		void VulkanSceneLights::CreateOcclusionAtlas(Slot& slot) {
			VkImageCreateInfo imageInfo{};
			imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageInfo.imageType = VK_IMAGE_TYPE_2D;
			imageInfo.format = VK_FORMAT_R32_SFLOAT;
			imageInfo.extent = {OcclusionAtlasSize, OcclusionAtlasSize, 1};
			imageInfo.mipLevels = 1;
			imageInfo.arrayLayers = 1;
			imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			// Written by the tracing pass and read by the lit fragments, as storage
			imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT;
			imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			VmaAllocationCreateInfo allocInfo{};
			allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
			if (vmaCreateImage(renderer.GetDevice()->GetAllocator(), &imageInfo, &allocInfo,
			                   &slot.occlusionAtlas, &slot.occlusionAtlasAllocation,
			                   nullptr) != VK_SUCCESS)
				SPRaise("Failed to create a dynamic light occlusion atlas");

			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = slot.occlusionAtlas;
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = VK_FORMAT_R32_SFLOAT;
			viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			if (vkCreateImageView(device, &viewInfo, nullptr, &slot.occlusionAtlasView) !=
			    VK_SUCCESS)
				SPRaise("Failed to create a dynamic light occlusion atlas view");

			const VkDescriptorImageInfo info{VK_NULL_HANDLE, slot.occlusionAtlasView,
			                                 VK_IMAGE_LAYOUT_GENERAL};
			VkWriteDescriptorSet write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = slot.set;
			write.dstBinding = 5;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			write.pImageInfo = &info;
			vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		}

		float VulkanSceneLights::GetOcclusionTexelSpread(const VulkanDynamicLight& light) {
			return 2.0F * light.GetSpotTangent() * VulkanDynamicLight::SpotFadeEnd /
			       (float)OcclusionTileSize;
		}

		bool VulkanSceneLights::IsOcclusionMappable(const VulkanDynamicLight& light) {
			const client::DynamicLightParam& param = light.GetParam();
			if (param.type != client::DynamicLightTypeSpotlight)
				return false;
			return param.radius * GetOcclusionTexelSpread(light) <= kMaxOcclusionTexelWidth;
		}

		VkDescriptorSet VulkanSceneLights::GetDescriptorSet(std::size_t frameSlot) const {
			SPAssert(frameSlot < slots.size());
			return slots[frameSlot].set;
		}

		void VulkanSceneLights::BindImages(
		  Slot& slot, const std::array<VulkanImage*, MaxImages>& images) {
			VulkanImage* white = renderer.GetWhiteImage();
			SPAssert(white);

			std::array<VkDescriptorImageInfo, MaxImages> infos{};
			std::uint32_t first = MaxImages, end = 0;
			for (std::uint32_t i = 0; i < MaxImages; i++) {
				VulkanImage* image = images[i] ? images[i] : white;
				infos[i] = {image->GetSampler(), image->GetImageView(),
				            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
				if (slot.images[i].GetPointerOrNull() == image)
					continue;
				slot.images[i] = Handle<VulkanImage>(image);
				first = std::min(first, i);
				end = i + 1;
			}
			if (first >= end)
				return;

			VkWriteDescriptorSet write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = slot.set;
			write.dstBinding = 3;
			write.dstArrayElement = first;
			write.descriptorCount = end - first;
			write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write.pImageInfo = infos.data() + first;
			vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		}

		void VulkanSceneLights::BindOccupancy(Slot& slot,
		                                               const VulkanMapOccupancy& occupancy) {
			const VkDescriptorImageInfo info{occupancy.GetSampler(), occupancy.GetImageView(),
			                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
			VkWriteDescriptorSet write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = slot.set;
			write.dstBinding = 4;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write.pImageInfo = &info;
			vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		}

		void VulkanSceneLights::Update(VkCommandBuffer commandBuffer,
		                                        std::size_t frameSlot,
		                                        const std::vector<VulkanDynamicLight>& lights,
		                                        const client::SceneDefinition& view,
		                                        const VulkanMapOccupancy* occupancy,
		                                        const SunSky& sunSky) {
			SPADES_MARK_FUNCTION();
			SPAssert(frameSlot < slots.size());
			Slot& slot = slots[frameSlot];

			const std::uint32_t count =
			  static_cast<std::uint32_t>(std::min<std::size_t>(lights.size(), MaxLights));

			// The atlas is laid out once for the passes to write and read it as they
			// like; a frame only reads the tiles it traced itself.
			if (!slot.occlusionAtlasInitialized) {
				VkImageMemoryBarrier layout{};
				layout.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				layout.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				layout.newLayout = VK_IMAGE_LAYOUT_GENERAL;
				layout.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				layout.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				layout.image = slot.occlusionAtlas;
				layout.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
				layout.srcAccessMask = 0;
				layout.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
				vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
				                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				                     0, 0, nullptr, 0, nullptr, 1, &layout);
				slot.occlusionAtlasInitialized = true;
			}

			GpuFrame frame{};

			// The table, the images its spotlights project, and the tiles of the
			// occlusion maps, which only a map has
			std::uint32_t tileCount = 0;
			std::array<VulkanImage*, MaxImages> images{};
			std::uint32_t imageCount = 0;
			auto* table = static_cast<GpuLight*>(slot.lights->Map());
			for (std::uint32_t i = 0; i < count; i++) {
				const VulkanDynamicLight& light = lights[i];
				const client::DynamicLightParam& param = light.GetParam();
				GpuLight& out = table[i];

				Store(out.originReach, param.origin, param.radius);
				Store(out.colorReachInversed, param.color, 1.0F / param.radius);

				const Matrix4 spotMatrix = param.type == client::DynamicLightTypeSpotlight
				                             ? light.GetProjectionMatrix()
				                             : Matrix4::Identity();
				std::memcpy(out.spotMatrix, spotMatrix.m, sizeof(out.spotMatrix));

				Vector3 direction = MakeVector3(0.0F, 0.0F, 0.0F);
				float length = 0.0F;
				if (param.type == client::DynamicLightTypeLinear) {
					direction = param.point2 - param.origin;
					length = direction.GetLength();
					direction = length > 1.0e-4F ? direction / length : MakeVector3(0.0F, 0.0F, 0.0F);
				}
				Store(out.linearDirectionLength, direction, length);

				float image = -1.0F;
				auto* wrapper = dynamic_cast<VulkanImageWrapper*>(param.image);
				VulkanImage* vulkanImage = wrapper ? wrapper->GetVulkanImage() : nullptr;
				if (param.type == client::DynamicLightTypeSpotlight && vulkanImage) {
					const auto found = std::find(images.begin(), images.begin() + imageCount,
					                             vulkanImage);
					if (found != images.begin() + imageCount) {
						image = static_cast<float>(found - images.begin());
					} else if (imageCount < MaxImages) {
						images[imageCount] = vulkanImage;
						image = static_cast<float>(imageCount++);
					}
				}
				out.kind[0] = KindOf(param.type);
				out.kind[1] = image;
				out.kind[2] = -1.0F;
				out.kind[3] = 0.0F;

				if (param.type == client::DynamicLightTypeSpotlight) {
					const float spread = GetOcclusionTexelSpread(light);
					Store(out.traceRight, param.spotAxis[0].Normalize(), spread);
					Store(out.traceUp, param.spotAxis[1].Normalize(), 0.0F);
					Store(out.traceForward, param.spotAxis[2].Normalize(), 0.0F);
					if (occupancy && tileCount < MaxOcclusionTiles && IsOcclusionMappable(light)) {
						out.kind[2] = static_cast<float>(tileCount);
						frame.occlusionTileLights[tileCount++] = i;
					}
				} else {
					const Vector3 none = MakeVector3(0.0F, 0.0F, 0.0F);
					Store(out.traceRight, none, 0.0F);
					Store(out.traceUp, none, 0.0F);
					Store(out.traceForward, none, 0.0F);
				}

				Vector3 center;
				float radius;
				light.GetBoundingSphere(center, radius);
				const Vector3 relative = center - view.viewOrigin;
				Store(out.viewSphere,
				      MakeVector3(Vector3::Dot(relative, view.viewAxis[0]),
				                  Vector3::Dot(relative, view.viewAxis[1]),
				                  Vector3::Dot(relative, view.viewAxis[2])),
				      radius);
			}
			BindImages(slot, images);

			// The map's occupancy, written anew every frame: a new map's image may
			// come with the handles its predecessor's had.
			BindOccupancy(slot, occupancy ? *occupancy : *noOccupancy);

			// How the clusters cut the view up
			const float tanX = std::tan(view.fovX * 0.5F);
			const float tanY = std::tan(view.fovY * 0.5F);
			const float far = std::max(view.zFar, ClusterNear * 2.0F);
			Store(frame.eyeNear, view.viewOrigin, ClusterNear);
			Store(frame.right, view.viewAxis[0], 1.0F / tanX);
			Store(frame.up, view.viewAxis[1], 1.0F / tanY);
			Store(frame.forward, view.viewAxis[2],
			      static_cast<float>(ClustersZ) / std::log(far / ClusterNear));
			frame.tangents[0] = tanX;
			frame.tangents[1] = tanY;
			frame.mapOcclusion = occupancy ? 1.0F : 0.0F;
			frame.firstPersonDepthEnd = VulkanRenderer::kFirstPersonDepthEnd;
			frame.counts[0] = ClustersX;
			frame.counts[1] = ClustersY;
			frame.counts[2] = ClustersZ;
			frame.counts[3] = count;
			std::memcpy(slot.frame->Map(), &frame, sizeof(frame));

			GpuSunSky sky{};
			sky.sunlight = sunSky.sunlight;
			sky.daylight = sunSky.daylight;
			Store(sky.skyLight, sunSky.skyLight, 0.0F);
			sky.ambientLight[0] = sunSky.ambientLight.x;
			sky.ambientLight[1] = sunSky.ambientLight.y;
			sky.ambientLight[2] = sunSky.ambientLight.z;
			std::memcpy(slot.sunSky->Map(), &sky, sizeof(sky));

			if (count == 0)
				return;

			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout,
			                        0, 1, &slot.set, 0, nullptr);

			// Neither pass reads what the other writes, so they run side by side.
			if (tileCount > 0) {
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, occlusionPipeline);
				vkCmdDispatch(commandBuffer, OcclusionTileSize / OcclusionGroupSize,
				              OcclusionTileSize / OcclusionGroupSize, tileCount);
			}
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
			vkCmdDispatch(commandBuffer, (kClusterCount + ClusterGroupSize - 1) / ClusterGroupSize,
			              1, 1);

			// The masks and the occlusion maps are read by the lit fragments of the
			// passes that follow.
			VkMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
			barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &barrier, 0, nullptr,
			                     0, nullptr);
		}
	} // namespace draw
} // namespace spades
