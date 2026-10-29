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

#include "GLLongSpriteRenderer.h"
#include "GLImage.h"
#include "GLProgram.h"
#include "GLRenderer.h"
#include "IGLDevice.h"
#include "../SW/SWFeatureLevel.h" // for fastRcp
#include <Core/Debug.h>
#include <Core/Settings.h>

namespace spades {
	namespace draw {

		GLLongSpriteRenderer::GLLongSpriteRenderer(GLRenderer &renderer)
		    : renderer(renderer),
		      device(renderer.GetGLDevice()),
		      settings(renderer.GetSettings()),
		      projectionViewMatrix("projectionViewMatrix"),
		      rightVector("rightVector"),
		      upVector("upVector"),
		      texture("mainTexture"),
		      viewMatrix("viewMatrix"),
		      fogDistance("fogDistance"),
		      fogColor("fogColor"),
		      viewOriginVector("viewOriginVector"),
		      positionAttribute("positionAttribute"),
		      texCoordAttribute("texCoordAttribute"),
		      colorAttribute("colorAttribute") {
			SPADES_MARK_FUNCTION();

			program = renderer.RegisterProgram("Shaders/OpenGL/LongSprite.program");
		}

		GLLongSpriteRenderer::~GLLongSpriteRenderer() { SPADES_MARK_FUNCTION(); }

		void GLLongSpriteRenderer::Add(spades::draw::GLImage *img, spades::Vector3 p1,
		                               spades::Vector3 p2, float rad, Vector4 color) {
			SPADES_MARK_FUNCTION_DEBUG();
			Sprite spr;
			spr.image = img;
			spr.start = p1;
			spr.end = p2;
			spr.radius = rad;
			spr.color = PassColor(color);
			sprites.push_back(spr);
		}

		Vector4 GLLongSpriteRenderer::PassColor(Vector4 color) const {
			if (settings.r_hdr) {
				// linearize color
				if (color.x > color.w || color.y > color.w || color.z > color.w) {
					// emissive material
					color.x *= color.x;
					color.y *= color.y;
					color.z *= color.z;
				} else {
					// scattering/absorptive material
					float rcp = fastRcp(color.w + 0.01F);
					color.x *= color.x * rcp;
					color.y *= color.y * rcp;
					color.z *= color.z * rcp;
				}
			}
			return color;
		}

		void GLLongSpriteRenderer::AddPlane(GLImage *img, Vector3 origin, Vector3 axisU,
		                                    Vector3 axisV, Vector4 color) {
			SPADES_MARK_FUNCTION_DEBUG();
			Plane plane;
			plane.image = img;
			plane.origin = origin;
			plane.axisU = axisU;
			plane.axisV = axisV;
			plane.color = PassColor(color);
			planes.push_back(plane);
		}

		void GLLongSpriteRenderer::Clear() {
			SPADES_MARK_FUNCTION();
			sprites.clear();
			planes.clear();
		}

		void GLLongSpriteRenderer::RenderPlanes() {
			// The GPU clips a plane against the view itself: unlike a long
			// sprite, nothing here is built in screen space.
			for (const Plane &plane : planes) {
				if (plane.image != lastImage) {
					Flush();
					lastImage = plane.image;
				}
				Vertex v;
				v.r = plane.color.x;
				v.g = plane.color.y;
				v.b = plane.color.z;
				v.a = plane.color.w;
				const uint32_t idx = (uint32_t)vertices.size();
				v = plane.origin;
				v.u = 0;
				v.v = 0;
				vertices.push_back(v);
				v = plane.origin + plane.axisU;
				v.u = 1;
				v.v = 0;
				vertices.push_back(v);
				v = plane.origin + plane.axisV;
				v.u = 0;
				v.v = 1;
				vertices.push_back(v);
				v = plane.origin + plane.axisU + plane.axisV;
				v.u = 1;
				v.v = 1;
				vertices.push_back(v);
				// Both windings, so the plane shows from either side whatever is
				// culled.
				for (uint32_t i : {0u, 1u, 2u, 1u, 3u, 2u, 0u, 2u, 1u, 1u, 2u, 3u})
					indices.push_back(idx + i);
			}
		}

