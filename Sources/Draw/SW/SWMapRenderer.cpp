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

#include <array>
#include <cstdint>
#include <cstring>

#include "SWMapRenderer.h"
#include "SWRenderer.h"
#include "SWUtils.h"
#include <Client/GameMap.h>
#include <Core/Bitmap.h>
#include <Core/ConcurrentDispatch.h>
#include <Core/MiniHeap.h>
#include <Core/Settings.h>
#include <Core/Stopwatch.h>

using namespace std;

DEFINE_SPADES_SETTING(r_swUndersampling, "0");

namespace spades {
	namespace draw {

		// special tan function whose value is finite.
		static inline float SpecialTan(float v) {
			static const float pi = M_PI_F;
			if (v <= -pi * 0.5F) {
				return -2.0F;
			} else if (v < -pi * 0.25F) {
				v = -2.0F - 1.0F / tanf(v);
			} else if (v < pi * 0.25F) {
				v = tanf(v);
			} else if (v < pi * 0.5F) {
				v = 2.0F - 1.0F / tanf(v);
			} else {
				return v = 2.0F;
			}
			return v;
		}
		// convert from tan value to special tan value.
		static inline float ToSpecialTan(float v) {
			if (v < -1.0F)
				return -2.0F - fastRcp(v);
			else if (v > 1.0F)
				return 2.0F - fastRcp(v);
			else
				return v;
		}

		enum class Face : short { PosX, NegX, PosY, NegY, PosZ, NegZ };

		struct SWMapRenderer::LinePixel {
			union {
				struct {
					uint32_t combined;
					float depth;
				};
				struct {
					unsigned int color : 24;
					// Face face: 7;
					bool filled : 1;
				};
				struct {
					uint64_t allData;
				};
			};

			// using "operator =" makes this struct non-POD
			void Set(const LinePixel& p) { allData = p.allData; }

			inline void Clear() {
				combined = 0;
				depth = 10000.0F;
			}

			inline bool IsEmpty() const { return combined == 0; }
		};

		// infinite length line from -z to +z
		struct SWMapRenderer::Line {
			std::vector<LinePixel> pixels;
			Vector3 horizonDir;
			float pitchTanMin;
			float pitchScale;
			int pitchTanMinI;
			int pitchScaleI;
		};

		SWMapRenderer::SWMapRenderer(SWRenderer& r, client::GameMap* m, SWFeatureLevel level)
		    : w(m->Width()),
		      h(m->Height()),
		      renderer(r),
		      level(level),
		      map(m),
		      frameBuf(nullptr),
		      depthBuf(nullptr),
		      rleHeap(m->Width() * m->Height() * 64) {
			rle.resize(w * h);
			rleLen.resize(w * h);

			Stopwatch sw;
			sw.Reset();
			SPLog("Building RLE map...");

			int idx = 0;
			for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++) {
				BuildRle(x, y, rleBuf);

				auto ref = rleHeap.Alloc(rleBuf.size() * sizeof(RleData));
				short* ptr = rleHeap.Dereference<short>(ref);
				std::memcpy(ptr, rleBuf.data(), rleBuf.size() * sizeof(RleData));

				rle[idx] = ref;
				rleLen[idx] = rleBuf.size() * sizeof(RleData);

				idx++;
			}

			SPLog("RLE map created in %.6f seconds", sw.GetTime());
		}

		SWMapRenderer::~SWMapRenderer() {}

		void SWMapRenderer::BuildRle(int x, int y, std::vector<RleData>& out) {
			out.clear();

			out.push_back(0); // [0] = +Z face position address
			out.push_back(0);
			out.push_back(0); // [2] = +X face position address
			out.push_back(0);
			out.push_back(0); // [4] = -X face position address
			out.push_back(0);
			out.push_back(0); // [6] = +Y face position address
			out.push_back(0);
			out.push_back(0); // [8] = -Y face position address
			out.push_back(0);

			auto setHeader = [&](size_t idx, size_t val) {
				reinterpret_cast<short*>(out.data())[idx] = static_cast<short>(val);
			};

			uint64_t smap = map->GetSolidMapWrapped(x, y);
			std::array<uint64_t, 4> adjs = {
			  map->GetSolidMapWrapped(x + 1, y), map->GetSolidMapWrapped(x - 1, y),
			  map->GetSolidMapWrapped(x, y + 1), map->GetSolidMapWrapped(x, y - 1)};
			bool old = false;

			for (int z = 0; z < 64; z++) {
				bool b = (smap >> z) & 1;
				if (b && !old)
					out.push_back(static_cast<RleData>(z));
				old = b;
			}
			out.push_back(-1);

			setHeader(0, out.size());

			old = true;
			for (int z = 63; z >= 0; z--) {
				bool b = (smap >> z) & 1;
				if (b && !old)
					out.push_back(static_cast<RleData>(z));
				old = b;
			}
			out.push_back(-1);

			for (int k = 0; k < 4; k++) {
				setHeader(k + 1, out.size());
				for (int z = 0; z < 64; z++) {
					if ((smap >> z) & 1) {
						if (!((adjs[k] >> z) & 1)) {
							out.push_back(static_cast<RleData>(z));
						}
					}
				}
				out.push_back(-1);
			}

			// padding
			while (out.size() & 3) {
				out.push_back(42);
			}
		}

