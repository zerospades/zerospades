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
#include <cstddef>

#include "VulkanMapChunk.h"
#include "VulkanMapRenderer.h"
#include "VulkanRenderer.h"
#include "VulkanBuffer.h"
#include "VulkanDynamicLight.h"
#include <Gui/SDLVulkanDevice.h>
#include <Client/GameMap.h>
#include <Core/Debug.h>
#include <Core/Settings.h>

SPADES_SETTING(r_water);

namespace spades {
	namespace draw {
		VulkanMapChunk::VulkanMapChunk(VulkanMapRenderer& r, client::GameMap* mp, int cx, int cy, int cz)
		    : renderer(r),
		      device(static_cast<gui::SDLVulkanDevice*>(r.renderer.GetDevice().Unmanage())) {
			SPADES_MARK_FUNCTION();

			map = mp;
			chunkX = cx;
			chunkY = cy;
			chunkZ = cz;
			needsUpdate = true;
			realized = false;

			centerPos = MakeVector3(
			    cx * (float)Size + (float)Size / 2,
			    cy * (float)Size + (float)Size / 2,
			    cz * (float)Size + (float)Size / 2
			);

			radius = (float)Size * 0.5F * sqrtf(3.0F);
			aabb = AABB3(
			    cx * (float)Size,
			    cy * (float)Size,
			    cz * (float)Size,
			    (float)Size,
			    (float)Size,
			    (float)Size
			);
		}

		VulkanMapChunk::~VulkanMapChunk() {
			SetRealized(false);
		}

		void VulkanMapChunk::SetRealized(bool b) {
			SPADES_MARK_FUNCTION_DEBUG();

			if (realized == b)
				return;

			if (!b) {
				// Release buffers
				vertexBuffer.Set(nullptr, false);
				indexBuffer.Set(nullptr, false);

				// Clear CPU-side data
				std::vector<Vertex> v;
				v.swap(vertices);

				std::vector<uint16_t> i;
				i.swap(indices);
			} else {
				needsUpdate = true;
			}

			realized = b;
		}

		uint8_t VulkanMapChunk::calcAOID(int x, int y, int z,
		                                 int ux, int uy, int uz, int vx, int vy, int vz) {
			int v = 0;
			if (IsSolid(x - ux, y - uy, z - uz))
				v |= 1;
			if (IsSolid(x + ux, y + uy, z + uz))
				v |= 1 << 1;
			if (IsSolid(x - vx, y - vy, z - vz))
				v |= 1 << 2;
			if (IsSolid(x + vx, y + vy, z + vz))
				v |= 1 << 3;
			if (IsSolid(x - ux + vx, y - uy + vy, z - uz + vz))
				v |= 1 << 4;
			if (IsSolid(x - ux - vx, y - uy - vy, z - uz - vz))
				v |= 1 << 5;
			if (IsSolid(x + ux + vx, y + uy + vy, z + uz + vz))
				v |= 1 << 6;
			if (IsSolid(x + ux - vx, y + uy - vy, z + uz - vz))
				v |= 1 << 7;
			return (uint8_t)v;
		}

