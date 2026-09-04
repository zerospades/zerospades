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
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include <set>

#include "CellToTriangle.h"
#include "GLDynamicLightShader.h"
#include "GLImage.h"
#include "GLOptimizedVoxelModel.h"
#include "GLProgram.h"
#include "GLProgramAttribute.h"
#include "GLProgramUniform.h"
#include "GLRenderer.h"
#include "GLShadowMapShader.h"
#include "GLShadowShader.h"
#include "IGLShadowMapRenderer.h"
#include <Core/Bitmap.h>
#include <Core/BitmapAtlasGenerator.h>
#include <Core/Debug.h>
#include <Core/Exception.h>

namespace spades {
	namespace draw {
		void GLOptimizedVoxelModel::PreloadShaders(GLRenderer& renderer) {
			if (renderer.GetSettings().r_physicalLighting)
				renderer.RegisterProgram("Shaders/OpenGL/OptimizedVoxelModelPhys.program");
			else
				renderer.RegisterProgram("Shaders/OpenGL/OptimizedVoxelModel.program");
			renderer.RegisterProgram("Shaders/OpenGL/OptimizedVoxelModelDynamicLit.program");
			renderer.RegisterProgram("Shaders/OpenGL/OptimizedVoxelModelShadowMap.program");
			renderer.RegisterProgram("Shaders/OpenGL/OptimizedVoxelModelOutlines.program");
			renderer.RegisterImage("Gfx/AmbientOcclusion.png");
		}
		GLOptimizedVoxelModel::GLOptimizedVoxelModel(VoxelModel* m, GLRenderer& r)
			: renderer{r}, device{r.GetGLDevice()} {
			SPADES_MARK_FUNCTION();

			BuildVertices(m);
			GenerateTexture();

			if (r.GetSettings().r_physicalLighting)
				program = renderer.RegisterProgram("Shaders/OpenGL/OptimizedVoxelModelPhys.program");
			else
				program = renderer.RegisterProgram("Shaders/OpenGL/OptimizedVoxelModel.program");
			dlightProgram = renderer.RegisterProgram("Shaders/OpenGL/OptimizedVoxelModelDynamicLit.program");
			shadowMapProgram = renderer.RegisterProgram("Shaders/OpenGL/OptimizedVoxelModelShadowMap.program");
			outlinesProgram = renderer.RegisterProgram("Shaders/OpenGL/OptimizedVoxelModelOutlines.program");
			aoImage = renderer.RegisterImage("Gfx/AmbientOcclusion.png").Cast<GLImage>();

			buffer = device.GenBuffer();
			device.BindBuffer(IGLDevice::ArrayBuffer, buffer);
			device.BufferData(IGLDevice::ArrayBuffer,
							  static_cast<IGLDevice::Sizei>(vertices.size() * sizeof(Vertex)),
							  vertices.data(), IGLDevice::StaticDraw);

			idxBuffer = device.GenBuffer();
			device.BindBuffer(IGLDevice::ArrayBuffer, idxBuffer);
			device.BufferData(IGLDevice::ArrayBuffer,
							  static_cast<IGLDevice::Sizei>(indices.size() * sizeof(uint32_t)),
							  indices.data(), IGLDevice::StaticDraw);
			device.BindBuffer(IGLDevice::ArrayBuffer, 0);

			origin = m->GetOrigin();
			origin -= 0.5F; // (0,0,0) is center of voxel (0,0,0)

			dimensions.x = m->GetWidth();
			dimensions.y = m->GetHeight();
			dimensions.z = m->GetDepth();

			Vector3 minPos = {0, 0, 0};
			Vector3 maxPos = MakeVector3(dimensions);
			minPos += origin;
			maxPos += origin;
			Vector3 maxDiff = {
				std::max(fabsf(minPos.x), fabsf(maxPos.x)),
				std::max(fabsf(minPos.y), fabsf(maxPos.y)),
				std::max(fabsf(minPos.z), fabsf(maxPos.z))
			};
			radius = maxDiff.GetLength();

			boundingBox.min = minPos;
			boundingBox.max = maxPos;

			// clean up
			numIndices = (unsigned int)indices.size();
			std::vector<Vertex>().swap(vertices);
			std::vector<uint32_t>().swap(indices);
		}
		GLOptimizedVoxelModel::~GLOptimizedVoxelModel() {
			SPADES_MARK_FUNCTION();

			device.DeleteBuffer(idxBuffer);
			device.DeleteBuffer(buffer);
		}