		void SWMapRenderer::UpdateRle(int x, int y) {
			int idx = x + y * w;
			BuildRle(x, y, rleBuf);

			rleHeap.Free(rle[idx], rleLen[idx]);

			auto ref = rleHeap.Alloc(rleBuf.size() * sizeof(RleData));
			short* ptr = rleHeap.Dereference<short>(ref);
			std::memcpy(ptr, rleBuf.data(), rleBuf.size() * sizeof(RleData));

			rle[idx] = ref;
			rleLen[idx] = rleBuf.size() * sizeof(RleData);
		}

		template <SWFeatureLevel flevel>
		void SWMapRenderer::BuildLine(Line& line, float minPitch, float maxPitch) {
			// hard code for further optimization
			enum { w = 512, h = 512 };
			SPAssert(map->Width() == 512);
			SPAssert(map->Height() == 512);

			const auto* rle = this->rle.data();
			auto& rleHeap = this->rleHeap;
			client::GameMap& map = *this->map;

			// pitch culling
			{
				const auto& frustrum = renderer.frustrum;
				static const float pi = M_PI_F;
				const auto& horz = line.horizonDir;
				minPitch = -pi * 0.4999F;
				maxPitch = pi * 0.4999F;

				auto cull = [&minPitch, &maxPitch]() {
					minPitch = 2.0F;
					maxPitch = -2.0F;
				};
				auto clip = [&minPitch, &maxPitch, &horz, &cull](Vector3 plane) {
					if (plane.x == 0.0F && plane.y == 0.0F) {
						if (plane.z > 0.0F) {
							minPitch = std::max(minPitch, 0.0F);
						} else {
							maxPitch = std::min(maxPitch, 0.0F);
						}
					} else if (plane.z == 0.0F) {
						if (Vector3::Dot(plane, horz) < 0.0F)
							cull();
					} else {
						Vector3 prj = plane;
						prj.z = 0.0F;
						prj = prj.Normalize();

						float zv = fabsf(plane.z);
						float cs = Vector3::Dot(prj, horz);

						float ang = zv * zv * (1.0F - cs * cs) / (cs * cs);
						ang = -cs * fastSqrt(1.0F + ang);
						ang = zv / ang;
						if (isnan(ang) || isinf(ang) || ang == 0.0F)
							return;

						// convert to tan
						ang = fastSqrt(1.0F - ang * ang) / ang;

						// convert to angle
						ang = atanf(ang);

						if (isnan(ang) || isinf(ang))
							return;

						if (plane.z > 0.0F) {
							minPitch = std::max(minPitch, ang - 0.01f);
						} else {
							maxPitch = std::min(maxPitch, -ang + 0.01f);
						}
					}
				};

				clip(frustrum[2].n);
				clip(frustrum[3].n);
				clip(frustrum[4].n);
				clip(frustrum[5].n);
			}

			float minTan = SpecialTan(minPitch);
			float maxTan = SpecialTan(maxPitch);

			{
				float minDiff = lineResolution / 10000.0F;
				if (maxTan < minTan + minDiff) {
					// too little difference; scale value might overflow.
					maxTan = minTan + minDiff;
				}
			}

			line.pitchTanMin = minTan;
			line.pitchScale = lineResolution / (maxTan - minTan);
			line.pitchTanMinI = static_cast<int>(minTan * 65536.0F);
			line.pitchScaleI = static_cast<int>(line.pitchScale * 65536.0F);

			// TODO: pitch culling

			// ray direction
			float dirX = line.horizonDir.x;
			float dirY = line.horizonDir.y;
			if (fabsf(dirY) < 1.0E-4F)
				dirY = 1.0E-4F;
			if (fabsf(dirX) < 1.0E-4F)
				dirX = 1.0E-4F;
			float invDirX = 1.0F / dirX;
			float invDirY = 1.0F / dirY;
			std::int_fast8_t signX = (dirX > 0.0F) ? 1 : -1;
			std::int_fast8_t signY = (dirY > 0.0F) ? 1 : -1;
			int invDirXI = static_cast<int>(invDirX * 256.0F);
			int invDirYI = static_cast<int>(invDirY * 256.0F);
			int dirXI = static_cast<int>(dirX * 512.0F);
			int dirYI = static_cast<int>(dirY * 512.0F);
			if (invDirXI < 0)
				invDirXI = -invDirXI;
			if (invDirYI < 0)
				invDirYI = -invDirYI;
			if (dirXI < 0)
				dirXI = -dirXI;
			if (dirYI < 0)
				dirYI = -dirYI;

			// camera position
			float cx = sceneDef.viewOrigin.x;
			float cy = sceneDef.viewOrigin.y;
			float cz = sceneDef.viewOrigin.z;

			int icz = static_cast<int>(floorf(cz));

			// ray position
			// float rx = cx, ry = cy;
			int rx = static_cast<int>(cx * 512.0F);
			int ry = static_cast<int>(cy * 512.0F);

			// ray position in integer
			std::int_fast16_t irx = rx >> 9; // static_cast<int>(floorf(rx));
			std::int_fast16_t iry = ry >> 9; // static_cast<int>(floorf(ry));

			float fogDist = 128.0F;
			float distance = 1.0E-20F; // traveled path
			float invDist = 1.0F / distance;

			// auto& pixels = line.pixels;

			line.pixels.resize(lineResolution);
			auto* pixels = line.pixels.data(); // std::vector feels slow...

			const float transScale = static_cast<float>(lineResolution) / (maxTan - minTan);
			const float transOffset = -minTan * transScale;

#if ENABLE_SSE
			if (lineResolution > 4) {
				static_assert(sizeof(LinePixel) == 8,
				              "size of LinePixel has changed; needs code modification");
				union {
					LinePixel pxs[2];
					__m128 m;
				};
				pxs[0].Clear();
				pxs[1].Clear();
				auto* ptr = pixels;
				for (auto* e = pixels + lineResolution;
				     (reinterpret_cast<size_t>(ptr) & 0xF) && (ptr < e); ptr++) {
					ptr->Clear();
				}
				for (auto* e = pixels + lineResolution - 2; ptr < e; ptr += 2)
					_mm_store_ps(reinterpret_cast<float*>(ptr), m);
				for (auto* e = pixels + lineResolution; ptr < e; ptr++)
					ptr->Clear();
			} else
#endif
				for (int i = 0; i < lineResolution; i++)
					pixels[i].Clear();

			// if culled out, bail out now (pixels are filled)
			if (minPitch >= maxPitch)
				return;

			std::uint_fast32_t emptyCount = static_cast<std::uint_fast32_t>(lineResolution);

			const std::array<float, 65>& zval = frameZVal; // precomputed (z - cz)

			float vmax = lineResolution + 0.5F;
			auto transform = [&zval, &transOffset, vmax, &transScale](float invDist, int z) {
				float p = ToSpecialTan(invDist * zval[z]) * transScale + transOffset;
				p = std::max(p, 0.0F);
				p = std::min(p, vmax);
				return static_cast<std::uint_fast16_t>(p);
			};

			// travel distance -> view Z value factor
			float zscale = Vector3::Dot(line.horizonDir, sceneDef.viewAxis[2]);

			// Z value -> view Z value factor
			float heightScale = sceneDef.viewAxis[2].z;

			const std::array<float, 65>& heightScaleVal = frameHeightScaleVal;

			float depthBias = -cz * heightScale;

			RleData* lastRle;
			{
				auto ref = rle[(irx & (w - 1)) + ((iry & (h - 1)) * w)];
				lastRle = rleHeap.Dereference<RleData>(ref);
			}

			std::uint_fast16_t count = 1;
			std::uint_fast16_t cnt2 = static_cast<int>(fogDist * 8.0F);

			while (distance < fogDist && (--cnt2) > 0) {
				std::int_fast16_t nextIRX, nextIRY;
				auto oirx = irx, oiry = iry;

				// DDE
				Face wallFace;

				if (signX > 0) {
					nextIRX = irx + 1;
					if (signY > 0) {
						nextIRY = iry + 1;

						unsigned int timeToNextX = (512 - (rx & 511)) * invDirXI;
						unsigned int timeToNextY = (512 - (ry & 511)) * invDirYI;

						if (timeToNextX < timeToNextY) {
							// go across x plane
							irx = nextIRX;
							rx = irx << 9;
							ry += (dirYI * timeToNextX) >> 17;
							distance += static_cast<float>(timeToNextX) * (1.0F / 512.0F / 256.0F);
							wallFace = Face::NegX;
						} else {
							// go across y plane
							iry = nextIRY;
							rx += (dirXI * timeToNextY) >> 17;
							ry = iry << 9;
							distance += static_cast<float>(timeToNextY) * (1.0F / 512.0F / 256.0F);
							wallFace = Face::NegY;
						}
					} else /* (signY < 0) */ {
						nextIRY = iry - 1;

						unsigned int timeToNextX = (512 - (rx & 511)) * invDirXI;
						unsigned int timeToNextY = (ry & 511) * invDirYI;

						if (timeToNextX < timeToNextY) {
							// go across x plane
							irx = nextIRX;
							rx = irx << 9;
							ry -= (dirYI * timeToNextX) >> 17;
							distance += static_cast<float>(timeToNextX) * (1.0F / 512.0F / 256.0F);
							wallFace = Face::NegX;
						} else {
							// go across y plane
							iry = nextIRY;
							rx += (dirXI * timeToNextY) >> 17;
							ry = (iry << 9) - 1;
							distance += static_cast<float>(timeToNextY) * (1.0F / 512.0F / 256.0F);
							wallFace = Face::PosY;
						}
					}
				} else /* signX < 0 */ {
					nextIRX = irx - 1;
					if (signY > 0) {
						nextIRY = iry + 1;

						unsigned int timeToNextX = (rx & 511) * invDirXI;
						unsigned int timeToNextY = (512 - (ry & 511)) * invDirYI;

						if (timeToNextX < timeToNextY) {
							// go across x plane
							irx = nextIRX;
							rx = (irx << 9) - 1;
							ry += (dirYI * timeToNextX) >> 17;
							distance += static_cast<float>(timeToNextX) * (1.0F / 512.0F / 256.0F);
							wallFace = Face::PosX;
						} else {
							// go across y plane
							iry = nextIRY;
							rx -= (dirXI * timeToNextY) >> 17;
							ry = iry << 9;
							distance += static_cast<float>(timeToNextY) * (1.0F / 512.0F / 256.0F);
							wallFace = Face::NegY;
						}
					} else /* (signY < 0) */ {
						nextIRY = iry - 1;

						unsigned int timeToNextX = (rx & 511) * invDirXI;
						unsigned int timeToNextY = (ry & 511) * invDirYI;

						if (timeToNextX < timeToNextY) {
							// go across x plane
							irx = nextIRX;
							rx = (irx << 9) - 1;
							ry -= (dirYI * timeToNextX) >> 17;
							distance += static_cast<float>(timeToNextX) * (1.0F / 512.0F / 256.0F);
							wallFace = Face::PosX;
						} else {
							// go across y plane
							iry = nextIRY;
							rx -= (dirXI * timeToNextY) >> 17;
							ry = (iry << 9) - 1;
							distance += static_cast<float>(timeToNextY) * (1.0F / 512.0F / 256.0F);
							wallFace = Face::PosY;
						}
					}
				}

				float oldInvDist = invDist;

				invDist = fastRcp(distance);

				float medDist = distance * zscale + depthBias; //(distance + oldDistance) * 0.5F;

				// check for new spans

				auto BuildLinePixel = [&map](int x, int y, int z, Face face, float dist) {
					LinePixel px;
					px.depth = dist;
#if ENABLE_SSE
					if constexpr (flevel == SWFeatureLevel::SSE2) {
						__m128i m;
						uint32_t col = map.GetColorWrapped(x, y, z);
						m = _mm_setr_epi32(col, 0, 0, 0);
						m = _mm_unpacklo_epi8(m, _mm_setzero_si128());
						m = _mm_shufflelo_epi16(m, 0xc6);

						switch (face) {
							case Face::PosZ: m = _mm_srli_epi16(m, 1); break;
							case Face::PosX:
							case Face::PosY:
							case Face::NegX:
								m = _mm_adds_epi16(_mm_srli_epi16(m, 1), _mm_srli_epi16(m, 2));
								break;
							default: break;
						}
						if ((col >> 24) < 100)
							m = _mm_srli_epi16(m, 1);
						m = _mm_packus_epi16(m, m);
						_mm_store_ss(reinterpret_cast<float*>(&px.combined), _mm_castsi128_ps(m));
						px.filled = true;
					} else
#endif
					// non-optimized
					{
						uint32_t col;
						col = map.GetColorWrapped(x, y, z);
						col = (col & 0xFF00) | ((col & 0xFF) << 16) | ((col & 0xFF0000) >> 16);
						switch (face) {
							case Face::PosZ: col = (col & 0xfcfcfc) >> 2; break;
							case Face::PosX:
							case Face::PosY:
							case Face::NegX: col = (col & 0xfefefe) >> 1; break;
							default: break;
						}
						px.combined = col;
						px.filled = true;
					}
					return px;
				};

				// floor/ceiling
				{

					// linear code

					// RLE scan
					RleData* rle = lastRle;
					{
						RleData* ptr = rle + 10;
						while (*ptr != -1) {
							std::int_fast8_t z = *ptr;
							if (z > icz) {
								std::uint_fast16_t p1 = transform(invDist, z);
								std::uint_fast16_t p2 = transform(oldInvDist, z);
								if (p1 < p2) {
									LinePixel px = BuildLinePixel(oirx, oiry, z,
										Face::NegZ, medDist + heightScaleVal[z]);

									for (std::uint_fast16_t j = p1; j < p2; j++) {
										auto& p = pixels[j];
										if (!p.IsEmpty())
											continue;
										p.Set(px);
										emptyCount--;
									}
								}
							}
							ptr++;
						}
						ptr++;
						while (*ptr != -1) {
							std::int_fast8_t z = *ptr;
							if (z < icz) {
								std::uint_fast16_t p1 = transform(invDist, z + 1);
								std::uint_fast16_t p2 = transform(oldInvDist, z + 1);
								if (p2 < p1) {
									LinePixel px = BuildLinePixel(oirx, oiry, z,
										Face::PosZ, medDist + heightScaleVal[z + 1]);

									for (std::uint_fast16_t j = p2; j < p1; j++) {
										auto& p = pixels[j];
										if (!p.IsEmpty())
											continue;
										p.Set(px);
										emptyCount--;
									}
								}
							}
							ptr++;
						}
					}

				} // done: floor/ceiling

				// add walls
				{
					// by RLE map
					auto ref = rle[static_cast<std::uint_fast32_t>(irx & (w - 1)) +
					               static_cast<std::uint_fast32_t>(iry & (h - 1)) * w];
					RleData* rle = rleHeap.Dereference<RleData>(ref);
					lastRle = rle;
					auto* ptr = rle;
					ptr += reinterpret_cast<unsigned short*>(rle)[1 + static_cast<int>(wallFace)];

					std::uint_fast16_t savedP = 0;
					std::int_fast8_t savedZ = 127;

					while (*ptr != -1) {
						std::int_fast8_t z = *(ptr++);

						std::uint_fast16_t p1 = savedZ == z ? savedP : transform(invDist, z);
						std::uint_fast16_t p2 = transform(invDist, z + 1);

						savedZ = z + 1;
						savedP = p2;

						if (p1 < p2) {
							LinePixel px =
							  BuildLinePixel(irx, iry, z, wallFace, medDist + heightScaleVal[z]);

							for (std::uint_fast16_t j = p1; j < p2; j++) {
								auto& p = pixels[j];
								if (!p.IsEmpty())
									continue;
								p.Set(px);
								emptyCount--;
							}
						}
					}

				} // add wall - end

				// every pixel of this line written; nothing more can appear
				if (emptyCount == 0)
					break;

				// check pitch cull
				if ((--count) == 0) {
					if ((int(transform(invDist, 0)) >= (lineResolution - 1) && icz >= 0) ||
					    transform(invDist, 63) <= 0)
						break;
					count = 4;
				}

				// let's go to next voxel!
			}
		}

#define tsize 5000
#define titems 4096.0F