		void VulkanMapChunk::EmitVertex(int aoX, int aoY, int aoZ, int x, int y, int z,
		                                int ux, int uy, int vx, int vy, uint32_t color,
		                                int nx, int ny, int nz) {
			SPADES_MARK_FUNCTION_DEBUG();

			int uz = (ux == 0 && uy == 0) ? 1 : 0;
			int vz = (vx == 0 && vy == 0) ? 1 : 0;

			// Evaluate ambient occlusion
			unsigned int aoID = calcAOID(aoX, aoY, aoZ, ux, uy, uz, vx, vy, vz);

			Vertex inst;
			if (nz == 1 || ny == 1)
				inst.shading = 0;
			else if (nx == 1 || nx == -1)
				inst.shading = 0;
			else if (nz == -1)
				inst.shading = 220;
			else
				inst.shading = 255;

			inst.x = x;
			inst.y = y;
			inst.z = z;

			inst.colorRed = (uint8_t)(color);
			inst.colorGreen = (uint8_t)(color >> 8);
			inst.colorBlue = (uint8_t)(color >> 16);

			inst.nx = nx;
			inst.ny = ny;
			inst.nz = nz;

			// Fixed position to avoid self-shadow glitch
			inst.sx = (x << 1) + ux + vx;
			inst.sy = (y << 1) + uy + vy;
			inst.sz = (z << 1) + uz + vz;

			unsigned int aoTexX = (aoID & 15) * 16;
			unsigned int aoTexY = (aoID >> 4) * 16;

			uint16_t idx = (uint16_t)vertices.size();
			inst.x = x;
			inst.y = y;
			inst.z = z;
			inst.aoX = aoTexX;
			inst.aoY = aoTexY;
			vertices.push_back(inst);

			inst.x = x + ux;
			inst.y = y + uy;
			inst.z = z + uz;
			inst.aoX = aoTexX + 15;
			inst.aoY = aoTexY;
			vertices.push_back(inst);

			inst.x = x + vx;
			inst.y = y + vy;
			inst.z = z + vz;
			inst.aoX = aoTexX;
			inst.aoY = aoTexY + 15;
			vertices.push_back(inst);

			inst.x = x + ux + vx;
			inst.y = y + uy + vy;
			inst.z = z + uz + vz;
			inst.aoX = aoTexX + 15;
			inst.aoY = aoTexY + 15;
			vertices.push_back(inst);

			indices.push_back(idx);
			indices.push_back(idx + 1);
			indices.push_back(idx + 2);
			indices.push_back(idx + 1);
			indices.push_back(idx + 3);
			indices.push_back(idx + 2);
		}

		bool VulkanMapChunk::IsSolid(int x, int y, int z) {
			if (!map)
				return false;
			if (z < 0)
				return false;
			if (z >= 64)
				return true;

			x = ((x % map->Width()) + map->Width()) % map->Width();
			y = ((y % map->Height()) + map->Height()) % map->Height();

			// Mirror GL's GLMapChunk hack: z=63 queries look up z=62 instead
			// when water is on, so coastal "water-surface" voxels get rendered
			// (their above-water neighbor counts as solid) and pure-water
			// columns drop out.
			return map->IsSolid(x, y, (z == 63 && (int)r_water > 0) ? 62 : z);
		}