		void GLOptimizedVoxelModel::GenerateTexture() {
			BitmapAtlasGenerator atlasGen;
			std::map<Bitmap*, int> idx;
			std::vector<IntVector3> poss;
			for (size_t i = 0; i < bmps.size(); i++) {
				idx[bmps[i]] = static_cast<int>(i);
				atlasGen.AddBitmap(bmps[i]);
			}

			BitmapAtlasGenerator::Result result = atlasGen.Pack();
			Handle<Bitmap> bmp(result.bitmap, false);
			SPAssert(result.items.size() == bmps.size());
			for (size_t i = 0; i < bmps.size(); i++)
				bmps[i]->Release();
			bmps.clear();

			poss.resize(result.items.size());
			for (size_t i = 0; i < result.items.size(); i++) {
				const BitmapAtlasGenerator::Item& item = result.items[i];
				int id = idx[item.bitmap];
				poss[id] = MakeIntVector3(item.x, item.y, 0);
			}

			// move vertices' texture coord
			SPAssert(bmpIndex.size() == vertices.size());
			for (size_t i = 0; i < bmpIndex.size(); i++) {
				int id = (int)bmpIndex[i];
				Vertex& v = vertices[i];
				IntVector3 p = poss[id];
				v.u += p.x;
				v.v += p.y;
			}

			std::vector<uint16_t>().swap(bmpIndex);

			image = renderer.CreateImage(*bmp).Cast<GLImage>();
		}

		uint8_t GLOptimizedVoxelModel::calcAOID(VoxelModel* m, int x, int y, int z, int ux, int uy,
												int uz, int vx, int vy, int vz) {
			int v = 0;
			if (m->IsSolid(x - ux, y - uy, z - uz))
				v |= 1;
			if (m->IsSolid(x + ux, y + uy, z + uz))
				v |= 1 << 1;
			if (m->IsSolid(x - vx, y - vy, z - vz))
				v |= 1 << 2;
			if (m->IsSolid(x + vx, y + vy, z + vz))
				v |= 1 << 3;
			if (m->IsSolid(x - ux + vx, y - uy + vy, z - uz + vz))
				v |= 1 << 4;
			if (m->IsSolid(x - ux - vx, y - uy - vy, z - uz - vz))
				v |= 1 << 5;
			if (m->IsSolid(x + ux + vx, y + uy + vy, z + uz + vz))
				v |= 1 << 6;
			if (m->IsSolid(x + ux - vx, y + uy - vy, z + uz - vz))
				v |= 1 << 7;
			return (uint8_t)v;
		}

		template <class C> static void FreeClear(C& cntr) {
			for (typename C::iterator it = cntr.begin(); it != cntr.end(); ++it)
				delete *it;
			cntr.clear();
		}

		class GLOptimizedVoxelModel::SliceGenerator {

		public:
			// 0 - air
			// 1 - unprocessed solid
			// 2 - "inside" the processed area, but not connected
			// 3 - found holes
			uint8_t* slice;
			int usize, vsize;
			int minU, maxU, minV, maxV;

			class Model {
				const uint8_t* slice;
				int usize, vsize;

			public:
				Model(const uint8_t* slice, int usize, int vsize)
					: slice(slice), usize(usize), vsize(vsize) {}
				inline int GetWidth() { return usize; }
				inline int GetHeight() { return vsize; }
				inline bool operator()(int x, int y) const {
					if (x < 0 || y < 0 || x >= usize || y >= vsize)
						return false;
					return slice[x * vsize + y] != 0;
				}
			};

			std::unique_ptr<c2t::Trianglulator<Model>> triangulator;

			uint8_t& Slice(int u, int v) {
				SPAssert(u >= 0 || u < usize);
				SPAssert(v >= 0 || v < vsize);
				return slice[u * vsize + v];
			}
			uint8_t GetSlice(int u, int v) {
				if (u < 0 || v < 0 || u >= usize || v >= vsize)
					return 0;
				return Slice(u, v);
			}

			std::vector<c2t::Point> ProcessArea() {
				if (triangulator == nullptr)
					triangulator = decltype(triangulator){
					  new c2t::Trianglulator<Model>(Model(slice, usize, vsize))};

				return triangulator->Triangulate();
			}
		};

		static IntVector3 ExactPoint(c2t::Point p) { return MakeIntVector3(p.x, p.y, 0); }

		static int64_t DoubledTriangleArea(IntVector3 v1, IntVector3 v2, IntVector3 v3) {
			int64_t x1 = v1.x, y1 = v1.y;
			int64_t x2 = v2.x, y2 = v2.y;
			int64_t x3 = v3.x, y3 = v3.y;
			return (x1 - x3) * (y2 - y1) - (x1 - x2) * (y3 - y1);
		}

