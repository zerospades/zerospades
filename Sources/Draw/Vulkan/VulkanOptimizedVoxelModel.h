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

#include "VulkanPushConstants.h"

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

		// Push-constant blocks for the model pipelines: what changes from draw to
		// draw, as `DrawConstants` of their shaders has it. The pass's view is
		// `VulkanSceneView`'s. The pipeline layout's range and the push are both sized
		// from these, so they can never drift apart; std430 aligns each vec3 to 16
		// bytes, hence the pad floats.
		struct ModelDrawConstants {
			Matrix4 modelMatrix;
			Vector3 modelOrigin;   float fogDensity;
			Vector3 customColor;   float opacity; // a ghost's
		};
		static_assert(sizeof(ModelDrawConstants) <= kPortablePushConstantsSize,
		              "push constants must fit the 128 bytes every device has (VulkanPushConstants.h)");
		struct ModelXRayDrawConstants {
			Matrix4 modelMatrix;
			Vector3 modelOrigin;    float _pad0;
			Vector3 xrayColor;      float _pad1;
			Vector3 customColor;    float _pad2;
		};
		static_assert(sizeof(ModelXRayDrawConstants) <= kPortablePushConstantsSize,
		              "push constants must fit the 128 bytes every device has (VulkanPushConstants.h)");

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
				VkPipeline pipeline = VK_NULL_HANDLE;
				VkPipeline mirroredPipeline = VK_NULL_HANDLE; // same as pipeline, VK_CULL_MODE_FRONT_BIT
				// The depth pre-pass's: the lit pipelines' vertex stage, and a
				// fragment stage that only clips away what the lit pass clips
				VkPipeline depthPipeline = VK_NULL_HANDLE;
				VkPipeline mirroredDepthPipeline = VK_NULL_HANDLE;
				VkPipeline shadowMapPipeline = VK_NULL_HANDLE;
				VkPipeline ghostDepthPipeline = VK_NULL_HANDLE;
				VkPipeline mirroredGhostDepthPipeline = VK_NULL_HANDLE; // VK_CULL_MODE_FRONT_BIT
				VkPipeline ghostColorPipeline = VK_NULL_HANDLE;
				VkPipeline mirroredGhostColorPipeline = VK_NULL_HANDLE; // VK_CULL_MODE_FRONT_BIT
				VkPipeline xrayPipeline = VK_NULL_HANDLE;
				VkPipeline mirroredXRayPipeline = VK_NULL_HANDLE;      // VK_CULL_MODE_FRONT_BIT
				// own layout: no descriptor sets, wider push range
				VkPipelineLayout xrayPipelineLayout = VK_NULL_HANDLE;
				VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
				VkPipelineLayout shadowMapPipelineLayout = VK_NULL_HANDLE;
				VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
				bool physicalLighting = false;

				/** Destroys every pipeline, layout and set layout it holds */
				void Destroy(VkDevice device);
			};

			static PipelineCache sharedPipeline;

			/** What the draws push as a model's opacity */
			enum class PushedOpacity {
				Opaque,  // 0: not a ghost
				Whole,   // 1: a ghost's depth, written whole
				Instance // the ghost's own
			};

			/**
			 * Draws the instances in `params` a pass draws (the ghosts, or the
			 * rest) with the shared pipeline `pipelineMember` names, or its mirrored
			 * sibling where the winding is reversed. The one loop every pass shares,
			 * so that each draws the very same fragments: the depth pre-pass, the
			 * lit pass, the ghosts' depth and colour.
			 */
			void DrawInstances(VkCommandBuffer, const std::vector<client::ModelRenderParam>& params,
			                   bool ghostPass, VkPipeline PipelineCache::*pipelineMember,
			                   VkPipeline PipelineCache::*mirroredPipelineMember,
			                   PushedOpacity pushedOpacity);
			static int pipelineRefCount;

			VulkanRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;

			VkDescriptorPool descriptorPool;

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
			static void CreatePipeline(VulkanRenderer& renderer, VkRenderPass renderPass);
			static void CreateShadowPipeline(VulkanRenderer& renderer, VkRenderPass shadowRenderPass);

		protected:
			~VulkanOptimizedVoxelModel();

		public:
			VulkanOptimizedVoxelModel(VoxelModel*, VulkanRenderer& r);

			static void PreloadShaders(VulkanRenderer&);
			/**
			 * Drops the pipelines every model shares, remade on the next draw: when
			 * what their layout is made from goes, the map's model-shadow sampling
			 * layout (set 1) on a map change, and at shutdown. Never needed for a new
			 * scene render pass, which is always compatible. With the device idle.
			 */
			static void InvalidateSharedPipeline(gui::SDLVulkanDevice* device);
			/**
			 * Makes the pipelines every model shares that are not there yet, the
			 * model-shadow one too while models cast shadows, so that the first
			 * frame drawing a model does not stall on compiling them. Once the map's
			 * renderers are made, which their layout is made from.
			 */
			static void PrepareSharedPipelines(VulkanRenderer& renderer);

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
			void RenderXRayPass(VkCommandBuffer commandBuffer,
			                    std::vector<client::ModelRenderParam> params) override;

			IntVector3 GetDimensions() override { return dimensions; }
			AABB3 GetBoundingBox() override { return boundingBox; }
		};
	} // namespace draw
} // namespace spades