		void VulkanMapChunk::Update() {
			SPADES_MARK_FUNCTION();

			if (!map)
				return;


			vertices.clear();
			indices.clear();

			int originX = chunkX << SizeBits;
			int originY = chunkY << SizeBits;
			int originZ = chunkZ << SizeBits;

			// Build mesh for this chunk
			for (int x = 0; x < Size; x++) {
				for (int y = 0; y < Size; y++) {
					for (int z = 0; z < Size; z++) {
						int wx = originX + x;
						int wy = originY + y;
						int wz = originZ + z;

						if (!IsSolid(wx, wy, wz))
							continue;

						uint32_t color = map->GetColor(wx, wy, wz);

						// Apply damage darkening - match OpenGL exactly
						// Health is stored in upper byte (0xHHBBGGRR format)
						int health = color >> 24;
						if (health < 100) {
							// Damaged block: darken by 50%
							color &= 0xFFFFFF;   // Mask to RGB only
							color &= 0xFEFEFE;   // Clear low bits
							color >>= 1;         // Divide RGB by 2
						}

						// Emit faces for this voxel
						// +Z face (up)
						if (!IsSolid(wx, wy, wz + 1)) {
							EmitVertex(wx, wy, wz + 1, x + 1, y, z + 1, -1, 0, 0, 1, color, 0, 0, 1);
						}

						// -Z face (down)
						if (!IsSolid(wx, wy, wz - 1)) {
							EmitVertex(wx, wy, wz - 1, x, y, z, 1, 0, 0, 1, color, 0, 0, -1);
						}

						// -X face
						if (!IsSolid(wx - 1, wy, wz)) {
							EmitVertex(wx - 1, wy, wz, x, y + 1, z, 0, 0, 0, -1, color, -1, 0, 0);
						}

						// +X face
						if (!IsSolid(wx + 1, wy, wz)) {
							EmitVertex(wx + 1, wy, wz, x + 1, y, z, 0, 0, 0, 1, color, 1, 0, 0);
						}

						// -Y face
						if (!IsSolid(wx, wy - 1, wz)) {
							EmitVertex(wx, wy - 1, wz, x, y, z, 0, 0, 1, 0, color, 0, -1, 0);
						}

						// +Y face
						if (!IsSolid(wx, wy + 1, wz)) {
							EmitVertex(wx, wy + 1, wz, x + 1, y + 1, z, 0, 0, -1, 0, color, 0, 1, 0);
						}
					}
				}
			}

			// Upload vertex buffer (reuse if size matches to reduce memory allocations)
			if (!vertices.empty()) {
				size_t vertexBufferSize = vertices.size() * sizeof(Vertex);

				if (!vertexBuffer || vertexBuffer->GetSize() != vertexBufferSize) {
					// Queue old buffer for deferred deletion to ensure GPU is done with it
					if (vertexBuffer) {
						renderer.GetRenderer().QueueBufferForDeletion(vertexBuffer);
					}
					vertexBuffer.Set(nullptr, false);
					vertexBuffer = Handle<VulkanBuffer>::New(
					    device, vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
					    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				}

				void* data = vertexBuffer->Map();
				memcpy(data, vertices.data(), vertexBufferSize);
				vertexBuffer->Unmap();
			} else {
				// Queue old buffer for deferred deletion
				if (vertexBuffer) {
					renderer.GetRenderer().QueueBufferForDeletion(vertexBuffer);
				}
				vertexBuffer.Set(nullptr, false);
			}

			// Upload index buffer (reuse if size matches to reduce memory allocations)
			if (!indices.empty()) {
				size_t indexBufferSize = indices.size() * sizeof(uint16_t);

				if (!indexBuffer || indexBuffer->GetSize() != indexBufferSize) {
					// Queue old buffer for deferred deletion to ensure GPU is done with it
					if (indexBuffer) {
						renderer.GetRenderer().QueueBufferForDeletion(indexBuffer);
					}
					indexBuffer.Set(nullptr, false);
					indexBuffer = Handle<VulkanBuffer>::New(
					    device, indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
					    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				}

				void* data = indexBuffer->Map();
				memcpy(data, indices.data(), indexBufferSize);
				indexBuffer->Unmap();
			} else {
				// Queue old buffer for deferred deletion
				if (indexBuffer) {
					renderer.GetRenderer().QueueBufferForDeletion(indexBuffer);
				}
				indexBuffer.Set(nullptr, false);
			}

			needsUpdate = false;
		}

		float VulkanMapChunk::DistanceFromEye(const Vector3& eye) {
			Vector3 diff = eye - centerPos;

			{
				const float mw = (float)map->Width();
				const float mh = (float)map->Height();
				if (diff.x < -(mw * 0.5F)) diff.x += mw;
				if (diff.y < -(mh * 0.5F)) diff.y += mh;
				if (diff.x >  (mw * 0.5F)) diff.x -= mw;
				if (diff.y >  (mh * 0.5F)) diff.y -= mh;
			}

			float dist = std::max(fabsf(diff.x), fabsf(diff.y));
			return std::max(dist - ((float)Size * 0.5F), 0.0F);
		}

		void VulkanMapChunk::UpdateIfNeeded() {
			if (needsUpdate && realized) {
				Update();
			}
		}

		void VulkanMapChunk::RenderSunlightPass(VkCommandBuffer commandBuffer) {
			SPADES_MARK_FUNCTION_DEBUG();

			if (indices.empty() || !vertexBuffer || !indexBuffer)
				return;

			const auto& eye = renderer.renderer.GetSceneDef().viewOrigin;
			Vector3 diff = eye - centerPos;
			float sx = 0.0F, sy = 0.0F;

			{
				const float mw = (float)map->Width();
				const float mh = (float)map->Height();
				if (diff.x >  (mw * 0.5F)) sx += mw;
				if (diff.y >  (mh * 0.5F)) sy += mh;
				if (diff.x < -(mw * 0.5F)) sx -= mw;
				if (diff.y < -(mh * 0.5F)) sy -= mh;
			}

			// Frustum cull
			{
				AABB3 bx = aabb;
				bx.min.x += sx; bx.max.x += sx;
				bx.min.y += sy; bx.max.y += sy;
				if (!renderer.renderer.BoxFrustrumCull(bx))
					return;
			}

			// Set up push constants (MVP matrix + model origin + fog data).
			// Matches GLMapRenderer: when r_fogShadow is on, the solid pass
			// must fade distant blocks to BLACK (not fog colour) so the
			// fog post-process can re-add the in-scattered light with
			// the directional shadow shafts on top. Using the full fog
			// colour here washes out the post-process contribution and
			// the shaft becomes invisible.
			Vector3 fogCol = renderer.renderer.GetFogColorForSolidPass();
			fogCol *= fogCol; // linearize

			if (renderer.physicalLighting) {
				MapSolidPushConstants pushConstants;

				pushConstants.projectionViewMatrix = renderer.renderer.GetProjectionViewMatrix();
				pushConstants.modelOrigin = MakeVector3(
					(float)(chunkX << SizeBits) + sx,
					(float)(chunkY << SizeBits) + sy,
					(float)(chunkZ << SizeBits)
				);
				pushConstants.fogDistance = renderer.renderer.GetFogDistance();
				pushConstants.viewOrigin = eye;
				pushConstants._pad = 0.0f;
				pushConstants.fogColor = fogCol;
				pushConstants._pad2 = 0.0f;
				pushConstants.sunDirection = renderer.renderer.GetSunDirection();
				pushConstants._pad3 = 0.0f;
				pushConstants.viewMatrix = renderer.renderer.GetViewMatrix();

				vkCmdPushConstants(commandBuffer, renderer.pipelineLayout,
				                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				                   0, sizeof(pushConstants), &pushConstants);
			} else {
				MapSolidPushConstantsBasic pushConstants;

				pushConstants.projectionViewMatrix = renderer.renderer.GetProjectionViewMatrix();
				pushConstants.modelOrigin = MakeVector3(
					(float)(chunkX << SizeBits) + sx,
					(float)(chunkY << SizeBits) + sy,
					(float)(chunkZ << SizeBits)
				);
				pushConstants.fogDistance = renderer.renderer.GetFogDistance();
				pushConstants.viewOrigin = eye;
				pushConstants._pad = 0.0f;
				pushConstants.fogColor = fogCol;
				pushConstants._pad2 = 0.0f;
				pushConstants.sunDirection = renderer.renderer.GetSunDirection();

				vkCmdPushConstants(commandBuffer, renderer.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
				                   0, sizeof(pushConstants), &pushConstants);
			}

			// Bind shadow map descriptor set
			if (renderer.textureDescriptorSet != VK_NULL_HANDLE) {
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				                        renderer.pipelineLayout, 0, 1,
				                        &renderer.textureDescriptorSet, 0, nullptr);
			}

			// Bind vertex buffer
			VkBuffer vb = vertexBuffer->GetBuffer();
			VkDeviceSize offsets[] = {0};
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, offsets);

			// Bind index buffer
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

			// Draw
			vkCmdDrawIndexed(commandBuffer, (uint32_t)indices.size(), 1, 0, 0, 0);
		}