		struct AtanTable {
			std::array<uint16_t, tsize> sm;
			std::array<uint16_t, tsize> lg;
			std::array<uint16_t, tsize> smN;
			std::array<uint16_t, tsize> lgN;

			// [0, 2pi] -> [0, 65536]
			static uint16_t ToFixed(float v) {
				v /= (M_PI_F * 2.0F);
				v *= 65536.0F;
				int i = static_cast<int>(v);
				return static_cast<uint16_t>(i & 65535);
			}

			AtanTable() {
				for (int i = 0; i < tsize; i++) {
					sm[i] = ToFixed(atanf(i / titems));
					lg[i] = ToFixed(atanf(1.0F / ((i + 0.5F) / titems)));
					smN[i] = ToFixed(-atanf(i / titems));
					lgN[i] = ToFixed(-atanf(1.0F / ((i + 0.5F) / titems)));
				}
			}
		};
		static AtanTable atanTable;
		static inline uint16_t fastATan(float v) {
			return (v < 0.0F)
				? (v > -1.0F) ? atanTable.smN[static_cast<int>(v * -titems)]
				: atanTable.lgN[static_cast<int>(fastDiv(-titems, v))]
				: (v < 1.0F) ? atanTable.sm[static_cast<int>(v * titems)]
				: atanTable.lg[static_cast<int>(fastDiv(titems, v))];
		}
		static inline uint16_t fastATan2(float y, float x) {
			return (x == 0.0F) ? (y > 0.0F) ? 16384 : -16384
			       : (x > 0.0F) ? fastATan(fastDiv(y, x))
			                    : fastATan(fastDiv(y, x)) + 32768;
		}

