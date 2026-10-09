/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace draw {
		class VulkanBuffer;
		class VulkanRenderer;

		/**
		 * How the scene's passes see the world: the camera's matrices, the eye, the fog
		 * and the sun, the same for every draw of a pass, as `SceneView.glsl` reads
		 * them. A frame draws through the main view and, for the water's reflection,
		 * through the mirrored one; both are written once a frame, a frame in flight
		 * each, and a pass binds its view by a dynamic offset into its frame's buffer.
		 * What does change from draw to draw, a model's matrix or a chunk's origin, is
		 * all the draws push.
		 */
		class VulkanSceneView {
		public:
			/** The descriptor set the scene's passes bind the view to, after the lit
			 * pipelines' map, model shadow and scene light sets */
			static constexpr std::uint32_t Set = 3;

			enum class View : std::uint32_t { Main = 0, Mirror = 1 };
			static constexpr std::uint32_t ViewCount = 2;

			/** A view, as `SceneView` has it */
			struct Parameters {
				Matrix4 projectionView;
				Matrix4 view;
				Vector3 eye;
				float fogDistance;
				/** The colour the solid passes fade to, linear */
				Vector3 fogColor;
				/** The height below which the mirror clips models away, or none */
				float mirrorClipZ;
				/** Towards the sun */
				Vector3 sunDirection;
			};

			VulkanSceneView(VulkanRenderer&, std::size_t framesInFlight);
			~VulkanSceneView();

			VulkanSceneView(const VulkanSceneView&) = delete;
			VulkanSceneView& operator=(const VulkanSceneView&) = delete;

			VkDescriptorSetLayout GetSetLayout() const { return setLayout; }

			/** Writes `view` of frame slot `frameSlot`, which the GPU is done with. */
			void Write(std::size_t frameSlot, View view, const Parameters&);

			/** Binds `view` of frame slot `frameSlot` as set `Set` of `layout`. */
			void Bind(VkCommandBuffer, VkPipelineLayout layout, VkPipelineBindPoint,
			          std::size_t frameSlot, View view) const;

		private:
			struct Slot {
				Handle<VulkanBuffer> buffer;
				VkDescriptorSet set = VK_NULL_HANDLE;
			};

			VkDevice device;
			VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
			VkDescriptorPool pool = VK_NULL_HANDLE;
			/** A view's size in the buffer, aligned for a dynamic offset */
			VkDeviceSize stride = 0;
			std::vector<Slot> slots;
		};
	} // namespace draw
} // namespace spades
