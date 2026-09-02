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

		class VulkanImageRenderer : public RefCountedObject {
			struct ImageVertex {
				float x, y, u, v;
				float r, g, b, a;
			};

			// Vertex + fragment push constants. Declared once and always sized
			// from sizeof(): the pipeline's push constant range and every
			// vkCmdPushConstants call must agree, or the tail is silently
			// dropped on some drivers.
			struct PushConstants {
				float invScreenSizeFactored[2];
				float invTextureSize[2];
				// Circular clip in screen pixels. A radius <= 0 disables it.
				float clipCircleCenter[2];
				float clipCircleRadius;
				float _pad;
			};

			// 2D clipping applied to a batch. The image renderer records its
			// draws long after the client issued them, so the clip in effect at
			// Add() time has to travel with the batch instead of being device
			// state set at call time.
			struct BatchClip {
				// The full-surface scissor is resolved at record time rather
				// than stored, so a swapchain resize can never leave a stale
				// rectangle behind.
				bool hasScissor = false;
				VkRect2D scissor{};
				Vector2 circleCenter{0.0f, 0.0f}; // screen pixels
				float circleRadius = 0.0f;        // <= 0: no circular clip
			};

			struct Batch {
				VulkanImage* image;
				std::vector<ImageVertex> vertices;
				std::vector<uint32_t> indices;
				BatchClip clip;
			};

			VulkanRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;
			VulkanImage* image;

			std::vector<ImageVertex> vertices;
			std::vector<uint32_t> indices;
			std::vector<Batch> batches;

			Handle<VulkanBuffer> vertexBuffer;
			Handle<VulkanBuffer> indexBuffer;

			// Per-frame resources (one per swapchain image to avoid use-after-free)
			std::vector<std::vector<Handle<VulkanBuffer>>> perFrameBuffers;
			std::vector<std::vector<VulkanImage*>> perFrameImages; // Images to release after frame completes

			VkPipeline pipeline;
			VkPipelineLayout pipelineLayout;
			VkDescriptorSetLayout descriptorSetLayout;
			std::vector<VkDescriptorPool> perFrameDescriptorPools; // One pool per swapchain image

			// Clip state applied to batches opened from now on.
			BatchClip currentClip;

			void CreatePipeline();
			void CreateDescriptorSet();

			// Close the batch being accumulated, if any, so that a following
			// image or clip change starts a new one. Ordering between draws and
			// clip changes is preserved this way.
			void FinishCurrentBatch();

			// The whole UI surface, i.e. "no rectangular clip".
			VkRect2D FullScissor() const;

		public:
			VulkanImageRenderer(VulkanRenderer& r);
			~VulkanImageRenderer();

			void Flush(VkCommandBuffer commandBuffer, uint32_t frameIndex);
			void SetImage(VulkanImage* img);

			// 2D clipping. Circle and rect cannot be nested or combined, which
			// matches what IRenderer documents.
			void SetClipCircle(const Vector2& center, float radius);
			void ClearClipCircle();
			void SetClipRect(const AABB2& rect);
			void ClearClipRect();
			void Add(float dx1, float dy1, float dx2, float dy2, float dx3, float dy3, float dx4,
			         float dy4, float sx1, float sy1, float sx2, float sy2, float sx3, float sy3,
			         float sx4, float sy4, float r, float g, float b, float a);
			// Flat-shaded triangle. Texture coordinates are pinned to (0, 0) because
			// the only caller draws with the white image.
			void AddTriangle(float dx1, float dy1, float dx2, float dy2, float dx3, float dy3,
			                 float r, float g, float b, float a);
			// Quad with a two-colour gradient. `horizontal` fades left-to-right
			// instead of top-to-bottom; vertices are in TL, TR, BR, BL order.
			void AddGradient(float dx1, float dy1, float dx2, float dy2, float dx3, float dy3,
			                 float dx4, float dy4, float sx1, float sy1, float sx2, float sy2,
			                 float sx3, float sy3, float sx4, float sy4, float r0, float g0,
			                 float b0, float a0, float r1, float g1, float b1, float a1,
			                 bool horizontal);
		};
	} // namespace draw
} // namespace spades
