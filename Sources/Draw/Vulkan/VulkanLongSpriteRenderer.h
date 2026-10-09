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

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>
#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanRenderer;
		class VulkanImage;
		class VulkanBuffer;

		/**
		 * Draws the scene's long sprites (`IRenderer::AddLongSprite`): tracers and the
		 * reflex sights' reticles, stretched along a segment between two caps.
		 *
		 * Their geometry depends on how the segment lies on screen, so it is built
		 * here, as GL builds it, into the vertex and index buffers of the frame in
		 * flight, once a frame; each run of sprites sharing an image is one draw, in
		 * the order they were added, as their blending needs. They read the view from
		 * the sprites' own set (`SpriteView.glsl`).
		 */
		class VulkanLongSpriteRenderer : public RefCountedObject {
			struct Sprite {
				VulkanImage* image;
				Vector3 start;
				Vector3 end;
				float radius;
				Vector4 color;
			};

			struct Vertex {
				float x, y, z;
				float pad;
				float u, v;
				float r, g, b, a;

				void operator=(const Vector3& vec) {
					x = vec.x;
					y = vec.y;
					z = vec.z;
				}
			};

			/** A run of sprites sharing an image, as indices of the frame's buffer */
			struct Batch {
				VulkanImage* image;
				std::uint32_t firstIndex;
				std::uint32_t indexCount;
			};

			/** What a frame in flight draws with, written only once the GPU is done
			 * with its previous frame */
			struct FrameResources {
				Handle<VulkanBuffer> vertexBuffer;
				std::size_t vertexCapacity = 0;
				Handle<VulkanBuffer> indexBuffer;
				std::size_t indexCapacity = 0;
				Handle<VulkanBuffer> view;
				VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
				/** The image sets allocated this frame, and the images they hold */
				std::unordered_map<VulkanImage*, VkDescriptorSet> imageSets;
				std::vector<Handle<VulkanImage>> images;
			};

			VulkanRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;
			std::vector<Sprite> sprites;

			// Built anew every frame, kept not to allocate them again
			std::vector<Vertex> vertices;
			std::vector<std::uint32_t> indices;
			std::vector<Batch> batches;

			VkPipeline pipeline = VK_NULL_HANDLE;
			VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
			/** The sprite's set: its view and its image */
			VkDescriptorSetLayout spriteSetLayout = VK_NULL_HANDLE;

			std::vector<FrameResources> frames;

			void CreatePipeline();
			void CreateFrameResources(FrameResources&);

			/** Builds the geometry of the sprites added, as GL does, into `vertices`,
			 * `indices` and `batches`. */
			void BuildGeometry();

			/** The set holding `image` for the frame, made the first time it is asked
			 * for. */
			VkDescriptorSet GetImageSet(FrameResources&, VulkanImage& image);

		public:
			VulkanLongSpriteRenderer(VulkanRenderer&);
			~VulkanLongSpriteRenderer();

			void Add(VulkanImage* img, Vector3 p1, Vector3 p2, float rad, Vector4 color);
			void Clear();

			/** Records the sprites added since the last `Clear` into the render pass
			 * under way, with frame slot `frameSlot`'s resources. */
			void Render(VkCommandBuffer commandBuffer, std::size_t frameSlot);
		};
	} // namespace draw
} // namespace spades
