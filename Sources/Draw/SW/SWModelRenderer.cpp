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

#include <algorithm>
#include <cmath>

#include "SWModelRenderer.h"
#include "SWModel.h"
#include "SWRenderer.h"
#include "SWUtils.h"
#include <Core/Bitmap.h>
#include <Core/Settings.h>
#include <Core/Debug.h>
#include <Core/Math.h>

namespace spades {
	namespace draw {
		SWModelRenderer::SWModelRenderer(SWRenderer* r, SWFeatureLevel level) : r(r), level(level) {}
		SWModelRenderer::~SWModelRenderer() {}

		struct ZVals {
			float zv[512];
			ZVals() {
				for (int i = 0; i < 512; i++)
					zv[i] = static_cast<float>(i);
			}
			inline float operator[](size_t idx) const { return zv[idx]; }
		};
		static ZVals zvals;

		template <SWFeatureLevel lvl>
		void SWModelRenderer::RenderInner(spades::draw::SWModel& model,
		                                  const client::ModelRenderParam& param) {
			auto& mat = param.matrix;
			auto origin = mat.GetOrigin();
			auto axis1 = mat.GetAxis(0);
			auto axis2 = mat.GetAxis(1);
			auto axis3 = mat.GetAxis(2);
			auto& rawModel = model.GetRawModel();
			auto rawModelOrigin = rawModel.GetOrigin();
			origin += axis1 * rawModelOrigin.x;
			origin += axis2 * rawModelOrigin.y;
			origin += axis3 * rawModelOrigin.z;

			int w = rawModel.GetWidth();
			int h = rawModel.GetHeight();
			// int d = rawModel.GetDepth();

			// evaluate brightness for each normals
			uint8_t brights[3 * 3 * 3 + 1];
			{
				// The sun's share of the shading follows its light; without it every face
				// is lit alike.
				auto lightVec = MakeVector3(0.0F, -0.707F, -0.707F) * r->sceneDef.sunlight;
				float dot1 = Vector3::Dot(axis1, lightVec) * fastRSqrt(axis1.GetSquaredLength());
				float dot2 = Vector3::Dot(axis2, lightVec) * fastRSqrt(axis2.GetSquaredLength());
				float dot3 = Vector3::Dot(axis3, lightVec) * fastRSqrt(axis3.GetSquaredLength());
				for (int x = 0; x < 3; x++) {
					float d = 0.0;
					int cnt = 0;
					switch (x) {
						case 0:
							d = -dot1;
							cnt = 1;
							break;
						case 1:
							d = 0.0F;
							cnt = 0;
							break;
						case 2:
							d = dot1;
							cnt = 1;
							break;
					}
					for (int y = 0; y < 3; y++) {
						auto d2 = d;
						auto cnt2 = cnt;
						switch (y) {
							case 0:
								d2 -= dot2;
								cnt2++;
								break;
							case 1: break;
							case 2:
								d2 += dot2;
								cnt2++;
								break;
						}
						for (int z = 0; z < 3; z++) {
							auto d3 = d2;
							auto cnt3 = cnt2;
							switch (y) {
								case 0:
									d3 -= dot3;
									cnt3++;
									break;
								case 1: break;
								case 2:
									d3 += dot3;
									cnt3++;
									break;
							}
							switch (cnt3) {
								case 2: d3 *= 0.707f; break;
								case 3: d3 *= 0.57735f; break;
							}
							d3 = 192.f + d3 * 62.0F;
							brights[x + y * 3 + z * 9] = static_cast<uint8_t>(d3);
						}
					}
				}
			}

			// Lit from every direction at once (SceneDefinition::flatModelLighting):
			// every face at its own colour, whichever way it points.
			if (r->sceneDef.flatModelLighting)
				std::fill(std::begin(brights), std::end(brights), std::uint8_t(255));

			// "emissive" material
			brights[27] = 255;

			// compute center coord. for culling
			{
				auto center = origin;
				auto localCenter = model.GetCenter();
				center += axis1 * localCenter.x;
				center += axis2 * localCenter.y;
				center += axis3 * localCenter.z;

				float largestAxis = axis1.GetSquaredLength();
				largestAxis = std::max(largestAxis, axis2.GetSquaredLength());
				largestAxis = std::max(largestAxis, axis3.GetSquaredLength());

				if (!r->SphereFrustrumCull(center, model.GetRadius() * sqrtf(largestAxis)))
					return;
			}

			Bitmap& fbmp = *r->fb;
			auto* fb = fbmp.GetPixels();
			int fw = fbmp.GetWidth();
			int fh = fbmp.GetHeight();
			auto* db = r->depthBuffer.data();

			Matrix4 viewproj = r->GetProjectionViewMatrix();
			Vector4 ndc2scrscale = {fw * 0.5f, -fh * 0.5f, 1.f, 1.f};
			// Vector4 ndc2scroff = {fw * 0.5f, fh * 0.5f, 0.f, 0.f};
			int ndc2scroffX = fw >> 1;
			int ndc2scroffY = fh >> 1;

			// render each points
			auto tOrigin = viewproj * MakeVector4(origin.x, origin.y, origin.z, 1.f);
			auto tAxis1 = viewproj * MakeVector4(axis1.x, axis1.y, axis1.z, 0.f);
			auto tAxis2 = viewproj * MakeVector4(axis2.x, axis2.y, axis2.z, 0.f);
			auto tAxis3 = viewproj * MakeVector4(axis3.x, axis3.y, axis3.z, 0.f);
			tOrigin *= ndc2scrscale;
			tAxis1 *= ndc2scrscale;
			tAxis2 *= ndc2scrscale;
			tAxis3 *= ndc2scrscale;

			float pointDiameter; // = largestAxis * 0.55f * fh * 0.5f;
			{
				float largestAxis = tAxis1.GetSquaredLength();
				largestAxis = std::max(largestAxis, tAxis2.GetSquaredLength());
				largestAxis = std::max(largestAxis, tAxis3.GetSquaredLength());
				pointDiameter = sqrtf(largestAxis);
			}

			uint32_t customColor;
			customColor = (ToFixed8(param.customColor.z)
						| (ToFixed8(param.customColor.y) << 8)
						| (ToFixed8(param.customColor.x) << 16));

			auto v1 = tOrigin;
			float zNear = r->sceneDef.zNear;
			for (int x = 0; x < w; x++) {
				auto v2 = v1;
				for (int y = 0; y < h; y++) {
					auto* mp = &model.renderData[model.renderDataAddr[x + y * w]];
					while (*mp != 0xFFFFFFFFu) {
						uint32_t data = *(mp++);
						uint32_t normal = *(mp++);
						int z = static_cast<int>(data >> 24);
						// SPAssert(z < d);
						SPAssert(z >= 0);

						auto vv = v2 + tAxis3 * zvals[z];
						if (vv.z < zNear)
							continue;

						// save Z value (don't divide this by W!)
						float zval = vv.z;

						// use vv.z for point radius to be divided by W
						vv.z = pointDiameter;

						// perspective division
						float scl = fastRcp(vv.w);
						vv *= scl;

						int ix = static_cast<int>(vv.x) + ndc2scroffX;
						int iy = static_cast<int>(vv.y) + ndc2scroffY;
						int idm = static_cast<int>(vv.z + 0.99F);
						idm = std::max(1, idm);
						int minX = ix - (idm >> 1);
						int minY = iy - (idm >> 1);
						if (minX >= fw || minY >= fh)
							continue;
						int maxX = ix + idm;
						int maxY = iy + idm;
						if (maxX <= 0 || maxY <= 0)
							continue;

						minX = std::max(minX, 0);
						minY = std::max(minY, 0);
						maxX = std::min(maxX, fw);
						maxY = std::min(maxY, fh);

						auto* fb2 = fb + (minX + minY * fw);
						auto* db2 = db + (minX + minY * fw);
						int w = maxX - minX;

						uint32_t color = data & 0xFFFFFF;
						if (color == 0)
							color = customColor;

						SPAssert(normal < 28);
						int bright = brights[normal];
#if ENABLE_SSE2
						if constexpr (lvl == SWFeatureLevel::SSE2) {
							auto m = _mm_setr_epi32(color, 0, 0, 0);
							auto f = _mm_set1_epi16(bright << 8);

							m = _mm_unpacklo_epi8(m, _mm_setzero_si128());
							m = _mm_mulhi_epu16(m, f);
							m = _mm_packus_epi16(m, m);

							_mm_store_ss(reinterpret_cast<float*>(&color), _mm_castsi128_ps(m));
						} else
#endif
						{
							uint32_t c1 = color & 0xFF00;
							uint32_t c2 = color & 0xFF00FF;
							c1 *= bright;
							c2 *= bright;
							color = ((c1 & 0xFF0000) | (c2 & 0xFF00FF00)) >> 8;
						}

						for (int yy = minY; yy < maxY; yy++) {
							auto* fb3 = fb2;
							auto* db3 = db2;

							for (int xx = w; xx > 0; xx--) {
								if (zval < *db3) {
									*db3 = zval;
									*fb3 = color;
								}
								fb3++;
								db3++;
							}

							fb2 += fw;
							db2 += fw;
						}
					}
					v2 += tAxis2;
				}
				v1 += tAxis1;
			}
		}

