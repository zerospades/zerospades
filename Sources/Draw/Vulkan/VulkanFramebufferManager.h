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
		class VulkanSettings;
		class VulkanImage;
		class VulkanBuffer;

		class VulkanFramebufferManager {
			/** Copies `depth`, an attachment, into `sample`, an R32F image sampled by
			 * fragment shaders and compute; `depth` stays an attachment */
			void CopyDepthForSampling(VkCommandBuffer commandBuffer, VulkanImage& depth,
			                          VulkanImage& sample);

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
			// Whether the 1x depth copies hold the depth as it is now: set by a
			// copy, cleared by `InvalidateDepthSamples`
			bool sceneDepthSampleCurrent = false;
			bool mirrorDepthSampleCurrent = false;

			// Main render framebuffer
			VkFramebuffer renderFramebuffer;
			Handle<VulkanImage> renderColorImage;
			Handle<VulkanImage> renderDepthImage;

			// Sampled copy of the scene depth (the 1x attachment has no SAMPLED usage)
			Handle<VulkanImage> sceneDepthSampleImage;
			// The mirror's, at 1x with SSAO or r_water 3
			Handle<VulkanImage> mirrorDepthSampleImage;
			// What the copies into those pass through, at 1x
			Handle<VulkanBuffer> depthCopyBuffer;

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
			// The scene pass continuing what is in its targets: colour, depth and
			// stencil loaded from where the scene's pass left them. Compatible with
			// renderPass, so every scene pipeline draws in it.
			VkRenderPass continueRenderPass;
			// The mirror's depth pre-pass, and its lit pass, clearing or continuing
			// that: the scene pass and its continue pass loading and storing only
			// what is read. Its stencil never is (the x-ray reveal is the main
			// view's), its depth after the lit pass only by the water's depth-aware
			// reflections (r_water 3).
			VkRenderPass mirrorPrepassRenderPass;
			VkRenderPass mirrorRenderPass;
			VkRenderPass mirrorContinueRenderPass;
			VkRenderPass spriteRenderPass; // Color-only render pass for soft sprite rendering
			VkFramebuffer spriteFramebuffer;

			std::vector<Buffer> buffers;

			void CreateRenderPass(bool mirrorDepthRead);

		public:
			/** Makes the targets, sized and formatted from the renderer's latched
			 * `settings` */
			VulkanFramebufferManager(Handle<gui::SDLVulkanDevice> device, VulkanSettings& settings,
			                         int renderWidth, int renderHeight);
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
			/** Tells the depth copies below the depth may have changed since they
			 * last ran (a new frame, or a draw writing depth after the pre-pass),
			 * so that each copies again; until then a repeated copy is skipped */
			void InvalidateDepthSamples();

			// Depth in/out: ATTACHMENT_OPTIMAL; copy ends SHADER_READ_ONLY. No-op under MSAA.
			void CopySceneDepthForSampling(VkCommandBuffer commandBuffer);
			/** The mirror's depth into its single-sample copy, at 1x with SSAO or
			 * r_water 3 */
			void CopyMirrorDepthForSampling(VkCommandBuffer commandBuffer);
			/** The mirror's depth sampled as the scene's (`GetResolvedDepthImage`): its
			 * resolve under MSAA, its copy at 1x; R32F, read in SHADER_READ_ONLY */
			Handle<VulkanImage> GetMirrorResolvedDepthImage() {
				return useMSAA ? mirrorDepthResolveImage : mirrorDepthSampleImage;
			}
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
			/** The scene pass continuing what is in its targets (`LOAD`): the
			 * water's, and the lit pass's after a depth pre-pass */
			VkRenderPass GetContinueRenderPass() { return continueRenderPass; }
			/** The mirror's depth pre-pass */
			VkRenderPass GetMirrorPrepassRenderPass() { return mirrorPrepassRenderPass; }
			/** The mirror's lit pass, after a depth pre-pass or not */
			VkRenderPass GetMirrorRenderPass(bool continuePrepass) {
				return continuePrepass ? mirrorContinueRenderPass : mirrorRenderPass;
			}
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
			// The mirror the water reflects, at r_water >= 2 only: null below.
			Handle<VulkanImage> GetWaterMirrorColorImage() {
				return mirrorColorResolveImage ? mirrorColorResolveImage : mirrorColorImage;
			}
			// r_water >= 3 only: the mirror's depth resolved, or copied at 1x
			Handle<VulkanImage> GetWaterMirrorDepthImage() {
				return GetMirrorResolvedDepthImage();
			}
			Handle<VulkanImage> GetMirrorColorResolveImage() { return mirrorColorResolveImage; }
			Handle<VulkanImage> GetMirrorDepthResolveImage() { return mirrorDepthResolveImage; }
		};

		// Shorter name
		typedef VulkanFramebufferManager::BufferHandle VulkanColorBuffer;
	} // namespace draw
} // namespace spades
