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

#include "GLImageRenderer.h"
#include "GLImage.h"
#include "GLRenderer.h"
#include "IGLDevice.h"
#include <Core/Debug.h>
#include <Core/Exception.h>

namespace spades {
	namespace draw {
		GLImageRenderer::GLImageRenderer(GLRenderer& r)
		    : renderer(r), device(r.GetGLDevice()) {

			SPADES_MARK_FUNCTION();
			image = NULL;

			program = renderer.RegisterProgram("Shaders/OpenGL/BasicImage.program");

			program->Use();

			positionAttribute = new GLProgramAttribute("positionAttribute");
			colorAttribute = new GLProgramAttribute("colorAttribute");
			textureCoordAttribute = new GLProgramAttribute("textureCoordAttribute");
			screenSize = new GLProgramUniform("invScreenSizeFactored");
			textureSize = new GLProgramUniform("invTextureSize");
			texture = new GLProgramUniform("mainTexture");

			(*positionAttribute)(program);
			(*colorAttribute)(program);
			(*textureCoordAttribute)(program);

			(*screenSize)(program);
			(*textureSize)(program);
			(*texture)(program);
		}

		GLImageRenderer::~GLImageRenderer() {
			if (image != NULL) {
				image->Release();
				image = NULL;
			}
			delete positionAttribute;
			delete colorAttribute;
			delete textureCoordAttribute;
			delete screenSize;
			delete textureSize;
			delete texture;
		}

		void GLImageRenderer::Flush() {
			if (vertices.empty())
				return;

			SPADES_MARK_FUNCTION();
			SPAssert(image);

			program->Use();

			device.ActiveTexture(0);
			image->Bind(IGLDevice::Texture2D);

			device.VertexAttribPointer((*positionAttribute)(), 2, IGLDevice::FloatType, false,
			                           sizeof(ImageVertex), vertices.data());
			device.VertexAttribPointer((*colorAttribute)(), 4, IGLDevice::FloatType, false,
			                           sizeof(ImageVertex),
			                           (const char*)vertices.data() + sizeof(float) * 4);
			device.VertexAttribPointer((*textureCoordAttribute)(), 2, IGLDevice::FloatType, false,
			                           sizeof(ImageVertex),
			                           (const char*)vertices.data() + sizeof(float) * 2);

			device.EnableVertexAttribArray((*positionAttribute)(), true);
			device.EnableVertexAttribArray((*colorAttribute)(), true);
			device.EnableVertexAttribArray((*textureCoordAttribute)(), true);

			// Vertices are in 2D units (window units), not framebuffer pixels, so a
			// high-DPI framebuffer draws the same layout with more pixels. Read
			// every flush: the window can change size or move to another display.
			screenSize->SetValue(2.0F / renderer.ScreenWidth(), -2.0F / renderer.ScreenHeight());
			textureSize->SetValue(image->GetInvWidth(), image->GetInvHeight());
			texture->SetValue(0);

			device.DrawElements(IGLDevice::Triangles, static_cast<IGLDevice::Sizei>(indices.size()),
			                    IGLDevice::UnsignedInt, indices.data());

			device.EnableVertexAttribArray((*positionAttribute)(), false);
			device.EnableVertexAttribArray((*colorAttribute)(), false);
			device.EnableVertexAttribArray((*textureCoordAttribute)(), false);

			vertices.clear();
			indices.clear();
			image->Release();
			image = NULL;
		}

		void GLImageRenderer::SetImage(spades::draw::GLImage* img) {
			if (img == image)
				return;
			Flush();
			image = img;
			image->AddRef();
		}

		void GLImageRenderer::Add(float dx1, float dy1, float dx2, float dy2, float dx3, float dy3,
		                          float dx4, float dy4, float sx1, float sy1, float sx2, float sy2,
		                          float sx3, float sy3, float sx4, float sy4, float r, float g,
		                          float b, float a) {
			ImageVertex v;
			v.r = r;
			v.g = g;
			v.b = b;
			v.a = a;

			uint32_t idx = (uint32_t)vertices.size();

			v.x = dx1;
			v.y = dy1;
			v.u = sx1;
			v.v = sy1;
			vertices.push_back(v);

			v.x = dx2;
			v.y = dy2;
			v.u = sx2;
			v.v = sy2;
			vertices.push_back(v);

			v.x = dx3;
			v.y = dy3;
			v.u = sx3;
			v.v = sy3;
			vertices.push_back(v);

			v.x = dx4;
			v.y = dy4;
			v.u = sx4;
			v.v = sy4;
			vertices.push_back(v);

			indices.push_back(idx);
			indices.push_back(idx + 1);
			indices.push_back(idx + 2);
			indices.push_back(idx);
			indices.push_back(idx + 2);
			indices.push_back(idx + 3);
		}

