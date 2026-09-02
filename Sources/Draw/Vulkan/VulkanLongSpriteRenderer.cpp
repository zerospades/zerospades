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

#include "VulkanLongSpriteRenderer.h"
#include "VulkanSpirvCache.h"
#include "VulkanRenderer.h"
#include "VulkanImage.h"
#include "VulkanBuffer.h"
#include "VulkanShader.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <algorithm>
#include <cstring>

namespace spades {
	namespace draw {
		namespace {
			// Push-constant block shared by LongSprite.vert / .frag. Vulkan's std430
			// layout aligns each vec3 to 16 bytes, so the explicit trailing floats pad
			// the vec3s up to match the shader exactly. Both the pipeline's
			// pushConstantRange size and the vkCmdPushConstants size use
			// sizeof(LongSpritePushConstants), so they can never disagree — a previous
			// undersized range left fogDistance outside the declared range, which
			// AMD/amdvlk drops (-> fogDistance garbage -> density clamps to 1 -> the
			// reflex reticle fogged out to invisible), while MoltenVK tolerated it.
			struct LongSpritePushConstants {
				Matrix4 projectionViewMatrix;
				Matrix4 viewMatrix;
				Vector3 rightVector;       float padding1;
				Vector3 upVector;          float padding2;
				Vector3 viewOriginVector;  float padding3;
				Vector3 fogColor;          float fogDistance;
			};
		} // namespace

		VulkanLongSpriteRenderer::VulkanLongSpriteRenderer(VulkanRenderer& r)
		    : renderer(r),
		      device(static_cast<gui::SDLVulkanDevice*>(r.GetDevice().Unmanage())),
		      lastImage(nullptr),
		      pipeline(VK_NULL_HANDLE),
		      pipelineLayout(VK_NULL_HANDLE),
		      descriptorSetLayout(VK_NULL_HANDLE) {
			SPADES_MARK_FUNCTION();

			const auto& swapchainImageViews = device->GetSwapchainImageViews();
			perFrameDescriptorPools.resize(swapchainImageViews.size(), VK_NULL_HANDLE);
			perFrameBuffers.resize(swapchainImageViews.size());
			perFrameImages.resize(swapchainImageViews.size());

			CreatePipeline();
			CreateDescriptorSet();
		}

