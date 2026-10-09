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

		/**
		 * Draws the scene's billboard sprites (`IRenderer::AddSprite`): smoke, debris,
		 * blood and the like.
		 *
		 * Each sprite is an instance written to a buffer of its frame in flight, which
		 * the vertex shader expands into a quad facing the camera, so a frame uploads
		 * its sprites once and draws each run of sprites sharing an image in one call.
		 * They are drawn in the order they were added, as their blending needs.
		 *
		 * Soft sprites (`r_softParticles`) fade where they meet the scene's depth, in
		 * a pass of their own over the scene's colour. At `r_softParticles` 2 they are
		 * also lit as the volumes they stand for, by the sun through the map's and the
		 * models' shadows, the sky, and the dynamic lights, with the lit pipelines'
		 * sets bound before their own.
		 */
		class VulkanSpriteRenderer : public RefCountedObject {
		public:
			/** The descriptor set the sprite shaders bind their own resources to: the
			 * lit pipelines' sets come first. */
			static constexpr std::uint32_t SpriteSet = 3;

		private:
			struct Sprite {
				VulkanImage* image;
				Vector3 center;
				float radius;
				float angle;
				Vector4 color;
				/** Whether it scatters the light it is lit by, rather than emits its own */
				bool scattering;
			};

			/** A sprite as the vertex shader takes it, one per instance */
			struct Instance {
				float centerRadius[4];
				float color[4];
				float angle;
			};

			/** What a frame in flight draws with, written only once the GPU is done
			 * with its previous frame */
			struct FrameResources {
				Handle<VulkanBuffer> instances;
				std::size_t instanceCapacity = 0;
				Handle<VulkanBuffer> view;
				VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
				/** The image sets allocated this frame, and the images they hold */
				std::unordered_map<VulkanImage*, VkDescriptorSet> imageSets;
				std::vector<Handle<VulkanImage>> images;
			};

			VulkanRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;
			std::vector<Sprite> sprites;

			bool softParticles;
			/** Whether soft sprites are lit, where there is a map to light them */
			bool litParticles;

			VkPipeline pipeline = VK_NULL_HANDLE;
			VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
			VkPipeline litPipeline = VK_NULL_HANDLE;
			VkPipelineLayout litPipelineLayout = VK_NULL_HANDLE;
			/** The sprite's own set: its view, its image and, for soft sprites, the
			 * scene's depth */
			VkDescriptorSetLayout spriteSetLayout = VK_NULL_HANDLE;

			std::vector<FrameResources> frames;

			/** Builds the sprites' pipeline with `layout` from the shaders given; with
			 * `specializeRadiosity`, `USE_RADIOSITY` follows `r_radiosity`. */
			VkPipeline BuildPipeline(VkPipelineLayout layout, const char* vertexShader,
			                         const char* fragmentShader, bool specializeRadiosity);
			void CreatePipeline();

			/** Makes the lit pipeline the first time there is a map to light the
			 * sprites with, and says whether there is one. */
			bool PrepareLitPipeline();
			void CreateFrameResources(FrameResources&);
			void ReserveInstances(FrameResources&, std::size_t count);

			/** The set holding `image` for the frame, made the first time it is asked
			 * for. */
			VkDescriptorSet GetImageSet(FrameResources&, VulkanImage& image);

		public:
			VulkanSpriteRenderer(VulkanRenderer&);
			~VulkanSpriteRenderer();

			void Add(VulkanImage* img, Vector3 center, float rad, float ang, Vector4 color);
			void Clear();

			/** Records the sprites added since the last `Clear` into the render pass
			 * under way, with frame slot `frameSlot`'s resources. */
			void Render(VkCommandBuffer commandBuffer, std::size_t frameSlot);

			bool IsSoftParticles() const { return softParticles; }
		};
	} // namespace draw
} // namespace spades
