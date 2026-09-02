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

#include "VulkanOptimizedVoxelModel.h"
#include "VulkanSpirvCache.h"
#include "VulkanRenderer.h"
#include "VulkanMapRenderer.h"
#include "VulkanShadowMapRenderer.h"
#include "VulkanBuffer.h"
#include "VulkanImage.h"
#include "VulkanImageWrapper.h"
#include "VulkanDynamicLight.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Bitmap.h>
#include <Core/BitmapAtlasGenerator.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/Settings.h>
#include <Core/IStream.h>
#include <array>
#include <cstddef>
#include <map>

namespace spades {
	namespace draw {
		// Initialize static members
		VulkanOptimizedVoxelModel::PipelineCache VulkanOptimizedVoxelModel::sharedPipeline;
		int VulkanOptimizedVoxelModel::pipelineRefCount = 0;

		void VulkanOptimizedVoxelModel::PreloadShaders(VulkanRenderer& renderer) {
			SPADES_MARK_FUNCTION();
			SPLog("Preloading Vulkan model shaders");

			SPADES_SETTING(r_physicalLighting);
			if ((int)r_physicalLighting != 0) {
				SpirvCache::Preload({"Shaders/Vulkan/BasicModelVertexColorPhys.vert.spv",
				                     "Shaders/Vulkan/BasicModelVertexColorPhys.frag.spv",
				                     "Shaders/Vulkan/BasicModelVertexColorPhysGhost.frag.spv"});
			} else {
				SpirvCache::Preload({"Shaders/Vulkan/BasicModelVertexColor.vert.spv",
				                     "Shaders/Vulkan/BasicModelVertexColor.frag.spv",
				                     "Shaders/Vulkan/BasicModelVertexColorGhost.frag.spv"});
			}
			SpirvCache::Preload({"Shaders/Vulkan/ModelDynamicLit.vert.spv",
			                     "Shaders/Vulkan/ModelDynamicLit.frag.spv",
			                     "Shaders/Vulkan/ModelShadowMap.vert.spv",
			                     "Shaders/Vulkan/ShadowMap.frag.spv"});
		}