		VulkanLongSpriteRenderer::~VulkanLongSpriteRenderer() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			for (auto pool : perFrameDescriptorPools) {
				if (pool != VK_NULL_HANDLE) {
					vkDestroyDescriptorPool(vkDevice, pool, nullptr);
				}
			}
			for (auto& frameImages : perFrameImages) {
				for (auto* img : frameImages) {
					img->Release();
				}
			}
			if (pipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, pipeline, nullptr);
			}
			if (pipelineLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, pipelineLayout, nullptr);
			}
			if (descriptorSetLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(vkDevice, descriptorSetLayout, nullptr);
			}
		}

		void VulkanLongSpriteRenderer::CreatePipeline() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			auto LoadSPIRVFile = [](const char* filename) -> std::vector<uint32_t> {
				return SpirvCache::Load(filename);
			};

			std::vector<uint32_t> vertCode = LoadSPIRVFile("Shaders/Vulkan/LongSprite.vert.spv");
			std::vector<uint32_t> fragCode = LoadSPIRVFile("Shaders/Vulkan/LongSprite.frag.spv");

			Handle<VulkanShader> vertShader(new VulkanShader(device, VulkanShader::VertexShader, "LongSprite.vert"), false);
			Handle<VulkanShader> fragShader(new VulkanShader(device, VulkanShader::FragmentShader, "LongSprite.frag"), false);

			vertShader->LoadSPIRV(vertCode);
			fragShader->LoadSPIRV(fragCode);

			VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
			vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
			vertShaderStageInfo.module = vertShader->GetShaderModule();
			vertShaderStageInfo.pName = "main";

			VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
			fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			fragShaderStageInfo.module = fragShader->GetShaderModule();
			fragShaderStageInfo.pName = "main";

			VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

			// Vertex format: position(3) + pad(1) + texCoord(2) + color(4) = 10 floats
			VkVertexInputBindingDescription bindingDescription{};
			bindingDescription.binding = 0;
			bindingDescription.stride = sizeof(Vertex);
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			VkVertexInputAttributeDescription attributeDescriptions[3]{};
			// position
			attributeDescriptions[0].binding = 0;
			attributeDescriptions[0].location = 0;
			attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDescriptions[0].offset = offsetof(Vertex, x);

			// texCoord
			attributeDescriptions[1].binding = 0;
			attributeDescriptions[1].location = 1;
			attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
			attributeDescriptions[1].offset = offsetof(Vertex, u);

			// color
			attributeDescriptions[2].binding = 0;
			attributeDescriptions[2].location = 2;
			attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
			attributeDescriptions[2].offset = offsetof(Vertex, r);

			VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
			vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputInfo.vertexBindingDescriptionCount = 1;
			vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
			vertexInputInfo.vertexAttributeDescriptionCount = 3;
			vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

			VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
			inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			inputAssembly.primitiveRestartEnable = VK_FALSE;

			VkPipelineViewportStateCreateInfo viewportState{};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.pViewports = nullptr;
			viewportState.scissorCount = 1;
			viewportState.pScissors = nullptr;

			VkPipelineRasterizationStateCreateInfo rasterizer{};
			rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizer.depthClampEnable = VK_FALSE;
			rasterizer.rasterizerDiscardEnable = VK_FALSE;
			rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = VK_CULL_MODE_NONE; // No culling for sprites
			rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rasterizer.depthBiasEnable = VK_FALSE;

			VkPipelineMultisampleStateCreateInfo multisampling{};
			multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampling.sampleShadingEnable = VK_FALSE;
			// Match the scene render pass sample count (MSAA).
			multisampling.rasterizationSamples = device->GetSampleCount();

			VkPipelineDepthStencilStateCreateInfo depthStencil{};
			depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencil.depthTestEnable = VK_TRUE;
			depthStencil.depthWriteEnable = VK_FALSE;
			depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
			depthStencil.depthBoundsTestEnable = VK_FALSE;
			depthStencil.stencilTestEnable = VK_FALSE;

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
			colorBlending.logicOpEnable = VK_FALSE;
			colorBlending.attachmentCount = 1;
			colorBlending.pAttachments = &colorBlendAttachment;

			VkDynamicState dynamicStates[] = {
				VK_DYNAMIC_STATE_VIEWPORT,
				VK_DYNAMIC_STATE_SCISSOR
			};

			VkPipelineDynamicStateCreateInfo dynamicState{};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = 2;
			dynamicState.pDynamicStates = dynamicStates;

			VkDescriptorSetLayoutBinding samplerLayoutBinding{};
			samplerLayoutBinding.binding = 0;
			samplerLayoutBinding.descriptorCount = 1;
			samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			samplerLayoutBinding.pImmutableSamplers = nullptr;
			samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = 1;
			layoutInfo.pBindings = &samplerLayoutBinding;

			VkResult result = vkCreateDescriptorSetLayout(vkDevice, &layoutInfo, nullptr, &descriptorSetLayout);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create descriptor set layout (error code: %d)", result);
			}

			// Same push constant layout as the sprite renderer
			VkPushConstantRange pushConstantRange{};
			pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			pushConstantRange.offset = 0;
			// Must cover the full std430-padded block (the trailing fogDistance lives
			// at offset 188); an undersized range made amdvlk drop it.
			pushConstantRange.size = sizeof(LongSpritePushConstants);

			VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
			pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutInfo.setLayoutCount = 1;
			pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
			pipelineLayoutInfo.pushConstantRangeCount = 1;
			pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

			result = vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create pipeline layout (error code: %d)", result);
			}

			VkGraphicsPipelineCreateInfo pipelineInfo{};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = 2;
			pipelineInfo.pStages = shaderStages;
			pipelineInfo.pVertexInputState = &vertexInputInfo;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterizer;
			pipelineInfo.pMultisampleState = &multisampling;
			pipelineInfo.pDepthStencilState = &depthStencil;
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.pDynamicState = &dynamicState;
			pipelineInfo.layout = pipelineLayout;
			pipelineInfo.renderPass = renderer.GetOffscreenRenderPass();
			pipelineInfo.subpass = 0;

			result = vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1, &pipelineInfo, nullptr, &pipeline);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create graphics pipeline (error code: %d)", result);
			}
		}

		void VulkanLongSpriteRenderer::CreateDescriptorSet() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			VkDescriptorPoolSize poolSize{};
			poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSize.descriptorCount = 1000;

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes = &poolSize;
			poolInfo.maxSets = 1000;
			poolInfo.flags = 0;

			for (size_t i = 0; i < perFrameDescriptorPools.size(); i++) {
				VkResult result = vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &perFrameDescriptorPools[i]);
				if (result != VK_SUCCESS) {
					SPRaise("Failed to create descriptor pool for frame %zu (error code: %d)", i, result);
				}
			}
		}

		void VulkanLongSpriteRenderer::Add(VulkanImage *img, Vector3 p1, Vector3 p2,
		                                   float rad, Vector4 color) {
			SPADES_MARK_FUNCTION_DEBUG();
			Sprite spr;
			spr.image = img;
			spr.start = p1;
			spr.end = p2;
			spr.radius = rad;
			spr.color = color;
			sprites.push_back(spr);
		}

		void VulkanLongSpriteRenderer::Clear() {
			SPADES_MARK_FUNCTION();
			sprites.clear();
			vertices.clear();
			indices.clear();
			lastImage = nullptr;
		}

		void VulkanLongSpriteRenderer::Flush(VkCommandBuffer commandBuffer, uint32_t frameIndex) {
			SPADES_MARK_FUNCTION();

			if (vertices.empty() || indices.empty())
				return;

			if (pipeline == VK_NULL_HANDLE)
				return;

			if (!lastImage)
				return;

			VkDevice vkDevice = device->GetDevice();

			size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
			Handle<VulkanBuffer> vertexBuffer(
				new VulkanBuffer(device, vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
				false);
			vertexBuffer->UpdateData(vertices.data(), vertexBufferSize);
			perFrameBuffers[frameIndex].push_back(vertexBuffer);

			size_t indexBufferSize = indices.size() * sizeof(uint32_t);
			Handle<VulkanBuffer> indexBuffer(
				new VulkanBuffer(device, indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
				false);
			indexBuffer->UpdateData(indices.data(), indexBufferSize);
			perFrameBuffers[frameIndex].push_back(indexBuffer);

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

			VkBuffer vb = vertexBuffer->GetBuffer();
			VkDeviceSize offsets[] = {0};
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, offsets);

			vkCmdBindIndexBuffer(commandBuffer, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = perFrameDescriptorPools[frameIndex];
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &descriptorSetLayout;

			VkDescriptorSet descriptorSet;
			VkResult result = vkAllocateDescriptorSets(vkDevice, &allocInfo, &descriptorSet);
			if (result != VK_SUCCESS) {
				SPLog("Failed to allocate descriptor set (error code: %d)", result);
				vertices.clear();
				indices.clear();
				return;
			}

			VkImageView imageView = lastImage->GetImageView();
			VkSampler sampler = lastImage->GetSampler();

			if (imageView == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE) {
				SPLog("Warning: Invalid image view or sampler, skipping");
				vertices.clear();
				indices.clear();
				return;
			}

			VkDescriptorImageInfo imageInfo{};
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = imageView;
			imageInfo.sampler = sampler;

			VkWriteDescriptorSet descriptorWrite{};
			descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite.dstSet = descriptorSet;
			descriptorWrite.dstBinding = 0;
			descriptorWrite.dstArrayElement = 0;
			descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrite.descriptorCount = 1;
			descriptorWrite.pImageInfo = &imageInfo;

			vkUpdateDescriptorSets(vkDevice, 1, &descriptorWrite, 0, nullptr);

			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
			                        0, 1, &descriptorSet, 0, nullptr);

			LongSpritePushConstants pushConstants;

			const Matrix4& projViewMatrix = renderer.GetProjectionViewMatrix();
			Vector3 fogCol = renderer.GetFogColor();
			fogCol *= fogCol; // linearize
			float fogDist = renderer.GetFogDistance();
			const client::SceneDefinition& sceneDef = renderer.GetSceneDef();

			pushConstants.projectionViewMatrix = projViewMatrix;
			pushConstants.viewMatrix = Matrix4::Identity();
			pushConstants.rightVector = sceneDef.viewAxis[0];
			pushConstants.upVector = sceneDef.viewAxis[1];
			pushConstants.viewOriginVector = sceneDef.viewOrigin;
			pushConstants.fogColor = fogCol;
			pushConstants.fogDistance = fogDist;

			vkCmdPushConstants(commandBuffer, pipelineLayout,
			                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			                   0, sizeof(pushConstants), &pushConstants);

			vkCmdDrawIndexed(commandBuffer, (uint32_t)indices.size(), 1, 0, 0, 0);

			lastImage->AddRef();
			perFrameImages[frameIndex].push_back(lastImage);

			vertices.clear();
			indices.clear();
		}

		void VulkanLongSpriteRenderer::Render(VkCommandBuffer commandBuffer, uint32_t frameIndex) {
			SPADES_MARK_FUNCTION();

			if (sprites.empty())
				return;

			VkDevice vkDevice = device->GetDevice();

			// Clear resources from this frame (GPU has finished with them due to fence wait)
			perFrameBuffers[frameIndex].clear();

			// Release images from previous use of this frame
			for (auto* img : perFrameImages[frameIndex]) {
				img->Release();
			}
			perFrameImages[frameIndex].clear();

			// Reset descriptor pool for this frame
			vkResetDescriptorPool(vkDevice, perFrameDescriptorPools[frameIndex], 0);

			// Sort sprites by image to minimize batch breaks and descriptor set allocations
			std::sort(sprites.begin(), sprites.end(),
			          [](const Sprite& a, const Sprite& b) { return a.image < b.image; });

			const client::SceneDefinition &def = renderer.GetSceneDef();

			for (size_t i = 0; i < sprites.size(); i++) {
				Sprite spr = sprites[i];

				if (spr.image != lastImage) {
					Flush(commandBuffer, frameIndex);
					lastImage = spr.image;
				}

				Vertex v;
				v.pad = 0;
				v.r = spr.color.x;
				v.g = spr.color.y;
				v.b = spr.color.z;
				v.a = spr.color.w;

				uint32_t idx = (uint32_t)vertices.size();

				// Clip by view plane (matches GLLongSpriteRenderer)
				{
					float d1 = Vector3::Dot(spr.start - def.viewOrigin, def.viewAxis[2]);
					float d2 = Vector3::Dot(spr.end - def.viewOrigin, def.viewAxis[2]);
					const float clipPlane = .1f;
					if (d1 < clipPlane && d2 < clipPlane)
						continue;
					if (d1 > clipPlane || d2 > clipPlane) {
						if (d1 < clipPlane) {
							float per = (clipPlane - d1) / (d2 - d1);
							spr.start = Mix(spr.start, spr.end, per);
						} else if (d2 < clipPlane) {
							float per = (clipPlane - d1) / (d2 - d1);
							spr.end = Mix(spr.start, spr.end, per);
						}
					}
				}

				// Calculate view position
				Vector3 view1 = spr.start - def.viewOrigin;
				Vector3 view2 = spr.end - def.viewOrigin;
				view1 = MakeVector3(Vector3::Dot(view1, def.viewAxis[0]),
				                    Vector3::Dot(view1, def.viewAxis[1]),
				                    Vector3::Dot(view1, def.viewAxis[2]));
				view2 = MakeVector3(Vector3::Dot(view2, def.viewAxis[0]),
				                    Vector3::Dot(view2, def.viewAxis[1]),
				                    Vector3::Dot(view2, def.viewAxis[2]));

				// Transform to screen
				Vector2 scr1 = MakeVector2(view1.x / view1.z, view1.y / view1.z);
				Vector2 scr2 = MakeVector2(view2.x / view2.z, view2.y / view2.z);

				Vector3 vecX = def.viewAxis[0] * spr.radius;
				Vector3 vecY = def.viewAxis[1] * spr.radius;
				float normalThreshold = spr.radius * 0.5f / ((view1.z + view2.z) * .5f);
				if ((scr2 - scr1).GetSquaredLength() < normalThreshold * normalThreshold) {
					// Too short in screen; normal sprite
					v = spr.start - vecX - vecY;
					v.u = 0; v.v = 0;
					vertices.push_back(v);

					v = spr.start + vecX - vecY;
					v.u = 1; v.v = 0;
					vertices.push_back(v);

					v = spr.start - vecX + vecY;
					v.u = 0; v.v = 1;
					vertices.push_back(v);

					v = spr.start + vecX + vecY;
					v.u = 1; v.v = 1;
					vertices.push_back(v);

					indices.push_back(idx);
					indices.push_back(idx + 1);
					indices.push_back(idx + 2);
					indices.push_back(idx + 1);
					indices.push_back(idx + 3);
					indices.push_back(idx + 2);
				} else {
					Vector2 scrDir = (scr2 - scr1).Normalize();
					Vector2 normDir = {scrDir.y, -scrDir.x};
					Vector3 vecU = vecX * normDir.x + vecY * normDir.y;
					Vector3 vecV = vecX * scrDir.x + vecY * scrDir.y;

					v = spr.start - vecU - vecV;
					v.u = 0; v.v = 0;
					vertices.push_back(v);

					v = spr.start + vecU - vecV;
					v.u = 1; v.v = 0;
					vertices.push_back(v);

					v = spr.start - vecU;
					v.u = 0; v.v = 0.5f;
					vertices.push_back(v);

					v = spr.start + vecU;
					v.u = 1; v.v = 0.5f;
					vertices.push_back(v);

					v = spr.end - vecU;
					v.u = 0; v.v = 0.5f;
					vertices.push_back(v);

					v = spr.end + vecU;
					v.u = 1; v.v = 0.5f;
					vertices.push_back(v);

					v = spr.end - vecU + vecV;
					v.u = 0; v.v = 1;
					vertices.push_back(v);

					v = spr.end + vecU + vecV;
					v.u = 1; v.v = 1;
					vertices.push_back(v);

					indices.push_back(idx);
					indices.push_back(idx + 1);
					indices.push_back(idx + 2);
					indices.push_back(idx + 1);
					indices.push_back(idx + 3);
					indices.push_back(idx + 2);

					indices.push_back(idx + 2);
					indices.push_back(idx + 2 + 1);
					indices.push_back(idx + 2 + 2);
					indices.push_back(idx + 2 + 1);
					indices.push_back(idx + 2 + 3);
					indices.push_back(idx + 2 + 2);

					indices.push_back(idx + 4);
					indices.push_back(idx + 4 + 1);
					indices.push_back(idx + 4 + 2);
					indices.push_back(idx + 4 + 1);
					indices.push_back(idx + 4 + 3);
					indices.push_back(idx + 4 + 2);

					idx = (uint32_t)vertices.size();

					v = spr.start - vecU + vecV;
					v.u = 0; v.v = 0;
					vertices.push_back(v);

					v = spr.start + vecU + vecV;
					v.u = 1; v.v = 0;
					vertices.push_back(v);

					v = spr.end - vecU - vecV;
					v.u = 0; v.v = 1;
					vertices.push_back(v);

					v = spr.end + vecU - vecV;
					v.u = 1; v.v = 1;
					vertices.push_back(v);

					v.r = v.g = v.b = v.a = 0.0F;

					v = spr.start - vecU;
					v.u = 0; v.v = 0.5f;
					vertices.push_back(v);

					v = spr.start + vecU;
					v.u = 1; v.v = 0.5f;
					vertices.push_back(v);

					v = spr.end - vecU;
					v.u = 0; v.v = 0.5f;
					vertices.push_back(v);

					v = spr.end + vecU;
					v.u = 1; v.v = 0.5f;
					vertices.push_back(v);

					indices.push_back(idx);
					indices.push_back(idx + 1);
					indices.push_back(idx + 2 + 2);
					indices.push_back(idx + 1);
					indices.push_back(idx + 2 + 3);
					indices.push_back(idx + 2 + 2);

					indices.push_back(idx + 2);
					indices.push_back(idx + 2 + 1);
					indices.push_back(idx + 4 + 2);
					indices.push_back(idx + 2 + 1);
					indices.push_back(idx + 4 + 3);
					indices.push_back(idx + 4 + 2);
				}
			}

			Flush(commandBuffer, frameIndex);

			Clear();
		}

	} // namespace draw
} // namespace spades