		void SWModelRenderer::Render(spades::draw::SWModel& model,
		                             const client::ModelRenderParam& param) {
#if ENABLE_SSE2
			if (static_cast<int>(level) >= static_cast<int>(SWFeatureLevel::SSE2)) {
				RenderInner<SWFeatureLevel::SSE2>(model, param);
			} else
#endif
				RenderInner<SWFeatureLevel::None>(model, param);
		}

		void SWModelRenderer::RenderOverlay(SWModel& model,
		                                    const client::ModelOverlayParam& param) {
			SPADES_MARK_FUNCTION();

			constexpr float kEmptyDepth = 1.0E10F;

			const float opacity = Clamp(param.opacity, 0.0F, 1.0F);
			if (opacity <= 0.0F)
				return;

			Bitmap& fbmp = *r->fb;
			uint32_t* fb = fbmp.GetPixels();
			const int fw = fbmp.GetWidth();
			const int fh = fbmp.GetHeight();

			auto& rawModel = model.GetRawModel();
			const int w = rawModel.GetWidth();
			const int h = rawModel.GetHeight();
			const int d = rawModel.GetDepth();

			// Overlay space (X right, Y depth where -Y is away, Z down) to screen space
			// (X right, Y down, Z depth where smaller is nearer).
			auto toScreen = [](const Vector3& v) { return MakeVector3(v.x, v.z, -v.y); };
			auto toUnit = [](const Vector3& v) {
				const float sq = v.GetSquaredLength();
				return (sq > 1.0E-12F) ? v * fastRSqrt(sq) : v;
			};
			auto toInt = [](float v) { return ToIntClamped(v, 1.0E6F); };

			const Matrix4& mat = param.matrix;
			const Vector3 axis1 = mat.GetAxis(0);
			const Vector3 axis2 = mat.GetAxis(1);
			const Vector3 axis3 = mat.GetAxis(2);

			Vector3 origin = mat.GetOrigin();
			const Vector3 rawModelOrigin = rawModel.GetOrigin();
			origin += axis1 * rawModelOrigin.x;
			origin += axis2 * rawModelOrigin.y;
			origin += axis3 * rawModelOrigin.z;

			const Vector3 sOrigin = toScreen(origin);
			const Vector3 sAxis1 = toScreen(axis1);
			const Vector3 sAxis2 = toScreen(axis2);
			const Vector3 sAxis3 = toScreen(axis3);

			// Each voxel is drawn as a square as wide as the longest axis on the screen,
			// which leaves no gaps between neighbouring voxels.
			float longestAxis = sAxis1.x * sAxis1.x + sAxis1.y * sAxis1.y;
			longestAxis = std::max(longestAxis, sAxis2.x * sAxis2.x + sAxis2.y * sAxis2.y);
			longestAxis = std::max(longestAxis, sAxis3.x * sAxis3.x + sAxis3.y * sAxis3.y);
			const int splat = std::max(1, static_cast<int>(sqrtf(longestAxis) + 0.99F));

			// The screen rectangle the model can reach, from the corners of its voxel range.
			float minX = 1.0E10F, minY = 1.0E10F, maxX = -1.0E10F, maxY = -1.0E10F;
			for (int i = 0; i < 8; i++) {
				const Vector3 p = sOrigin + sAxis1 * static_cast<float>((i & 1) ? w - 1 : 0) +
				                  sAxis2 * static_cast<float>((i & 2) ? h - 1 : 0) +
				                  sAxis3 * static_cast<float>((i & 4) ? d - 1 : 0);
				minX = std::min(minX, p.x);
				maxX = std::max(maxX, p.x);
				minY = std::min(minY, p.y);
				maxY = std::max(maxY, p.y);
			}

			const float margin = static_cast<float>(splat + 1);
			const int rx0 = toInt(floorf(Clamp(minX - margin, 0.0F, static_cast<float>(fw))));
			const int ry0 = toInt(floorf(Clamp(minY - margin, 0.0F, static_cast<float>(fh))));
			const int rx1 = toInt(ceilf(Clamp(maxX + margin, 0.0F, static_cast<float>(fw))));
			const int ry1 = toInt(ceilf(Clamp(maxY + margin, 0.0F, static_cast<float>(fh))));
			if (rx1 <= rx0 || ry1 <= ry0)
				return;

			const int bw = rx1 - rx0;
			const int bh = ry1 - ry0;
			const std::size_t bufferSize = static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh);
			if (overlayDepth.size() < bufferSize) {
				overlayDepth.resize(bufferSize);
				overlayColor.resize(bufferSize);
			}
			std::fill(overlayDepth.begin(), overlayDepth.begin() + bufferSize, kEmptyDepth);

