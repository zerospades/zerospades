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

#include <atomic>
#include <cstdlib>

#include "GLMapShadowRenderer.h"
#include "GLRadiosityRenderer.h"
#include "GLRenderer.h"
#include <Client/GameMap.h>

#include <Core/ConcurrentDispatch.h>
#include <Core/Settings.h>
#if defined(__APPLE__)
#if defined(__x86_64__)
#include <xmmintrin.h>
#else
#include <arm_neon.h>
#endif
#endif

#include "GLProfiler.h"

namespace spades {
	namespace draw {
		class GLRadiosityRenderer::UpdateDispatch : public ConcurrentDispatch {
			GLRadiosityRenderer& renderer;
			// Where the eye was when it started: the scene changes as the worker runs.
			const Vector3 eye;

		public:
			std::atomic<bool> done{false};
			UpdateDispatch(GLRadiosityRenderer& r, const Vector3& eye) : renderer(r), eye(eye) {}
			void Run() override {
				SPADES_MARK_FUNCTION();

				renderer.UpdateDirtyChunks(eye);

				done = true;
			}
		};

		GLRadiosityRenderer::GLRadiosityRenderer(GLRenderer& r, client::GameMap* m)
		    : renderer(r), device(r.GetGLDevice()), settings(r.GetSettings()), map(m) {
			SPADES_MARK_FUNCTION();

			w = map->Width();
			h = map->Height();
			d = map->Depth();

			chunkW = w / ChunkSize;
			chunkH = h / ChunkSize;
			chunkD = d / ChunkSize;

			chunks = std::vector<Chunk>{static_cast<std::size_t>(chunkW * chunkH * chunkD)};

			for (const auto& c : chunks) {
				uint32_t* data;

				data = (uint32_t*)c.dataFlat;
				std::fill(data, data + ChunkSize * ChunkSize * ChunkSize, 0x20080200);

				data = (uint32_t*)c.dataX;
				std::fill(data, data + ChunkSize * ChunkSize * ChunkSize, 0x20080200);

				data = (uint32_t*)c.dataY;
				std::fill(data, data + ChunkSize * ChunkSize * ChunkSize, 0x20080200);

				data = (uint32_t*)c.dataZ;
				std::fill(data, data + ChunkSize * ChunkSize * ChunkSize, 0x20080200);
			}

			for (int x = 0; x < chunkW; x++)
			for (int y = 0; y < chunkH; y++)
			for (int z = 0; z < chunkD; z++) {
				Chunk& c = GetChunk(x, y, z);
				c.cx = x;
				c.cy = y;
				c.cz = z;
			}

			SPLog("Chunk buffer allocated (%d bytes)",
			      (int)sizeof(Chunk) * chunkW * chunkH * chunkD);

			// make texture
			textureFlat = device.GenTexture();
			textureX = device.GenTexture();
			textureY = device.GenTexture();
			textureZ = device.GenTexture();

			IGLDevice::UInteger texs[] = {textureFlat, textureX, textureY, textureZ};

			for (int i = 0; i < 4; i++) {
				device.BindTexture(IGLDevice::Texture3D, texs[i]);
				device.TexParamater(IGLDevice::Texture3D, IGLDevice::TextureMagFilter,
				                    IGLDevice::Linear);
				device.TexParamater(IGLDevice::Texture3D, IGLDevice::TextureMinFilter,
				                    IGLDevice::Linear);
				device.TexParamater(IGLDevice::Texture3D, IGLDevice::TextureWrapS,
				                    IGLDevice::Repeat);
				device.TexParamater(IGLDevice::Texture3D, IGLDevice::TextureWrapT,
				                    IGLDevice::Repeat);
				device.TexParamater(IGLDevice::Texture3D, IGLDevice::TextureWrapR,
				                    IGLDevice::ClampToEdge);
				device.TexImage3D(
				  IGLDevice::Texture3D, 0,
				  ((int)settings.r_radiosity >= 2) ? IGLDevice::RGB10A2 : IGLDevice::RGB5A1, w, h,
				  d, 0, IGLDevice::BGRA, IGLDevice::UnsignedInt2101010Rev, NULL);
			}

			SPLog("Chunk texture allocated");

			std::vector<uint32_t> v;
			v.resize(w * h);
			std::fill(v.begin(), v.end(), 0x20080200 /*0x4210 */);

			for (int j = 0; j < 4; j++) {
				device.BindTexture(IGLDevice::Texture3D, texs[j]);
				for (int i = 0; i < d; i++) {
					device.TexSubImage3D(IGLDevice::Texture3D, 0, 0, 0, i, w, h, 1, IGLDevice::BGRA,
					                     IGLDevice::UnsignedInt2101010Rev, v.data());
				}
			}
			dispatch = NULL;

			SPLog("Chunk texture initialized");
		}

