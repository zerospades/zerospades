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

#include "VulkanMapRenderer.h"
#include "VulkanSpirvCache.h"
#include "VulkanMapChunk.h"
#include "VulkanRenderer.h"
#include "VulkanShadowMapRenderer.h"
#include "VulkanBuffer.h"
#include "VulkanImage.h"
#include "VulkanImageWrapper.h"
#include <Gui/SDLVulkanDevice.h>
#include <Client/GameMap.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/Settings.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <array>
#include <vector>

namespace spades {
	namespace draw {

		VulkanMapRenderer::VulkanMapRenderer(client::GameMap* map, VulkanRenderer& r)
		    : renderer(r),
		      device(static_cast<gui::SDLVulkanDevice*>(r.GetDevice().Unmanage())),
		      gameMap(map),
		      chunks(nullptr),
		      chunkInfos(nullptr),
		      depthonlyPipeline(VK_NULL_HANDLE),
		      basicPipeline(VK_NULL_HANDLE),
		      basicMirrorPipeline(VK_NULL_HANDLE),
		      dlightPipeline(VK_NULL_HANDLE),
		      backfacePipeline(VK_NULL_HANDLE),
		      pipelineLayout(VK_NULL_HANDLE),
		      dlightPipelineLayout(VK_NULL_HANDLE),
		      descriptorSetLayout(VK_NULL_HANDLE),
		      descriptorPool(VK_NULL_HANDLE),
		      textureDescriptorSet(VK_NULL_HANDLE),
		      physicalLighting(false) {
			SPADES_MARK_FUNCTION();

			{
				SPADES_SETTING(r_physicalLighting);
				physicalLighting = (int)r_physicalLighting != 0;
			}

			SPLog("Initializing Vulkan map renderer (physicalLighting=%d)", (int)physicalLighting);

			int w = map->Width();
			int h = map->Height();
			int d = map->Depth();

			numChunkWidth = w >> VulkanMapChunk::SizeBits;
			numChunkHeight = h >> VulkanMapChunk::SizeBits;
			numChunkDepth = d >> VulkanMapChunk::SizeBits;

			if ((w & (VulkanMapChunk::Size - 1)) != 0)
				numChunkWidth++;
			if ((h & (VulkanMapChunk::Size - 1)) != 0)
				numChunkHeight++;
			if ((d & (VulkanMapChunk::Size - 1)) != 0)
				numChunkDepth++;

			numChunks = numChunkWidth * numChunkHeight * numChunkDepth;

			SPLog("Chunk count: %d (%d x %d x %d)", numChunks, numChunkWidth, numChunkHeight,
			      numChunkDepth);

			chunks = new VulkanMapChunk*[numChunks];
			chunkInfos = new ChunkRenderInfo[numChunks];

			for (int i = 0; i < numChunks; i++) {
				chunks[i] = nullptr;
				chunkInfos[i].rendered = false;
				chunkInfos[i].distance = 0.0f;
			}

			// Create chunks
			for (int cx = 0; cx < numChunkWidth; cx++) {
				for (int cy = 0; cy < numChunkHeight; cy++) {
					for (int cz = 0; cz < numChunkDepth; cz++) {
						int idx = GetChunkIndex(cx, cy, cz);
						chunks[idx] = new VulkanMapChunk(*this, map, cx, cy, cz);
					}
				}
			}

			SPLog("Vulkan map renderer initialized");
		}

		VulkanMapRenderer::~VulkanMapRenderer() {
			SPADES_MARK_FUNCTION();

			if (chunks) {
				for (int i = 0; i < numChunks; i++) {
					if (chunks[i]) {
						delete chunks[i];
					}
				}
				delete[] chunks;
			}

			if (chunkInfos) {
				delete[] chunkInfos;
			}

			DestroyPipelines();
		}

		void VulkanMapRenderer::PreloadShaders(VulkanRenderer& r) {
			SPADES_MARK_FUNCTION();
			SPLog("Preloading Vulkan map shaders");

			SPADES_SETTING(r_physicalLighting);
			if ((int)r_physicalLighting != 0) {
				SpirvCache::Preload({"Shaders/Vulkan/BasicMapPhys.vert.spv",
				                     "Shaders/Vulkan/BasicMapPhys.frag.spv"});
			} else {
				SpirvCache::Preload({"Shaders/Vulkan/BasicMap.vert.spv",
				                     "Shaders/Vulkan/BasicMap.frag.spv"});
			}
			SpirvCache::Preload({"Shaders/Vulkan/BasicBlockDynamicLit.vert.spv",
			                     "Shaders/Vulkan/BasicBlockDynamicLit.frag.spv",
			                     "Shaders/Vulkan/ShadowMap.vert.spv",
			                     "Shaders/Vulkan/ShadowMap.frag.spv"});
		}

