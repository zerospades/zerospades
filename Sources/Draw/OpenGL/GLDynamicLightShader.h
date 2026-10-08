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

#include "GLDynamicLight.h"
#include "GLProgramUniform.h"
#include <Client/IRenderer.h>
#include <Core/Math.h>

namespace spades {
	namespace draw {
		class GLRenderer;
		class GLImage;
		class GLProgramManager;

		/**
		 * Lights draws with several dynamic lights at once.
		 *
		 * The lights that reach a draw are split into batches of up to
		 * `MaxLightsPerDraw` that share one spotlight image, and each batch takes a
		 * single draw. The lights themselves are uploaded once a frame, in the
		 * renderer's `GLDynamicLightTable`: a draw only names the rows of its batch.
		 */
		class GLDynamicLightShader {
		public:
			/** The most lights one draw takes: `DYNAMIC_LIGHT_MAX` in the shaders. */
			static constexpr std::size_t MaxLightsPerDraw = 8;

		private:
			GLRenderer* lastRenderer = nullptr;
			Handle<GLImage> whiteImage;

			// Set once a frame
			GLProgramUniform projectionTexture;
			GLProgramUniform mapOccupancy;
			GLProgramUniform mapSizeInversed;
			GLProgramUniform mapOcclusion;
			GLProgramUniform eye;
			GLProgramUniform table;
			GLProgramUniform tableRowsInversed;

			// Set for each batch: how many lights it has, and their rows in the table,
			// four to a vector
			GLProgramUniform count;
			GLProgramUniform rowsLow;
			GLProgramUniform rowsHigh;

			// Batching scratch, kept from draw to draw.
			std::vector<const GLDynamicLight*> pending;
			std::vector<const GLDynamicLight*> deferred;
			std::vector<const GLDynamicLight*> batch;

			// The frame the last program was set up for, and the batch it holds.
			GLProgram* uploadedProgram = nullptr;
			std::uint32_t uploadedFrame = 0;
			std::vector<const GLDynamicLight*> uploadedBatch;

			/** The image a spotlight projects, or none for another kind of light. */
			static GLImage* GetSpotImage(const GLDynamicLight&);

			/**
			 * Fills `batch` with the first lights of `pending` that fit one draw, in
			 * their order, and `deferred` with the rest; returns the batch's image.
			 */
			GLImage* TakeBatch();

			/** Binds `image`, the map's occupancy and the light table, and sets the
			 * program up for `batch`. */
			void SetUp(GLRenderer*, GLProgram*, GLImage* image, int texStage);

		public:
			GLDynamicLightShader();
			~GLDynamicLightShader();

			static std::vector<GLShader*> RegisterShader(GLProgramManager*);

			/**
			 * Lights a draw with every light of `lights`, the renderer's for this frame,
			 * that `reaches` accepts: calls `draw` once per batch, with `program` set up
			 * for it, the batch's image on `texStage`, the map's occupancy on the next
			 * stage and the light table on the one after. `texStage` is left the active
			 * texture stage.
			 */
			template <class Reaches, class Draw>
			void Render(GLRenderer* renderer, GLProgram* program,
			            const std::vector<GLDynamicLight>& lights, int texStage,
			            Reaches&& reaches, Draw&& draw) {
				pending.clear();
				for (const GLDynamicLight& light : lights)
					if (reaches(light))
						pending.push_back(&light);

				while (!pending.empty()) {
					GLImage* image = TakeBatch();
					SetUp(renderer, program, image, texStage);
					draw();
					pending.swap(deferred);
				}
			}
		};
	} // namespace draw
} // namespace spades