		GLRadiosityRenderer::~GLRadiosityRenderer() {
			SPADES_MARK_FUNCTION();
			if (dispatch) {
				dispatch->Join();
				delete dispatch;
			}
			SPLog("Releasing textures");

			device.DeleteTexture(textureFlat);
			device.DeleteTexture(textureX);
			device.DeleteTexture(textureY);
			device.DeleteTexture(textureZ);
		}

		GLRadiosityRenderer::Result GLRadiosityRenderer::Evaluate(IntVector3 ipos) {
			SPADES_MARK_FUNCTION_DEBUG();

			GLRadiosityRenderer::Result result;
			result.base = MakeVector3(0, 0, 0);
			result.x = MakeVector3(0, 0, 0);
			result.y = MakeVector3(0, 0, 0);
			result.z = MakeVector3(0, 0, 0);

			Vector3 pos = MakeVector3(ipos) + 0.5F;

			GLMapShadowRenderer* shadowmap = renderer.mapShadowRenderer;
			uint32_t* bitmap = shadowmap->bitmap.data();
			int centerX = ipos.x;
			int centerY = ipos.y - ipos.z;
			const int yMask = h - 1;
			const int pitch = w;

			for (int x = -Envelope; x <= Envelope; x++) {
				uint32_t* column = bitmap + ((centerX + x) & (w - 1));
				for (int y = -Envelope; y <= Envelope; y++) {
					uint32_t pixel = column[pitch * ((centerY + y) & yMask)];
					int depth = pixel >> 24;

					// shadowmap pixel's world coord
					int wx = centerX + x;
					int wy = centerY + y + depth;
					int wz = depth;

					// if true, this is negative-y faced plane
					// if false, this is negative-z faced plane
					bool isSide = (pixel & 0x80) != 0;

					// direction dependent process
					Vector3 center; // center of face
					Vector3 diff;   // pos - center
					float diffDot;  // dot(diff, normal)
					if (isSide) {
						// normal cull
						if (wy <= ipos.y)
							continue;

						center.x = wx + 0.5F;
						center.y = (float)wy;
						center.z = wz - 0.5F;

						diff = pos - center;
						diffDot = -diff.y;
					} else {
						if (wz <= ipos.z)
							continue;

						center.x = wx + 0.5F;
						center.y = wy + 0.5F;
						center.z = (float)wz;

						diff = pos - center;
						diffDot = -diff.z;
					}

					SPAssert(diffDot >= 0.0F);

					float diffLen = diff.GetLength();
					float invDiffLen = 1.0F / diffLen;
					float invDiffLenSmooth = 1.0F / ((diffLen) + 0.4F);

					// fall-off because of direciton
					float intensity = diffDot * invDiffLen;

					// 1/(r^2) distance fall-off
					intensity *= invDiffLenSmooth;
					intensity *= invDiffLenSmooth;

					// normalize
					Vector3 normDiff = diff * -invDiffLen;

					// extract shadowmap color
					float red = static_cast<float>((pixel) & 0x3F);
					float green = static_cast<float>((pixel >> 8) & 0x3F);
					float blue = static_cast<float>((pixel >> 16) & 0x3F);

					Vector3 color = {red, green, blue};
					color *= intensity;

					// add to result
					result.base += color;
					result.x += color * normDiff.x;
					result.y += color * normDiff.y;
					result.z += color * normDiff.z;

					SPAssert(!std::isnan(intensity));
					SPAssert(intensity >= 0.0F);
					SPAssert(red >= 0.0F && red < 64.0F);
					SPAssert(green >= 0.0F && green < 64.0F);
					SPAssert(blue >= 0.0F && blue < 64.0F);
				}
			}

			float scale = 0.1F / 64.0F;
			result.base *= scale;
			result.x *= scale;
			result.y *= scale;
			result.z *= scale;

			return result;
		}

