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

#include <memory>
#include <vector>
#include <vulkan/vulkan.h>
#include <Core/RefCountedObject.h>
#include <Core/Math.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanImage;

		class VulkanFramebufferManager {
		public:
			class BufferHandle {
				VulkanFramebufferManager *manager;
				size_t bufferIndex;
				bool valid;

			public:
				BufferHandle();
				BufferHandle(VulkanFramebufferManager *manager, size_t index);
				BufferHandle(const BufferHandle &);
				~BufferHandle();

				void operator=(const BufferHandle &);

				bool IsValid() const { return valid; }
				void Release();
				VkFramebuffer GetFramebuffer();
				Handle<VulkanImage> GetColorImage();
				Handle<VulkanImage> GetDepthImage();
				int GetWidth();
				int GetHeight();
				VkFormat GetColorFormat();

				VulkanFramebufferManager *GetManager() { return manager; }
			};

		private:
			Handle<gui::SDLVulkanDevice> device;

			struct Buffer {
				VkFramebuffer framebuffer;
				Handle<VulkanImage> colorImage;
				Handle<VulkanImage> depthImage;
				int refCount;
				int w, h;
				VkFormat colorFormat;
			};

			bool useHighPrec;
			bool useHdr;
			bool useSRGB;

			bool doingPostProcessing;

			int renderWidth;
			int renderHeight;

			VkFormat fbColorFormat;
			VkFormat fbDepthFormat;

			// MSAA sample count for the scene attachments (1 = MSAA off). When > 1
			// the scene renders to multisampled colour/depth and is resolved into the
			// single-sample *Resolve images below before post-processing.
			VkSampleCountFlagBits sampleCount;
			bool useMSAA;

			// Main render framebuffer
			VkFramebuffer renderFramebuffer;
			Handle<VulkanImage> renderColorImage;
			Handle<VulkanImage> renderDepthImage;

			// Sampled copy of the scene depth (the 1x attachment has no SAMPLED usage)
			Handle<VulkanImage> sceneDepthSampleImage;

			// Single-sample resolve targets for the scene attachments. Only created
			// when useMSAA; the post-process chain and any code that *reads* the
			// finished scene samples these instead of the multisampled attachments
			// (which can't be linearly sampled). When MSAA is off these stay null and
			// GetResolved*Image() falls back to the render images.
			Handle<VulkanImage> renderColorResolveImage;
			Handle<VulkanImage> renderDepthResolveImage;

			// Mirror framebuffer for water reflections
			VkFramebuffer mirrorFramebuffer;
			Handle<VulkanImage> mirrorColorImage;
			Handle<VulkanImage> mirrorDepthImage;

			// Single-sample resolves of the mirror images, so the water shader can
			// sample reflections under MSAA (the mirror attachments are multisampled
			// for pipeline compatibility with the scene). Colour is R8/16-format like
			// the scene; depth is the R32F raw-depth convention. Only when useMSAA.
			Handle<VulkanImage> mirrorColorResolveImage;
			Handle<VulkanImage> mirrorDepthResolveImage;

			// Screen copy images for water refraction sampling
			// (can't sample from render targets during the water pass)
			Handle<VulkanImage> screenCopyColorImage;
			Handle<VulkanImage> screenCopyDepthImage;

			// Render pass used for all framebuffers
			VkRenderPass renderPass;
			VkRenderPass waterRenderPass; // Render pass with LOAD_OP for water rendering
			VkRenderPass spriteRenderPass; // Color-only render pass for soft sprite rendering
			VkFramebuffer spriteFramebuffer;

			std::vector<Buffer> buffers;

			void CreateRenderPass();

		public:
			VulkanFramebufferManager(Handle<gui::SDLVulkanDevice> device, int renderWidth, int renderHeight);
			~VulkanFramebufferManager();

			/** Sets up for scene rendering. */
			void PrepareSceneRendering(VkCommandBuffer commandBuffer);

			BufferHandle PrepareForWaterRendering(VkCommandBuffer commandBuffer);
			BufferHandle StartPostProcessing();

			void MakeSureAllBuffersReleased();

			// Raw scene attachments (multisampled when useMSAA). Used as render-pass
			// attachments and for the scene-pass layout barriers — NOT for sampling.
			Handle<VulkanImage> GetDepthImage() { return renderDepthImage; }
			Handle<VulkanImage> GetColorImage() { return renderColorImage; }

			// Single-sample views of the finished scene for everything downstream
			// (post-process, water screen copy, framebuffer readback). Identical to
			// GetColorImage()/GetDepthImage() when MSAA is off.
			Handle<VulkanImage> GetResolvedColorImage() {
				return useMSAA ? renderColorResolveImage : renderColorImage;
			}
			Handle<VulkanImage> GetResolvedDepthImage() {
				return useMSAA ? renderDepthResolveImage : sceneDepthSampleImage;
			}
			bool IsMSAA() const { return useMSAA; }

			// Depth in/out: ATTACHMENT_OPTIMAL; copy ends SHADER_READ_ONLY. No-op under MSAA.
			void CopySceneDepthForSampling(VkCommandBuffer commandBuffer);
			VkSampleCountFlagBits GetSampleCount() const { return sampleCount; }

			// Resolves the multisampled scene colour into renderColorResolveImage
			// (single-sample), which the post-process chain then samples. No-op when
			// MSAA is off. Call once after all scene passes (main + water + sprites)
			// and before post-processing. `currentColorLayout` is the layout the
			// multisampled colour attachment is in at call time; the resolved image
			// is left in SHADER_READ_ONLY_OPTIMAL. Depth is resolved separately by
			// VulkanDepthResolveFilter.
			void ResolveScene(VkCommandBuffer commandBuffer, VkImageLayout currentColorLayout);

			VkFormat GetMainColorFormat() { return fbColorFormat; }
			VkRenderPass GetRenderPass() { return renderPass; }
			VkRenderPass GetWaterRenderPass() { return waterRenderPass; }
			VkRenderPass GetSpriteRenderPass() { return spriteRenderPass; }
			VkFramebuffer GetRenderFramebuffer() { return renderFramebuffer; }
			VkFramebuffer GetSpriteFramebuffer() { return spriteFramebuffer; }

			/**
			 * Creates BufferHandle with a given size and format.
			 */
			BufferHandle CreateBufferHandle(int w = -1, int h = -1, bool alpha = false);

			/**
			 * Creates BufferHandle with a given size and format.
			 */
			BufferHandle CreateBufferHandle(int w, int h, VkFormat colorFormat);

			void CopyToMirrorImage(VkCommandBuffer commandBuffer, VkFramebuffer srcFb = VK_NULL_HANDLE);
			void ClearMirrorImage(VkCommandBuffer commandBuffer, Vector3 bgCol);
			Handle<VulkanImage> GetMirrorColorImage() { return mirrorColorImage; }
			Handle<VulkanImage> GetMirrorDepthImage() { return mirrorDepthImage; }
			VkFramebuffer GetMirrorFramebuffer() { return mirrorFramebuffer; }

			// Resolves the multisampled mirror colour into mirrorColorResolveImage
			// for the water shader (r_water >= 2). No-op when MSAA is off. Mirror
			// depth is resolved by the renderer via VulkanDepthResolveFilter.
			// `currentColorLayout` is the mirror colour's layout at call time; the
			// resolve target ends in SHADER_READ_ONLY_OPTIMAL.
			void ResolveMirrorColor(VkCommandBuffer commandBuffer, VkImageLayout currentColorLayout);

			void CopySceneForWaterSampling(VkCommandBuffer commandBuffer);
			Handle<VulkanImage> GetScreenCopyColorImage() { return screenCopyColorImage; }
			Handle<VulkanImage> GetScreenCopyDepthImage() { return screenCopyDepthImage; }

			// Single-sample images the water shader samples. These hide MSAA: at 1x
			// they are the original copy targets; under MSAA they are the resolved
			// versions (refraction depth reuses the scene's resolved depth, since the
			// depth behind the water is the scene depth before the water pass).
			Handle<VulkanImage> GetWaterRefractionColorImage() { return screenCopyColorImage; }
			Handle<VulkanImage> GetWaterRefractionDepthImage() {
				return useMSAA ? renderDepthResolveImage : screenCopyDepthImage;
			}
			Handle<VulkanImage> GetWaterMirrorColorImage() {
				return useMSAA ? mirrorColorResolveImage : mirrorColorImage;
			}
			Handle<VulkanImage> GetWaterMirrorDepthImage() {
				return useMSAA ? mirrorDepthResolveImage : mirrorDepthImage;
			}
			Handle<VulkanImage> GetMirrorColorResolveImage() { return mirrorColorResolveImage; }
			Handle<VulkanImage> GetMirrorDepthResolveImage() { return mirrorDepthResolveImage; }
		};

		// Shorter name
		typedef VulkanFramebufferManager::BufferHandle VulkanColorBuffer;
	} // namespace draw
} // namespace spades
