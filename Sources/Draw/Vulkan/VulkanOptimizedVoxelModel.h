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

#pragma once

#include <vector>
#include <vulkan/vulkan.h>
#include "VulkanModel.h"
#include <Core/VoxelModel.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanRenderer;
		class VulkanBuffer;
		class VulkanImage;

		// Push-constant blocks for the model pipelines, shared between the pipeline
		// layout (range size) and the draw calls (the push) so both derive from the
		// same sizeof() and can never drift. std430 aligns each vec3 to 16 bytes,
		// hence the trailing pad floats. The non-physical solid pass pushes only the
		// prefix up to the physical-only tail: offsetof(ModelSolidPushConstants,
		// physicalTail). An undersized range drops the tail on AMD (fine on MoltenVK).
		struct ModelSolidPushConstants { // physical lighting (268 bytes); 188 used non-physical
			Matrix4 projectionViewMatrix;
			Matrix4 modelMatrix;
			Vector3 modelOrigin;   float fogDensity;
			Vector3 customColor;   float opacity; // _pad: opacity for ghost models
			Vector3 fogColor;      float mirrorClipZ; // water-plane Z in the reflection pass (else +inf); also pads sunDirection to its 16B slot
			Vector3 sunDirection;  // points toward the sun (renderer GetSunDirection)
			// --- physical-lighting-only tail (non-physical push stops here) ---
			float   physicalTail;  // _pad3 — aligns viewMatrix
			Matrix4 viewMatrix;
			Vector3 viewOrigin;
		};
		struct ModelDlightPushConstants { // dynamic light pass (272 bytes)
			Matrix4 projectionViewModelMatrix;
			Matrix4 modelMatrix;
			Vector3 modelOrigin;           float fogDensityVal;
			Vector3 customColor;           float lightRadius;
			Vector3 lightOrigin;           float lightTypeVal;
			Vector3 lightColor;            float lightRadiusInversed;
			Vector3 lightLinearDirection;  float lightLinearLength;
			Matrix4 lightSpotMatrix;
		};

		class VulkanOptimizedVoxelModel : public VulkanModel {
			struct Vertex {
				uint8_t x, y, z;
				uint8_t aoX;          // 2D AO atlas coord (tile_x*16 + corner)

				// color
				uint8_t colorR, colorG, colorB;
				uint8_t aoY;          // 2D AO atlas coord (tile_y*16 + corner)

				// normal
				int8_t nx, ny, nz;

				uint8_t padding;
			};

			// Shared pipeline cache across all model instances
			struct PipelineCache {
				VkRenderPass renderPass;
				VkPipeline pipeline;
				VkPipeline mirroredPipeline;          // same as pipeline, VK_CULL_MODE_FRONT_BIT
				VkPipeline dlightPipeline;
				VkPipeline mirroredDlightPipeline;    // same as dlightPipeline, VK_CULL_MODE_FRONT_BIT
				VkPipeline shadowMapPipeline;
				VkPipeline ghostDepthPipeline;
				VkPipeline mirroredGhostDepthPipeline; // same as ghostDepthPipeline, VK_CULL_MODE_FRONT_BIT
				VkPipeline ghostColorPipeline;
				VkPipeline mirroredGhostColorPipeline; // same as ghostColorPipeline, VK_CULL_MODE_FRONT_BIT
				VkPipelineLayout pipelineLayout;
				VkPipelineLayout dlightPipelineLayout;
				VkPipelineLayout shadowMapPipelineLayout;
				VkRenderPass shadowMapRenderPass;     // render pass shadowMapPipeline was built against
				VkDescriptorSetLayout descriptorSetLayout;
				bool physicalLighting;

				PipelineCache() : renderPass(VK_NULL_HANDLE), pipeline(VK_NULL_HANDLE),
				                  mirroredPipeline(VK_NULL_HANDLE),
				                  dlightPipeline(VK_NULL_HANDLE), mirroredDlightPipeline(VK_NULL_HANDLE),
				                  shadowMapPipeline(VK_NULL_HANDLE),
				                  ghostDepthPipeline(VK_NULL_HANDLE), mirroredGhostDepthPipeline(VK_NULL_HANDLE),
				                  ghostColorPipeline(VK_NULL_HANDLE), mirroredGhostColorPipeline(VK_NULL_HANDLE),
				                  pipelineLayout(VK_NULL_HANDLE),
				                  dlightPipelineLayout(VK_NULL_HANDLE),
				                  shadowMapPipelineLayout(VK_NULL_HANDLE),
				                  shadowMapRenderPass(VK_NULL_HANDLE),
				                  descriptorSetLayout(VK_NULL_HANDLE),
				                  physicalLighting(false) {}
			};

			static PipelineCache sharedPipeline;
			static int pipelineRefCount;

			VulkanRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;

			VkDescriptorPool descriptorPool;
			VkDescriptorSet descriptorSet;

			Handle<VulkanImage> image;
			Handle<VulkanImage> aoImage;

			Handle<VulkanBuffer> vertexBuffer;
			Handle<VulkanBuffer> indexBuffer;
			std::vector<Vertex> vertices;
			std::vector<uint32_t> indices;
			std::vector<uint16_t> bmpIndex; // bmp id for vertex (not index)
			std::vector<Bitmap*> bmps;
			unsigned int numIndices;

			Vector3 origin;
			float radius;
			IntVector3 dimensions;

			AABB3 boundingBox;

			uint8_t calcAOID(VoxelModel*, int x, int y, int z,
			                 int ux, int uy, int uz, int vx, int vy, int vz);
			void BuildVertices(VoxelModel*);
			void GenerateTexture();
			void CreatePipeline(VkRenderPass renderPass);
			void CreateShadowPipeline(VkRenderPass shadowRenderPass);

		protected:
			~VulkanOptimizedVoxelModel();

		public:
			VulkanOptimizedVoxelModel(VoxelModel*, VulkanRenderer& r);

			static void PreloadShaders(VulkanRenderer&);
			static void InvalidateSharedPipeline(gui::SDLVulkanDevice* device);

			void Prerender(VkCommandBuffer commandBuffer,
			               std::vector<client::ModelRenderParam> params,
			               bool ghostPass) override;
			void RenderShadowMapPass(VkCommandBuffer commandBuffer,
			                         std::vector<client::ModelRenderParam> params,
			                         const Matrix4& lightMatrix,
			                         VkRenderPass shadowRenderPass) override;
			void RenderSunlightPass(VkCommandBuffer commandBuffer,
			                        std::vector<client::ModelRenderParam> params,
			                        bool ghostPass) override;
			void RenderDynamicLightPass(VkCommandBuffer commandBuffer,
			                            std::vector<client::ModelRenderParam> params,
			                            std::vector<void*> lights) override;

			IntVector3 GetDimensions() override { return dimensions; }
			AABB3 GetBoundingBox() override { return boundingBox; }
		};
	} // namespace draw
} // namespace spades
