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

#include "VulkanSpriteRenderer.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "../SW/SWFeatureLevel.h" // for fastRcp
#include "VulkanBuffer.h"
#include "VulkanFramebufferManager.h"
#include "VulkanImage.h"
#include "VulkanMapRenderer.h"
#include "VulkanRenderer.h"
#include "VulkanSceneLights.h"
#include "VulkanShadowMapRenderer.h"
#include "VulkanSpirvCache.h"
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/Settings.h>
#include <Gui/SDLVulkanDevice.h>

SPADES_SETTING(r_softParticles);

namespace spades {
	namespace draw {
		namespace {
			/** `SpriteView` of `SpriteView.glsl`, std140 */
			struct GpuView {
				float projectionView[16];
				float right[4];
				float up[4];
				float front[4];
				float eye[4];
				float fogColorDistance[4];
				float nearFar[4];
				float sunDirection[4];
			};
			static_assert(sizeof(GpuView) == 176, "GpuView must match SpriteView");

			/** The fewest sprites a frame's instance buffer is made for */
			constexpr std::size_t kInitialInstanceCapacity = 1024;

			/** The most images a frame's sprites use, a descriptor set each */
			constexpr std::uint32_t kMaxImagesPerFrame = 1024;

			/** The vertices of a sprite's quad, as a triangle strip */
			constexpr std::uint32_t kQuadVertices = 4;

			void Store(float (&out)[4], const Vector3& v, float w) {
				out[0] = v.x;
				out[1] = v.y;
				out[2] = v.z;
				out[3] = w;
			}

			VkShaderModule CreateModule(VkDevice device, const char* path) {
				const std::vector<std::uint32_t> code = SpirvCache::Load(path);
				VkShaderModuleCreateInfo info{};
				info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				info.codeSize = code.size() * sizeof(std::uint32_t);
				info.pCode = code.data();
				VkShaderModule module;
				if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS)
					SPRaise("Failed to create shader module: %s", path);
				return module;
			}
		} // namespace