			// Brightness of each normal. This follows the lighting of the OpenGL overlay
			// without the baked ambient occlusion: sqrt(ambient + sun * N.L), the square
			// root being the gamma encoding the OpenGL shader applies.
			uint8_t brights[28];
			{
				const Vector3 lightVec = MakeVector3(0.0F, 0.707F, -0.707F);
				const Vector3 unit1 = toUnit(axis1);
				const Vector3 unit2 = toUnit(axis2);
				const Vector3 unit3 = toUnit(axis3);
				for (int n = 0; n < 27; n++) {
					const float nx = static_cast<float>(n % 3 - 1);
					const float ny = static_cast<float>((n / 3) % 3 - 1);
					const float nz = static_cast<float>(n / 9 - 1);
					const Vector3 normal = unit1 * nx + unit2 * ny + unit3 * nz;
					const float sq = normal.GetSquaredLength();
					float lit = 0.0F;
					if (sq > 1.0E-12F)
						lit = std::max(Vector3::Dot(normal, lightVec) * fastRSqrt(sq), 0.0F);
					const float shade = sqrtf(0.55F + 0.6F * lit);
					brights[n] = static_cast<uint8_t>(ToFixed8(shade));
				}
				// "emissive" material
				brights[27] = 255;
			}

			uint32_t customColor;
			customColor = (ToFixed8(param.customColor.z)
						| (ToFixed8(param.customColor.y) << 8)
						| (ToFixed8(param.customColor.x) << 16));

