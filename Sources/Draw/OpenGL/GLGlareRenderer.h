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

#include <vector>

#include <Client/IRenderer.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace draw {
		class GLRenderer;
		class IGLDevice;
		class GLImage;
		class GLProgram;

		/**
		 * Draws the scene's glares (`client::GlareParam`) over the finished frame,
		 * and under the first-person view's models, which the scene's depth buffer
		 * tells apart.
		 */
		class GLGlareRenderer {
			struct Glare {
				Handle<GLImage> image;
				client::GlareParam param;
			};

			GLRenderer& renderer;
			IGLDevice& device;
			GLProgram* program;
			std::vector<Glare> glares;

		public:
			GLGlareRenderer(GLRenderer&);

			void Clear();
			void Add(GLImage&, const client::GlareParam&);

			/**
			 * Draws the glares onto the bound framebuffer, which holds the finished
			 * frame at the screen's size, while the scene's depth texture still holds
			 * the scene the glares were added to.
			 */
			void Render();
		};
	} // namespace draw
} // namespace spades
