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

#include <vector>
#include <vulkan/vulkan.h>
#include <Client/IModel.h>
#include <Client/IRenderer.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanRenderer;
		class VulkanModel;

		class VulkanModelRenderer {
			VulkanRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;

			struct RenderModel {
				VulkanModel* model;
				std::vector<client::ModelRenderParam> params;
			};

			std::vector<RenderModel> models;
			int modelCount;

		public:
			VulkanModelRenderer(VulkanRenderer&);
			~VulkanModelRenderer();

			void AddModel(VulkanModel* model, const client::ModelRenderParam& param);

			void RenderShadowMapPass(VkCommandBuffer commandBuffer,
			                         const Matrix4& lightMatrix,
			                         VkRenderPass shadowRenderPass);

			void Prerender(VkCommandBuffer commandBuffer, bool ghostPass);
			void RenderSunlightPass(VkCommandBuffer commandBuffer, bool ghostPass);
			void RenderDynamicLightPass(VkCommandBuffer commandBuffer, std::vector<void*> lights);

			void Clear();
		};
	} // namespace draw
} // namespace spades