			Vector3 v1 = sOrigin;
			for (int x = 0; x < w; x++) {
				Vector3 v2 = v1;
				for (int y = 0; y < h; y++) {
					const uint32_t* mp = &model.renderData[model.renderDataAddr[x + y * w]];
					while (*mp != 0xFFFFFFFFu) {
						const uint32_t data = *(mp++);
						const uint32_t normal = *(mp++);
						const int z = static_cast<int>(data >> 24);
						SPAssert(z >= 0);
						SPAssert(normal < 28);

						const Vector3 vv = v2 + sAxis3 * static_cast<float>(z);

						const int minPX = toInt(floorf(vv.x - 0.5F * splat + 0.5F));
						const int minPY = toInt(floorf(vv.y - 0.5F * splat + 0.5F));
						const int px0 = std::max(minPX, rx0);
						const int py0 = std::max(minPY, ry0);
						const int px1 = std::min(minPX + splat, rx1);
						const int py1 = std::min(minPY + splat, ry1);
						if (px0 >= px1 || py0 >= py1)
							continue;

						uint32_t color = data & 0xFFFFFF;
						if (color == 0)
							color = customColor;

						const uint32_t bright = brights[normal];
						{
							uint32_t c1 = color & 0xFF00;
							uint32_t c2 = color & 0xFF00FF;
							c1 *= bright;
							c2 *= bright;
							color = ((c1 & 0xFF0000) | (c2 & 0xFF00FF00)) >> 8;
						}

						const float zval = vv.z;
						for (int yy = py0; yy < py1; yy++) {
							const std::size_t offset = static_cast<std::size_t>(yy - ry0) * bw + (px0 - rx0);
							float* db = overlayDepth.data() + offset;
							uint32_t* cb = overlayColor.data() + offset;
							for (int xx = px0; xx < px1; xx++) {
								if (zval < *db) {
									*db = zval;
									*cb = color;
								}
								db++;
								cb++;
							}
						}
					}
					v2 += sAxis2;
				}
				v1 += sAxis1;
			}

			// put the model over the framebuffer, blended by its opacity.
			const uint32_t alpha = static_cast<uint32_t>(opacity * 256.0F + 0.5F);
			for (int yy = 0; yy < bh; yy++) {
				const float* db = overlayDepth.data() + static_cast<std::size_t>(yy) * bw;
				const uint32_t* cb = overlayColor.data() + static_cast<std::size_t>(yy) * bw;
				uint32_t* out = fb + (ry0 + yy) * fw + rx0;
				for (int xx = 0; xx < bw; xx++) {
					if (!(db[xx] < kEmptyDepth))
						continue;
					if (alpha >= 256) {
						out[xx] = cb[xx];
					} else {
						const uint32_t src = cb[xx];
						const uint32_t dst = out[xx];
						const uint32_t rb = ((src & 0xFF00FF) * alpha + (dst & 0xFF00FF) * (256 - alpha)) >> 8;
						const uint32_t g = ((src & 0xFF00) * alpha + (dst & 0xFF00) * (256 - alpha)) >> 8;
						out[xx] = (rb & 0xFF00FF) | (g & 0xFF00);
					}
				}
			}
		}
	} // namespace draw
} // namespace spades