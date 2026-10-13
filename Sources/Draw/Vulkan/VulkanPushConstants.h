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
		 * The push constant space every program and every push constant struct
		 * must fit in: 128 bytes, what Vulkan guarantees every device has
		 * (`VkPhysicalDeviceLimits::maxPushConstantsSize` is at least this).
		 *
		 * It is this fixed minimum, not the limit the device in hand reports, on
		 * purpose. A push constant block's size is compiled into its shader and
		 * its pipeline layout, so it cannot follow the device at run time; and
		 * checking against the device's own limit would pass on the GPUs that
		 * report more (MoltenVK reports 4096) and fail only on the players' that
		 * report the minimum. Held to this, a block that would not fit everywhere
		 * is refused on every machine, the developer's included.
		 *
		 * Push constants carry what changes from draw to draw. What is larger, or
		 * shared by a pass, belongs in a uniform buffer: `VulkanSceneView` holds a
		 * pass's view, bound once with a dynamic offset per view.
		 *
		 * Should a shader ever measurably gain from a larger block on the devices
		 * that allow one, the way to adapt is two variants of it compiled from one
		 * source (the block in push constants, or in a uniform buffer), the one
		 * chosen at pipeline creation from `maxPushConstantsSize`, both fed from
		 * the same C++ struct. Never a block sized past this alone.
		 *
		 * Checked where every program's shaders are reflected (`VulkanProgram`),
		 * and at compile time for every type pushed: by `PushConstantRange` and
		 * `PushConstants`, or a `static_assert` beside the struct.
		 */
		constexpr std::uint32_t kPortablePushConstantsSize = 128;

		/**
		 * The push constant range a pipeline layout declares for `T`, the type
		 * its draws push: the range and the pushes are both sized from `T`, so
		 * they can never disagree (a range shorter than what is pushed drops the
		 * tail on some drivers, and draws nothing right on others).
		 */
		template <typename T>
		inline VkPushConstantRange PushConstantRange(VkShaderStageFlags stages) {
			static_assert(sizeof(T) <= kPortablePushConstantsSize,
			              "push constants must fit the 128 bytes every device has");
			static_assert(sizeof(T) % 4 == 0, "push constant sizes are multiples of 4 bytes");
			return VkPushConstantRange{stages, 0, static_cast<std::uint32_t>(sizeof(T))};
		}

		/** Pushes `value` through `layout`, whose range is `PushConstantRange<T>` */
		template <typename T>
		inline void PushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout,
		                          VkShaderStageFlags stages, const T& value) {
			static_assert(sizeof(T) <= kPortablePushConstantsSize,
			              "push constants must fit the 128 bytes every device has");
			vkCmdPushConstants(commandBuffer, layout, stages, 0,
			                   static_cast<std::uint32_t>(sizeof(T)), &value);
		}

	} // namespace draw
} // namespace spades