		void GLRadiosityRenderer::GameMapChanged(int x, int y, int z, client::GameMap* map) {
			SPADES_MARK_FUNCTION_DEBUG();
			if (map != this->map)
				return;

			Invalidate(x - Envelope, y - Envelope, z - Envelope, x + Envelope, y + Envelope,
			           z + Envelope);
		}

		void GLRadiosityRenderer::Invalidate(int minX, int minY, int minZ, int maxX, int maxY, int maxZ) {
			SPADES_MARK_FUNCTION_DEBUG();
			if (minZ < 0)
				minZ = 0;
			if (maxZ > d - 1)
				maxZ = d - 1;
			if (minX > maxX || minY > maxY || minZ > maxZ)
				return;

			// these should be floor div
			int cx1 = minX >> ChunkSizeBits;
			int cy1 = minY >> ChunkSizeBits;
			int cz1 = minZ >> ChunkSizeBits;
			int cx2 = maxX >> ChunkSizeBits;
			int cy2 = maxY >> ChunkSizeBits;
			int cz2 = maxZ >> ChunkSizeBits;

			// The worker takes these as it runs.
			std::lock_guard<std::mutex> lock(dirtyMutex);
			for (int cx = cx1; cx <= cx2; cx++)
			for (int cy = cy1; cy <= cy2; cy++)
			for (int cz = cz1; cz <= cz2; cz++) {
				Chunk& c = GetChunkWrapped(cx, cy, cz);
				int originX = cx * ChunkSize;
				int originY = cy * ChunkSize;
				int originZ = cz * ChunkSize;

				int inMinX = std::max(minX - originX, 0);
				int inMinY = std::max(minY - originY, 0);
				int inMinZ = std::max(minZ - originZ, 0);
				int inMaxX = std::min(maxX - originX, ChunkSize - 1);
				int inMaxY = std::min(maxY - originY, ChunkSize - 1);
				int inMaxZ = std::min(maxZ - originZ, ChunkSize - 1);

				if (!c.dirty) {
					c.dirtyMinX = inMinX;
					c.dirtyMinY = inMinY;
					c.dirtyMinZ = inMinZ;
					c.dirtyMaxX = inMaxX;
					c.dirtyMaxY = inMaxY;
					c.dirtyMaxZ = inMaxZ;
					c.dirty = true;
				} else {
					c.dirtyMinX = std::min(inMinX, c.dirtyMinX);
					c.dirtyMinY = std::min(inMinY, c.dirtyMinY);
					c.dirtyMinZ = std::min(inMinZ, c.dirtyMinZ);
					c.dirtyMaxX = std::max(inMaxX, c.dirtyMaxX);
					c.dirtyMaxY = std::max(inMaxY, c.dirtyMaxY);
					c.dirtyMaxZ = std::max(inMaxZ, c.dirtyMaxZ);
				}
			}
		}

		int GLRadiosityRenderer::GetNumDirtyChunks() {
			std::lock_guard<std::mutex> lock(dirtyMutex);
			int cnt = 0;
			for (const auto& c : chunks) {
				if (c.dirty)
					cnt++;
			}
			return cnt;
		}