		void VulkanMapRenderer::GameMapChanged(int x, int y, int z, client::GameMap* map) {
			SPADES_MARK_FUNCTION();

			if (map != gameMap)
				return;

			int cx = x >> VulkanMapChunk::SizeBits;
			int cy = y >> VulkanMapChunk::SizeBits;
			int cz = z >> VulkanMapChunk::SizeBits;

			for (int dx = -1; dx <= 1; dx++) {
				for (int dy = -1; dy <= 1; dy++) {
					for (int dz = -1; dz <= 1; dz++) {
						int xx = (cx + dx) & (numChunkWidth - 1);
						int yy = (cy + dy) & (numChunkHeight - 1);
						int zz = cz + dz;

						if (zz < 0 || zz >= numChunkDepth)
							continue;

						VulkanMapChunk* chunk = GetChunk(xx, yy, zz);
						if (chunk) {
							chunk->SetNeedsUpdate();
						}
					}
				}
			}
		}

		void VulkanMapRenderer::Realize() {
			SPADES_MARK_FUNCTION();
			// Realize chunks based on view origin
			Vector3 viewOrigin = renderer.GetSceneDef().viewOrigin;
			RealizeChunks(viewOrigin);
		}

		void VulkanMapRenderer::RealizeChunks(Vector3 eye) {
			SPADES_MARK_FUNCTION();

			float cullDistance = 128.0F;
			float releaseDistance = cullDistance + 32.0F;

			// Calculate distance and realize/unrealize chunks based on distance
			for (int i = 0; i < numChunks; i++) {
				VulkanMapChunk* chunk = chunks[i];
				if (chunk) {
					float dist = chunk->DistanceFromEye(eye);
					chunkInfos[i].distance = dist;

					// Frustum culling via distance-based LOD
					if (dist < cullDistance) {
						chunk->SetRealized(true);
					} else if (dist > releaseDistance) {
						chunk->SetRealized(false);
					}
				}
			}

			// Update realized chunks that need updates (must be done outside render passes)
			for (int i = 0; i < numChunks; i++) {
				VulkanMapChunk* chunk = chunks[i];
				if (chunk && chunk->IsRealized()) {
					chunk->UpdateIfNeeded();
				}
			}
		}

		void VulkanMapRenderer::Prerender() {
			SPADES_MARK_FUNCTION();
			// Depth-only prerender pass is now handled via RenderDepthPass
			// This method can be used for other preprocessing if needed
		}

		void VulkanMapRenderer::RenderSunlightPass(VkCommandBuffer commandBuffer) {
			SPADES_MARK_FUNCTION();

			if (basicPipeline == VK_NULL_HANDLE) {
				SPLog("Warning: Map pipeline not initialized - map will not render");
				return;
			}

			Vector3 viewOrigin = renderer.GetSceneDef().viewOrigin;
			IntVector3 c = viewOrigin.Floor();
			c.x >>= VulkanMapChunk::SizeBits;
			c.y >>= VulkanMapChunk::SizeBits;
			c.z >>= VulkanMapChunk::SizeBits;

			// Bind the basic pipeline. The water reflection (mirror) pass reflects
			// the scene across the water plane with a negative-Z scale, which
			// reverses triangle winding — render it with the reversed-winding
			// sibling so back-face culling stays correct (GL flips glFrontFace here).
			VkPipeline sunlightPipeline =
			  (renderer.IsRenderingMirror() && basicMirrorPipeline != VK_NULL_HANDLE)
			    ? basicMirrorPipeline
			    : basicPipeline;
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, sunlightPipeline);

			// Bind the model-shadow sampling set (set 1) once for the whole pass; the
			// per-chunk set-0 binds don't disturb it. The UBO's enabled flag tells the
			// shader whether the cascade was rendered this frame.
			VulkanShadowMapRenderer* smr = renderer.GetShadowMapRenderer();
			if (smr && smr->GetSamplingDescriptorSet() != VK_NULL_HANDLE) {
				VkDescriptorSet samplingSet = smr->GetSamplingDescriptorSet();
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				                        pipelineLayout, 1, 1, &samplingSet, 0, nullptr);
			}

			// Draw from nearest to farthest for optimal depth testing
			// Include all vertical chunks
			for (int cz = 0; cz < numChunkDepth; cz++) {
				DrawColumnSunlight(commandBuffer, c.x, c.y, cz, viewOrigin);
			}