		void GLOptimizedVoxelModel::EmitSlice(uint8_t* slice, int usize, int vsize, int sx, int sy,
											  int sz, int ux, int uy, int uz, int vx, int vy,
											  int vz, int mx, int my, int mz, bool flip,
											  VoxelModel* model) {
			SPADES_MARK_FUNCTION();
			int minU = -1, minV = -1, maxU = -1, maxV = -1;

			for (int u = 0; u < usize; u++) {
				for (int v = 0; v < vsize; v++) {
					if (slice[u * vsize + v]) {
						if (minU == -1 || u < minU)
							minU = u;
						if (maxU == -1 || u > maxU)
							maxU = u;
						if (minV == -1 || v < minV)
							minV = v;
						if (maxV == -1 || v > maxV)
							maxV = v;
					}
				}
			}

			if (minU == -1)
				return; // no face

			int nx, ny, nz;
			nx = uy * vz - uz * vy;
			ny = uz * vx - ux * vz;
			nz = ux * vy - uy * vx;

			if (!flip) {
				nx = -nx;
				ny = -ny;
				nz = -nz;
			}

			SliceGenerator generator;
			generator.slice = slice;
			generator.usize = usize;
			generator.vsize = vsize;
			generator.minU = minU;
			generator.maxU = maxU;
			generator.minV = minV;
			generator.maxV = maxV;

			int tu = minU - 1, tv = minV - 1;
			int bw = (maxU - minU) + 3, bh = (maxV - minV) + 3;
			int bId = static_cast<int>(bmps.size());
			Bitmap* bmp = new Bitmap(bw, bh);
			bmps.push_back(bmp);
			{
				uint32_t* pixels = bmp->GetPixels();
				IntVector3 p1 = {mx, my, mz};

				IntVector3 uu = {ux, uy, uz};
				IntVector3 vv = {vx, vy, vz};
				IntVector3 nn = {nx, ny, nz};

				p1 += uu * tu;
				p1 += vv * tv;

				for (int y = 0; y < bh; y++) {
					IntVector3 p2 = p1;
					for (int x = 0; x < bw; x++) {
						IntVector3 p3 = p2;
						int u = x + tu, v = y + tv;
						if (u < 0 || v < 0 || u >= usize || v >= vsize || !slice[u * vsize + v]) {
							if ((v >= 0 && v < vsize) && u > 0 && slice[(u - 1) * vsize + (v)]) {
								u--;
								p3 -= uu;
							} else if ((v >= 0 && v < vsize) && u < usize - 1 && slice[(u + 1) * vsize + (v)]) {
								u++;
								p3 += uu;
							} else if ((u >= 0 && u < usize) && v > 0 && slice[(u)*vsize + (v - 1)]) {
								v--;
								p3 -= vv;
							} else if ((u >= 0 && u < usize) && v < vsize - 1 && slice[(u)*vsize + (v + 1)]) {
								v++;
								p3 += vv;
							} else if (u > 0 && v > 0 && slice[(u - 1) * vsize + (v - 1)]) {
								u--;
								v--;
								p3 -= uu;
								p3 -= vv;
							} else if (u > 0 && v < vsize - 1 && slice[(u - 1) * vsize + (v + 1)]) {
								u--;
								v++;
								p3 -= uu;
								p3 += vv;
							} else if (u < usize - 1 && v > 0 && slice[(u + 1) * vsize + (v - 1)]) {
								u++;
								v--;
								p3 += uu;
								p3 -= vv;
							} else if (u < usize - 1 && v < vsize - 1 && slice[(u + 1) * vsize + (v + 1)]) {
								u++;
								v++;
								p3 += uu;
								p3 += vv;
							} else {
								// No neighbouring voxel to take a color from. Extend the
								// previous texel (or black) instead of stamping a magenta
								// sentinel, so this unfilled padding doesn't bleed into the
								// face edge under texture filtering.
								*pixels = (x > 0) ? pixels[-1] : 0;
								pixels++;
								p2 += uu;
								continue;
							}
						}

						SPAssert(model->IsSolid(p3.x, p3.y, p3.z));
						uint32_t col = model->GetColor(p3.x, p3.y, p3.z);
						auto material = static_cast<MaterialType>(col >> 24);

						col &= 0xFFFFFF;

						uint8_t r = (col >> 16) & 0xFF;
						uint8_t g = (col >> 8) & 0xFF;
						uint8_t b = col & 0xFF;

						int threshold = 250;

						// Add AOID (selector for the pre-integrated ambient occlusion texture).
						// Use special values for certain materials.
						if (material == MaterialType::Emissive &&
							((r >= threshold || g >= threshold || b >= threshold)
								|| (r == 0 && g == 0 && b == 0))) {
							col |= ((uint32_t)255) << 24;
						} else {
							p3 += nn;
							SPAssert(!model->IsSolid(p3.x, p3.y, p3.z));

							uint8_t aoId = calcAOID(model, p3.x, p3.y, p3.z, ux, uy, uz, vx, vy, vz);

							// These AOIDs are allocated for non-default materials.
							if (aoId % 16 == 15)
								aoId = 15;

							col |= ((uint8_t)aoId) << 24;
						}

						*(pixels++) = col;
						p2 += uu;
					}

					p1 += vv;
				}
			}

			// TODO: optimize scan range
			auto polys = generator.ProcessArea();
			for (std::size_t i = 0; i < polys.size(); i += 3) {
				uint32_t idx = (uint32_t)vertices.size();
				auto p1 = ExactPoint(polys[i]);
				auto p2 = ExactPoint(polys[i + 1]);
				auto p3 = ExactPoint(polys[i + 2]);

				// degenerate triangle
				if (DoubledTriangleArea(p1, p2, p3) == 0)
					continue;

				Vertex vtx;
				vtx.nx = nx;
				vtx.ny = ny;
				vtx.nz = nz;

				vtx.x = sx + p1.x * ux + p1.y * vx;
				vtx.y = sy + p1.x * uy + p1.y * vy;
				vtx.z = sz + p1.x * uz + p1.y * vz;
				vtx.u = p1.x - tu;
				vtx.v = p1.y - tv;
				vertices.push_back(vtx);

				vtx.x = sx + p2.x * ux + p2.y * vx;
				vtx.y = sy + p2.x * uy + p2.y * vy;
				vtx.z = sz + p2.x * uz + p2.y * vz;
				vtx.u = p2.x - tu;
				vtx.v = p2.y - tv;
				vertices.push_back(vtx);

				vtx.x = sx + p3.x * ux + p3.y * vx;
				vtx.y = sy + p3.x * uy + p3.y * vy;
				vtx.z = sz + p3.x * uz + p3.y * vz;
				vtx.u = p3.x - tu;
				vtx.v = p3.y - tv;
				vertices.push_back(vtx);

				indices.push_back(flip ? idx : idx + 2);
				indices.push_back(idx + 1);
				indices.push_back(flip ? idx + 2 : idx);

				bmpIndex.push_back(bId);
				bmpIndex.push_back(bId);
				bmpIndex.push_back(bId);
			}
		}