		void VulkanMapChunk::RenderDepthPass(VkCommandBuffer commandBuffer) {
			SPADES_MARK_FUNCTION_DEBUG();

			if (indices.empty() || !vertexBuffer || !indexBuffer)
				return;

			// Bind vertex buffer
			VkBuffer vb = vertexBuffer->GetBuffer();
			VkDeviceSize offsets[] = {0};
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, offsets);

			// Bind index buffer
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

			// Draw
			vkCmdDrawIndexed(commandBuffer, (uint32_t)indices.size(), 1, 0, 0, 0);
		}

		void VulkanMapChunk::RenderShadowMapPass(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout) {
			SPADES_MARK_FUNCTION_DEBUG();

			if (indices.empty() || !vertexBuffer || !indexBuffer)
				return;

			const auto& eye = renderer.renderer.GetSceneDef().viewOrigin;
			Vector3 diff = eye - centerPos;
			float sx = 0.0F, sy = 0.0F;

			{
				const float mw = (float)map->Width();
				const float mh = (float)map->Height();
				if (diff.x >  (mw * 0.5F)) sx += mw;
				if (diff.y >  (mh * 0.5F)) sy += mh;
				if (diff.x < -(mw * 0.5F)) sx -= mw;
				if (diff.y < -(mh * 0.5F)) sy -= mh;
			}

			// Push model origin for this chunk
			Vector3 modelOrigin = MakeVector3(
				(float)(chunkX << SizeBits) + sx,
				(float)(chunkY << SizeBits) + sy,
				(float)(chunkZ << SizeBits)
			);
			vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
			                   0, sizeof(Vector3), &modelOrigin);