		void GLRadiosityRenderer::Update() {
			if (GetNumDirtyChunks() > 0 && (dispatch == NULL || dispatch->done.load())) {
				if (dispatch) {
					dispatch->Join();
					delete dispatch;
				}
				dispatch = new UpdateDispatch(*this, renderer.GetSceneDef().viewOrigin);
				dispatch->Start();
			}

			int cnt = 0;
			for (const auto& c : chunks) {
				if (!c.transferDone.load())
					cnt++;
			}

			GLProfiler::Context profiler(renderer.GetGLProfiler(), "Radiosity [>= %d chunk(s)]", cnt);
			for (size_t i = 0; i < chunks.size(); i++) {
				Chunk& c = chunks[i];
				if (!c.transferDone.load()) {
					device.BindTexture(IGLDevice::Texture3D, textureFlat);
					device.TexSubImage3D(IGLDevice::Texture3D, 0, c.cx * ChunkSize,
					                     c.cy * ChunkSize, c.cz * ChunkSize, ChunkSize, ChunkSize,
					                     ChunkSize, IGLDevice::BGRA,
					                     IGLDevice::UnsignedInt2101010Rev, c.dataFlat);

					device.BindTexture(IGLDevice::Texture3D, textureX);
					device.TexSubImage3D(IGLDevice::Texture3D, 0, c.cx * ChunkSize,
					                     c.cy * ChunkSize, c.cz * ChunkSize, ChunkSize, ChunkSize,
					                     ChunkSize, IGLDevice::BGRA,
					                     IGLDevice::UnsignedInt2101010Rev, c.dataX);

					device.BindTexture(IGLDevice::Texture3D, textureY);
					device.TexSubImage3D(IGLDevice::Texture3D, 0, c.cx * ChunkSize,
					                     c.cy * ChunkSize, c.cz * ChunkSize, ChunkSize, ChunkSize,
					                     ChunkSize, IGLDevice::BGRA,
					                     IGLDevice::UnsignedInt2101010Rev, c.dataY);

					device.BindTexture(IGLDevice::Texture3D, textureZ);
					device.TexSubImage3D(IGLDevice::Texture3D, 0, c.cx * ChunkSize,
					                     c.cy * ChunkSize, c.cz * ChunkSize, ChunkSize, ChunkSize,
					                     ChunkSize, IGLDevice::BGRA,
					                     IGLDevice::UnsignedInt2101010Rev, c.dataZ);

					// Copied out: the worker may compute it again.
					c.transferDone.store(true);
				}
			}
		}

		void GLRadiosityRenderer::UpdateDirtyChunks(const Vector3& eye) {
			int dirtyChunkIds[256];
			int numDirtyChunks = 0;

			// The chunks to compute are chosen at once, while edits wait; one still
			// waiting for its last result to be uploaded is left for later.
			std::unique_lock<std::mutex> lock(dirtyMutex);
			auto isPending = [](const Chunk& c) { return c.dirty && c.transferDone.load(); };

			// first, check only chunks in near range
			int eyeX = (int)(eye.x) >> ChunkSizeBits;
			int eyeY = (int)(eye.y) >> ChunkSizeBits;
			int eyeZ = (int)(eye.z) >> ChunkSizeBits;

			for (size_t i = 0; i < chunks.size(); i++) {
				Chunk& c = chunks[i];
				int dx = (c.cx - eyeX) & (chunkW - 1);
				int dy = (c.cy - eyeY) & (chunkH - 1);
				int dz = (c.cz - eyeZ);
				if (dx >= 6 && dx <= chunkW - 6)
					continue;
				if (dy >= 6 && dy <= chunkW - 6)
					continue;
				if (dz >= 6 || dz <= -6)
					continue;
				if (isPending(c)) {
					dirtyChunkIds[numDirtyChunks++] = static_cast<int>(i);
					if (numDirtyChunks >= 256)
						break;
				}
			}

			// far chunks
			if (numDirtyChunks == 0) {
				for (size_t i = 0; i < chunks.size(); i++) {
					Chunk& c = chunks[i];
					if (isPending(c)) {
						dirtyChunkIds[numDirtyChunks++] = static_cast<int>(i);
						if (numDirtyChunks >= 256)
							break;
					}
				}
			}

			lock.unlock();

			// limit update count per frame
			for (int i = 0; i < 8; i++) {
				if (numDirtyChunks <= 0)
					break;

				int idx = SampleRandomInt(0, numDirtyChunks - 1);
				Chunk& c = chunks[dirtyChunkIds[idx]];

				// remove from list (fast)
				if (idx < numDirtyChunks - 1)
					std::swap(dirtyChunkIds[idx], dirtyChunkIds[numDirtyChunks - 1]);
				numDirtyChunks--;

				UpdateChunk(c.cx, c.cy, c.cz);
			}
		}