		void GLOptimizedVoxelModel::BuildVertices(spades::VoxelModel* model) {
			SPADES_MARK_FUNCTION();

			SPAssert(vertices.empty());
			SPAssert(indices.empty());

			int w = model->GetWidth();
			int h = model->GetHeight();
			int d = model->GetDepth();

			std::vector<uint8_t> slice;

			// x-slice
			slice.resize(h * d);
			std::fill(slice.begin(), slice.end(), 0);
			for (int x = 0; x < w; x++) {
				for (int y = 0; y < h; y++) {
					for (int z = 0; z < d; z++) {
						uint8_t& s = slice[y * d + z];
						if (x == 0)
							s = model->IsSolid(x, y, z) ? 1 : 0;
						else
							s = (model->IsSolid(x, y, z) && !model->IsSolid(x - 1, y, z)) ? 1 : 0;
					}
				}
				EmitSlice(slice.data(), h, d, x, 0, 0, 0, 1, 0, 0, 0, 1, x, 0, 0, false, model);

				for (int y = 0; y < h; y++) {
					for (int z = 0; z < d; z++) {
						uint8_t& s = slice[y * d + z];
						if (x == w - 1)
							s = model->IsSolid(x, y, z) ? 1 : 0;
						else
							s = (model->IsSolid(x, y, z) && !model->IsSolid(x + 1, y, z)) ? 1 : 0;
					}
				}
				EmitSlice(slice.data(), h, d, x + 1, 0, 0, 0, 1, 0, 0, 0, 1, x, 0, 0, true, model);
			}

			// y-slice
			slice.resize(w * d);
			std::fill(slice.begin(), slice.end(), 0);
			for (int y = 0; y < h; y++) {
				for (int x = 0; x < w; x++) {
					for (int z = 0; z < d; z++) {
						uint8_t& s = slice[x * d + z];
						if (y == 0)
							s = model->IsSolid(x, y, z) ? 1 : 0;
						else
							s = (model->IsSolid(x, y, z) && !model->IsSolid(x, y - 1, z)) ? 1 : 0;
					}
				}
				EmitSlice(slice.data(), w, d, 0, y, 0, 1, 0, 0, 0, 0, 1, 0, y, 0, true, model);

				for (int x = 0; x < w; x++) {
					for (int z = 0; z < d; z++) {
						uint8_t& s = slice[x * d + z];
						if (y == h - 1)
							s = model->IsSolid(x, y, z) ? 1 : 0;
						else
							s = (model->IsSolid(x, y, z) && !model->IsSolid(x, y + 1, z)) ? 1 : 0;
					}
				}
				EmitSlice(slice.data(), w, d, 0, y + 1, 0, 1, 0, 0, 0, 0, 1, 0, y, 0, false, model);
			}

			// z-slice
			slice.resize(w * h);
			std::fill(slice.begin(), slice.end(), 0);
			for (int z = 0; z < d; z++) {
				for (int x = 0; x < w; x++) {
					for (int y = 0; y < h; y++) {
						uint8_t& s = slice[x * h + y];
						if (z == 0)
							s = model->IsSolid(x, y, z) ? 1 : 0;
						else
							s = (model->IsSolid(x, y, z) && !model->IsSolid(x, y, z - 1)) ? 1 : 0;
					}
				}
				EmitSlice(slice.data(), w, h, 0, 0, z, 1, 0, 0, 0, 1, 0, 0, 0, z, false, model);

				for (int x = 0; x < w; x++) {
					for (int y = 0; y < h; y++) {
						uint8_t& s = slice[x * h + y];
						if (z == d - 1)
							s = model->IsSolid(x, y, z) ? 1 : 0;
						else
							s = (model->IsSolid(x, y, z) && !model->IsSolid(x, y, z + 1)) ? 1 : 0;
					}
				}
				EmitSlice(slice.data(), w, h, 0, 0, z + 1, 1, 0, 0, 0, 1, 0, 0, 0, z, true, model);
			}
		}

