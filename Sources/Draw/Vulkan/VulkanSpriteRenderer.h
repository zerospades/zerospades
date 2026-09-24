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
#include <Client/IRenderer.h>
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

		class VulkanSpriteRenderer : public RefCountedObject {
			struct Sprite {
				VulkanImage *image;
				Vector3 center;
				float radius;
				float angle;
				Vector4 color;
			};

			struct Vertex {
				// center position
				float x, y, z;
				float radius;

				// point coord
				float sx, sy;
				float angle;

				// color
				float r, g, b, a;
			};

			VulkanRenderer &renderer;
			Handle<gui::SDLVulkanDevice> device;
			std::vector<Sprite> sprites;

			VulkanImage *lastImage;

			std::vector<Vertex> vertices;
			std::vector<uint32_t> indices;

			bool softParticles;

			VkPipeline pipeline;
			VkPipelineLayout pipelineLayout;
			VkDescriptorSetLayout descriptorSetLayout;
			VkDescriptorSetLayout depthDescriptorSetLayout;

			std::vector<VkDescriptorPool> perFrameDescriptorPools;
			std::vector<std::vector<Handle<VulkanBuffer>>> perFrameBuffers;
			std::vector<std::vector<VulkanImage*>> perFrameImages;

			VkDescriptorSet depthDescriptorSet;

			void CreatePipeline();
			void CreateDescriptorSet();
			void Flush(VkCommandBuffer commandBuffer, uint32_t frameIndex);

		public:
			VulkanSpriteRenderer(VulkanRenderer &);
			~VulkanSpriteRenderer();

			void Add(VulkanImage *img, Vector3 center, float rad, float ang, Vector4 color);
			void Clear();
			void Render(VkCommandBuffer commandBuffer, uint32_t frameIndex);

			bool IsSoftParticles() const { return softParticles; }
		};
	} // namespace draw
} // namespace spades