		float GLRadiosityRenderer::CompressDynamicRange(float v) {
			if ((int)settings.r_radiosity >= 2)
				return v;
			if (v >= 0.0F)
				return sqrtf(v);
			else
				return -sqrtf(-v);
		}

		uint32_t GLRadiosityRenderer::EncodeValue(Vector3 vec) {
			float v;
			int iv;
			unsigned int out = 0xC0000000;

			vec.x = CompressDynamicRange(vec.x);
			vec.y = CompressDynamicRange(vec.y);
			vec.z = CompressDynamicRange(vec.z);

			vec *= 0.5F;
			vec += 0.5F;
			vec *= 1022.0F / 1023.0F;

			v = vec.x * 1023.0F + 0.5F;
			if (v > 1023.2F)
				v = 1023.2F;
			if (v < 0.0F)
				v = 0.0F;
			iv = (unsigned int)v;
			if (iv > 1023)
				iv = 1023;
			if (iv < 0)
				iv = 0;
			out |= iv << 20;

			v = vec.y * 1023.0F + 0.5F;
			if (v > 1023.2F)
				v = 1023.2F;
			if (v < 0.0F)
				v = 0.0F;
			iv = (unsigned int)v;
			if (iv > 1023)
				iv = 1023;
			if (iv < 0)
				iv = 0;
			out |= iv << 10;

			v = vec.z * 1023.0F + 0.5F;
			if (v > 1023.2F)
				v = 1023.2F;
			if (v < 0.0F)
				v = 0.0F;
			iv = (unsigned int)v;
			if (iv > 1023)
				iv = 1023;
			if (iv < 0)
				iv = 0;
			out |= iv;

			return (uint32_t)out;
		}

		bool GLRadiosityRenderer::TakeDirtyRange(Chunk& c, DirtyRange& range) {
			std::lock_guard<std::mutex> lock(dirtyMutex);
			if (!c.dirty || !c.transferDone.load())
				return false;
			range = DirtyRange{c.dirtyMinX, c.dirtyMaxX, c.dirtyMinY,
			                   c.dirtyMaxY, c.dirtyMinZ, c.dirtyMaxZ};
			c.dirty = false;
			return true;
		}

		void GLRadiosityRenderer::UpdateChunk(int cx, int cy, int cz) {
			Chunk& c = GetChunk(cx, cy, cz);
			DirtyRange range;
			if (!TakeDirtyRange(c, range))
				return;

			int originX = cx * ChunkSize;
			int originY = cy * ChunkSize;
			int originZ = cz * ChunkSize;

			for (int z = range.minZ; z <= range.maxZ; z++)
			for (int y = range.minY; y <= range.maxY; y++)
			for (int x = range.minX; x <= range.maxX; x++) {
				IntVector3 pos;
				pos.x = (x + originX);
				pos.y = (y + originY);
				pos.z = (z + originZ);

				Result res = Evaluate(pos);
				c.dataFlat[z][y][x] = EncodeValue(res.base);
				c.dataX[z][y][x] = EncodeValue(res.x);
				c.dataY[z][y][x] = EncodeValue(res.y);
				c.dataZ[z][y][x] = EncodeValue(res.z);
			}

			// The render thread's to upload, now
			c.transferDone = false;
		}
	} // namespace draw
} // namespace spades