		void GLOptimizedVoxelModel::Prerender(
			std::vector<client::ModelRenderParam> params, bool ghostPass) {
			SPADES_MARK_FUNCTION();

			RenderSunlightPass(params, ghostPass);
		}

		void GLOptimizedVoxelModel::RenderShadowMapPass(
			std::vector<client::ModelRenderParam> params) {
			SPADES_MARK_FUNCTION();

			device.Enable(IGLDevice::CullFace, true);
			device.Enable(IGLDevice::DepthTest, true);

			shadowMapProgram->Use();

			static GLShadowMapShader shadowMapShader;
			shadowMapShader(&renderer, shadowMapProgram, 0);

			static GLProgramUniform modelOrigin("modelOrigin");
			modelOrigin(shadowMapProgram);
			modelOrigin.SetValue(origin.x, origin.y, origin.z);

			// setup attributes
			static GLProgramAttribute positionAttribute("positionAttribute");
			static GLProgramAttribute normalAttribute("normalAttribute");
			positionAttribute(shadowMapProgram);
			normalAttribute(shadowMapProgram);

			device.BindBuffer(IGLDevice::ArrayBuffer, buffer);
			device.VertexAttribPointer(positionAttribute(), 4,
				IGLDevice::UnsignedByte, false, sizeof(Vertex), (void*)0);
			if (normalAttribute() != -1)
				device.VertexAttribPointer(normalAttribute(), 3,
					IGLDevice::Byte, false, sizeof(Vertex), (void*)8);
			device.BindBuffer(IGLDevice::ArrayBuffer, 0);

			device.EnableVertexAttribArray(positionAttribute(), true);
			if (normalAttribute() != -1)
				device.EnableVertexAttribArray(normalAttribute(), true);

			device.BindBuffer(IGLDevice::ElementArrayBuffer, idxBuffer);

			for (const auto& param : params) {
				if (param.depthHack || !param.castShadow || param.ghost)
					continue;

				const auto& modelMatrix = param.matrix;
				const auto& axisX = modelMatrix.GetAxis(0);
				const auto& axisY = modelMatrix.GetAxis(1);
				const auto& axisZ = modelMatrix.GetAxis(2);
				const auto& modelOrigin = modelMatrix.GetOrigin();

				// frustrum cull
				float rad = radius * axisX.GetLength();
				if (!renderer.GetShadowMapRenderer()->SphereCull(modelOrigin, rad))
					continue;

				static GLProgramUniform modelMatrixU("modelMatrix");
				modelMatrixU(shadowMapProgram);
				modelMatrixU.SetValue(modelMatrix);

				bool isMirrored = Vector3::Dot(Vector3::Cross(axisX, axisY), axisZ) < 0.0F;
				if (isMirrored)
					device.FrontFace(IGLDevice::CCW);

				device.DrawElements(IGLDevice::Triangles,
					numIndices, IGLDevice::UnsignedInt, (void*)0);

				if (isMirrored)
					device.FrontFace(IGLDevice::CW);
			}

			device.BindBuffer(IGLDevice::ElementArrayBuffer, 0);

			device.EnableVertexAttribArray(positionAttribute(), false);
			if (normalAttribute() != -1)
				device.EnableVertexAttribArray(normalAttribute(), false);

			device.ActiveTexture(0);
			device.BindTexture(IGLDevice::Texture2D, 0);
		}

