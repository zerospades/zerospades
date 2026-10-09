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
		 * The scene's stencil buffer is cleared with the colour and depth buffers
		 * and read by nothing else, so this bit is the whole allocation. Unlike GL,
		 * the 2D pass never touches stencil at all -- it clips with a fragment
		 * reject and a dynamic scissor, and its render pass has no depth/stencil
		 * attachment (see VulkanRenderer::CreateRenderPass).
		 */
		constexpr uint32_t kStencilBitWorld = 1u << 0;

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
		 * Stamps `reference` into the world bit wherever this pipeline's fragments
		 * land. The map passes kStencilBitWorld, every model pass that writes depth
		 * passes 0: together they keep the bit equal to "the world is the nearest
		 * surface here". Without the models clearing it again, a player standing
		 * against a wall would leave the bit set under themselves and the x-ray
		 * would draw their hidden parts over their visible body.
		 */
		inline VkStencilOpState MakeWorldStampStencilOp(uint32_t reference) {
			VkStencilOpState op{};
			op.failOp = VK_STENCIL_OP_KEEP;
			op.passOp = VK_STENCIL_OP_REPLACE;
			op.depthFailOp = VK_STENCIL_OP_KEEP;
			op.compareOp = VK_COMPARE_OP_ALWAYS;
			op.compareMask = kStencilBitWorld;
			op.writeMask = kStencilBitWorld;
			op.reference = reference;
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