			// Bind vertex buffer
			VkBuffer vb = vertexBuffer->GetBuffer();
			VkDeviceSize offsets[] = {0};
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, offsets);

			// Bind index buffer
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

			// Draw
			vkCmdDrawIndexed(commandBuffer, (uint32_t)indices.size(), 1, 0, 0, 0);
		}

		void VulkanMapChunk::RenderDynamicLightPass(VkCommandBuffer commandBuffer,
		                                            const client::DynamicLightParam& light) {
			SPADES_MARK_FUNCTION_DEBUG();

			if (indices.empty() || !vertexBuffer || !indexBuffer)
				return;

			const auto& eye = renderer.renderer.GetSceneDef().viewOrigin;
			Vector3 diff = eye - centerPos;
			float sx = 0.0F, sy = 0.0F;

			{
				const float mw = (float)map->Width();
				const float mh = (float)map->Height();
				if (diff.x >  (mw * 0.5F)) sx += mw;
				if (diff.y >  (mh * 0.5F)) sy += mh;
				if (diff.x < -(mw * 0.5F)) sx -= mw;
				if (diff.y < -(mh * 0.5F)) sy -= mh;
			}

			// Frustum cull
			{
				AABB3 bx = aabb;
				bx.min.x += sx; bx.max.x += sx;
				bx.min.y += sy; bx.max.y += sy;
				if (!renderer.renderer.BoxFrustrumCull(bx))
					return;
			}

			Vector3 fogCol = renderer.renderer.GetFogColor();
			fogCol *= fogCol; // linearize

			// Build spot matrix for spotlights. GetProjectionMatrix() already maps
			// world space to [0,1] cookie UVs (its projMatrix bakes in the +0.5
			// bias), so use it directly — matches GL GLDynamicLightShader, which
			// sets dynamicLightSpotMatrix = light.GetProjectionMatrix() verbatim.
			// (Applying an extra Scale(0.5)*Translate(1,1,1) here double-biased the
			// projection and pushed the cone into a corner.)
			VulkanDynamicLight vkLight(light);
			Matrix4 spotMatrix = Matrix4::Identity();
			if (light.type == client::DynamicLightTypeSpotlight)
				spotMatrix = vkLight.GetProjectionMatrix();

			// Determine light type for shader
			float lightType = 0.0f; // point
			if (light.type == client::DynamicLightTypeLinear)
				lightType = 1.0f;
			else if (light.type == client::DynamicLightTypeSpotlight)
				lightType = 2.0f;

			// Linear light direction and length
			Vector3 linearDir = MakeVector3(0, 0, 0);
			float linearLength = 0.0f;
			if (light.type == client::DynamicLightTypeLinear) {
				Vector3 dir = light.point2 - light.origin;
				linearLength = dir.GetLength();
				if (linearLength > 0.0001f)
					linearDir = dir / linearLength;
			}

			MapDlightPushConstants pushConstants;

			pushConstants.projectionViewMatrix = renderer.renderer.GetProjectionViewMatrix();
			pushConstants.modelOrigin = MakeVector3(
				(float)(chunkX << SizeBits) + sx,
				(float)(chunkY << SizeBits) + sy,
				(float)(chunkZ << SizeBits)
			);
			pushConstants.fogDistance = renderer.renderer.GetFogDistance();
			pushConstants.viewOrigin = eye;
			pushConstants.lightRadius = light.radius;
			pushConstants.fogColor = fogCol;
			pushConstants.lightRadiusInversed = 1.0f / light.radius;
			pushConstants.lightOrigin = light.origin;
			pushConstants.lightTypeVal = lightType;
			pushConstants.lightColor = light.color;
			pushConstants.lightLinearLength = linearLength;
			pushConstants.lightLinearDirection = linearDir;
			pushConstants._pad = 0.0f;
			pushConstants.lightSpotMatrix = spotMatrix;

			vkCmdPushConstants(commandBuffer, renderer.dlightPipelineLayout,
			                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			                   0, sizeof(pushConstants), &pushConstants);

			// Bind vertex buffer
			VkBuffer vb = vertexBuffer->GetBuffer();
			VkDeviceSize offsets[] = {0};
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, offsets);

			// Bind index buffer
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

			// Draw
			vkCmdDrawIndexed(commandBuffer, (uint32_t)indices.size(), 1, 0, 0, 0);
		}

	} // namespace draw
} // namespace spades