		void GLOptimizedVoxelModel::RenderSunlightPass(
			std::vector<client::ModelRenderParam> params, bool ghostPass) {
			SPADES_MARK_FUNCTION();

			bool mirror = renderer.IsRenderingMirror();

			const auto& viewOrigin = renderer.GetSceneDef().viewOrigin;
			const auto& viewMatrix = renderer.GetViewMatrix();
			const auto& pvMat = renderer.GetProjectionViewMatrix();

			device.ActiveTexture(0);
			aoImage->Bind(IGLDevice::Texture2D);
			device.TexParamater(IGLDevice::Texture2D,
				IGLDevice::TextureMinFilter, IGLDevice::Linear);

			device.ActiveTexture(1);
			image->Bind(IGLDevice::Texture2D);
			device.TexParamater(IGLDevice::Texture2D,
				IGLDevice::TextureMinFilter, IGLDevice::Nearest);
			device.TexParamater(IGLDevice::Texture2D,
				IGLDevice::TextureMagFilter, IGLDevice::Nearest);

			device.Enable(IGLDevice::CullFace, true);
			device.Enable(IGLDevice::DepthTest, true);

			program->Use();

			static GLShadowShader shadowShader;
			shadowShader(&renderer, program, 2);

			static GLProgramUniform fogDistance("fogDistance");
			fogDistance(program);
			fogDistance.SetValue(renderer.GetFogDistance());

			static GLProgramUniform fogColor("fogColor");
			fogColor(program);
			Vector3 fogCol = renderer.GetFogColorForSolidPass();
			fogCol *= fogCol; // linearize
			fogColor.SetValue(fogCol.x, fogCol.y, fogCol.z);

			static GLProgramUniform aoUniform("ambientOcclusionTexture");
			aoUniform(program);
			aoUniform.SetValue(0);

			static GLProgramUniform modelOrigin("modelOrigin");
			modelOrigin(program);
			modelOrigin.SetValue(origin.x, origin.y, origin.z);

			static GLProgramUniform texScale("texScale");
			texScale(program);
			texScale.SetValue(1.0F / image->GetWidth(), 1.0F / image->GetHeight());

			static GLProgramUniform modelTexture("modelTexture");
			modelTexture(program);
			modelTexture.SetValue(1);

			static GLProgramUniform viewMatrixU("viewMatrix");
			viewMatrixU(program);
			viewMatrixU.SetValue(viewMatrix);

			static GLProgramUniform viewSpaceLight("viewSpaceLight");
			viewSpaceLight(program);
			Vector3 vspLight = (viewMatrix * MakeVector4(0, -1, -1, 0)).GetXYZ();
			viewSpaceLight.SetValue(vspLight.x, vspLight.y, vspLight.z);

			static GLProgramUniform sunLightDirection("sunLightDirection");
			sunLightDirection(program);
			Vector3 sunPos = MakeVector3(0, -1, -1);
			sunPos = sunPos.Normalize();
			sunLightDirection.SetValue(sunPos.x, sunPos.y, sunPos.z);

			static GLProgramUniform viewOriginVector("viewOriginVector");
			viewOriginVector(program);
			viewOriginVector.SetValue(viewOrigin.x, viewOrigin.y, viewOrigin.z);

			// setup attributes
			static GLProgramAttribute positionAttribute("positionAttribute");
			static GLProgramAttribute textureCoordAttribute("textureCoordAttribute");
			static GLProgramAttribute normalAttribute("normalAttribute");
			positionAttribute(program);
			textureCoordAttribute(program);
			normalAttribute(program);

			device.BindBuffer(IGLDevice::ArrayBuffer, buffer);
			device.VertexAttribPointer(positionAttribute(), 4,
				IGLDevice::UnsignedByte, false, sizeof(Vertex), (void*)0);
			device.VertexAttribPointer(textureCoordAttribute(), 2,
				IGLDevice::UnsignedShort, false, sizeof(Vertex), (void*)4);
			device.VertexAttribPointer(normalAttribute(), 3,
				IGLDevice::Byte, false, sizeof(Vertex), (void*)8);
			device.BindBuffer(IGLDevice::ArrayBuffer, 0);

			device.EnableVertexAttribArray(positionAttribute(), true);
			device.EnableVertexAttribArray(textureCoordAttribute(), true);
			device.EnableVertexAttribArray(normalAttribute(), true);

			device.BindBuffer(IGLDevice::ElementArrayBuffer, idxBuffer);

			for (const auto& param : params) {
				if (mirror && param.depthHack)
					continue;
				if (param.ghost != ghostPass)
					continue;

				const auto& modelMatrix = param.matrix;
				const auto& axisX = modelMatrix.GetAxis(0);
				const auto& axisY = modelMatrix.GetAxis(1);
				const auto& axisZ = modelMatrix.GetAxis(2);
				const auto& modelOrigin = modelMatrix.GetOrigin();

				// frustrum cull
				float rad = radius * axisX.GetLength();
				if (!renderer.SphereFrustrumCull(modelOrigin, rad))
					continue;

				static GLProgramUniform customColor("customColor");
				customColor(program);
				customColor.SetValue(param.customColor.x, param.customColor.y, param.customColor.z);

				static GLProgramUniform projectionViewModelMatrix("projectionViewModelMatrix");
				projectionViewModelMatrix(program);
				projectionViewModelMatrix.SetValue(pvMat * modelMatrix);

				static GLProgramUniform viewModelMatrix("viewModelMatrix");
				viewModelMatrix(program);
				viewModelMatrix.SetValue(viewMatrix * modelMatrix);

				static GLProgramUniform modelMatrixU("modelMatrix");
				modelMatrixU(program);
				modelMatrixU.SetValue(modelMatrix);

				static GLProgramUniform modelOpacity("modelOpacity");
				modelOpacity(program);
				modelOpacity.SetValue(param.opacity);

				bool isMirrored = Vector3::Dot(Vector3::Cross(axisX, axisY), axisZ) < 0.0F;
				if (isMirrored)
					device.FrontFace(IGLDevice::CCW);

				if (param.depthHack)
					device.DepthRange(0.0F, 0.1F);

				device.DrawElements(IGLDevice::Triangles,
					numIndices, IGLDevice::UnsignedInt, (void*)0);

				if (isMirrored)
					device.FrontFace(IGLDevice::CW);

				if (param.depthHack)
					device.DepthRange(0.0F, 1.0F);
			}

			device.BindBuffer(IGLDevice::ElementArrayBuffer, 0);

			device.EnableVertexAttribArray(positionAttribute(), false);
			device.EnableVertexAttribArray(textureCoordAttribute(), false);
			device.EnableVertexAttribArray(normalAttribute(), false);

			device.ActiveTexture(1);
			device.BindTexture(IGLDevice::Texture2D, 0);
			device.ActiveTexture(0);
			device.BindTexture(IGLDevice::Texture2D, 0);
		}

