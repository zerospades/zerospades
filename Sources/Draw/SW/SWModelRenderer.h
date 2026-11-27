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

#pragma once

#include <cstdint>
#include <vector>

#include "SWFeatureLevel.h"
#include <Client/IRenderer.h>

namespace spades {
	namespace draw {
		class SWModel;
		class SWRenderer;
		class SWModelRenderer {
			friend class SWRenderer;
			SWRenderer *r;
			SWFeatureLevel level;

			/** Scratch buffers of `RenderOverlay`, as large as the biggest overlay so far. */
			std::vector<float> overlayDepth;
			std::vector<uint32_t> overlayColor;

			template <SWFeatureLevel>
			void RenderInner(SWModel &model, const client::ModelRenderParam &param);

		public:
			SWModelRenderer(SWRenderer *, SWFeatureLevel level);
			~SWModelRenderer();

			void Render(SWModel &model, const client::ModelRenderParam &param);

			/** Draws the model flat onto the framebuffer (see `IRenderer::DrawModelOverlay`). */
			void RenderOverlay(SWModel &model, const client::ModelOverlayParam &param);
		};
	} // namespace draw
} // namespace spades