		template <SWFeatureLevel flevel, int under>
		void SWMapRenderer::RenderFinal(float yawMin, float yawMax, unsigned int numLines,
		                                unsigned int threadId, unsigned int numThreads) {
			float fovX = tanf(sceneDef.fovX * 0.5F);
			float fovY = tanf(sceneDef.fovY * 0.5F);
			Vector3 front = sceneDef.viewAxis[2];
			Vector3 right = sceneDef.viewAxis[0];
			Vector3 down = sceneDef.viewAxis[1];

			unsigned int fw = frameBuf->GetWidth();
			unsigned int fh = frameBuf->GetHeight();
			uint32_t* fb = frameBuf->GetPixels();
			float* depthBuf = this->depthBuf;
			Vector3 v1 = front - right * fovX + down * fovY;
			Vector3 deltaDown = -down * (fovY * 2.0F / static_cast<float>(fh));
			Vector3 deltaRight = right * (fovX * 2.0F / static_cast<float>(fw) * under);

			Vector2 screenPos = {-fovX, -fovY};
			float deltaScreenPosRight = fovX * 2.0F / static_cast<float>(fw);
			float deltaScreenPosDown = fovY * 2.0F / static_cast<float>(fh);

			static const float pi = M_PI_F;
			float yawScale = 65536.0F / (pi * 2.0F);
			std::int32_t yawScale2 =
			  static_cast<std::int32_t>(pi * 2.0F / (yawMax - yawMin) * 65536.0F);
			std::int32_t yawMin2 = static_cast<std::int32_t>(yawMin * yawScale);
			auto& lineList = this->lines;

			enum { blockSize = 8, hBlock = blockSize / under };

			Vector3 deltaDownLarge = deltaDown * blockSize;
			Vector3 deltaRightLarge = deltaRight * hBlock;

			unsigned int startX = threadId * fw / numThreads;
			unsigned int endX = (threadId + 1) * fw / numThreads;

			startX = (startX / blockSize) * blockSize;
			endX = (endX / blockSize) * blockSize;

			deltaScreenPosRight *= static_cast<float>(blockSize);
			deltaScreenPosDown *= static_cast<float>(blockSize);

			v1 += deltaRight * static_cast<float>(startX / under);
			screenPos.x += deltaScreenPosRight * static_cast<float>(startX / blockSize);

			for (unsigned int fx = startX; fx < endX; fx += blockSize) {
				Vector3 v2 = v1;
				screenPos.y = -fovY;
				for (unsigned int fy = 0; fy < fh; fy += blockSize) {
					uint32_t* fb2 = fb + fx + fy * fw;
					float* db2 = depthBuf + fx + fy * fw;

					// Use bi-linear interpolation for faster yaw/pitch
					// computation.
					{
					auto calcYawindex = [yawMin2](Vector3 v) {
						std::int32_t yawIndex;
						{
							float x = v.x, y = v.y;
							int yaw;
							yaw = fastATan2(y, x);
							yaw -= yawMin2;
							yawIndex = static_cast<int>(yaw & 0xFFFF);
						}
						yawIndex <<= 8;
						return yawIndex;
					};
					auto calcPitch = [](Vector3 vv) {
						float pitch;
						pitch = vv.z * fastRSqrt(vv.x * vv.x + vv.y * vv.y);
						pitch = ToSpecialTan(pitch);
						return static_cast<int>(pitch * (65536.0F * 8192.0F));
					};
					std::int32_t yawIndex1 = calcYawindex(v2);
					std::int32_t pitch1 = calcPitch(v2);
					std::int32_t yawIndex2 = calcYawindex(v2 + deltaRightLarge);
					std::int32_t pitch2 = calcPitch(v2 + deltaRightLarge);
					std::int32_t yawIndex3 = calcYawindex(v2 + deltaDownLarge);
					std::int32_t pitch3 = calcPitch(v2 + deltaDownLarge);
					std::int32_t yawIndex4 = calcYawindex(v2 + deltaRightLarge + deltaDownLarge);
					std::int32_t pitch4 = calcPitch(v2 + deltaRightLarge + deltaDownLarge);

					// note: `<<8>>8` is phase unwrapping
					std::int32_t yawDiff1 = ((yawIndex2 - yawIndex1) << 8 >> 8) / hBlock;
					std::int32_t yawDiff2 = ((yawIndex4 - yawIndex3) << 8 >> 8) / hBlock;
					std::int32_t pitchDiff1 = (pitch2 - pitch1) / hBlock;
					std::int32_t pitchDiff2 = (pitch4 - pitch3) / hBlock;

					std::int32_t yawIndexA = yawIndex1;
					std::int32_t yawIndexB = yawIndex3;
					std::int32_t pitchA = pitch1;
					std::int32_t pitchB = pitch3;

					for (unsigned int x = 0; x < blockSize; x += under) {
						uint32_t* fb3 = fb2 + x;
						auto* db3 = db2 + x;

						std::int32_t yawIndexC = yawIndexA;
						std::int32_t yawDelta = ((yawIndexB - yawIndexA) << 8 >> 8) / blockSize;
						std::int32_t pitchC = pitchA;
						std::int32_t pitchDelta = (pitchB - pitchA) / blockSize;

						std::uint32_t cachedYawIndex = 0xFFFFFFFFu;
						const LinePixel* pixels = nullptr;
						std::int32_t cachedPitchTanMinI = 0;
						std::int32_t cachedPitchScaleI = 0;

						for (unsigned int y = 0; y < blockSize; y++) {
							std::uint32_t yawIndex =
							  static_cast<unsigned int>(yawIndexC << 8 >> 16);
							yawIndex = (yawIndex * yawScale2) >> 16;
							yawIndex = (yawIndex * numLines) >> 16;
							if (yawIndex != cachedYawIndex) {
								auto& line = lineList[yawIndex];
								pixels = line.pixels.data();
								cachedPitchTanMinI = line.pitchTanMinI;
								cachedPitchScaleI = line.pitchScaleI;
								cachedYawIndex = yawIndex;
							}

							// solve pitch
							std::int32_t pitchIndex;

							{
								pitchIndex = pitchC >> 13;
								pitchIndex -= cachedPitchTanMinI;
								pitchIndex =
								  static_cast<int>((static_cast<int64_t>(pitchIndex) *
								                    static_cast<int64_t>(cachedPitchScaleI)) >>
								                   32);
								// pitch = (pitch - line.pitchTanMin) * line.pitchScale;
								// pitchIndex = static_cast<int>(pitch);
								pitchIndex &= lineResolution - 1;
								// pitchIndex = std::max(pitchIndex, 0);
								// pitchIndex = std::min(pitchIndex, lineResolution - 1);
							}

							auto& pix = pixels[pitchIndex];

// write color.
// NOTE: combined contains both color and other information,
// though this isn't a problem as long as the color comes
// in the LSB's
#if ENABLE_SSE
							if constexpr (flevel == SWFeatureLevel::SSE2) {
								__m128i m;

								if (under == 1) {
									*fb3 = pix.combined;
									*db3 = pix.depth;
								} else if (under == 2) {
									m = _mm_castpd_si128(
									  _mm_load_sd(reinterpret_cast<const double*>(&pix)));
									_mm_store_sd(reinterpret_cast<double*>(fb3),
									             _mm_castsi128_pd(_mm_shuffle_epi32(m, 0)));
									_mm_store_sd(reinterpret_cast<double*>(db3),
									             _mm_castsi128_pd(_mm_shuffle_epi32(m, 0x55)));
								} else if (under == 4) {
									m = _mm_castpd_si128(
									  _mm_load_sd(reinterpret_cast<const double*>(&pix)));
									_mm_stream_si128(reinterpret_cast<__m128i*>(fb3),
									                 _mm_shuffle_epi32(m, 0));
									_mm_stream_si128(reinterpret_cast<__m128i*>(db3),
									                 _mm_shuffle_epi32(m, 0x55));
								}

							} else
#endif
							// non-optimized
							{
								uint32_t col = pix.combined;
								float d = pix.depth;

								for (int k = 0; k < under; k++) {
									fb3[k] = col;
									db3[k] = d;
								}
							}

							fb3 += fw;
							db3 += fw;

							yawIndexC += yawDelta;
							pitchC += pitchDelta;
						}

						yawIndexA += yawDiff1;
						yawIndexB += yawDiff2;
						pitchA += pitchDiff1;
						pitchB += pitchDiff2;
					}
				}

					v2 += deltaDownLarge;
					screenPos.y += deltaScreenPosDown;
				} // fy
				v1 += deltaRightLarge;
				screenPos.x += deltaScreenPosRight;
			} // fx
		}