		void GLOptimizedVoxelModel::RenderDynamicLightPass(
			std::vector<client::ModelRenderParam> params, std::vector<GLDynamicLight> lights) {
			SPADES_MARK_FUNCTION();

			bool mirror = renderer.IsRenderingMirror();

			const auto& viewOrigin = renderer.GetSceneDef().viewOrigin;
			const auto& viewMatrix = renderer.GetViewMatrix();
			const auto& pvMat = renderer.GetProjectionViewMatrix();

			device.ActiveTexture(0);
			aoImage->Bind(IGLDevice::Texture2D);
			device.TexParamater(IGLDevice::Texture2D,
				IGLDevice::TextureMinFilter,IGLDevice::Linear);

			device.ActiveTexture(1);
			image->Bind(IGLDevice::Texture2D);
			device.TexParamater(IGLDevice::Texture2D,
				IGLDevice::TextureMinFilter, IGLDevice::Nearest);
			device.TexParamater(IGLDevice::Texture2D,
				IGLDevice::TextureMagFilter, IGLDevice::Nearest);

			device.Enable(IGLDevice::CullFace, true);
			device.Enable(IGLDevice::DepthTest, true);

			dlightProgram->Use();

			static GLDynamicLightShader dlightShader;

			static GLProgramUniform fogDistance("fogDistance");
			fogDistance(dlightProgram);
			fogDistance.SetValue(renderer.GetFogDistance());

			static GLProgramUniform modelOrigin("modelOrigin");
			modelOrigin(dlightProgram);
			modelOrigin.SetValue(origin.x, origin.y, origin.z);

			static GLProgramUniform texScale("texScale");
			texScale(dlightProgram);
			texScale.SetValue(1.0F / image->GetWidth(), 1.0F / image->GetHeight());

			static GLProgramUniform modelTexture("modelTexture");
			modelTexture(dlightProgram);
			modelTexture.SetValue(1);

			static GLProgramUniform viewOriginVector("viewOriginVector");
			viewOriginVector(dlightProgram);
			viewOriginVector.SetValue(viewOrigin.x, viewOrigin.y, viewOrigin.z);

			// setup attributes
			static GLProgramAttribute positionAttribute("positionAttribute");
			static GLProgramAttribute textureCoordAttribute("textureCoordAttribute");
			static GLProgramAttribute normalAttribute("normalAttribute");
			positionAttribute(dlightProgram);
			textureCoordAttribute(dlightProgram);
			normalAttribute(dlightProgram);

			device.BindBuffer(IGLDevice::ArrayBuffer, buffer);
			device.VertexAttribPointer(positionAttribute(), 4,
				IGLDevice::UnsignedByte, false, sizeof(Vertex), (void*)0);
			device.VertexAttribPointer(textureCoordAttribute(), 2,
				IGLDevice::UnsignedShort, false, sizeof(Vertex), (void*)4);
			device.VertexAttribPointer(normalAttribute(), 3,
				IGLDevice::Byte, false, sizeof(Vertex), (void*)8);
			device.BindBuffer(IGLDevice::ArrayBuffer, 0);

			device.EnableVertexAttribArray(positionAttribute(), true);
			device.EnableVertexAttribArray(textureCoordAttribute(), true);
			device.EnableVertexAttribArray(normalAttribute(), true);

			device.BindBuffer(IGLDevice::ElementArrayBuffer, idxBuffer);

			for (const auto& param : params) {
				if (mirror && param.depthHack)
					continue;
				if (param.ghost)
					continue;

				const auto& modelMatrix = param.matrix;
				const auto& axisX = modelMatrix.GetAxis(0);
				const auto& axisY = modelMatrix.GetAxis(1);
				const auto& axisZ = modelMatrix.GetAxis(2);
				const auto& modelOrigin = modelMatrix.GetOrigin();

				// frustrum cull
				float rad = radius * axisX.GetLength();
				if (!renderer.SphereFrustrumCull(modelOrigin, rad))
					continue;

				static GLProgramUniform customColor("customColor");
				customColor(dlightProgram);
				customColor.SetValue(param.customColor.x, param.customColor.y, param.customColor.z);

				static GLProgramUniform projectionViewModelMatrix("projectionViewModelMatrix");
				projectionViewModelMatrix(dlightProgram);
				projectionViewModelMatrix.SetValue(pvMat * modelMatrix);

				static GLProgramUniform viewModelMatrix("viewModelMatrix");
				viewModelMatrix(dlightProgram);
				viewModelMatrix.SetValue(viewMatrix * modelMatrix);

				static GLProgramUniform modelMatrixU("modelMatrix");
				modelMatrixU(dlightProgram);
				modelMatrixU.SetValue(modelMatrix);

				bool isMirrored = Vector3::Dot(Vector3::Cross(axisX, axisY), axisZ) < 0.0F;
				if (isMirrored)
					device.FrontFace(IGLDevice::CCW);

				if (param.depthHack)
					device.DepthRange(0.0F, 0.1F);

				for (const auto& light : lights) {
					if (!light.SphereCull(modelOrigin, rad))
						continue;

					dlightShader(&renderer, dlightProgram, light, 2);
					device.DrawElements(IGLDevice::Triangles,
						numIndices, IGLDevice::UnsignedInt, (void*)0);
				}

				if (isMirrored)
					device.FrontFace(IGLDevice::CW);

				if (param.depthHack)
					device.DepthRange(0.0F, 1.0F);
			}

			device.BindBuffer(IGLDevice::ElementArrayBuffer, 0);

			device.EnableVertexAttribArray(positionAttribute(), false);
			device.EnableVertexAttribArray(textureCoordAttribute(), false);
			device.EnableVertexAttribArray(normalAttribute(), false);

			device.ActiveTexture(0);
		}