		void GLImageRenderer::AddTriangle(float dx1, float dy1, float dx2, float dy2,
                                  float dx3, float dy3,
                                  float r, float g, float b, float a) {
			ImageVertex v;
			v.r = r; v.g = g; v.b = b; v.a = a;

			uint32_t idx = (uint32_t)vertices.size();

			v.x = dx1; v.y = dy1; v.u = 0.0F; v.v = 0.0F;
			vertices.push_back(v);
			v.x = dx2; v.y = dy2; v.u = 0.0F; v.v = 0.0F;
			vertices.push_back(v);
			v.x = dx3; v.y = dy3; v.u = 0.0F; v.v = 0.0F;
			vertices.push_back(v);

			indices.push_back(idx);
			indices.push_back(idx + 1);
			indices.push_back(idx + 2);
		}

		void GLImageRenderer::AddShadedTriangle(const Vector2& p1, const Vector2& p2,
		                                        const Vector2& p3, const Vector4& c1,
		                                        const Vector4& c2, const Vector4& c3) {
			uint32_t idx = (uint32_t)vertices.size();

			const Vector2* p[3] = {&p1, &p2, &p3};
			const Vector4* c[3] = {&c1, &c2, &c3};
			for (int i = 0; i < 3; i++) {
				ImageVertex v;
				v.x = p[i]->x;
				v.y = p[i]->y;
				v.u = 0.0F;
				v.v = 0.0F;
				v.r = c[i]->x;
				v.g = c[i]->y;
				v.b = c[i]->z;
				v.a = c[i]->w;
				vertices.push_back(v);
			}

			indices.push_back(idx);
			indices.push_back(idx + 1);
			indices.push_back(idx + 2);
		}

		void GLImageRenderer::AddGradient(
			float dx1, float dy1, float dx2, float dy2,
			float dx3, float dy3, float dx4, float dy4,
			float sx1, float sy1, float sx2, float sy2,
			float sx3, float sy3, float sx4, float sy4,
			float r0, float g0, float b0, float a0,
			float r1, float g1, float b1, float a1,
			bool horizontal) {

			uint32_t idx = (uint32_t)vertices.size();
			ImageVertex v;

			if (horizontal) {
				// left verts = color0, right verts = color1
				// top-left (color0)
				v.x = dx1; v.y = dy1; v.u = sx1; v.v = sy1;
				v.r = r0; v.g = g0; v.b = b0; v.a = a0;
				vertices.push_back(v);

				// top-right (color1)
				v.x = dx2; v.y = dy2; v.u = sx2; v.v = sy2;
				v.r = r1; v.g = g1; v.b = b1; v.a = a1;
				vertices.push_back(v);

				// bottom-right (color1)
				v.x = dx3; v.y = dy3; v.u = sx3; v.v = sy3;
				vertices.push_back(v);

				// bottom-left (color0)
				v.x = dx4; v.y = dy4; v.u = sx4; v.v = sy4;
				v.r = r0; v.g = g0; v.b = b0; v.a = a0;
				vertices.push_back(v);
			} else {
				// top verts = color0, bottom verts = color1
				// top-left (color0)
				v.x = dx1; v.y = dy1; v.u = sx1; v.v = sy1;
				v.r = r0; v.g = g0; v.b = b0; v.a = a0;
				vertices.push_back(v);

				// top-right (color0)
				v.x = dx2; v.y = dy2; v.u = sx2; v.v = sy2;
				vertices.push_back(v);

				// bottom-right (color1)
				v.x = dx3; v.y = dy3; v.u = sx3; v.v = sy3;
				v.r = r1; v.g = g1; v.b = b1; v.a = a1;
				vertices.push_back(v);

				// bottom-left (color1)
				v.x = dx4; v.y = dy4; v.u = sx4; v.v = sy4;
				vertices.push_back(v);
			}

			indices.push_back(idx);
			indices.push_back(idx + 1);
			indices.push_back(idx + 2);
			indices.push_back(idx);
			indices.push_back(idx + 2);
			indices.push_back(idx + 3);
		}
	} // namespace draw
} // namespace spades