		template <SWFeatureLevel flevel>
		void SWMapRenderer::RenderInner(const client::SceneDefinition& def, Bitmap* frame,
		                                float* depthBuffer) {
			sceneDef = def;
			frameBuf = frame;
			depthBuf = depthBuffer;

			// calculate line density.
			float yawMin, yawMax;
			float pitchMin, pitchMax;
			size_t numLines;

			int under = r_swUndersampling;

			{
				float fovX = tanf(def.fovX * 0.5F);
				float fovY = tanf(def.fovY * 0.5F);
				float fovDiag = sqrtf(fovX * fovX + fovY * fovY);
				float fovDiagAng = atanf(fovDiag);
				float pitch = asinf(def.viewAxis[2].z);
				static const float pi = M_PI_F;

				// pitch = 0.0F;

				if (fabsf(pitch) >= pi * 0.49F - fovDiagAng) {
					// pole is visible
					yawMin = 0.0F;
					yawMax = pi * 2.0F;
				} else {
					float yaw = atan2f(def.viewAxis[2].y, def.viewAxis[2].x);
					// TODO: incorrect!
					yawMin = yaw - pi * 0.5F; // fovDiagAng;
					yawMax = yaw + pi * 0.5F; // fovDiagAng;
				}

				pitchMin = pitch - fovDiagAng;
				pitchMax = pitch + fovDiagAng;
				if (pitchMin < -pi * 0.5F) {
					pitchMax = std::max(pitchMax, -pi - pitchMin);
					pitchMin = -pi * 0.5F;
				}
				if (pitchMax > pi * 0.5F) {
					pitchMin = std::min(pitchMin, pi - pitchMax);
					pitchMax = pi * 0.5F;
				}

				// pitch of PI/2 will make tan(x) infinite
				pitchMin = std::max(pitchMin, -pi * 0.4999f);
				pitchMax = std::min(pitchMax, pi * 0.4999f);

				float interval = static_cast<float>(frame->GetHeight());
				interval = fovY * 2.0F / interval;
				lineResolution = static_cast<int>((pitchMax - pitchMin) / interval * 1.5f);
				lineResolution = frame->GetHeight();

				for (int i = lineResolution, j = 1; j <= i; j <<= 1)
					lineResolution = j;

				numLines = static_cast<size_t>((yawMax - yawMin) / interval);

				int under = Clamp((int)r_swUndersampling, 1, 4);
				numLines /= under;

				if (numLines < 8)
					numLines = 8;
				if (numLines > 8192)
					numLines = 8192;

				lines.resize(std::max(numLines, lines.size()));
			}

			// calculate vector for each lines
			{
				float scl = (yawMax - yawMin) / numLines;
				Vector3 horiz = Vector3::Make(cosf(yawMin), sinf(yawMin), 0.0F);
				float c = cosf(scl);
				float s = sinf(scl);
				for (size_t i = 0; i < numLines; i++) {
					Line& l = lines[i];
					l.horizonDir = horiz;

					float x = horiz.x * c - horiz.y * s;
					float y = horiz.x * s + horiz.y * c;
					horiz.x = x;
					horiz.y = y;
				}
			}

			{
				float cz = def.viewOrigin.z;
				float heightScale = def.viewAxis[2].z;
				for (size_t i = 0; i < frameZVal.size(); i++) {
					frameZVal[i] = static_cast<float>(i) - cz;
					frameHeightScaleVal[i] = static_cast<float>(i) * heightScale;
				}
			}

			{
				unsigned int nlines = static_cast<unsigned int>(numLines);
				InvokeParallel2([&](unsigned int th, unsigned int numThreads) {
					unsigned int start = th * nlines / numThreads;
					unsigned int end = (th + 1) * nlines / numThreads;

					for (size_t i = start; i < end; i++)
						BuildLine<flevel>(lines[i], pitchMin, pitchMax);
				});
			}

			InvokeParallel2([&](unsigned int th, unsigned int numThreads) {
				if (under <= 1) {
					RenderFinal<flevel, 1>(yawMin, yawMax, static_cast<unsigned int>(numLines), th, numThreads);
				} else if (under <= 2) {
					RenderFinal<flevel, 2>(yawMin, yawMax, static_cast<unsigned int>(numLines), th, numThreads);
				} else {
					RenderFinal<flevel, 4>(yawMin, yawMax, static_cast<unsigned int>(numLines), th, numThreads);
				}
			});

			frameBuf = nullptr;
			depthBuf = nullptr;
		}

		void SWMapRenderer::Render(const client::SceneDefinition& def, Bitmap& frame,
		                           float* depthBuffer) {
			if (!depthBuffer)
				SPInvalidArgument("depthBuffer");

			IntVector3 p = def.viewOrigin.Floor();
			if (map->IsSolidWrapped(p.x, p.y, p.z))
				return;

#if ENABLE_SSE2
			if (static_cast<int>(level) >= static_cast<int>(SWFeatureLevel::SSE2)) {
				RenderInner<SWFeatureLevel::SSE2>(def, &frame, depthBuffer);
				return;
			}
#endif

			RenderInner<SWFeatureLevel::None>(def, &frame, depthBuffer);
		}
	} // namespace draw
} // namespace spades
