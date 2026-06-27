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

#include <cstdint>
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

		class VulkanLongSpriteRenderer : public RefCountedObject {
			struct Sprite {
				VulkanImage *image;
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

				void operator=(const Vector3 &vec) {
					x = vec.x;
					y = vec.y;
					z = vec.z;
				}
			};

			VulkanRenderer &renderer;
			Handle<gui::SDLVulkanDevice> device;
			std::vector<Sprite> sprites;

			VulkanImage *lastImage;

			std::vector<Vertex> vertices;
			std::vector<uint32_t> indices;

			VkPipeline pipeline;
			VkPipelineLayout pipelineLayout;
			VkDescriptorSetLayout descriptorSetLayout;

			std::vector<VkDescriptorPool> perFrameDescriptorPools;
			std::vector<std::vector<Handle<VulkanBuffer>>> perFrameBuffers;
			std::vector<std::vector<VulkanImage*>> perFrameImages;

			void CreatePipeline();
			void CreateDescriptorSet();
			void Flush(VkCommandBuffer commandBuffer, uint32_t frameIndex);

		public:
			VulkanLongSpriteRenderer(VulkanRenderer &);
			~VulkanLongSpriteRenderer();

			void Add(VulkanImage *img, Vector3 p1, Vector3 p2, float rad, Vector4 color);
			void Clear();
			void Render(VkCommandBuffer commandBuffer, uint32_t frameIndex);
		};
	} // namespace draw
} // namespace spades