		VulkanSpriteRenderer::VulkanSpriteRenderer(VulkanRenderer& r)
		    : renderer(r),
		      device(r.GetDevice()),
		      softParticles((int)r_softParticles != 0),
		      litParticles((int)r_softParticles >= 2) {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			// The sprite's set: its view, its image, and the scene's depth to fade into
			const VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			std::array<VkDescriptorSetLayoutBinding, 3> bindings{{
			  {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, stages, nullptr},
			  {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
			  {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
			}};
			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			// Sprites drawn in the scene's pass can't read the depth it is testing.
			layoutInfo.bindingCount = softParticles ? 3 : 2;
			layoutInfo.pBindings = bindings.data();
			if (vkCreateDescriptorSetLayout(vkDevice, &layoutInfo, nullptr, &spriteSetLayout) !=
			    VK_SUCCESS)
				SPRaise("Failed to create the sprite descriptor set layout");

			frames.resize(device->GetMaxFramesInFlight());
			for (FrameResources& frame : frames)
				CreateFrameResources(frame);
		}

		VulkanSpriteRenderer::~VulkanSpriteRenderer() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();
			for (FrameResources& frame : frames)
				if (frame.descriptorPool != VK_NULL_HANDLE)
					vkDestroyDescriptorPool(vkDevice, frame.descriptorPool, nullptr);
			for (VkPipeline built : {pipeline, litPipeline})
				if (built != VK_NULL_HANDLE)
					vkDestroyPipeline(vkDevice, built, nullptr);
			for (VkPipelineLayout layout : {pipelineLayout, litPipelineLayout})
				if (layout != VK_NULL_HANDLE)
					vkDestroyPipelineLayout(vkDevice, layout, nullptr);
			if (spriteSetLayout != VK_NULL_HANDLE)
				vkDestroyDescriptorSetLayout(vkDevice, spriteSetLayout, nullptr);
		}

		void VulkanSpriteRenderer::CreateFrameResources(FrameResources& frame) {
			const VkMemoryPropertyFlags hostVisible =
			  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
			frame.view = Handle<VulkanBuffer>::New(device, sizeof(GpuView),
			                                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, hostVisible);
			frame.view->Map();
			ReserveInstances(frame, kInitialInstanceCapacity);

			std::array<VkDescriptorPoolSize, 2> poolSizes{{
			  {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxImagesPerFrame},
			  {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxImagesPerFrame * 2},
			}};
			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.maxSets = kMaxImagesPerFrame;
			poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
			poolInfo.pPoolSizes = poolSizes.data();
			if (vkCreateDescriptorPool(device->GetDevice(), &poolInfo, nullptr,
			                           &frame.descriptorPool) != VK_SUCCESS)
				SPRaise("Failed to create a sprite descriptor pool");
		}

		void VulkanSpriteRenderer::ReserveInstances(FrameResources& frame, std::size_t count) {
			if (count <= frame.instanceCapacity)
				return;

			// Twice as many as asked for, not to grow again for every few more
			std::size_t capacity = std::max(frame.instanceCapacity, kInitialInstanceCapacity);
			while (capacity < count)
				capacity *= 2;

			// The frame's previous buffer is done with: its fence was waited for.
			frame.instances = Handle<VulkanBuffer>::New(
			  device, sizeof(Instance) * capacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			frame.instances->Map();
			frame.instanceCapacity = capacity;
		}

		VkPipeline VulkanSpriteRenderer::BuildPipeline(VkPipelineLayout layout,
		                                               const char* vertexShader,
		                                               const char* fragmentShader,
		                                               bool specializeRadiosity) {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();
			VkShaderModule vertexModule = CreateModule(vkDevice, vertexShader);
			VkShaderModule fragmentModule = CreateModule(vkDevice, fragmentShader);

			std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
			shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			shaderStages[0].module = vertexModule;
			shaderStages[0].pName = "main";
			shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			shaderStages[1].module = fragmentModule;
			shaderStages[1].pName = "main";

			// The lit sprites take the map's ambient light as its shaders do.
			SPADES_SETTING(r_radiosity);
			const std::int32_t useRadiosity = (int)r_radiosity != 0 ? 1 : 0;
			const VkSpecializationMapEntry radiosityEntry{0, 0, sizeof(useRadiosity)};
			VkSpecializationInfo specialization{};
			specialization.mapEntryCount = 1;
			specialization.pMapEntries = &radiosityEntry;
			specialization.dataSize = sizeof(useRadiosity);
			specialization.pData = &useRadiosity;
			if (specializeRadiosity)
				shaderStages[1].pSpecializationInfo = &specialization;

			// A sprite per instance; the vertices are the quad's corners.
			VkVertexInputBindingDescription bindingDescription{};
			bindingDescription.binding = 0;
			bindingDescription.stride = sizeof(Instance);
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

			std::array<VkVertexInputAttributeDescription, 4> attributes{{
			  {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, centerRadius)},
			  {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, color)},
			  {2, 0, VK_FORMAT_R32_SFLOAT, offsetof(Instance, angle)},
			  {3, 0, VK_FORMAT_R32_SFLOAT, offsetof(Instance, scattering)},
			}};

			VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
			vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputInfo.vertexBindingDescriptionCount = 1;
			vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
			vertexInputInfo.vertexAttributeDescriptionCount =
			  static_cast<std::uint32_t>(attributes.size());
			vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

			VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
			inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

			VkPipelineViewportStateCreateInfo viewportState{};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.scissorCount = 1;

			VkPipelineRasterizationStateCreateInfo rasterizer{};
			rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = VK_CULL_MODE_NONE; // Y-flip viewport inverts winding; disable culling like LongSpriteRenderer
			rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

			VkPipelineMultisampleStateCreateInfo multisampling{};
			multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			// Match the scene render pass sample count (MSAA).
			multisampling.rasterizationSamples = device->GetSampleCount();

			VkPipelineDepthStencilStateCreateInfo depthStencil{};
			depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			if (softParticles) {
				// No depth attachment in sprite render pass; depth test done in shader
				depthStencil.depthTestEnable = VK_FALSE;
				depthStencil.depthWriteEnable = VK_FALSE;
			} else {
				depthStencil.depthTestEnable = VK_TRUE;
				depthStencil.depthWriteEnable = VK_FALSE;
				depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
			}

			// Premultiplied alpha
			VkPipelineColorBlendAttachmentState colorBlendAttachment{};
			colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			colorBlendAttachment.blendEnable = VK_TRUE;
			colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
			colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
			colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

			VkPipelineColorBlendStateCreateInfo colorBlending{};
			colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlending.attachmentCount = 1;
			colorBlending.pAttachments = &colorBlendAttachment;

			const std::array<VkDynamicState, 2> dynamicStates{
			  {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}};
			VkPipelineDynamicStateCreateInfo dynamicState{};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
			dynamicState.pDynamicStates = dynamicStates.data();

			VkGraphicsPipelineCreateInfo pipelineInfo{};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = static_cast<std::uint32_t>(shaderStages.size());
			pipelineInfo.pStages = shaderStages.data();
			pipelineInfo.pVertexInputState = &vertexInputInfo;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterizer;
			pipelineInfo.pMultisampleState = &multisampling;
			pipelineInfo.pDepthStencilState = &depthStencil;
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.pDynamicState = &dynamicState;
			pipelineInfo.layout = layout;
			pipelineInfo.renderPass = softParticles
			                            ? renderer.GetFramebufferManager()->GetSpriteRenderPass()
			                            : renderer.GetOffscreenRenderPass();
			pipelineInfo.subpass = 0;

			VkPipeline built = VK_NULL_HANDLE;
			const VkResult result = vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(),
			                                                  1, &pipelineInfo, nullptr, &built);
			vkDestroyShaderModule(vkDevice, vertexModule, nullptr);
			vkDestroyShaderModule(vkDevice, fragmentModule, nullptr);
			if (result != VK_SUCCESS)
				SPRaise("Failed to create the sprite pipeline (error code: %d)", result);
			return built;
		}


		void VulkanSpriteRenderer::CreatePipeline() {
			SPADES_MARK_FUNCTION();

			// These sprites are lit by the scene's lights alone (set 2), at their
			// corners; the map's and the model shadows' sets are of no use to them.
			const VkDescriptorSetLayout empty = renderer.GetSceneLights().GetEmptySetLayout();
			const std::array<VkDescriptorSetLayout, SpriteSet + 1> setLayouts{
			  {empty, empty, renderer.GetSceneLights().GetSetLayout(), spriteSetLayout}};
			VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
			pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
			pipelineLayoutInfo.pSetLayouts = setLayouts.data();
			if (vkCreatePipelineLayout(device->GetDevice(), &pipelineLayoutInfo, nullptr,
			                           &pipelineLayout) != VK_SUCCESS)
				SPRaise("Failed to create the sprite pipeline layout");

			pipeline = softParticles
			             ? BuildPipeline(pipelineLayout, "Shaders/Vulkan/SoftSprite.vert.spv",
			                             "Shaders/Vulkan/SoftSprite.frag.spv", false)
			             : BuildPipeline(pipelineLayout, "Shaders/Vulkan/Sprite.vert.spv",
			                             "Shaders/Vulkan/Sprite.frag.spv", false);
		}

		bool VulkanSpriteRenderer::PrepareLitPipeline() {
			VulkanMapRenderer* mapRenderer = renderer.GetMapRenderer();
			VulkanShadowMapRenderer* shadowMapRenderer = renderer.GetShadowMapRenderer();
			if (!mapRenderer || !shadowMapRenderer ||
			    mapRenderer->GetShadowDescriptorSet() == VK_NULL_HANDLE ||
			    shadowMapRenderer->GetSamplingDescriptorSet() == VK_NULL_HANDLE)
				return false;
			if (litPipeline != VK_NULL_HANDLE)
				return true;

			// The lit pipelines' sets, then the sprite's own. A later map's sets are
			// laid out the same, so they still bind to it.
			const std::array<VkDescriptorSetLayout, SpriteSet + 1> setLayouts{
			  {mapRenderer->GetShadowDescriptorSetLayout(),
			   shadowMapRenderer->GetSamplingSetLayout(), renderer.GetSceneLights().GetSetLayout(),
			   spriteSetLayout}};
			VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
			pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
			pipelineLayoutInfo.pSetLayouts = setLayouts.data();
			if (vkCreatePipelineLayout(device->GetDevice(), &pipelineLayoutInfo, nullptr,
			                           &litPipelineLayout) != VK_SUCCESS)
				SPRaise("Failed to create the lit sprite pipeline layout");

			litPipeline = BuildPipeline(litPipelineLayout, "Shaders/Vulkan/SoftLitSprite.vert.spv",
			                            "Shaders/Vulkan/SoftLitSprite.frag.spv", true);
			return true;
		}

		void VulkanSpriteRenderer::Add(VulkanImage* img, Vector3 center, float rad, float ang,
		                               Vector4 color) {
			SPADES_MARK_FUNCTION_DEBUG();

			Sprite spr;
			spr.image = img;
			spr.center = center;
			spr.radius = rad;
			spr.angle = ang;
			// Linearize the colour exactly as GLSpriteRenderer::Add does, with the
			// same emissive / scattering split. GL gates this on r_hdr because its
			// non-HDR framebuffer holds gamma-space values; the Vulkan offscreen
			// target is linear in every mode, so it always applies here. Without
			// it a dark, mostly-opaque particle (blood, debris) is drawn at its
			// gamma value in a linear buffer and comes out several times too
			// bright and desaturated.
			spr.scattering = !(color.x > color.w || color.y > color.w || color.z > color.w);
			if (!spr.scattering) {
				// emissive material
				color.x *= color.x;
				color.y *= color.y;
				color.z *= color.z;
			} else {
				// scattering/absorptive material
				float rcp = fastRcp(color.w + 0.01F);
				color.x *= color.x * rcp;
				color.y *= color.y * rcp;
				color.z *= color.z * rcp;
			}

			spr.color = color;
			sprites.push_back(spr);
		}

		void VulkanSpriteRenderer::Clear() {
			SPADES_MARK_FUNCTION();
			sprites.clear();
		}

		VkDescriptorSet VulkanSpriteRenderer::GetImageSet(FrameResources& frame,
		                                                  VulkanImage& image) {
			auto found = frame.imageSets.find(&image);
			if (found != frame.imageSets.end())
				return found->second;

			VkDevice vkDevice = device->GetDevice();

			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = frame.descriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &spriteSetLayout;
			VkDescriptorSet set;
			if (vkAllocateDescriptorSets(vkDevice, &allocInfo, &set) != VK_SUCCESS)
				SPRaise("Failed to allocate a sprite descriptor set: more than %u images",
				        kMaxImagesPerFrame);

			const VkDescriptorBufferInfo viewInfo{frame.view->GetBuffer(), 0, VK_WHOLE_SIZE};
			std::array<VkDescriptorImageInfo, 2> imageInfos{{
			  {image.GetSampler(), image.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
			  {},
			}};
			std::array<VkWriteDescriptorSet, 3> writes{};
			for (VkWriteDescriptorSet& write : writes) {
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.dstSet = set;
				write.descriptorCount = 1;
			}
			writes[0].dstBinding = 0;
			writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writes[0].pBufferInfo = &viewInfo;
			writes[1].dstBinding = 1;
			writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[1].pImageInfo = &imageInfos[0];
			std::uint32_t writeCount = 2;
			if (softParticles) {
				// Resolved depth == raw depth at 1x; the single-sample R32F resolve
				// under MSAA (a multisampled depth attachment can't be sampled here).
				Handle<VulkanImage> depth = renderer.GetFramebufferManager()->GetResolvedDepthImage();
				imageInfos[1] = {depth->GetSampler(), depth->GetImageView(),
				                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
				writes[2].dstBinding = 2;
				writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[2].pImageInfo = &imageInfos[1];
				writeCount = 3;
				frame.images.push_back(depth);
			}
			vkUpdateDescriptorSets(vkDevice, writeCount, writes.data(), 0, nullptr);

			frame.imageSets.emplace(&image, set);
			frame.images.push_back(Handle<VulkanImage>(&image));
			return set;
		}

		void VulkanSpriteRenderer::Render(VkCommandBuffer commandBuffer, std::size_t frameSlot) {
			SPADES_MARK_FUNCTION();

			SPAssert(frameSlot < frames.size());
			FrameResources& frame = frames[frameSlot];

			// What the frame drew with last time is done with: its fence was waited for.
			vkResetDescriptorPool(device->GetDevice(), frame.descriptorPool, 0);
			frame.imageSets.clear();
			frame.images.clear();

			if (sprites.empty())
				return;

			if (pipeline == VK_NULL_HANDLE)
				CreatePipeline();

			// The view, the same for every sprite
			const client::SceneDefinition& def = renderer.GetSceneDef();
			Vector3 fogColor = renderer.GetFogColor();
			fogColor *= fogColor; // linearize
			GpuView view{};
			std::memcpy(view.projectionView, renderer.GetProjectionViewMatrix().m,
			            sizeof(view.projectionView));
			Store(view.right, def.viewAxis[0], 0.0F);
			Store(view.up, def.viewAxis[1], 0.0F);
			Store(view.front, def.viewAxis[2], 0.0F);
			Store(view.eye, def.viewOrigin, 1.0F);
			Store(view.fogColorDistance, fogColor, renderer.GetFogDistance());
			view.nearFar[0] = def.zNear;
			view.nearFar[1] = def.zFar;
			Store(view.sunDirection, renderer.GetSunDirection(), 0.0F);
			std::memcpy(frame.view->Map(), &view, sizeof(view));

			// Lit by the map, the models' shadows and the scene's lights, when there is
			// a map; else as plain soft sprites
			const bool lit = softParticles && litParticles && PrepareLitPipeline();

			// The sprites, in the order they were added
			ReserveInstances(frame, sprites.size());
			auto* instances = static_cast<Instance*>(frame.instances->Map());
			for (std::size_t i = 0; i < sprites.size(); i++) {
				const Sprite& sprite = sprites[i];
				Instance& out = instances[i];
				Store(out.centerRadius, sprite.center, sprite.radius);
				// The shaders light a scattering sprite: by the daylight and the dynamic
				// lights, or, lit as a volume, by the sun, the sky and those lights.
				out.color[0] = sprite.color.x;
				out.color[1] = sprite.color.y;
				out.color[2] = sprite.color.z;
				out.color[3] = sprite.color.w;
				out.angle = sprite.angle;
				out.scattering = sprite.scattering ? 1.0F : 0.0F;
			}

			const VkPipelineLayout layout = lit ? litPipelineLayout : pipelineLayout;
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			                  lit ? litPipeline : pipeline);
			if (lit) {
				const std::array<VkDescriptorSet, 2> mapSets{
				  {renderer.GetMapRenderer()->GetShadowDescriptorSet(),
				   renderer.GetShadowMapRenderer()->GetSamplingDescriptorSet()}};
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
				                        static_cast<std::uint32_t>(mapSets.size()), mapSets.data(),
				                        0, nullptr);
			}
			// The scene's lights, which every sprite pipeline lights sprites by
			const VkDescriptorSet lightSet = renderer.GetSceneLights().GetDescriptorSet(frameSlot);
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 2, 1,
			                        &lightSet, 0, nullptr);

			const VkBuffer instanceBuffer = frame.instances->GetBuffer();
			const VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &instanceBuffer, &offset);

			// A draw for each run of sprites sharing an image
			std::size_t first = 0;
			while (first < sprites.size()) {
				VulkanImage* image = sprites[first].image;
				std::size_t end = first + 1;
				while (end < sprites.size() && sprites[end].image == image)
					end++;

				const VkDescriptorSet set = GetImageSet(frame, *image);
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
				                        SpriteSet, 1, &set, 0, nullptr);
				vkCmdDraw(commandBuffer, kQuadVertices, static_cast<std::uint32_t>(end - first), 0,
				          static_cast<std::uint32_t>(first));
				first = end;
			}

			Clear();
		}
	} // namespace draw
} // namespace spades
