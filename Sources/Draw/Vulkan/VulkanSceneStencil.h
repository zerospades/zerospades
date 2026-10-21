/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

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
#include <vulkan/vulkan.h>

namespace spades {
	namespace draw {
		/**
		 * Stencil bit the world's own pixels are stamped with while the scene is
		 * drawn, so the x-ray pass can tell "hidden behind the world" apart from
		 * "hidden behind another model". Mirrors GLRenderer's kStencilBitWorld.
		 *
		 * The scene's stencil buffer is cleared with the colour and depth buffers.
		 * Its bits say what the nearest surface is: this one and
		 * kStencilBitNoOutline are the whole allocation. Unlike GL, the 2D pass
		 * never touches stencil at all -- it clips with a fragment reject and a
		 * dynamic scissor, and its render pass has no depth/stencil attachment (see
		 * VulkanRenderer::CreateRenderPass).
		 */
		constexpr uint32_t kStencilBitWorld = 1u << 0;

		/**
		 * Stencil bit stamped where the nearest surface draws no outline: a model
		 * drawn without `drawOutline`, or a ghost, the models GL's outline pass
		 * skips. The cavity outline filter reads it back, copied or resolved
		 * (VulkanFramebufferManager::GetResolvedStencilImage), and leaves out the
		 * edges such a surface is the near side of. CavityOutline.vk.fs repeats
		 * the value.
		 */
		constexpr uint32_t kStencilBitNoOutline = 1u << 1;

		/** The bits an opaque surface stamps: all of what the nearest one is */
		constexpr uint32_t kStencilSurfaceBits = kStencilBitWorld | kStencilBitNoOutline;

		/**
		 * The aspects a layout barrier on a depth attachment has to cover. Core
		 * Vulkan moves a combined depth+stencil image's two aspects together: naming
		 * only DEPTH leaves the stencil half in an undefined layout, and the world
		 * marks with it. Copy regions and sampled views still name one aspect.
		 */
		inline VkImageAspectFlags DepthStencilBarrierAspects(VkFormat format) {
			switch (format) {
				case VK_FORMAT_D16_UNORM_S8_UINT:
				case VK_FORMAT_D24_UNORM_S8_UINT:
				case VK_FORMAT_D32_SFLOAT_S8_UINT:
					return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
				default:
					return VK_IMAGE_ASPECT_DEPTH_BIT;
			}
		}

		/**
		 * Stamps `reference` into the surface bits wherever this pipeline's
		 * fragments land. The map passes kStencilBitWorld; the models' lit pass
		 * sets its reference per draw (dynamic state), kStencilBitNoOutline for a
		 * model without an outline and 0 otherwise: together they keep the bits
		 * equal to what the nearest surface is. Without the models clearing the
		 * world bit again, a player standing against a wall would leave it set
		 * under themselves and the x-ray would draw their hidden parts over their
		 * visible body.
		 */
		inline VkStencilOpState MakeSurfaceStampStencilOp(uint32_t reference) {
			VkStencilOpState op{};
			op.failOp = VK_STENCIL_OP_KEEP;
			op.passOp = VK_STENCIL_OP_REPLACE;
			op.depthFailOp = VK_STENCIL_OP_KEEP;
			op.compareOp = VK_COMPARE_OP_ALWAYS;
			op.compareMask = kStencilSurfaceBits;
			op.writeMask = kStencilSurfaceBits;
			op.reference = reference;
			return op;
		}

		/**
		 * Stamps kStencilBitNoOutline wherever a ghost lands in front, and leaves
		 * the world bit as the surface behind it set it: the x-ray still tells what
		 * hides a player behind a ghost, while the ghost draws no outline.
		 */
		inline VkStencilOpState MakeGhostStampStencilOp() {
			VkStencilOpState op{};
			op.failOp = VK_STENCIL_OP_KEEP;
			op.passOp = VK_STENCIL_OP_REPLACE;
			op.depthFailOp = VK_STENCIL_OP_KEEP;
			op.compareOp = VK_COMPARE_OP_ALWAYS;
			op.compareMask = kStencilBitNoOutline;
			op.writeMask = kStencilBitNoOutline;
			op.reference = kStencilBitNoOutline;
			return op;
		}

		/**
		 * Keeps only the fragments the world itself hides, and writes nothing back.
		 * Used by the x-ray pass, whose `Greater` depth test alone would also reveal
		 * a player through another player.
		 */
		inline VkStencilOpState MakeWorldTestStencilOp() {
			VkStencilOpState op{};
			op.failOp = VK_STENCIL_OP_KEEP;
			op.passOp = VK_STENCIL_OP_KEEP;
			op.depthFailOp = VK_STENCIL_OP_KEEP;
			op.compareOp = VK_COMPARE_OP_EQUAL;
			op.compareMask = kStencilBitWorld;
			op.writeMask = 0;
			op.reference = kStencilBitWorld;
			return op;
		}
	} // namespace draw
} // namespace spades
