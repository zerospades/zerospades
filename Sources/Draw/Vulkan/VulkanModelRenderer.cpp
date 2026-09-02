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

#include "VulkanModelRenderer.h"
#include "VulkanModel.h"
#include "VulkanRenderer.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>

namespace spades {
	namespace draw {
		VulkanModelRenderer::VulkanModelRenderer(VulkanRenderer& r)
		    : renderer(r),
		      device(static_cast<gui::SDLVulkanDevice*>(r.GetDevice().Unmanage())),
		      modelCount(0) {
			SPADES_MARK_FUNCTION();
		}

		VulkanModelRenderer::~VulkanModelRenderer() {
			SPADES_MARK_FUNCTION();
			Clear();
		}

		void VulkanModelRenderer::AddModel(VulkanModel* model, const client::ModelRenderParam& param) {
			SPADES_MARK_FUNCTION();
			if (model->renderId == -1) {
				model->renderId = (int)models.size();
				RenderModel m;
				m.model = model;
				model->AddRef();
				models.push_back(m);
			}
			modelCount++;
			models[model->renderId].params.push_back(param);
		}

		void VulkanModelRenderer::RenderShadowMapPass(VkCommandBuffer commandBuffer,
		                                              const Matrix4& lightMatrix,
		                                              VkRenderPass shadowRenderPass) {
			SPADES_MARK_FUNCTION();

			for (const auto& m : models) {
				VulkanModel* model = m.model;
				model->RenderShadowMapPass(commandBuffer, m.params, lightMatrix, shadowRenderPass);
			}
		}

		void VulkanModelRenderer::Prerender(VkCommandBuffer commandBuffer, bool ghostPass) {
			SPADES_MARK_FUNCTION();

			// Depth-only prerender pass
			for (const auto& m : models) {
				VulkanModel* model = m.model;
				model->Prerender(commandBuffer, m.params, ghostPass);
			}
		}

		void VulkanModelRenderer::RenderSunlightPass(VkCommandBuffer commandBuffer, bool ghostPass) {
			SPADES_MARK_FUNCTION();

			for (const auto& m : models) {
				VulkanModel* model = m.model;
				model->RenderSunlightPass(commandBuffer, m.params, ghostPass);
			}
		}

		void VulkanModelRenderer::RenderDynamicLightPass(VkCommandBuffer commandBuffer,
		                                                 std::vector<void*> lights) {
			SPADES_MARK_FUNCTION();

			if (lights.empty())
				return;

			for (const auto& m : models) {
				VulkanModel* model = m.model;
				model->RenderDynamicLightPass(commandBuffer, m.params, lights);
			}
		}

		void VulkanModelRenderer::Clear() {
			SPADES_MARK_FUNCTION();

			// Clear scene
			for (const auto& m : models) {
				VulkanModel* model = m.model;
				model->renderId = -1;
				model->Release();
			}
			models.clear();
			modelCount = 0;
		}
	} // namespace draw
} // namespace spades