		void VulkanOptimizedVoxelModel::InvalidateSharedPipeline(gui::SDLVulkanDevice* device) {
			SPADES_MARK_FUNCTION();

			if (!device || sharedPipeline.pipeline == VK_NULL_HANDLE)
				return;

			VkDevice vkDevice = device->GetDevice();

			// Clean up all shared pipelines
			if (sharedPipeline.pipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, sharedPipeline.pipeline, nullptr);
				sharedPipeline.pipeline = VK_NULL_HANDLE;
			}
			if (sharedPipeline.mirroredPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, sharedPipeline.mirroredPipeline, nullptr);
				sharedPipeline.mirroredPipeline = VK_NULL_HANDLE;
			}
			if (sharedPipeline.dlightPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, sharedPipeline.dlightPipeline, nullptr);
				sharedPipeline.dlightPipeline = VK_NULL_HANDLE;
			}
			if (sharedPipeline.mirroredDlightPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, sharedPipeline.mirroredDlightPipeline, nullptr);
				sharedPipeline.mirroredDlightPipeline = VK_NULL_HANDLE;
			}
			if (sharedPipeline.shadowMapPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, sharedPipeline.shadowMapPipeline, nullptr);
				sharedPipeline.shadowMapPipeline = VK_NULL_HANDLE;
			}
			if (sharedPipeline.shadowMapPipelineLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, sharedPipeline.shadowMapPipelineLayout, nullptr);
				sharedPipeline.shadowMapPipelineLayout = VK_NULL_HANDLE;
			}
			sharedPipeline.shadowMapRenderPass = VK_NULL_HANDLE;
			if (sharedPipeline.ghostDepthPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, sharedPipeline.ghostDepthPipeline, nullptr);
				sharedPipeline.ghostDepthPipeline = VK_NULL_HANDLE;
			}
			if (sharedPipeline.mirroredGhostDepthPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, sharedPipeline.mirroredGhostDepthPipeline, nullptr);
				sharedPipeline.mirroredGhostDepthPipeline = VK_NULL_HANDLE;
			}
			if (sharedPipeline.ghostColorPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, sharedPipeline.ghostColorPipeline, nullptr);
				sharedPipeline.ghostColorPipeline = VK_NULL_HANDLE;
			}
			if (sharedPipeline.mirroredGhostColorPipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, sharedPipeline.mirroredGhostColorPipeline, nullptr);
				sharedPipeline.mirroredGhostColorPipeline = VK_NULL_HANDLE;
			}
			if (sharedPipeline.pipelineLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, sharedPipeline.pipelineLayout, nullptr);
				sharedPipeline.pipelineLayout = VK_NULL_HANDLE;
			}
			if (sharedPipeline.dlightPipelineLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, sharedPipeline.dlightPipelineLayout, nullptr);
				sharedPipeline.dlightPipelineLayout = VK_NULL_HANDLE;
			}
			if (sharedPipeline.descriptorSetLayout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(vkDevice, sharedPipeline.descriptorSetLayout, nullptr);
				sharedPipeline.descriptorSetLayout = VK_NULL_HANDLE;
			}
			sharedPipeline.renderPass = VK_NULL_HANDLE;

			SPLog("Invalidated shared voxel model pipeline cache");
		}

		VulkanOptimizedVoxelModel::VulkanOptimizedVoxelModel(VoxelModel* m, VulkanRenderer& r)
		    : renderer(r),
		      device(static_cast<gui::SDLVulkanDevice*>(r.GetDevice().Unmanage())),
		      descriptorPool(VK_NULL_HANDLE),
		      descriptorSet(VK_NULL_HANDLE),
		      numIndices(0) {
			SPADES_MARK_FUNCTION();

			// Increment reference count for shared pipeline
			pipelineRefCount++;

			BuildVertices(m);

			// Create vertex buffer
			if (!vertices.empty()) {
				size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
				vertexBuffer = Handle<VulkanBuffer>::New(
				    device, vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

				void* data = vertexBuffer->Map();
				memcpy(data, vertices.data(), vertexBufferSize);
				vertexBuffer->Unmap();
			}

			// Create index buffer
			if (!indices.empty()) {
				size_t indexBufferSize = indices.size() * sizeof(uint32_t);
				indexBuffer = Handle<VulkanBuffer>::New(
				    device, indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

				void* data = indexBuffer->Map();
				memcpy(data, indices.data(), indexBufferSize);
				indexBuffer->Unmap();
			}

			origin = m->GetOrigin();
			origin -= 0.5F; // (0,0,0) is center of voxel (0,0,0)

			dimensions.x = m->GetWidth();
			dimensions.y = m->GetHeight();
			dimensions.z = m->GetDepth();

			Vector3 minPos = {0, 0, 0};
			Vector3 maxPos = MakeVector3(dimensions);
			minPos += origin;
			maxPos += origin;
			Vector3 maxDiff = {
			    std::max(fabsf(minPos.x), fabsf(maxPos.x)),
			    std::max(fabsf(minPos.y), fabsf(maxPos.y)),
			    std::max(fabsf(minPos.z), fabsf(maxPos.z))
			};
			radius = maxDiff.GetLength();

			boundingBox.min = minPos;
			boundingBox.max = maxPos;

			// Clean up CPU-side data
			numIndices = (unsigned int)indices.size();
			std::vector<Vertex>().swap(vertices);
			std::vector<uint32_t>().swap(indices);
		}

		VulkanOptimizedVoxelModel::~VulkanOptimizedVoxelModel() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			// Clean up per-instance resources
			if (descriptorPool != VK_NULL_HANDLE) {
				vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
			}

			// Decrement reference count and clean up shared pipeline if this is the last instance
			pipelineRefCount--;
			if (pipelineRefCount == 0) {
				if (sharedPipeline.pipeline != VK_NULL_HANDLE) {
					vkDestroyPipeline(vkDevice, sharedPipeline.pipeline, nullptr);
					sharedPipeline.pipeline = VK_NULL_HANDLE;
				}
				if (sharedPipeline.dlightPipeline != VK_NULL_HANDLE) {
					vkDestroyPipeline(vkDevice, sharedPipeline.dlightPipeline, nullptr);
					sharedPipeline.dlightPipeline = VK_NULL_HANDLE;
				}
				if (sharedPipeline.shadowMapPipeline != VK_NULL_HANDLE) {
					vkDestroyPipeline(vkDevice, sharedPipeline.shadowMapPipeline, nullptr);
					sharedPipeline.shadowMapPipeline = VK_NULL_HANDLE;
				}
				if (sharedPipeline.ghostDepthPipeline != VK_NULL_HANDLE) {
					vkDestroyPipeline(vkDevice, sharedPipeline.ghostDepthPipeline, nullptr);
					sharedPipeline.ghostDepthPipeline = VK_NULL_HANDLE;
				}
				if (sharedPipeline.ghostColorPipeline != VK_NULL_HANDLE) {
					vkDestroyPipeline(vkDevice, sharedPipeline.ghostColorPipeline, nullptr);
					sharedPipeline.ghostColorPipeline = VK_NULL_HANDLE;
				}
				if (sharedPipeline.pipelineLayout != VK_NULL_HANDLE) {
					vkDestroyPipelineLayout(vkDevice, sharedPipeline.pipelineLayout, nullptr);
					sharedPipeline.pipelineLayout = VK_NULL_HANDLE;
				}
				if (sharedPipeline.dlightPipelineLayout != VK_NULL_HANDLE) {
					vkDestroyPipelineLayout(vkDevice, sharedPipeline.dlightPipelineLayout, nullptr);
					sharedPipeline.dlightPipelineLayout = VK_NULL_HANDLE;
				}
				if (sharedPipeline.descriptorSetLayout != VK_NULL_HANDLE) {
					vkDestroyDescriptorSetLayout(vkDevice, sharedPipeline.descriptorSetLayout, nullptr);
					sharedPipeline.descriptorSetLayout = VK_NULL_HANDLE;
				}
				sharedPipeline.renderPass = VK_NULL_HANDLE;
			}
		}

		void VulkanOptimizedVoxelModel::GenerateTexture() {
			SPADES_MARK_FUNCTION();

			if (bmps.empty()) {
				return;
			}

			// Since we're using vertex colors, we don't need texture atlas
			// Just release the bitmaps and clear the index
			for (size_t i = 0; i < bmps.size(); i++)
				bmps[i]->Release();
			bmps.clear();

			std::vector<uint16_t>().swap(bmpIndex);

			// Create a white placeholder texture for compatibility
			Handle<Bitmap> bmp(new Bitmap(1, 1), false);
			bmp->SetPixel(0, 0, 0xFFFFFFFF);

			// Create Vulkan texture from bitmap
			Handle<client::IImage> imgHandle = renderer.CreateImage(*bmp);

			// Get VulkanImage from the IImage (unwrap from VulkanImageWrapper if needed)
			VulkanImageWrapper* wrapper = dynamic_cast<VulkanImageWrapper*>(imgHandle.GetPointerOrNull());
			if (wrapper) {
				image = Handle<VulkanImage>(wrapper->GetVulkanImage());
			} else {
				image = imgHandle.Cast<VulkanImage>();
			}
		}

		uint8_t VulkanOptimizedVoxelModel::calcAOID(VoxelModel* m, int x, int y, int z,
		                                            int ux, int uy, int uz, int vx, int vy, int vz) {
			int v = 0;
			if (m->IsSolid(x - ux, y - uy, z - uz))
				v |= 1;
			if (m->IsSolid(x + ux, y + uy, z + uz))
				v |= 1 << 1;
			if (m->IsSolid(x - vx, y - vy, z - vz))
				v |= 1 << 2;
			if (m->IsSolid(x + vx, y + vy, z + vz))
				v |= 1 << 3;
			if (m->IsSolid(x - ux + vx, y - uy + vy, z - uz + vz))
				v |= 1 << 4;
			if (m->IsSolid(x - ux - vx, y - uy - vy, z - uz - vz))
				v |= 1 << 5;
			if (m->IsSolid(x + ux + vx, y + uy + vy, z + uz + vz))
				v |= 1 << 6;
			if (m->IsSolid(x + ux - vx, y + uy - vy, z + uz - vz))
				v |= 1 << 7;
			return (uint8_t)v;
		}

		void VulkanOptimizedVoxelModel::BuildVertices(VoxelModel* m) {
			SPADES_MARK_FUNCTION();

			// Use vertex colors instead of textures for simplicity and performance
			int w = m->GetWidth();
			int h = m->GetHeight();
			int d = m->GetDepth();

			// Helper lambda to emit a face with vertex colors.
			//
			// For each exposed face we bake the same per-face aoID byte that GL's
			// `OptimizedVoxelModel.fs` uses, then store per-vertex (aoX, aoY) atlas
			// coords so the no-radiosity fragment shader can sample the 2D AO atlas
			// (Gfx/AmbientOcclusion.png). Each face vertex sits at one of the four
			// 16×16-tile corners; v[0,1,2,3] wind CCW around the quad so the corners
			// in AO space are (lo,lo)→(hi,lo)→(hi,hi)→(lo,hi).
			auto EmitFace = [&](int x, int y, int z, int nx, int ny, int nz, uint32_t color) {
				uint32_t idx = (uint32_t)vertices.size();

				// Extract RGB components
				uint8_t r = color & 0xFF;
				uint8_t g = (color >> 8) & 0xFF;
				uint8_t b = (color >> 16) & 0xFF;

				// Calculate face vertices based on normal direction
				Vertex v[4];
				for (int i = 0; i < 4; i++) {
					v[i].nx = nx;
					v[i].ny = ny;
					v[i].nz = nz;
					v[i].colorR = r;
					v[i].colorG = g;
					v[i].colorB = b;
					v[i].padding = 0;
				}

				// Per-face geometry + AO sample axes (u along v[0]→v[1], v along v[0]→v[3]).
				int aoSampleX = x, aoSampleY = y, aoSampleZ = z;
				int ux = 0, uy = 0, uz = 0;
				int vx = 0, vy = 0, vz = 0;

				if (nx == 1) { // +X face
					v[0].x = x + 1; v[0].y = y;     v[0].z = z;
					v[1].x = x + 1; v[1].y = y + 1; v[1].z = z;
					v[2].x = x + 1; v[2].y = y + 1; v[2].z = z + 1;
					v[3].x = x + 1; v[3].y = y;     v[3].z = z + 1;
					aoSampleX = x + 1;
					uy = 1; vz = 1;
				} else if (nx == -1) { // -X face
					v[0].x = x; v[0].y = y;     v[0].z = z;
					v[1].x = x; v[1].y = y;     v[1].z = z + 1;
					v[2].x = x; v[2].y = y + 1; v[2].z = z + 1;
					v[3].x = x; v[3].y = y + 1; v[3].z = z;
					aoSampleX = x - 1;
					uz = 1; vy = 1;
				} else if (ny == 1) { // +Y face
					v[0].x = x;     v[0].y = y + 1; v[0].z = z;
					v[1].x = x;     v[1].y = y + 1; v[1].z = z + 1;
					v[2].x = x + 1; v[2].y = y + 1; v[2].z = z + 1;
					v[3].x = x + 1; v[3].y = y + 1; v[3].z = z;
					aoSampleY = y + 1;
					uz = 1; vx = 1;
				} else if (ny == -1) { // -Y face
					v[0].x = x;     v[0].y = y; v[0].z = z;
					v[1].x = x + 1; v[1].y = y; v[1].z = z;
					v[2].x = x + 1; v[2].y = y; v[2].z = z + 1;
					v[3].x = x;     v[3].y = y; v[3].z = z + 1;
					aoSampleY = y - 1;
					ux = 1; vz = 1;
				} else if (nz == 1) { // +Z face
					v[0].x = x;     v[0].y = y;     v[0].z = z + 1;
					v[1].x = x + 1; v[1].y = y;     v[1].z = z + 1;
					v[2].x = x + 1; v[2].y = y + 1; v[2].z = z + 1;
					v[3].x = x;     v[3].y = y + 1; v[3].z = z + 1;
					aoSampleZ = z + 1;
					ux = 1; vy = 1;
				} else { // -Z face (nz == -1)
					v[0].x = x;     v[0].y = y;     v[0].z = z;
					v[1].x = x;     v[1].y = y + 1; v[1].z = z;
					v[2].x = x + 1; v[2].y = y + 1; v[2].z = z;
					v[3].x = x + 1; v[3].y = y;     v[3].z = z;
					aoSampleZ = z - 1;
					uy = 1; vx = 1;
				}

				uint8_t aoID = calcAOID(m, aoSampleX, aoSampleY, aoSampleZ,
				                        ux, uy, uz, vx, vy, vz);
				uint8_t aoTexX = static_cast<uint8_t>((aoID & 15) * 16);
				uint8_t aoTexY = static_cast<uint8_t>((aoID >> 4) * 16);
				v[0].aoX = aoTexX;       v[0].aoY = aoTexY;
				v[1].aoX = aoTexX + 15;  v[1].aoY = aoTexY;
				v[2].aoX = aoTexX + 15;  v[2].aoY = aoTexY + 15;
				v[3].aoX = aoTexX;       v[3].aoY = aoTexY + 15;

				// Add vertices
				for (int i = 0; i < 4; i++) {
					vertices.push_back(v[i]);
				}

				// Add indices for two triangles
				indices.push_back(idx);
				indices.push_back(idx + 1);
				indices.push_back(idx + 2);
				indices.push_back(idx);
				indices.push_back(idx + 2);
				indices.push_back(idx + 3);
			};

			// Generate faces for all solid voxels
			for (int x = 0; x < w; x++) {
				for (int y = 0; y < h; y++) {
					for (int z = 0; z < d; z++) {
						if (!m->IsSolid(x, y, z))
							continue;

						uint32_t color = m->GetColor(x, y, z);

						// Check each face and emit if exposed
						if (!m->IsSolid(x + 1, y, z)) EmitFace(x, y, z, 1, 0, 0, color);
						if (!m->IsSolid(x - 1, y, z)) EmitFace(x, y, z, -1, 0, 0, color);
						if (!m->IsSolid(x, y + 1, z)) EmitFace(x, y, z, 0, 1, 0, color);
						if (!m->IsSolid(x, y - 1, z)) EmitFace(x, y, z, 0, -1, 0, color);
						if (!m->IsSolid(x, y, z + 1)) EmitFace(x, y, z, 0, 0, 1, color);
						if (!m->IsSolid(x, y, z - 1)) EmitFace(x, y, z, 0, 0, -1, color);
					}
				}
			}
		}

		void VulkanOptimizedVoxelModel::Prerender(VkCommandBuffer commandBuffer,
		                                          std::vector<client::ModelRenderParam> params,
		                                          bool ghostPass) {
			SPADES_MARK_FUNCTION();

			// Depth prerender writes only to depth buffer, no color output
			// This is used for early-Z optimization and occlusion culling

			if (numIndices == 0 || !vertexBuffer || !indexBuffer)
				return;

			// Skip if no instances to render
			if (params.empty())
				return;

			VkRenderPass renderPass = renderer.GetOffscreenRenderPass();
			if (sharedPipeline.pipeline == VK_NULL_HANDLE || sharedPipeline.renderPass != renderPass) {
				CreatePipeline(renderPass);
			}

			// Select pipeline: ghost depth prepass or opaque depth prepass
			VkPipeline activePipeline = ghostPass ? sharedPipeline.ghostDepthPipeline : sharedPipeline.pipeline;
			VkPipeline activeMirroredPipeline = ghostPass ? sharedPipeline.mirroredGhostDepthPipeline : sharedPipeline.mirroredPipeline;
			if (activePipeline == VK_NULL_HANDLE)
				return;

			VkPipeline boundPipeline = VK_NULL_HANDLE;
			auto bindPipeline = [&](VkPipeline p) {
				if (p != boundPipeline) {
					vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
					boundPipeline = p;
				}
			};
			bindPipeline(activePipeline);

			// Bind shadow map descriptor set
			VulkanMapRenderer* mapRendererPrerender = renderer.GetMapRenderer();
			if (mapRendererPrerender) {
				VkDescriptorSet shadowDs = mapRendererPrerender->GetShadowDescriptorSet();
				if (shadowDs != VK_NULL_HANDLE) {
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
					                        sharedPipeline.pipelineLayout, 0, 1,
					                        &shadowDs, 0, nullptr);
				}
			}
			{
				VulkanShadowMapRenderer* smr = renderer.GetShadowMapRenderer();
				if (smr && smr->GetSamplingDescriptorSet() != VK_NULL_HANDLE) {
					VkDescriptorSet samplingSet = smr->GetSamplingDescriptorSet();
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
					                        sharedPipeline.pipelineLayout, 1, 1,
					                        &samplingSet, 0, nullptr);
				}
			}

			// Bind vertex buffer
			VkBuffer vb = vertexBuffer->GetBuffer();
			VkDeviceSize offsets[] = {0};
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, offsets);

			// Bind index buffer
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

			const Matrix4& projectionViewMatrix = renderer.GetProjectionViewMatrix();
			const auto& eye = renderer.GetSceneDef().viewOrigin;
			// Match GL (GLOptimizedVoxelModel): models fade to BLACK under r_fogShadow
			// so the fog post-process re-adds in-scattered light. Same as the sunlight
			// pass below.
			Vector3 fogCol = renderer.GetFogColorForSolidPass();
			fogCol *= fogCol; // linearize
			float fogDist = renderer.GetFogDistance();

			for (const auto& param : params) {
				if (ghostPass != param.ghost)
					continue;

				// Switch to mirrored pipeline when the model matrix has a negative determinant
				{
					const auto& ax = param.matrix.GetAxis(0);
					const auto& ay = param.matrix.GetAxis(1);
					const auto& az = param.matrix.GetAxis(2);
					bool isMirrored = Vector3::Dot(Vector3::Cross(ax, ay), az) < 0.0F;
					bindPipeline(isMirrored ? activeMirroredPipeline : activePipeline);
				}

				Matrix4 mvpMatrix = projectionViewMatrix * param.matrix;

				// Compute fog density from model's world position
				Vector4 modelWorldPos4 = param.matrix * MakeVector4(origin.x, origin.y, origin.z, 1.0f);
				float dx = modelWorldPos4.x - eye.x;
				float dy = modelWorldPos4.y - eye.y;
				float horzDistSq = dx * dx + dy * dy;
				float fogDensity = std::min(horzDistSq / (fogDist * fogDist), 1.0f);

				ModelSolidPushConstants pushConstants;

				pushConstants.projectionViewMatrix = mvpMatrix;
				pushConstants.modelMatrix = param.matrix;
				pushConstants.modelOrigin = origin;
				pushConstants.fogDensity = fogDensity;
				pushConstants.customColor = param.customColor;
				// Ghost depth prepass writes full color; set opacity=1.0 (blend is OFF)
				pushConstants.opacity = ghostPass ? 1.0f : 0.0f;
				pushConstants.fogColor = fogCol;
				pushConstants.sunDirection = renderer.GetSunDirection();
				// Reflection pass: clip geometry below the water plane (z=63) so
				// underwater players can't leak into the mirror. +inf elsewhere = no clip.
				pushConstants.mirrorClipZ = renderer.IsRenderingMirror() ? 63.0f : 1.0e9f;

				uint32_t pcSize = offsetof(ModelSolidPushConstants, physicalTail);
				VkShaderStageFlags pcStages = (ghostPass || sharedPipeline.physicalLighting)
					? (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
					: VK_SHADER_STAGE_VERTEX_BIT;
				if (sharedPipeline.physicalLighting) {
					pushConstants.physicalTail = 0.0f;
					pushConstants.viewMatrix = renderer.GetViewMatrix();
					pushConstants.viewOrigin = renderer.GetSceneDef().viewOrigin;
					pcSize = sizeof(pushConstants);
				}
				vkCmdPushConstants(commandBuffer, sharedPipeline.pipelineLayout, pcStages,
				                   0, pcSize, &pushConstants);

				vkCmdDrawIndexed(commandBuffer, numIndices, 1, 0, 0, 0);
			}
		}

		void VulkanOptimizedVoxelModel::RenderShadowMapPass(VkCommandBuffer commandBuffer,
		                                                    std::vector<client::ModelRenderParam> params,
		                                                    const Matrix4& lightMatrix,
		                                                    VkRenderPass shadowRenderPass) {
			SPADES_MARK_FUNCTION();

			if (numIndices == 0 || !vertexBuffer || !indexBuffer)
				return;

			if (params.empty())
				return;

			// Models can't share the map-chunk shadow pipeline (chunk vertex stride +
			// translation-only push constant). Build/refresh a dedicated one carrying
			// the full per-instance MVP. Lazy + rebuilt only if the shadow render pass
			// changed (e.g. shadow map recreated).
			if (sharedPipeline.shadowMapPipeline == VK_NULL_HANDLE ||
			    sharedPipeline.shadowMapRenderPass != shadowRenderPass) {
				CreateShadowPipeline(shadowRenderPass);
			}
			if (sharedPipeline.shadowMapPipeline == VK_NULL_HANDLE)
				return;

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			                  sharedPipeline.shadowMapPipeline);

			// Bind vertex buffer
			VkBuffer vb = vertexBuffer->GetBuffer();
			VkDeviceSize offsets[] = {0};
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, offsets);

			// Bind index buffer
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

			// Render each shadow-casting instance with its own light-space MVP.
			// The sunlight path transforms local vertices as param.matrix * (pos +
			// origin) (see BasicModelVertexColor.vert), so bake the origin offset into
			// the matrix here rather than into the shader. mvp maps model-space ->
			// light clip space.
			Matrix4 originMatrix = Matrix4::Translate(origin);
			for (const auto& param : params) {
				if (param.depthHack || !param.castShadow || param.ghost)
					continue;
				Matrix4 mvp = lightMatrix * param.matrix * originMatrix;
				vkCmdPushConstants(commandBuffer, sharedPipeline.shadowMapPipelineLayout,
				                   VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Matrix4), &mvp);
				vkCmdDrawIndexed(commandBuffer, numIndices, 1, 0, 0, 0);
			}
		}

		void VulkanOptimizedVoxelModel::CreateShadowPipeline(VkRenderPass shadowRenderPass) {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			// Tear down a stale pipeline/layout if the render pass changed.
			if (sharedPipeline.shadowMapPipeline != VK_NULL_HANDLE) {
				vkDeviceWaitIdle(vkDevice);
				vkDestroyPipeline(vkDevice, sharedPipeline.shadowMapPipeline, nullptr);
				sharedPipeline.shadowMapPipeline = VK_NULL_HANDLE;
			}
			if (sharedPipeline.shadowMapPipelineLayout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, sharedPipeline.shadowMapPipelineLayout, nullptr);
				sharedPipeline.shadowMapPipelineLayout = VK_NULL_HANDLE;
			}

			// Depth-only vertex shader; reuse the empty map-chunk shadow fragment shader.
			auto LoadSPIRVFile = [](const char* filename) -> std::vector<uint32_t> {
				return SpirvCache::Load(filename);
			};

			std::vector<uint32_t> vertCode = LoadSPIRVFile("Shaders/Vulkan/ModelShadowMap.vert.spv");
			std::vector<uint32_t> fragCode = LoadSPIRVFile("Shaders/Vulkan/ShadowMap.frag.spv");

			VkShaderModuleCreateInfo vertInfo{};
			vertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			vertInfo.codeSize = vertCode.size() * sizeof(uint32_t);
			vertInfo.pCode = vertCode.data();
			VkShaderModule vertModule;
			if (vkCreateShaderModule(vkDevice, &vertInfo, nullptr, &vertModule) != VK_SUCCESS) {
				SPRaise("Failed to create model shadow vertex shader module");
			}

			VkShaderModuleCreateInfo fragInfo{};
			fragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			fragInfo.codeSize = fragCode.size() * sizeof(uint32_t);
			fragInfo.pCode = fragCode.data();
			VkShaderModule fragModule;
			if (vkCreateShaderModule(vkDevice, &fragInfo, nullptr, &fragModule) != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertModule, nullptr);
				SPRaise("Failed to create model shadow fragment shader module");
			}

			VkPipelineShaderStageCreateInfo vertStage{};
			vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
			vertStage.module = vertModule;
			vertStage.pName = "main";

			VkPipelineShaderStageCreateInfo fragStage{};
			fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			fragStage.module = fragModule;
			fragStage.pName = "main";

			VkPipelineShaderStageCreateInfo shaderStages[] = {vertStage, fragStage};

			// Vertex input: model format, position (uint8 x,y,z) at location 0 only.
			VkVertexInputBindingDescription binding{};
			binding.binding = 0;
			binding.stride = sizeof(Vertex);
			binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			VkVertexInputAttributeDescription attr{};
			attr.binding = 0;
			attr.location = 0;
			attr.format = VK_FORMAT_R8G8B8_UINT;
			attr.offset = offsetof(Vertex, x);

			VkPipelineVertexInputStateCreateInfo vertexInput{};
			vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInput.vertexBindingDescriptionCount = 1;
			vertexInput.pVertexBindingDescriptions = &binding;
			vertexInput.vertexAttributeDescriptionCount = 1;
			vertexInput.pVertexAttributeDescriptions = &attr;

			VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
			inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			inputAssembly.primitiveRestartEnable = VK_FALSE;

			VkPipelineViewportStateCreateInfo viewportState{};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.scissorCount = 1;

			VkPipelineRasterizationStateCreateInfo rasterizer{};
			rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizer.depthClampEnable = VK_FALSE;
			rasterizer.rasterizerDiscardEnable = VK_FALSE;
			rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizer.lineWidth = 1.0f;
			// Models carry arbitrary (incl. negative-determinant) transforms, so cull
			// nothing: a depth-only caster must not drop faces based on winding.
			rasterizer.cullMode = VK_CULL_MODE_NONE;
			rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rasterizer.depthBiasEnable = VK_TRUE; // bias value set dynamically by the caller

			VkPipelineMultisampleStateCreateInfo multisampling{};
			multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampling.sampleShadingEnable = VK_FALSE;
			multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			VkPipelineDepthStencilStateCreateInfo depthStencil{};
			depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencil.depthTestEnable = VK_TRUE;
			depthStencil.depthWriteEnable = VK_TRUE;
			depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
			depthStencil.depthBoundsTestEnable = VK_FALSE;
			depthStencil.stencilTestEnable = VK_FALSE;

			// Depth-only pass: no colour attachments.
			VkPipelineColorBlendStateCreateInfo colorBlending{};
			colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlending.logicOpEnable = VK_FALSE;
			colorBlending.attachmentCount = 0;
			colorBlending.pAttachments = nullptr;

			VkDynamicState dynamicStates[] = {
				VK_DYNAMIC_STATE_VIEWPORT,
				VK_DYNAMIC_STATE_SCISSOR,
				VK_DYNAMIC_STATE_DEPTH_BIAS
			};
			VkPipelineDynamicStateCreateInfo dynamicState{};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = 3;
			dynamicState.pDynamicStates = dynamicStates;

			// Pipeline layout: no descriptor sets, single mat4 push constant (the MVP).
			VkPushConstantRange pushRange{};
			pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			pushRange.offset = 0;
			pushRange.size = sizeof(Matrix4);

			VkPipelineLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layoutInfo.setLayoutCount = 0;
			layoutInfo.pSetLayouts = nullptr;
			layoutInfo.pushConstantRangeCount = 1;
			layoutInfo.pPushConstantRanges = &pushRange;

			if (vkCreatePipelineLayout(vkDevice, &layoutInfo, nullptr,
			                           &sharedPipeline.shadowMapPipelineLayout) != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertModule, nullptr);
				vkDestroyShaderModule(vkDevice, fragModule, nullptr);
				SPRaise("Failed to create model shadow pipeline layout");
			}

			VkGraphicsPipelineCreateInfo pipelineInfo{};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = 2;
			pipelineInfo.pStages = shaderStages;
			pipelineInfo.pVertexInputState = &vertexInput;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterizer;
			pipelineInfo.pMultisampleState = &multisampling;
			pipelineInfo.pDepthStencilState = &depthStencil;
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.pDynamicState = &dynamicState;
			pipelineInfo.layout = sharedPipeline.shadowMapPipelineLayout;
			pipelineInfo.renderPass = shadowRenderPass;
			pipelineInfo.subpass = 0;
			pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

			VkResult result = vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1,
			                                            &pipelineInfo, nullptr,
			                                            &sharedPipeline.shadowMapPipeline);

			vkDestroyShaderModule(vkDevice, vertModule, nullptr);
			vkDestroyShaderModule(vkDevice, fragModule, nullptr);

			if (result != VK_SUCCESS) {
				vkDestroyPipelineLayout(vkDevice, sharedPipeline.shadowMapPipelineLayout, nullptr);
				sharedPipeline.shadowMapPipelineLayout = VK_NULL_HANDLE;
				SPRaise("Failed to create model shadow graphics pipeline");
			}

			sharedPipeline.shadowMapRenderPass = shadowRenderPass;
		}

		void VulkanOptimizedVoxelModel::RenderSunlightPass(VkCommandBuffer commandBuffer,
		                                                   std::vector<client::ModelRenderParam> params,
		                                                   bool ghostPass) {
			SPADES_MARK_FUNCTION();

			if (numIndices == 0 || !vertexBuffer || !indexBuffer)
				return;

			// Lazy pipeline creation on first render (shared across all instances)
			VkRenderPass renderPass = renderer.GetOffscreenRenderPass();
			if (sharedPipeline.pipeline == VK_NULL_HANDLE || sharedPipeline.renderPass != renderPass) {
				CreatePipeline(renderPass);
			}

			// Select pipeline: ghost color pass or opaque pass
			VkPipeline activePipeline = ghostPass ? sharedPipeline.ghostColorPipeline : sharedPipeline.pipeline;
			VkPipeline activeMirroredPipeline = ghostPass ? sharedPipeline.mirroredGhostColorPipeline : sharedPipeline.mirroredPipeline;
			if (activePipeline == VK_NULL_HANDLE)
				return;

			// Bind pipeline (will be switched per-draw for mirrored models)
			VkPipeline boundPipeline = VK_NULL_HANDLE;
			auto bindPipeline = [&](VkPipeline p) {
				if (p != boundPipeline) {
					vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
					boundPipeline = p;
				}
			};
			bindPipeline(activePipeline);

			// Bind shadow map descriptor set from map renderer
			VulkanMapRenderer* mapRenderer = renderer.GetMapRenderer();
			if (mapRenderer) {
				VkDescriptorSet shadowDs = mapRenderer->GetShadowDescriptorSet();
				if (shadowDs != VK_NULL_HANDLE) {
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
					                        sharedPipeline.pipelineLayout, 0, 1,
					                        &shadowDs, 0, nullptr);
				}
			}
			{
				VulkanShadowMapRenderer* smr = renderer.GetShadowMapRenderer();
				if (smr && smr->GetSamplingDescriptorSet() != VK_NULL_HANDLE) {
					VkDescriptorSet samplingSet = smr->GetSamplingDescriptorSet();
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
					                        sharedPipeline.pipelineLayout, 1, 1,
					                        &samplingSet, 0, nullptr);
				}
			}

			// Bind vertex buffer
			VkBuffer vb = vertexBuffer->GetBuffer();
			VkDeviceSize offsets[] = {0};
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, offsets);

			// Bind index buffer
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

			// Get projection-view matrix from renderer
			const Matrix4& projectionViewMatrix = renderer.GetProjectionViewMatrix();
			const auto& eye = renderer.GetSceneDef().viewOrigin;
			// GetFogColorForSolidPass returns BLACK when r_fogShadow is on, so
			// distant models fade to black and the fog post-process can paint
			// the lit/shadow directional shafts back in. Matches GL.
			Vector3 fogCol = renderer.GetFogColorForSolidPass();
			fogCol *= fogCol; // linearize
			float fogDist = renderer.GetFogDistance();

			bool mirror = renderer.IsRenderingMirror();
			int rw = renderer.GetRenderWidth();
			int rh = renderer.GetRenderHeight();

			// Draw each instance
			for (const auto& param : params) {
				if (mirror && param.depthHack)
					continue;
				if (ghostPass != param.ghost)
					continue;

				// Frustum cull
				{
					const auto& modelOrigin = param.matrix.GetOrigin();
					float rad = radius * param.matrix.GetAxis(0).GetLength();
					if (!renderer.SphereFrustrumCull(modelOrigin, rad))
						continue;
				}

				// Switch to mirrored pipeline when the effective winding is reversed.
				// A negative-determinant model matrix reverses winding; so does the
				// water reflection (mirror) pass, which scales the whole scene by
				// negative Z. XOR the two so culling stays correct when either — or
				// both — apply (matches GL toggling glFrontFace for the mirror pass).
				{
					const auto& ax = param.matrix.GetAxis(0);
					const auto& ay = param.matrix.GetAxis(1);
					const auto& az = param.matrix.GetAxis(2);
					bool isMirrored = Vector3::Dot(Vector3::Cross(ax, ay), az) < 0.0F;
					bindPipeline((isMirrored != mirror) ? activeMirroredPipeline : activePipeline);
				}

				// Compute final MVP matrix
				Matrix4 mvpMatrix = projectionViewMatrix * param.matrix;

				// Compute fog density from model's world position
				Vector4 modelWorldPos4 = param.matrix * MakeVector4(origin.x, origin.y, origin.z, 1.0f);
				float dx = modelWorldPos4.x - eye.x;
				float dy = modelWorldPos4.y - eye.y;
				float horzDistSq = dx * dx + dy * dy;
				float fogDensity = std::min(horzDistSq / (fogDist * fogDist), 1.0f);

				ModelSolidPushConstants pushConstants;

				pushConstants.projectionViewMatrix = mvpMatrix;
				pushConstants.modelMatrix = param.matrix;
				pushConstants.modelOrigin = origin;
				pushConstants.fogDensity = fogDensity;
				pushConstants.customColor = param.customColor;
				// Pass param.opacity as alpha for ghost models
				pushConstants.opacity = ghostPass ? param.opacity : 0.0f;
				pushConstants.fogColor = fogCol;
				pushConstants.sunDirection = renderer.GetSunDirection();
				// Reflection pass: clip geometry below the water plane (z=63) so
				// underwater players can't leak into the mirror. +inf elsewhere = no clip.
				pushConstants.mirrorClipZ = renderer.IsRenderingMirror() ? 63.0f : 1.0e9f;

				uint32_t pcSize = offsetof(ModelSolidPushConstants, physicalTail);
				VkShaderStageFlags pcStages = (ghostPass || sharedPipeline.physicalLighting)
					? (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
					: VK_SHADER_STAGE_VERTEX_BIT;
				if (sharedPipeline.physicalLighting) {
					pushConstants.physicalTail = 0.0f;
					pushConstants.viewMatrix = renderer.GetViewMatrix();
					pushConstants.viewOrigin = renderer.GetSceneDef().viewOrigin;
					pcSize = sizeof(pushConstants);
				}
				vkCmdPushConstants(commandBuffer, sharedPipeline.pipelineLayout, pcStages,
				                   0, pcSize, &pushConstants);

				if (param.depthHack) {
					VkViewport vp{0.0f, (float)rh, (float)rw, -(float)rh, 0.0f, 0.1f};
					vkCmdSetViewport(commandBuffer, 0, 1, &vp);
				}

				vkCmdDrawIndexed(commandBuffer, numIndices, 1, 0, 0, 0);

				if (param.depthHack) {
					VkViewport vp{0.0f, (float)rh, (float)rw, -(float)rh, 0.0f, 1.0f};
					vkCmdSetViewport(commandBuffer, 0, 1, &vp);
				}
			}
		}

		void VulkanOptimizedVoxelModel::RenderDynamicLightPass(VkCommandBuffer commandBuffer,
		                                                       std::vector<client::ModelRenderParam> params,
		                                                       std::vector<void*> lights) {
			SPADES_MARK_FUNCTION();

			if (numIndices == 0 || !vertexBuffer || !indexBuffer)
				return;

			if (params.empty() || lights.empty())
				return;

			VkRenderPass renderPass = renderer.GetOffscreenRenderPass();
			if (sharedPipeline.dlightPipeline == VK_NULL_HANDLE || sharedPipeline.renderPass != renderPass) {
				CreatePipeline(renderPass);
			}

			if (sharedPipeline.dlightPipeline == VK_NULL_HANDLE)
				return;

			VkPipeline boundDlightPipeline = VK_NULL_HANDLE;
			auto bindDlightPipeline = [&](VkPipeline p) {
				if (p != boundDlightPipeline) {
					vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
					boundDlightPipeline = p;
				}
			};
			bindDlightPipeline(sharedPipeline.dlightPipeline);

			// Bind vertex buffer
			VkBuffer vb = vertexBuffer->GetBuffer();
			VkDeviceSize offsets[] = {0};
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, offsets);

			// Bind index buffer
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

			const Matrix4& projectionViewMatrix = renderer.GetProjectionViewMatrix();
			const auto& eye = renderer.GetSceneDef().viewOrigin;
			float fogDist = renderer.GetFogDistance();
			bool mirror = renderer.IsRenderingMirror();
			int rw = renderer.GetRenderWidth();
			int rh = renderer.GetRenderHeight();

			for (void* lightPtr : lights) {
				const client::DynamicLightParam* light =
				    static_cast<const client::DynamicLightParam*>(lightPtr);

				// Light type
				float lightType = 0.0f; // point
				if (light->type == client::DynamicLightTypeLinear)
					lightType = 1.0f;
				else if (light->type == client::DynamicLightTypeSpotlight)
					lightType = 2.0f;

				// Linear light direction and length
				Vector3 linearDir = MakeVector3(0, 0, 0);
				float linearLength = 0.0f;
				if (light->type == client::DynamicLightTypeLinear) {
					Vector3 dir = light->point2 - light->origin;
					linearLength = dir.GetLength();
					if (linearLength > 0.0001f)
						linearDir = dir / linearLength;
				}

				// Spotlight projection matrix (matches VulkanMapChunk dlight path).
				// GetProjectionMatrix() already maps world space to [0,1] cookie
				// UVs, so use it directly — same as GL.
				Matrix4 spotMatrix = Matrix4::Identity();
				if (light->type == client::DynamicLightTypeSpotlight) {
					VulkanDynamicLight vkLight(*light);
					spotMatrix = vkLight.GetProjectionMatrix();
				}

				// Bind this light's spotlight cookie (set 0). Point/linear lights
				// have no image and fall back to the 1x1 white texture.
				VulkanImage* cookieImage = nullptr;
				if (light->image)
					cookieImage = static_cast<VulkanImageWrapper*>(light->image)->GetVulkanImage();
				VkDescriptorSet cookieSet = renderer.GetDlightCookieDescriptorSet(cookieImage);
				if (cookieSet != VK_NULL_HANDLE) {
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
					                        sharedPipeline.dlightPipelineLayout, 0, 1,
					                        &cookieSet, 0, nullptr);
				}

				for (const auto& param : params) {
					if (mirror && param.depthHack)
						continue;
					if (param.ghost)
						continue;

					// Switch to mirrored pipeline when the model matrix has a negative determinant
					{
						const auto& ax = param.matrix.GetAxis(0);
						const auto& ay = param.matrix.GetAxis(1);
						const auto& az = param.matrix.GetAxis(2);
						bool isMirrored = Vector3::Dot(Vector3::Cross(ax, ay), az) < 0.0F;
						bindDlightPipeline(isMirrored ? sharedPipeline.mirroredDlightPipeline : sharedPipeline.dlightPipeline);
					}

					Matrix4 mvpMatrix = projectionViewMatrix * param.matrix;

					// Compute fog density from model's world position
					Vector4 modelWorldPos4 = param.matrix * MakeVector4(origin.x, origin.y, origin.z, 1.0f);
					float dx = modelWorldPos4.x - eye.x;
					float dy = modelWorldPos4.y - eye.y;
					float horzDistSq = dx * dx + dy * dy;
					float fogDensity = std::min(horzDistSq / (fogDist * fogDist), 1.0f);

					ModelDlightPushConstants pushConstants;

					pushConstants.projectionViewModelMatrix = mvpMatrix;
					pushConstants.modelMatrix = param.matrix;
					pushConstants.modelOrigin = origin;
					pushConstants.fogDensityVal = fogDensity;
					pushConstants.customColor = param.customColor;
					pushConstants.lightRadius = light->radius;
					pushConstants.lightOrigin = light->origin;
					pushConstants.lightTypeVal = lightType;
					pushConstants.lightColor = light->color;
					pushConstants.lightRadiusInversed = 1.0f / light->radius;
					pushConstants.lightLinearDirection = linearDir;
					pushConstants.lightLinearLength = linearLength;
					pushConstants.lightSpotMatrix = spotMatrix;

					if (param.depthHack) {
						VkViewport vp{0.0f, (float)rh, (float)rw, -(float)rh, 0.0f, 0.1f};
						vkCmdSetViewport(commandBuffer, 0, 1, &vp);
					}

					vkCmdPushConstants(commandBuffer, sharedPipeline.dlightPipelineLayout,
					                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
					                   0, sizeof(pushConstants), &pushConstants);

					vkCmdDrawIndexed(commandBuffer, numIndices, 1, 0, 0, 0);

					if (param.depthHack) {
						VkViewport vp{0.0f, (float)rh, (float)rw, -(float)rh, 0.0f, 1.0f};
						vkCmdSetViewport(commandBuffer, 0, 1, &vp);
					}
				}
			}
		}

		void VulkanOptimizedVoxelModel::CreatePipeline(VkRenderPass renderPass) {
			SPADES_MARK_FUNCTION();

			// Clean up old pipeline if render pass changed
			VkDevice vkDevice = device->GetDevice();
			if (sharedPipeline.pipeline != VK_NULL_HANDLE && sharedPipeline.renderPass != renderPass) {
				// Wait for GPU to finish using the old pipeline before destroying it
				vkDeviceWaitIdle(vkDevice);
				vkDestroyPipeline(vkDevice, sharedPipeline.pipeline, nullptr);
				sharedPipeline.pipeline = VK_NULL_HANDLE;
				if (sharedPipeline.pipelineLayout != VK_NULL_HANDLE) {
					vkDestroyPipelineLayout(vkDevice, sharedPipeline.pipelineLayout, nullptr);
					sharedPipeline.pipelineLayout = VK_NULL_HANDLE;
				}
			}

			sharedPipeline.renderPass = renderPass;

			{
				SPADES_SETTING(r_physicalLighting);
				sharedPipeline.physicalLighting = (int)r_physicalLighting != 0;
			}

			// Load SPIR-V shaders
			auto LoadSPIRVFile = [](const char* filename) -> std::vector<uint32_t> {
				return SpirvCache::Load(filename);
			};

			std::vector<uint32_t> vertCode, fragCode;
			if (sharedPipeline.physicalLighting) {
				vertCode = LoadSPIRVFile("Shaders/Vulkan/BasicModelVertexColorPhys.vert.spv");
				fragCode = LoadSPIRVFile("Shaders/Vulkan/BasicModelVertexColorPhys.frag.spv");
			} else {
				vertCode = LoadSPIRVFile("Shaders/Vulkan/BasicModelVertexColor.vert.spv");
				fragCode = LoadSPIRVFile("Shaders/Vulkan/BasicModelVertexColor.frag.spv");
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

			// Specialization constant: USE_RADIOSITY for BasicModelVertexColor.frag —
			// picks the MapRadiosityNull-style 2D-AO ambient when r_radiosity == 0,
			// the radiosity-style 3D-ambient branch otherwise.  Mirrors the map renderer.
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
			if (!sharedPipeline.physicalLighting) {
				fragShaderStageInfo.pSpecializationInfo = &specInfo;
			}

			VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

			// Vertex input state
			VkVertexInputBindingDescription bindingDescription{};
			bindingDescription.binding = 0;
			bindingDescription.stride = sizeof(Vertex);
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			std::array<VkVertexInputAttributeDescription, 5> attributeDescriptions{};

			// Position (location 0)
			attributeDescriptions[0].binding = 0;
			attributeDescriptions[0].location = 0;
			attributeDescriptions[0].format = VK_FORMAT_R8G8B8_UINT;
			attributeDescriptions[0].offset = offsetof(Vertex, x);

			// Color (location 1)
			attributeDescriptions[1].binding = 0;
			attributeDescriptions[1].location = 1;
			attributeDescriptions[1].format = VK_FORMAT_R8G8B8_UINT;
			attributeDescriptions[1].offset = offsetof(Vertex, colorR);

			// Normal (location 2)
			attributeDescriptions[2].binding = 0;
			attributeDescriptions[2].location = 2;
			attributeDescriptions[2].format = VK_FORMAT_R8G8B8_SINT;
			attributeDescriptions[2].offset = offsetof(Vertex, nx);

			// aoX (location 3) and aoY (location 4) are sub-byte fields in the
			// otherwise-padded spaces after position and color. They live on
			// separate non-adjacent offsets so we bind them as two R8_UINT
			// attributes rather than one packed R8G8.
			attributeDescriptions[3].binding = 0;
			attributeDescriptions[3].location = 3;
			attributeDescriptions[3].format = VK_FORMAT_R8_UINT;
			attributeDescriptions[3].offset = offsetof(Vertex, aoX);

			attributeDescriptions[4].binding = 0;
			attributeDescriptions[4].location = 4;
			attributeDescriptions[4].format = VK_FORMAT_R8_UINT;
			attributeDescriptions[4].offset = offsetof(Vertex, aoY);

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
			rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rasterizer.depthBiasEnable = VK_FALSE;

			// Multisampling
			VkPipelineMultisampleStateCreateInfo multisampling{};
			multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampling.sampleShadingEnable = VK_FALSE;
			// Match the scene render pass sample count (MSAA). Models are
			// alpha-blended, so plain coverage (no alpha-to-coverage, which would
			// dither blended edges) is used.
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
			colorBlendAttachment.blendEnable = VK_TRUE;
			colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
			colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

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

			// Descriptor set layout (set 0). Must stay binding-compatible with
			// VulkanMapRenderer's layout — the voxel model reuses that
			// renderer's descriptor set (see RenderSunlightPass below).
			//   binding 0 — heightmap shadow 2D texture
			//   binding 1 — per-block ambient occlusion 3D texture
			//   binding 2 — radiosity flat (directional GI base) 3D texture
			//   binding 3 — radiosity X 3D texture
			//   binding 4 — radiosity Y 3D texture
			//   binding 5 — radiosity Z 3D texture
			{
				// Mirrors VulkanMapRenderer's 7-binding shadow descriptor set
				// (the model binds the map renderer's set at draw time, so the
				// layouts must match).  Binding 6 is the 2D AO atlas.
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

				result = vkCreateDescriptorSetLayout(vkDevice, &descriptorLayoutInfo, nullptr, &sharedPipeline.descriptorSetLayout);
				if (result != VK_SUCCESS) {
					SPRaise("Failed to create model descriptor set layout (error code: %d)", result);
				}
			}

			// Pipeline layout with push constants and shadow map descriptor set.
			// Physical lighting pushes the whole block; non-physical pushes only the
			// prefix before the physical-only tail.
			VkPushConstantRange pushConstantRange{};
			pushConstantRange.offset = 0;
			pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			if (sharedPipeline.physicalLighting) {
				pushConstantRange.size = sizeof(ModelSolidPushConstants);
			} else {
				pushConstantRange.size = offsetof(ModelSolidPushConstants, physicalTail);
			}

			// Set 1 = model-shadow cascade sampling (owned by the shadow map
			// renderer, same set the map lit pipeline binds). The Phys/ghost
			// fragment shaders that don't declare it are still compatible with
			// the wider layout.
			VulkanShadowMapRenderer* smrLayout = renderer.GetShadowMapRenderer();
			VkDescriptorSetLayout modelSetLayouts[2] = {
			    sharedPipeline.descriptorSetLayout,
			    smrLayout ? smrLayout->GetSamplingSetLayout() : VK_NULL_HANDLE};

			VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
			pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutInfo.setLayoutCount =
			    (modelSetLayouts[1] != VK_NULL_HANDLE) ? 2 : 1;
			pipelineLayoutInfo.pSetLayouts = modelSetLayouts;
			pipelineLayoutInfo.pushConstantRangeCount = 1;
			pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

			result = vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &sharedPipeline.pipelineLayout);
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
			pipelineInfo.layout = sharedPipeline.pipelineLayout;
			pipelineInfo.renderPass = renderPass;
			pipelineInfo.subpass = 0;

			result = vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1, &pipelineInfo, nullptr, &sharedPipeline.pipeline);

			// Mirrored variant: same pipeline, front-face culled (for models with negative-scale matrices)
			if (result == VK_SUCCESS) {
				VkPipelineRasterizationStateCreateInfo mirroredRasterizer = rasterizer;
				mirroredRasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
				pipelineInfo.pRasterizationState = &mirroredRasterizer;
				vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1, &pipelineInfo, nullptr, &sharedPipeline.mirroredPipeline);
				pipelineInfo.pRasterizationState = &rasterizer;
			}

			// Cleanup shader modules
			vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
			vkDestroyShaderModule(vkDevice, fragShaderModule, nullptr);

			if (result != VK_SUCCESS) {
				SPRaise("Failed to create graphics pipeline (error code: %d)", result);
			}

			SPLog("Created shared model rendering pipeline (vertex colors)");

			// --- Create dynamic light pipeline ---
			{
				std::vector<uint32_t> dlVertCode = LoadSPIRVFile("Shaders/Vulkan/ModelDynamicLit.vert.spv");
				std::vector<uint32_t> dlFragCode = LoadSPIRVFile("Shaders/Vulkan/ModelDynamicLit.frag.spv");

				VkShaderModuleCreateInfo dlVertInfo{};
				dlVertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				dlVertInfo.codeSize = dlVertCode.size() * sizeof(uint32_t);
				dlVertInfo.pCode = dlVertCode.data();
				VkShaderModule dlVertModule;
				result = vkCreateShaderModule(vkDevice, &dlVertInfo, nullptr, &dlVertModule);
				if (result != VK_SUCCESS) {
					SPLog("Warning: Failed to create model dlight vertex shader module");
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
					SPLog("Warning: Failed to create model dlight fragment shader module");
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

				// Dlight pipeline layout
				VkPushConstantRange dlPushRange{};
				dlPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
				dlPushRange.offset = 0;
				dlPushRange.size = sizeof(ModelDlightPushConstants);

				// Set 0: spotlight projection cookie (combined image sampler).
				VkDescriptorSetLayout dlCookieLayout = renderer.GetDlightCookieSetLayout();

				VkPipelineLayoutCreateInfo dlLayoutInfo{};
				dlLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				dlLayoutInfo.setLayoutCount = (dlCookieLayout != VK_NULL_HANDLE) ? 1 : 0;
				dlLayoutInfo.pSetLayouts = (dlCookieLayout != VK_NULL_HANDLE) ? &dlCookieLayout : nullptr;
				dlLayoutInfo.pushConstantRangeCount = 1;
				dlLayoutInfo.pPushConstantRanges = &dlPushRange;

				result = vkCreatePipelineLayout(vkDevice, &dlLayoutInfo, nullptr, &sharedPipeline.dlightPipelineLayout);
				if (result != VK_SUCCESS) {
					vkDestroyShaderModule(vkDevice, dlVertModule, nullptr);
					vkDestroyShaderModule(vkDevice, dlFragModule, nullptr);
					SPLog("Warning: Failed to create model dlight pipeline layout");
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

				// Additive blending
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
				dlPipelineInfo.layout = sharedPipeline.dlightPipelineLayout;
				dlPipelineInfo.renderPass = renderPass;
				dlPipelineInfo.subpass = 0;

				result = vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1, &dlPipelineInfo, nullptr, &sharedPipeline.dlightPipeline);

				// Mirrored dlight variant
				if (result == VK_SUCCESS) {
					VkPipelineRasterizationStateCreateInfo dlMirroredRasterizer = rasterizer;
					dlMirroredRasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
					dlPipelineInfo.pRasterizationState = &dlMirroredRasterizer;
					vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1, &dlPipelineInfo, nullptr, &sharedPipeline.mirroredDlightPipeline);
				}

				vkDestroyShaderModule(vkDevice, dlVertModule, nullptr);
				vkDestroyShaderModule(vkDevice, dlFragModule, nullptr);

				if (result != VK_SUCCESS) {
					SPLog("Warning: Failed to create model dlight pipeline (error code: %d)", result);
					sharedPipeline.dlightPipeline = VK_NULL_HANDLE;
				} else {
					SPLog("Created shared model dynamic light pipeline");
				}
			}

			// (Outlines are produced by the screen-space cavity post-process
			// pass — see VulkanCavityOutlineFilter — so the model renderer no
			// longer owns an outline pipeline.)

			// --- Create ghost depth pipeline (depth prepass for transparent models) ---
			{
				std::vector<uint32_t> gdVertCode, gdFragCode;
				if (sharedPipeline.physicalLighting) {
					gdVertCode = LoadSPIRVFile("Shaders/Vulkan/BasicModelVertexColorPhys.vert.spv");
					gdFragCode = LoadSPIRVFile("Shaders/Vulkan/BasicModelVertexColorPhys.frag.spv");
				} else {
					gdVertCode = LoadSPIRVFile("Shaders/Vulkan/BasicModelVertexColor.vert.spv");
					gdFragCode = LoadSPIRVFile("Shaders/Vulkan/BasicModelVertexColor.frag.spv");
				}

				VkShaderModuleCreateInfo gdVertInfo{};
				gdVertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				gdVertInfo.codeSize = gdVertCode.size() * sizeof(uint32_t);
				gdVertInfo.pCode = gdVertCode.data();
				VkShaderModule gdVertModule;
				result = vkCreateShaderModule(vkDevice, &gdVertInfo, nullptr, &gdVertModule);
				if (result != VK_SUCCESS) {
					SPLog("Warning: Failed to create ghost depth vertex shader module");
				} else {
					VkShaderModuleCreateInfo gdFragInfo{};
					gdFragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
					gdFragInfo.codeSize = gdFragCode.size() * sizeof(uint32_t);
					gdFragInfo.pCode = gdFragCode.data();
					VkShaderModule gdFragModule;
					result = vkCreateShaderModule(vkDevice, &gdFragInfo, nullptr, &gdFragModule);
					if (result != VK_SUCCESS) {
						vkDestroyShaderModule(vkDevice, gdVertModule, nullptr);
						SPLog("Warning: Failed to create ghost depth fragment shader module");
					} else {
						VkPipelineShaderStageCreateInfo gdStages[2]{};
						gdStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
						gdStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
						gdStages[0].module = gdVertModule;
						gdStages[0].pName = "main";
						gdStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
						gdStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
						gdStages[1].module = gdFragModule;
						gdStages[1].pName = "main";

						// Depth: write depth, compare LESS
						VkPipelineDepthStencilStateCreateInfo gdDepth{};
						gdDepth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
						gdDepth.depthTestEnable = VK_TRUE;
						gdDepth.depthWriteEnable = VK_TRUE;
						gdDepth.depthCompareOp = VK_COMPARE_OP_LESS;
						gdDepth.depthBoundsTestEnable = VK_FALSE;
						gdDepth.stencilTestEnable = VK_FALSE;

						// Write color (matches GL ghost prepass which renders full color + depth)
						VkPipelineColorBlendAttachmentState gdBlend{};
						gdBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
						                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
						gdBlend.blendEnable = VK_FALSE;

						VkPipelineColorBlendStateCreateInfo gdColorBlending{};
						gdColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
						gdColorBlending.logicOpEnable = VK_FALSE;
						gdColorBlending.attachmentCount = 1;
						gdColorBlending.pAttachments = &gdBlend;

						VkGraphicsPipelineCreateInfo gdPipelineInfo{};
						gdPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
						gdPipelineInfo.stageCount = 2;
						gdPipelineInfo.pStages = gdStages;
						gdPipelineInfo.pVertexInputState = &vertexInputInfo;
						gdPipelineInfo.pInputAssemblyState = &inputAssembly;
						gdPipelineInfo.pViewportState = &viewportState;
						gdPipelineInfo.pRasterizationState = &rasterizer;
						gdPipelineInfo.pMultisampleState = &multisampling;
						gdPipelineInfo.pDepthStencilState = &gdDepth;
						gdPipelineInfo.pColorBlendState = &gdColorBlending;
						gdPipelineInfo.pDynamicState = &dynamicState;
						gdPipelineInfo.layout = sharedPipeline.pipelineLayout;
						gdPipelineInfo.renderPass = renderPass;
						gdPipelineInfo.subpass = 0;

						result = vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1, &gdPipelineInfo, nullptr, &sharedPipeline.ghostDepthPipeline);

						// Mirrored ghost depth variant
						if (result == VK_SUCCESS) {
							VkPipelineRasterizationStateCreateInfo gdMirroredRasterizer = rasterizer;
							gdMirroredRasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
							gdPipelineInfo.pRasterizationState = &gdMirroredRasterizer;
							vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1, &gdPipelineInfo, nullptr, &sharedPipeline.mirroredGhostDepthPipeline);
						}

						vkDestroyShaderModule(vkDevice, gdVertModule, nullptr);
						vkDestroyShaderModule(vkDevice, gdFragModule, nullptr);

						if (result != VK_SUCCESS) {
							SPLog("Warning: Failed to create ghost depth pipeline (error code: %d)", result);
							sharedPipeline.ghostDepthPipeline = VK_NULL_HANDLE;
						} else {
							SPLog("Created shared model ghost depth pipeline");
						}
					}
				}
			}

			// --- Create ghost color pipeline (semi-transparent with EQUAL depth test) ---
			{
				std::vector<uint32_t> gcVertCode, gcFragCode;
				if (sharedPipeline.physicalLighting) {
					gcVertCode = LoadSPIRVFile("Shaders/Vulkan/BasicModelVertexColorPhys.vert.spv");
					gcFragCode = LoadSPIRVFile("Shaders/Vulkan/BasicModelVertexColorPhysGhost.frag.spv");
				} else {
					gcVertCode = LoadSPIRVFile("Shaders/Vulkan/BasicModelVertexColor.vert.spv");
					gcFragCode = LoadSPIRVFile("Shaders/Vulkan/BasicModelVertexColorGhost.frag.spv");
				}

				VkShaderModuleCreateInfo gcVertInfo{};
				gcVertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				gcVertInfo.codeSize = gcVertCode.size() * sizeof(uint32_t);
				gcVertInfo.pCode = gcVertCode.data();
				VkShaderModule gcVertModule;
				result = vkCreateShaderModule(vkDevice, &gcVertInfo, nullptr, &gcVertModule);
				if (result != VK_SUCCESS) {
					SPLog("Warning: Failed to create ghost color vertex shader module");
				} else {
					VkShaderModuleCreateInfo gcFragInfo{};
					gcFragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
					gcFragInfo.codeSize = gcFragCode.size() * sizeof(uint32_t);
					gcFragInfo.pCode = gcFragCode.data();
					VkShaderModule gcFragModule;
					result = vkCreateShaderModule(vkDevice, &gcFragInfo, nullptr, &gcFragModule);
					if (result != VK_SUCCESS) {
						vkDestroyShaderModule(vkDevice, gcVertModule, nullptr);
						SPLog("Warning: Failed to create ghost color fragment shader module");
					} else {
						VkPipelineShaderStageCreateInfo gcStages[2]{};
						gcStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
						gcStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
						gcStages[0].module = gcVertModule;
						gcStages[0].pName = "main";
						gcStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
						gcStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
						gcStages[1].module = gcFragModule;
						gcStages[1].pName = "main";

						// Depth: test EQUAL, no depth write (reads depth written by ghost depth prepass)
						VkPipelineDepthStencilStateCreateInfo gcDepth{};
						gcDepth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
						gcDepth.depthTestEnable = VK_TRUE;
						gcDepth.depthWriteEnable = VK_FALSE;
						gcDepth.depthCompareOp = VK_COMPARE_OP_EQUAL;
						gcDepth.depthBoundsTestEnable = VK_FALSE;
						gcDepth.stencilTestEnable = VK_FALSE;

						// Alpha blending: SRC_ALPHA / ONE_MINUS_SRC_ALPHA
						VkPipelineColorBlendAttachmentState gcBlend{};
						gcBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
						                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
						gcBlend.blendEnable = VK_TRUE;
						gcBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
						gcBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
						gcBlend.colorBlendOp = VK_BLEND_OP_ADD;
						gcBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
						gcBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
						gcBlend.alphaBlendOp = VK_BLEND_OP_ADD;

						VkPipelineColorBlendStateCreateInfo gcColorBlending{};
						gcColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
						gcColorBlending.logicOpEnable = VK_FALSE;
						gcColorBlending.attachmentCount = 1;
						gcColorBlending.pAttachments = &gcBlend;

						VkGraphicsPipelineCreateInfo gcPipelineInfo{};
						gcPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
						gcPipelineInfo.stageCount = 2;
						gcPipelineInfo.pStages = gcStages;
						gcPipelineInfo.pVertexInputState = &vertexInputInfo;
						gcPipelineInfo.pInputAssemblyState = &inputAssembly;
						gcPipelineInfo.pViewportState = &viewportState;
						gcPipelineInfo.pRasterizationState = &rasterizer;
						gcPipelineInfo.pMultisampleState = &multisampling;
						gcPipelineInfo.pDepthStencilState = &gcDepth;
						gcPipelineInfo.pColorBlendState = &gcColorBlending;
						gcPipelineInfo.pDynamicState = &dynamicState;
						gcPipelineInfo.layout = sharedPipeline.pipelineLayout;
						gcPipelineInfo.renderPass = renderPass;
						gcPipelineInfo.subpass = 0;

						result = vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1, &gcPipelineInfo, nullptr, &sharedPipeline.ghostColorPipeline);

						// Mirrored ghost color variant
						if (result == VK_SUCCESS) {
							VkPipelineRasterizationStateCreateInfo gcMirroredRasterizer = rasterizer;
							gcMirroredRasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
							gcPipelineInfo.pRasterizationState = &gcMirroredRasterizer;
							vkCreateGraphicsPipelines(vkDevice, renderer.GetPipelineCache(), 1, &gcPipelineInfo, nullptr, &sharedPipeline.mirroredGhostColorPipeline);
						}

						vkDestroyShaderModule(vkDevice, gcVertModule, nullptr);
						vkDestroyShaderModule(vkDevice, gcFragModule, nullptr);

						if (result != VK_SUCCESS) {
							SPLog("Warning: Failed to create ghost color pipeline (error code: %d)", result);
							sharedPipeline.ghostColorPipeline = VK_NULL_HANDLE;
						} else {
							SPLog("Created shared model ghost color pipeline");
						}
					}
				}
			}
		}

	} // namespace draw
} // namespace spades
