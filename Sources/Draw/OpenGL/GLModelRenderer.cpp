/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

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

#include "GLModelRenderer.h"
#include "GLModel.h"
#include "GLProfiler.h"
#include "GLRenderer.h"
#include <Core/Debug.h>

namespace spades {
	namespace draw {
		GLModelRenderer::GLModelRenderer(GLRenderer& r) : renderer(r), device(r.GetGLDevice()) {
			SPADES_MARK_FUNCTION();
			modelCount = 0;
		}

		GLModelRenderer::~GLModelRenderer() {
			SPADES_MARK_FUNCTION();
			Clear();
		}

		void GLModelRenderer::AddModel(GLModel* model, const client::ModelRenderParam& param) {
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

		void GLModelRenderer::RenderShadowMapPass() {
			SPADES_MARK_FUNCTION();

			GLProfiler::Context profiler(renderer.GetGLProfiler(),
				"Model [%d model(s), %d unique model type(s)]",
				modelCount, (int)models.size());

			for (const auto& m : models) {
				GLModel* model = m.model;
				model->RenderShadowMapPass(m.params);
			}
		}

		void GLModelRenderer::Prerender(bool ghostPass) {
			device.ColorMask(false, false, false, false);

			GLProfiler::Context profiler(renderer.GetGLProfiler(),
				"Model [%d model(s), %d unique model type(s)]",
				modelCount, (int)models.size());

			for (const auto& m : models) {
				GLModel* model = m.model;
				model->Prerender(m.params, ghostPass);
			}
			device.ColorMask(true, true, true, true);
		}

		void GLModelRenderer::RenderSunlightPass(bool ghostPass) {
			SPADES_MARK_FUNCTION();

			GLProfiler::Context profiler(renderer.GetGLProfiler(),
				"Model [%d model(s), %d unique model type(s)]",
				modelCount, (int)models.size());

			for (const auto& m : models) {
				GLModel* model = m.model;
				model->RenderSunlightPass(m.params, ghostPass);
			}
		}

		void GLModelRenderer::RenderDynamicLightPass(std::vector<GLDynamicLight> lights) {
			SPADES_MARK_FUNCTION();

			GLProfiler::Context profiler(renderer.GetGLProfiler(),
				"Model [%d model(s), %d unique model type(s)]",
				modelCount, (int)models.size());

			if (lights.empty())
				return;

			for (const auto& m : models) {
				GLModel* model = m.model;
				model->RenderDynamicLightPass(m.params, lights);
			}
		}

		void GLModelRenderer::RenderOutlinePass() {
			SPADES_MARK_FUNCTION();

			GLProfiler::Context profiler(renderer.GetGLProfiler(),
				"Model [%d model(s), %d unique model type(s)]",
				modelCount, (int)models.size());

			for (const auto& m : models) {
				GLModel* model = m.model;
				model->RenderOutlinePass(m.params);
			}
		}

		void GLModelRenderer::RenderXRayPass() {
			SPADES_MARK_FUNCTION();

			GLProfiler::Context profiler(renderer.GetGLProfiler(),
				"Model [%d model(s), %d unique model type(s)]",
				modelCount, (int)models.size());

			for (const auto& m : models) {
				GLModel* model = m.model;
				model->RenderXRayPass(m.params);
			}
		}

		void GLModelRenderer::Clear() {
			// last phase: clear scene
			for (const auto& m : models) {
				GLModel* model = m.model;
				model->renderId = -1;
				model->Release();
			}
			models.clear();
			modelCount = 0;
		}
	} // namespace draw
} // namespace spades