		void GLLongSpriteRenderer::Render() {
			SPADES_MARK_FUNCTION();
			lastImage = NULL;
			program->Use();

			projectionViewMatrix(program);
			rightVector(program);
			upVector(program);
			texture(program);
			viewMatrix(program);
			fogDistance(program);
			fogColor(program);
			viewOriginVector(program);

			positionAttribute(program);
			texCoordAttribute(program);
			colorAttribute(program);

			projectionViewMatrix.SetValue(renderer.GetProjectionViewMatrix());
			viewMatrix.SetValue(renderer.GetViewMatrix());

			fogDistance.SetValue(renderer.GetFogDistance());

			const auto &viewOrigin = renderer.GetSceneDef().viewOrigin;
			viewOriginVector.SetValue(viewOrigin.x, viewOrigin.y, viewOrigin.z);

			Vector3 fogCol = renderer.GetFogColor();
			//fogCol *= fogCol; // linearize
			fogColor.SetValue(fogCol.x, fogCol.y, fogCol.z);

			const client::SceneDefinition &def = renderer.GetSceneDef();
			rightVector.SetValue(def.viewAxis[0].x, def.viewAxis[0].y, def.viewAxis[0].z);
			upVector.SetValue(def.viewAxis[1].x, def.viewAxis[1].y, def.viewAxis[1].z);
			texture.SetValue(0);

			device.ActiveTexture(0);

			device.EnableVertexAttribArray(positionAttribute(), true);
			device.EnableVertexAttribArray(texCoordAttribute(), true);
			device.EnableVertexAttribArray(colorAttribute(), true);

			for (size_t i = 0; i < sprites.size(); i++) {
				Sprite &spr = sprites[i];
				if (spr.image != lastImage) {
					Flush();
					lastImage = spr.image;
					SPAssert(vertices.empty());
				}

				Vertex v;
				v.r = spr.color.x;
				v.g = spr.color.y;
				v.b = spr.color.z;
				v.a = spr.color.w;

				uint32_t idx = (uint32_t)vertices.size();

				// clip by view plane
				{
					float d1 = Vector3::Dot(spr.start - def.viewOrigin, def.viewAxis[2]);
					float d2 = Vector3::Dot(spr.end - def.viewOrigin, def.viewAxis[2]);
					const float clipPlane = .1f;
					if (d1 < clipPlane && d2 < clipPlane)
						continue;
					if (d1 > clipPlane || d2 > clipPlane) {
						if (d1 < clipPlane) {
							float per = (clipPlane - d1) / (d2 - d1);
							spr.start = Mix(spr.start, spr.end, per);
						} else if (d2 < clipPlane) {
							float per = (clipPlane - d1) / (d2 - d1);
							spr.end = Mix(spr.start, spr.end, per);
						}
					}
				}

				// calculate view position
				Vector3 view1 = spr.start - def.viewOrigin;
				Vector3 view2 = spr.end - def.viewOrigin;
				view1 = MakeVector3(Vector3::Dot(view1, def.viewAxis[0]),
				                    Vector3::Dot(view1, def.viewAxis[1]),
				                    Vector3::Dot(view1, def.viewAxis[2]));
				view2 = MakeVector3(Vector3::Dot(view2, def.viewAxis[0]),
				                    Vector3::Dot(view2, def.viewAxis[1]),
				                    Vector3::Dot(view2, def.viewAxis[2]));

				// transform to screen
				Vector2 scr1 = MakeVector2(view1.x / view1.z, view1.y / view1.z);
				Vector2 scr2 = MakeVector2(view2.x / view2.z, view2.y / view2.z);

				Vector3 vecX = def.viewAxis[0] * spr.radius;
				Vector3 vecY = def.viewAxis[1] * spr.radius;
				float normalThreshold = spr.radius * 0.5f / ((view1.z + view2.z) * .5f);
				if ((scr2 - scr1).GetSquaredLength() < normalThreshold * normalThreshold) {
					// too short in screen; normal sprite
					v = spr.start - vecX - vecY;
					v.u = 0;
					v.v = 0;
					vertices.push_back(v);

					v = spr.start + vecX - vecY;
					v.u = 1;
					v.v = 0;
					vertices.push_back(v);

					v = spr.start - vecX + vecY;
					v.u = 0;
					v.v = 1;
					vertices.push_back(v);

					v = spr.start + vecX + vecY;
					v.u = 1;
					v.v = 1;
					vertices.push_back(v);

					indices.push_back(idx);
					indices.push_back(idx + 1);
					indices.push_back(idx + 2);
					indices.push_back(idx + 1);
					indices.push_back(idx + 3);
					indices.push_back(idx + 2);
				} else {
					Vector2 scrDir = (scr2 - scr1).Normalize();
					Vector2 normDir = {scrDir.y, -scrDir.x};
					Vector3 vecU = vecX * normDir.x + vecY * normDir.y;
					Vector3 vecV = vecX * scrDir.x + vecY * scrDir.y;

					v = spr.start - vecU - vecV;
					v.u = 0;
					v.v = 0;
					vertices.push_back(v);

					v = spr.start + vecU - vecV;
					v.u = 1;
					v.v = 0;
					vertices.push_back(v);

					v = spr.start - vecU;
					v.u = 0;
					v.v = 0.5f;
					vertices.push_back(v);

					v = spr.start + vecU;
					v.u = 1;
					v.v = 0.5f;
					vertices.push_back(v);

					v = spr.end - vecU;
					v.u = 0;
					v.v = 0.5f;
					vertices.push_back(v);

					v = spr.end + vecU;
					v.u = 1;
					v.v = 0.5f;
					vertices.push_back(v);

					v = spr.end - vecU + vecV;
					v.u = 0;
					v.v = 1;
					vertices.push_back(v);

					v = spr.end + vecU + vecV;
					v.u = 1;
					v.v = 1;
					vertices.push_back(v);

					indices.push_back(idx);
					indices.push_back(idx + 1);
					indices.push_back(idx + 2);
					indices.push_back(idx + 1);
					indices.push_back(idx + 3);
					indices.push_back(idx + 2);

					indices.push_back(idx + 2);
					indices.push_back(idx + 2 + 1);
					indices.push_back(idx + 2 + 2);
					indices.push_back(idx + 2 + 1);
					indices.push_back(idx + 2 + 3);
					indices.push_back(idx + 2 + 2);

					indices.push_back(idx + 4);
					indices.push_back(idx + 4 + 1);
					indices.push_back(idx + 4 + 2);
					indices.push_back(idx + 4 + 1);
					indices.push_back(idx + 4 + 3);
					indices.push_back(idx + 4 + 2);

					idx = (uint32_t)vertices.size();

					v = spr.start - vecU + vecV;
					v.u = 0;
					v.v = 0;
					vertices.push_back(v);

					v = spr.start + vecU + vecV;
					v.u = 1;
					v.v = 0;
					vertices.push_back(v);

					v = spr.end - vecU - vecV;
					v.u = 0;
					v.v = 1;
					vertices.push_back(v);

					v = spr.end + vecU - vecV;
					v.u = 1;
					v.v = 1;
					vertices.push_back(v);

					v.r = v.g = v.b = v.a = 0.0F;

					v = spr.start - vecU;
					v.u = 0;
					v.v = 0.5f;
					vertices.push_back(v);

					v = spr.start + vecU;
					v.u = 1;
					v.v = 0.5f;
					vertices.push_back(v);

					v = spr.end - vecU;
					v.u = 0;
					v.v = 0.5f;
					vertices.push_back(v);

					v = spr.end + vecU;
					v.u = 1;
					v.v = 0.5f;
					vertices.push_back(v);

					indices.push_back(idx);
					indices.push_back(idx + 1);
					indices.push_back(idx + 2 + 2);
					indices.push_back(idx + 1);
					indices.push_back(idx + 2 + 3);
					indices.push_back(idx + 2 + 2);

					indices.push_back(idx + 2);
					indices.push_back(idx + 2 + 1);
					indices.push_back(idx + 4 + 2);
					indices.push_back(idx + 2 + 1);
					indices.push_back(idx + 4 + 3);
					indices.push_back(idx + 4 + 2);
				}
			}

			RenderPlanes();
			Flush();

			device.EnableVertexAttribArray(positionAttribute(), false);
			device.EnableVertexAttribArray(texCoordAttribute(), false);
			device.EnableVertexAttribArray(colorAttribute(), false);
		}

		void GLLongSpriteRenderer::Flush() {
			SPADES_MARK_FUNCTION_DEBUG();

			if (vertices.empty())
				return;

			device.VertexAttribPointer(positionAttribute(), 3, IGLDevice::FloatType, false,
			                           sizeof(Vertex), &(vertices[0].x));
			device.VertexAttribPointer(texCoordAttribute(), 2, IGLDevice::FloatType, false,
			                           sizeof(Vertex), &(vertices[0].u));
			device.VertexAttribPointer(colorAttribute(), 4, IGLDevice::FloatType, false,
			                           sizeof(Vertex), &(vertices[0].r));

			SPAssert(lastImage);
			lastImage->Bind(IGLDevice::Texture2D);

			device.DrawElements(IGLDevice::Triangles, static_cast<IGLDevice::Sizei>(indices.size()),
			                    IGLDevice::UnsignedInt, indices.data());

			vertices.clear();
			indices.clear();
		}
	} // namespace draw
} // namespace spades