		void GLOptimizedVoxelModel::RenderOutlinePass(
			std::vector<client::ModelRenderParam> params) {
			SPADES_MARK_FUNCTION();

			bool mirror = renderer.IsRenderingMirror();

			const auto& viewOrigin = renderer.GetSceneDef().viewOrigin;
			const auto& pvMat = renderer.GetProjectionViewMatrix();

			device.ActiveTexture(0);
			aoImage->Bind(IGLDevice::Texture2D);
			device.TexParamater(IGLDevice::Texture2D,
				IGLDevice::TextureMinFilter, IGLDevice::Linear);

			device.ActiveTexture(1);
			image->Bind(IGLDevice::Texture2D);
			device.TexParamater(IGLDevice::Texture2D,
				IGLDevice::TextureMinFilter, IGLDevice::Nearest);
			device.TexParamater(IGLDevice::Texture2D,
				IGLDevice::TextureMagFilter, IGLDevice::Nearest);

			device.Enable(IGLDevice::CullFace, true);
			device.Enable(IGLDevice::DepthTest, true);

			outlinesProgram->Use();

			static GLProgramUniform fogColor("fogColor");
			fogColor(outlinesProgram);
			Vector3 fogCol = renderer.GetFogColorForSolidPass();
			fogCol *= fogCol; // linearize
			fogColor.SetValue(fogCol.x, fogCol.y, fogCol.z);

			static GLProgramUniform fogDistance("fogDistance");
			fogDistance(outlinesProgram);
			fogDistance.SetValue(renderer.GetFogDistance());

			static GLProgramUniform modelOrigin("modelOrigin");
			modelOrigin(outlinesProgram);
			modelOrigin.SetValue(origin.x, origin.y, origin.z);

			static GLProgramUniform viewOriginVector("viewOriginVector");
			viewOriginVector(outlinesProgram);
			viewOriginVector.SetValue(viewOrigin.x, viewOrigin.y, viewOrigin.z);

			// setup attributes
			static GLProgramAttribute positionAttribute("positionAttribute");
			positionAttribute(outlinesProgram);

			device.BindBuffer(IGLDevice::ArrayBuffer, buffer);
			device.VertexAttribPointer(positionAttribute(), 4,
				IGLDevice::UnsignedByte, false, sizeof(Vertex), (void*)0);
			device.BindBuffer(IGLDevice::ArrayBuffer, 0);

			device.EnableVertexAttribArray(positionAttribute(), true);

			device.BindBuffer(IGLDevice::ElementArrayBuffer, idxBuffer);

			for (const auto& param : params) {
				if (mirror && param.depthHack)
					continue;
				if (param.ghost)
					continue;

				const auto& modelMatrix = param.matrix;
				const auto& axisX = modelMatrix.GetAxis(0);
				const auto& axisY = modelMatrix.GetAxis(1);
				const auto& axisZ = modelMatrix.GetAxis(2);
				const auto& modelOrigin = modelMatrix.GetOrigin();

				// frustrum cull
				float rad = radius * axisX.GetLength();
				if (!renderer.SphereFrustrumCull(modelOrigin, rad))
					continue;

				static GLProgramUniform projectionViewModelMatrix("projectionViewModelMatrix");
				projectionViewModelMatrix(outlinesProgram);
				projectionViewModelMatrix.SetValue(pvMat * modelMatrix);

				static GLProgramUniform modelMatrixU("modelMatrix");
				modelMatrixU(outlinesProgram);
				modelMatrixU.SetValue(modelMatrix);

				bool isMirrored = Vector3::Dot(Vector3::Cross(axisX, axisY), axisZ) < 0.0F;
				if (isMirrored)
					device.FrontFace(IGLDevice::CCW);

				if (param.depthHack)
					device.DepthRange(0.0F, 0.1F);

				device.DrawElements(IGLDevice::Triangles,
					numIndices, IGLDevice::UnsignedInt, (void*)0);

				if (isMirrored)
					device.FrontFace(IGLDevice::CW);

				if (param.depthHack)
					device.DepthRange(0.0F, 1.0F);
			}

			device.BindBuffer(IGLDevice::ElementArrayBuffer, 0);

			device.EnableVertexAttribArray(positionAttribute(), false);
		}
	} // namespace draw
} // namespace spades