			// Draw in a spiral pattern outward from the camera
			for (int dist = 1; dist <= 128 / VulkanMapChunk::Size; dist++) {
				for (int x = c.x - dist; x <= c.x + dist; x++) {
					for (int cz = 0; cz < numChunkDepth; cz++) {
						DrawColumnSunlight(commandBuffer, x, c.y + dist, cz, viewOrigin);
						DrawColumnSunlight(commandBuffer, x, c.y - dist, cz, viewOrigin);
					}
				}
				for (int y = c.y - dist + 1; y <= c.y + dist - 1; y++) {
					for (int cz = 0; cz < numChunkDepth; cz++) {
						DrawColumnSunlight(commandBuffer, c.x + dist, y, cz, viewOrigin);
						DrawColumnSunlight(commandBuffer, c.x - dist, y, cz, viewOrigin);
					}
				}
			}
		}

		void VulkanMapRenderer::RenderDynamicLightPass(VkCommandBuffer commandBuffer,
		                                               std::vector<void*> lights) {
			SPADES_MARK_FUNCTION();

			if (lights.empty())
				return;

			if (dlightPipeline == VK_NULL_HANDLE) {
				return;
			}

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, dlightPipeline);

			Vector3 viewOrigin = renderer.GetSceneDef().viewOrigin;
			IntVector3 c = viewOrigin.Floor();
			c.x >>= VulkanMapChunk::SizeBits;
			c.y >>= VulkanMapChunk::SizeBits;
			c.z >>= VulkanMapChunk::SizeBits;

			// For each light, render all visible chunks
			for (void* lightPtr : lights) {
				const client::DynamicLightParam* light =
				    static_cast<const client::DynamicLightParam*>(lightPtr);

				// Bind this light's spotlight cookie (set 0). Point/linear lights
				// have no image and fall back to the 1x1 white texture.
				VulkanImage* cookieImage = nullptr;
				if (light->image)
					cookieImage = static_cast<VulkanImageWrapper*>(light->image)->GetVulkanImage();
				VkDescriptorSet cookieSet = renderer.GetDlightCookieDescriptorSet(cookieImage);
				if (cookieSet != VK_NULL_HANDLE) {
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
					                        dlightPipelineLayout, 0, 1, &cookieSet, 0, nullptr);
				}

				// Draw from nearest to farthest
				for (int cz = 0; cz < numChunkDepth; cz++) {
					DrawColumnDynamicLight(commandBuffer, c.x, c.y, cz, viewOrigin, *light);
				}

				for (int dist = 1; dist <= 128 / VulkanMapChunk::Size; dist++) {
					for (int x = c.x - dist; x <= c.x + dist; x++) {
						for (int cz = 0; cz < numChunkDepth; cz++) {
							DrawColumnDynamicLight(commandBuffer, x, c.y + dist, cz, viewOrigin, *light);
							DrawColumnDynamicLight(commandBuffer, x, c.y - dist, cz, viewOrigin, *light);
						}
					}
					for (int y = c.y - dist + 1; y <= c.y + dist - 1; y++) {
						for (int cz = 0; cz < numChunkDepth; cz++) {
							DrawColumnDynamicLight(commandBuffer, c.x + dist, y, cz, viewOrigin, *light);
							DrawColumnDynamicLight(commandBuffer, c.x - dist, y, cz, viewOrigin, *light);
						}
					}
				}
			}
		}

		void VulkanMapRenderer::RenderDepthPass(VkCommandBuffer commandBuffer) {
			SPADES_MARK_FUNCTION();

			// Depth-only pass for shadow mapping
			// This renders all visible chunks to the depth buffer only

			Vector3 viewOrigin = renderer.GetSceneDef().viewOrigin;
			IntVector3 c = viewOrigin.Floor();
			c.x >>= VulkanMapChunk::SizeBits;
			c.y >>= VulkanMapChunk::SizeBits;
			c.z >>= VulkanMapChunk::SizeBits;

			// Draw from nearest to farthest for optimal depth testing
			// Include all vertical chunks
			for (int cz = 0; cz < numChunkDepth; cz++) {
				DrawColumnDepth(commandBuffer, c.x, c.y, cz, viewOrigin);
			}

			// Draw in a spiral pattern outward from the camera
			for (int dist = 1; dist <= 128 / VulkanMapChunk::Size; dist++) {
				for (int x = c.x - dist; x <= c.x + dist; x++) {
					for (int cz = 0; cz < numChunkDepth; cz++) {
						DrawColumnDepth(commandBuffer, x, c.y + dist, cz, viewOrigin);
						DrawColumnDepth(commandBuffer, x, c.y - dist, cz, viewOrigin);
					}
				}
				for (int y = c.y - dist + 1; y <= c.y + dist - 1; y++) {
					for (int cz = 0; cz < numChunkDepth; cz++) {
						DrawColumnDepth(commandBuffer, c.x + dist, y, cz, viewOrigin);
						DrawColumnDepth(commandBuffer, c.x - dist, y, cz, viewOrigin);
					}
				}
			}
		}

		void VulkanMapRenderer::RenderShadowMapPass(VkCommandBuffer commandBuffer, VkPipelineLayout shadowPipelineLayout) {
			SPADES_MARK_FUNCTION();

			// Render all visible chunks for shadow mapping
			// This is called from within the shadow map render pass set up by VulkanShadowMapRenderer
			// Simply render all realized chunks
			for (int i = 0; i < numChunks; i++) {
				VulkanMapChunk* chunk = chunks[i];
				if (chunk && chunk->IsRealized()) {
					chunk->RenderShadowMapPass(commandBuffer, shadowPipelineLayout);
				}
			}
		}

		void VulkanMapRenderer::DrawColumnDepth(VkCommandBuffer commandBuffer, int cx, int cy, int cz,
		                                        Vector3 eye) {
			SPADES_MARK_FUNCTION();

			cx &= numChunkWidth - 1;
			cy &= numChunkHeight - 1;
			if (cz < 0 || cz >= numChunkDepth)
				return;

			VulkanMapChunk* chunk = GetChunk(cx, cy, cz);
			if (chunk && chunk->IsRealized()) {
				chunk->RenderDepthPass(commandBuffer);
			}
		}

		void VulkanMapRenderer::DrawColumnSunlight(VkCommandBuffer commandBuffer, int cx, int cy, int cz,
		                                           Vector3 eye) {
			SPADES_MARK_FUNCTION();

			cx &= numChunkWidth - 1;
			cy &= numChunkHeight - 1;
			if (cz < 0 || cz >= numChunkDepth)
				return;

			VulkanMapChunk* chunk = GetChunk(cx, cy, cz);
			if (chunk && chunk->IsRealized()) {
				chunk->RenderSunlightPass(commandBuffer);
			}
		}

		void VulkanMapRenderer::DrawColumnDynamicLight(VkCommandBuffer commandBuffer, int cx, int cy,
		                                               int cz, Vector3 eye,
		                                               const client::DynamicLightParam& light) {
			SPADES_MARK_FUNCTION();

			cx &= numChunkWidth - 1;
			cy &= numChunkHeight - 1;
			if (cz < 0 || cz >= numChunkDepth)
				return;

			VulkanMapChunk* chunk = GetChunk(cx, cy, cz);
			if (chunk && chunk->IsRealized()) {
				chunk->RenderDynamicLightPass(commandBuffer, light);
			}
		}

		void VulkanMapRenderer::RenderBackface(VkCommandBuffer commandBuffer) {
			SPADES_MARK_FUNCTION();

			// Backface rendering is useful for water reflections and special effects
			// Render map geometry with reversed front face (backface culling off or CW)

			Vector3 viewOrigin = renderer.GetSceneDef().viewOrigin;
			IntVector3 c = viewOrigin.Floor();
			c.x >>= VulkanMapChunk::SizeBits;
			c.y >>= VulkanMapChunk::SizeBits;
			c.z >>= VulkanMapChunk::SizeBits;

			// For backfaces, draw from farthest to nearest (reverse order)
			// Draw in a reverse spiral pattern
			for (int dist = 128 / VulkanMapChunk::Size; dist >= 1; dist--) {
				for (int x = c.x - dist; x <= c.x + dist; x++) {
					for (int cz = 0; cz < numChunkDepth; cz++) {
						DrawColumnSunlight(commandBuffer, x, c.y + dist, cz, viewOrigin);
						DrawColumnSunlight(commandBuffer, x, c.y - dist, cz, viewOrigin);
					}
				}
				for (int y = c.y - dist + 1; y <= c.y + dist - 1; y++) {
					for (int cz = 0; cz < numChunkDepth; cz++) {
						DrawColumnSunlight(commandBuffer, c.x + dist, y, cz, viewOrigin);
						DrawColumnSunlight(commandBuffer, c.x - dist, y, cz, viewOrigin);
					}
				}
			}

			for (int cz = 0; cz < numChunkDepth; cz++) {
				DrawColumnSunlight(commandBuffer, c.x, c.y, cz, viewOrigin);
			}
		}

		void VulkanMapRenderer::CreatePipelines(VkRenderPass renderPass) {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			// Load SPIR-V shaders
			auto LoadSPIRVFile = [](const char* filename) -> std::vector<uint32_t> {
				return SpirvCache::Load(filename);
			};

			std::vector<uint32_t> vertCode, fragCode;
			if (physicalLighting) {
				vertCode = LoadSPIRVFile("Shaders/Vulkan/BasicMapPhys.vert.spv");
				fragCode = LoadSPIRVFile("Shaders/Vulkan/BasicMapPhys.frag.spv");
			} else {
				vertCode = LoadSPIRVFile("Shaders/Vulkan/BasicMap.vert.spv");
				fragCode = LoadSPIRVFile("Shaders/Vulkan/BasicMap.frag.spv");
			}

			// Create shader modules
			VkShaderModuleCreateInfo vertShaderModuleInfo{};
			vertShaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			vertShaderModuleInfo.codeSize = vertCode.size() * sizeof(uint32_t);
			vertShaderModuleInfo.pCode = vertCode.data();

			VkShaderModule vertShaderModule;
			VkResult result = vkCreateShaderModule(vkDevice, &vertShaderModuleInfo, nullptr, &vertShaderModule);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create vertex shader module (error code: %d)", result);
			}

			VkShaderModuleCreateInfo fragShaderModuleInfo{};
			fragShaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			fragShaderModuleInfo.codeSize = fragCode.size() * sizeof(uint32_t);
			fragShaderModuleInfo.pCode = fragCode.data();

			VkShaderModule fragShaderModule;
			result = vkCreateShaderModule(vkDevice, &fragShaderModuleInfo, nullptr, &fragShaderModule);
			if (result != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
				SPRaise("Failed to create fragment shader module (error code: %d)", result);
			}

			// Shader stage creation
			VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
			vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
			vertShaderStageInfo.module = vertShaderModule;
			vertShaderStageInfo.pName = "main";

			VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
			fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			fragShaderStageInfo.module = fragShaderModule;
			fragShaderStageInfo.pName = "main";

			// Specialization constant: USE_RADIOSITY (BasicMap.frag) — picks
			// the MapRadiosityNull vs MapRadiosity ambient permutation.
			SPADES_SETTING(r_radiosity);
			int32_t useRadiosity = (int)r_radiosity != 0 ? 1 : 0;
			VkSpecializationMapEntry specEntry{};
			specEntry.constantID = 0;
			specEntry.offset = 0;
			specEntry.size = sizeof(int32_t);
			VkSpecializationInfo specInfo{};
			specInfo.mapEntryCount = 1;
			specInfo.pMapEntries = &specEntry;
			specInfo.dataSize = sizeof(int32_t);
			specInfo.pData = &useRadiosity;
			if (!physicalLighting) {
				fragShaderStageInfo.pSpecializationInfo = &specInfo;
			}

			VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

			// Vertex input state - matches VulkanMapChunk::Vertex
			// Vertex layout: uint8 x,y,z,pad + uint16 aoX,aoY + uint8 r,g,b,shading + int8 nx,ny,nz,pad2 + int8 sx,sy,sz,pad3
			// Total size: 4 + 4 + 4 + 4 + 4 = 20 bytes
			VkVertexInputBindingDescription bindingDescription{};
			bindingDescription.binding = 0;
			bindingDescription.stride = 20;
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			std::array<VkVertexInputAttributeDescription, 5> attributeDescriptions{};

			// Position (location 0) - x, y, z are uint8_t at offset 0
			attributeDescriptions[0].binding = 0;
			attributeDescriptions[0].location = 0;
			attributeDescriptions[0].format = VK_FORMAT_R8G8B8_UINT;
			attributeDescriptions[0].offset = 0;

			// AO coordinates (location 1) - aoX, aoY are uint16_t at offset 4
			attributeDescriptions[1].binding = 0;
			attributeDescriptions[1].location = 1;
			attributeDescriptions[1].format = VK_FORMAT_R16G16_UINT;
			attributeDescriptions[1].offset = 4;

			// Color (location 2) - colorRed, colorGreen, colorBlue are uint8_t at offset 8
			attributeDescriptions[2].binding = 0;
			attributeDescriptions[2].location = 2;
			attributeDescriptions[2].format = VK_FORMAT_R8G8B8_UINT;
			attributeDescriptions[2].offset = 8;

			// Normal (location 3) - nx, ny, nz are int8_t at offset 12
			attributeDescriptions[3].binding = 0;
			attributeDescriptions[3].location = 3;
			attributeDescriptions[3].format = VK_FORMAT_R8G8B8_SINT;
			attributeDescriptions[3].offset = 12;

			// Fixed position (location 4) - sx, sy, sz are int8_t at offset 16.
			// Face-center * 2 (chunk-local); used for map-shadow/AO/radiosity
			// sampling to avoid voxel-boundary bleed (lit sliver on face edges).
			attributeDescriptions[4].binding = 0;
			attributeDescriptions[4].location = 4;
			attributeDescriptions[4].format = VK_FORMAT_R8G8B8_SINT;
			attributeDescriptions[4].offset = 16;

			VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
			vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputInfo.vertexBindingDescriptionCount = 1;
			vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
			vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
			vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

			// Input assembly
			VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
			inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			inputAssembly.primitiveRestartEnable = VK_FALSE;

			// Viewport and scissor (dynamic)
			VkPipelineViewportStateCreateInfo viewportState{};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.scissorCount = 1;

			// Rasterization
			VkPipelineRasterizationStateCreateInfo rasterizer{};
			rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizer.depthClampEnable = VK_FALSE;
			rasterizer.rasterizerDiscardEnable = VK_FALSE;
			rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
			// Vulkan has Y-down NDC (opposite to OpenGL's Y-up), so CCW in OpenGL = CW in Vulkan
			rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
			rasterizer.depthBiasEnable = VK_FALSE;

			// Multisampling
			VkPipelineMultisampleStateCreateInfo multisampling{};
			multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampling.sampleShadingEnable = VK_FALSE;
			// Match the scene render pass sample count (MSAA). Map geometry is
			// opaque, so plain multisample coverage antialiases its silhouettes.
			multisampling.rasterizationSamples = device->GetSampleCount();

			// Depth stencil
			VkPipelineDepthStencilStateCreateInfo depthStencil{};
			depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencil.depthTestEnable = VK_TRUE;
			depthStencil.depthWriteEnable = VK_TRUE;
			depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
			depthStencil.depthBoundsTestEnable = VK_FALSE;
			depthStencil.stencilTestEnable = VK_FALSE;

			// Color blending
			VkPipelineColorBlendAttachmentState colorBlendAttachment{};
			colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			colorBlendAttachment.blendEnable = VK_FALSE;

			VkPipelineColorBlendStateCreateInfo colorBlending{};
			colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlending.logicOpEnable = VK_FALSE;
			colorBlending.attachmentCount = 1;
			colorBlending.pAttachments = &colorBlendAttachment;

			// Dynamic state
			VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
			VkPipelineDynamicStateCreateInfo dynamicState{};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = 2;
			dynamicState.pDynamicStates = dynamicStates;

			// Descriptor set layout:
			//   binding 0 — heightmap shadow 2D texture
			//   binding 1 — per-block ambient occlusion 3D texture (radiosity path AO)
			//   binding 2 — radiosity flat (directional GI base) 3D texture
			//   binding 3 — radiosity X 3D texture
			//   binding 4 — radiosity Y 3D texture
			//   binding 5 — radiosity Z 3D texture
			//   binding 6 — 2D AmbientOcclusion atlas (no-radiosity path AO, GL parity)
			VkDescriptorSetLayoutBinding bindings[7]{};
			for (uint32_t i = 0; i < 7; ++i) {
				bindings[i].binding = i;
				bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				bindings[i].descriptorCount = 1;
				bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			}

			VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{};
			descriptorLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorLayoutInfo.bindingCount = 7;
			descriptorLayoutInfo.pBindings = bindings;

			result = vkCreateDescriptorSetLayout(vkDevice, &descriptorLayoutInfo, nullptr, &descriptorSetLayout);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create descriptor set layout (error code: %d)", result);
			}

			VkDescriptorPoolSize poolSize{};
			poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSize.descriptorCount = 7;

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes = &poolSize;
			poolInfo.maxSets = 1;

			result = vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &descriptorPool);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create descriptor pool (error code: %d)", result);
			}

			// Allocate descriptor set
			VkDescriptorSetAllocateInfo dsAllocInfo{};
			dsAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			dsAllocInfo.descriptorPool = descriptorPool;
			dsAllocInfo.descriptorSetCount = 1;
			dsAllocInfo.pSetLayouts = &descriptorSetLayout;

			result = vkAllocateDescriptorSets(vkDevice, &dsAllocInfo, &textureDescriptorSet);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to allocate descriptor set (error code: %d)", result);
			}

			// Pipeline layout with push constants and shadow map descriptor set
			VkPushConstantRange pushConstantRange{};
			pushConstantRange.offset = 0;
			if (physicalLighting) {
				pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
				pushConstantRange.size = sizeof(MapSolidPushConstants);
			} else {
				pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
				pushConstantRange.size = sizeof(MapSolidPushConstantsBasic);
			}

			// Set 1 = the shadow renderer's model-shadow sampling layout (cascade UBO +
			// depth maps), so the lit shaders can fold in dynamic model shadows. The
			// shadow renderer is created before the map renderer (see SetGameMap).
			VulkanShadowMapRenderer* smr = renderer.GetShadowMapRenderer();
			VkDescriptorSetLayout setLayouts[2] = {
			    descriptorSetLayout,
			    smr ? smr->GetSamplingSetLayout() : VK_NULL_HANDLE,
			};
			SPAssert(setLayouts[1] != VK_NULL_HANDLE);

			VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
			pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutInfo.setLayoutCount = 2;
			pipelineLayoutInfo.pSetLayouts = setLayouts;
			pipelineLayoutInfo.pushConstantRangeCount = 1;
			pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

			result = vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout);
			if (result != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
				vkDestroyShaderModule(vkDevice, fragShaderModule, nullptr);
				SPRaise("Failed to create pipeline layout (error code: %d)", result);
			}

			// Create graphics pipeline
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
			pipelineInfo.renderPass = renderPass;
			pipelineInfo.subpass = 0;

			result = vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1, &pipelineInfo, nullptr, &basicPipeline);

			// Reversed-winding sibling for the water reflection (mirror) pass. The
			// mirror view applies a negative-Z scale that flips triangle winding,
			// so the front face must be reversed to keep back-face culling correct
			// (matches GL toggling glFrontFace between the normal and mirror passes).
			if (result == VK_SUCCESS) {
				VkPipelineRasterizationStateCreateInfo mirrorRasterizer = rasterizer;
				mirrorRasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
				VkGraphicsPipelineCreateInfo mirrorPipelineInfo = pipelineInfo;
				mirrorPipelineInfo.pRasterizationState = &mirrorRasterizer;
				result = vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1,
				                                   &mirrorPipelineInfo, nullptr, &basicMirrorPipeline);
			}

			// Cleanup shader modules
			vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
			vkDestroyShaderModule(vkDevice, fragShaderModule, nullptr);

			if (result != VK_SUCCESS) {
				SPRaise("Failed to create graphics pipeline (error code: %d)", result);
			}

			// Load the 2D AO atlas (Gfx/AmbientOcclusion.png) once. Same texture
			// GL uses in BasicBlock.fs — 256×256 image of 16×16 precomputed
			// ambient-occlusion tiles, indexed by per-vertex aoCoord.
			if (!aoImage) {
				Handle<client::IImage> img = renderer.RegisterImage("Gfx/AmbientOcclusion.png");
				VulkanImageWrapper* wrapper = dynamic_cast<VulkanImageWrapper*>(img.GetPointerOrNull());
				if (wrapper && wrapper->GetVulkanImage()) {
					aoImage = Handle<VulkanImage>(wrapper->GetVulkanImage());
				} else {
					SPLog("Warning: failed to load Gfx/AmbientOcclusion.png for map AO");
				}
			}

			SPLog("Map renderer pipeline created successfully");

			// (Outlines are produced by the screen-space cavity post-process
			// pass — see VulkanCavityOutlineFilter — so the map renderer no
			// longer owns an outline pipeline.)

			// --- Create dynamic light pipeline ---
			{
				std::vector<uint32_t> dlVertCode = LoadSPIRVFile("Shaders/Vulkan/BasicBlockDynamicLit.vert.spv");
				std::vector<uint32_t> dlFragCode = LoadSPIRVFile("Shaders/Vulkan/BasicBlockDynamicLit.frag.spv");

				VkShaderModuleCreateInfo dlVertInfo{};
				dlVertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				dlVertInfo.codeSize = dlVertCode.size() * sizeof(uint32_t);
				dlVertInfo.pCode = dlVertCode.data();
				VkShaderModule dlVertModule;
				result = vkCreateShaderModule(vkDevice, &dlVertInfo, nullptr, &dlVertModule);
				if (result != VK_SUCCESS) {
					SPLog("Warning: Failed to create dlight vertex shader module");
					return;
				}

				VkShaderModuleCreateInfo dlFragInfo{};
				dlFragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				dlFragInfo.codeSize = dlFragCode.size() * sizeof(uint32_t);
				dlFragInfo.pCode = dlFragCode.data();
				VkShaderModule dlFragModule;
				result = vkCreateShaderModule(vkDevice, &dlFragInfo, nullptr, &dlFragModule);
				if (result != VK_SUCCESS) {
					vkDestroyShaderModule(vkDevice, dlVertModule, nullptr);
					SPLog("Warning: Failed to create dlight fragment shader module");
					return;
				}

				VkPipelineShaderStageCreateInfo dlStages[2]{};
				dlStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
				dlStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
				dlStages[0].module = dlVertModule;
				dlStages[0].pName = "main";
				dlStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
				dlStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
				dlStages[1].module = dlFragModule;
				dlStages[1].pName = "main";

				// Dlight pipeline layout: push constants for both vertex + fragment
				VkPushConstantRange dlPushRange{};
				dlPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
				dlPushRange.offset = 0;
				dlPushRange.size = sizeof(MapDlightPushConstants);

				// Set 0: spotlight projection cookie (combined image sampler).
				VkDescriptorSetLayout dlCookieLayout = renderer.GetDlightCookieSetLayout();

				VkPipelineLayoutCreateInfo dlLayoutInfo{};
				dlLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				dlLayoutInfo.setLayoutCount = (dlCookieLayout != VK_NULL_HANDLE) ? 1 : 0;
				dlLayoutInfo.pSetLayouts = (dlCookieLayout != VK_NULL_HANDLE) ? &dlCookieLayout : nullptr;
				dlLayoutInfo.pushConstantRangeCount = 1;
				dlLayoutInfo.pPushConstantRanges = &dlPushRange;

				result = vkCreatePipelineLayout(vkDevice, &dlLayoutInfo, nullptr, &dlightPipelineLayout);
				if (result != VK_SUCCESS) {
					vkDestroyShaderModule(vkDevice, dlVertModule, nullptr);
					vkDestroyShaderModule(vkDevice, dlFragModule, nullptr);
					SPLog("Warning: Failed to create dlight pipeline layout");
					return;
				}

				// Depth: test EQUAL, no write (additive pass on existing geometry)
				VkPipelineDepthStencilStateCreateInfo dlDepth{};
				dlDepth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
				dlDepth.depthTestEnable = VK_TRUE;
				dlDepth.depthWriteEnable = VK_FALSE;
				dlDepth.depthCompareOp = VK_COMPARE_OP_EQUAL;
				dlDepth.depthBoundsTestEnable = VK_FALSE;
				dlDepth.stencilTestEnable = VK_FALSE;

				// Additive blending: src*srcAlpha + dst*1 for color, 0 + dst*1 for alpha
				VkPipelineColorBlendAttachmentState dlBlend{};
				dlBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
				                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
				dlBlend.blendEnable = VK_TRUE;
				dlBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				dlBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
				dlBlend.colorBlendOp = VK_BLEND_OP_ADD;
				dlBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
				dlBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				dlBlend.alphaBlendOp = VK_BLEND_OP_ADD;

				VkPipelineColorBlendStateCreateInfo dlColorBlending{};
				dlColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
				dlColorBlending.logicOpEnable = VK_FALSE;
				dlColorBlending.attachmentCount = 1;
				dlColorBlending.pAttachments = &dlBlend;

				VkGraphicsPipelineCreateInfo dlPipelineInfo{};
				dlPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
				dlPipelineInfo.stageCount = 2;
				dlPipelineInfo.pStages = dlStages;
				dlPipelineInfo.pVertexInputState = &vertexInputInfo;
				dlPipelineInfo.pInputAssemblyState = &inputAssembly;
				dlPipelineInfo.pViewportState = &viewportState;
				dlPipelineInfo.pRasterizationState = &rasterizer;
				dlPipelineInfo.pMultisampleState = &multisampling;
				dlPipelineInfo.pDepthStencilState = &dlDepth;
				dlPipelineInfo.pColorBlendState = &dlColorBlending;
				dlPipelineInfo.pDynamicState = &dynamicState;
				dlPipelineInfo.layout = dlightPipelineLayout;
				dlPipelineInfo.renderPass = renderPass;
				dlPipelineInfo.subpass = 0;

				result = vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1, &dlPipelineInfo, nullptr, &dlightPipeline);

				vkDestroyShaderModule(vkDevice, dlVertModule, nullptr);
				vkDestroyShaderModule(vkDevice, dlFragModule, nullptr);

				if (result != VK_SUCCESS) {
					SPLog("Warning: Failed to create dlight pipeline (error code: %d)", result);
					dlightPipeline = VK_NULL_HANDLE;
				} else {
					SPLog("Map dynamic light pipeline created successfully");
				}
			}
		}

		void VulkanMapRenderer::DestroyPipelines() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			if (depthonlyPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, depthonlyPipeline, nullptr);
				depthonlyPipeline = VK_NULL_HANDLE;
			}

			if (basicPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, basicPipeline, nullptr);
				basicPipeline = VK_NULL_HANDLE;
			}

			if (basicMirrorPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, basicMirrorPipeline, nullptr);
				basicMirrorPipeline = VK_NULL_HANDLE;
			}

			if (dlightPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, dlightPipeline, nullptr);
				dlightPipeline = VK_NULL_HANDLE;
			}

			if (backfacePipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, backfacePipeline, nullptr);
				backfacePipeline = VK_NULL_HANDLE;
			}

			if (pipelineLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, pipelineLayout, nullptr);
				pipelineLayout = VK_NULL_HANDLE;
			}

			if (dlightPipelineLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, dlightPipelineLayout, nullptr);
				dlightPipelineLayout = VK_NULL_HANDLE;
			}

			if (descriptorSetLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(vkDevice, descriptorSetLayout, nullptr);
				descriptorSetLayout = VK_NULL_HANDLE;
			}

			if (descriptorPool != VK_NULL_HANDLE) {
				vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
				descriptorPool = VK_NULL_HANDLE;
			}
		}

		void VulkanMapRenderer::UpdateShadowDescriptor(VulkanImage* shadowImage,
		                                                VkImageView aoView,
		                                                VkSampler aoSampler,
		                                                VkImageView radFlatView,
		                                                VkImageView radXView,
		                                                VkImageView radYView,
		                                                VkImageView radZView,
		                                                VkSampler radSampler) {
			if (!shadowImage || textureDescriptorSet == VK_NULL_HANDLE)
				return;

			VkDescriptorImageInfo shadowInfo{};
			shadowInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			shadowInfo.imageView = shadowImage->GetImageView();
			shadowInfo.sampler = shadowImage->GetSampler();

			VkDescriptorImageInfo aoInfo{};
			aoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			aoInfo.imageView = aoView;
			aoInfo.sampler = aoSampler;

			VkDescriptorImageInfo radInfos[4]{};
			VkImageView radViews[4] = {radFlatView, radXView, radYView, radZView};
			for (int i = 0; i < 4; ++i) {
				radInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				radInfos[i].imageView = radViews[i];
				radInfos[i].sampler = radSampler;
			}

			// Binding 6 — 2D ambient-occlusion atlas (no-radiosity GL parity path).
			// Falls back to the 3D AO view if the atlas image isn't available so
			// the descriptor write doesn't reference an invalid image.
			VkDescriptorImageInfo aoAtlasInfo{};
			aoAtlasInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			if (aoImage) {
				aoAtlasInfo.imageView = aoImage->GetImageView();
				aoAtlasInfo.sampler = aoImage->GetSampler();
			} else {
				aoAtlasInfo.imageView = aoView;
				aoAtlasInfo.sampler = aoSampler;
			}

			VkWriteDescriptorSet writes[7]{};
			writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[0].dstSet = textureDescriptorSet;
			writes[0].dstBinding = 0;
			writes[0].descriptorCount = 1;
			writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[0].pImageInfo = &shadowInfo;
			writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[1].dstSet = textureDescriptorSet;
			writes[1].dstBinding = 1;
			writes[1].descriptorCount = 1;
			writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[1].pImageInfo = &aoInfo;
			for (int i = 0; i < 4; ++i) {
				writes[2 + i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[2 + i].dstSet = textureDescriptorSet;
				writes[2 + i].dstBinding = static_cast<uint32_t>(2 + i);
				writes[2 + i].descriptorCount = 1;
				writes[2 + i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[2 + i].pImageInfo = &radInfos[i];
			}
			writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[6].dstSet = textureDescriptorSet;
			writes[6].dstBinding = 6;
			writes[6].descriptorCount = 1;
			writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[6].pImageInfo = &aoAtlasInfo;

			vkUpdateDescriptorSets(device->GetDevice(), 7, writes, 0, nullptr);
		}

	} // namespace draw
} // namespace spades
