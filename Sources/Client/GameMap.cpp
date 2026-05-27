/*
 Copyright (c) 2013 yvt
 based on code of pysnip (c) Mathias Kaerlev 2011-2012.

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
#include <cstdlib>
#include <cstring>
#include <vector>

#include "GameMap.h"
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/RandomAccessAdaptor.h>

namespace spades {
	namespace client {

		static uint32_t swapColorMap(uint32_t col) {
			return swapColor(col) | (100UL << 24);
		}

		GameMap::GameMap() {
			SPADES_MARK_FUNCTION();

			for (int x = 0; x < DefaultWidth; x++)
			for (int y = 0; y < DefaultHeight; y++) {
				solidMap[x][y] = 1; // ground only
				for (int z = 0; z < DefaultDepth; z++) {
					uint32_t col = GetDirtColor(x, y, z);
					colorMap[x][y][z] = swapColorMap(col);
				}
			}
		}
		GameMap::~GameMap() { SPADES_MARK_FUNCTION(); }

		Handle<GameMap> GameMap::Clone() const {
			Handle<GameMap> copy{new GameMap(NoInit{})};
			memcpy(copy->solidMap, solidMap, sizeof(solidMap));
			memcpy(copy->colorMap, colorMap, sizeof(colorMap));
			return copy;
		}

		void GameMap::AddListener(spades::client::IGameMapListener* l) {
			std::lock_guard<std::mutex> _guard{listenersMutex};
			listeners.push_back(l);
		}

		void GameMap::RemoveListener(spades::client::IGameMapListener* l) {
			std::lock_guard<std::mutex> _guard{listenersMutex};
			auto it = std::find(listeners.begin(), listeners.end(), l);
			if (it != listeners.end())
				listeners.erase(it);
		}

		static void WriteColor(std::vector<char>& buffer, int color) {
			buffer.push_back((char)(color >> 16));
			buffer.push_back((char)(color >> 8));
			buffer.push_back((char)(color >> 0));
			buffer.push_back((char)(color >> 24));
		}

		// base on pysnip
		void GameMap::Save(spades::IStream* stream) {
			int w = Width();
			int h = Height();
			int d = Depth();
			std::vector<char> buffer;
			buffer.reserve(10 * 1024 * 1024);
			for (int y = 0; y < h; y++) {
				for (int x = 0; x < w; x++) {
					int z = 0;
					while (z < d) {
						// find the air region
						int air_start = z;
						while (z < d && !IsSolid(x, y, z))
							++z;

						// find the top region
						int top_colors_start = z;
						while (z < d && IsSurface(x, y, z))
							++z;
						int top_colors_end = z;

						// now skip past the solid voxels
						while (z < d && IsSolid(x, y, z) && !IsSurface(x, y, z))
							++z;

						// at the end of the solid voxels, we have colored voxels.
						// in the "normal" case they're bottom colors; but it's
						// possible to have air-color-solid-color-solid-color-air,
						// which we encode as air-color-solid-0, 0-color-solid-air

						// so figure out if we have any bottom colors at this point
						int bottom_colors_start = z;

						int i = z;
						while (i < d && IsSurface(x, y, i))
							++i;

						if (i != d) {
							while (IsSurface(x, y, z))
								++z;
						}
						int bottom_colors_end = z;

						// now we're ready to write a span
						int top_colors_len = top_colors_end - top_colors_start;
						int bottom_colors_len = bottom_colors_end - bottom_colors_start;

						int colors = top_colors_len + bottom_colors_len;

						if (z == d)
							buffer.push_back(0);
						else
							buffer.push_back(colors + 1);
						buffer.push_back(top_colors_start);
						buffer.push_back(top_colors_end - 1);
						buffer.push_back(air_start);

						for (i = 0; i < top_colors_len; ++i)
							WriteColor(buffer, GetColor(x, y, top_colors_start + i));
						for (i = 0; i < bottom_colors_len; ++i)
							WriteColor(buffer, GetColor(x, y, bottom_colors_start + i));
					}
				}
			}

			stream->Write(buffer.data(), buffer.size());
		}

		int GameMap::GetTop(int x, int y) const {
			if (x < 0 || x >= DefaultWidth || y < 0 || y >= DefaultHeight)
				return 0;
			for (int z = 0; z < DefaultDepth; z++) {
				if (IsSolid(x, y, z))
					return z;
			}
			return 0;
		}

		bool GameMap::ClipBox(int x, int y, int z) const {
			if (x < 0 || x >= DefaultWidth || y < 0 || y >= DefaultHeight)
				return true;
			else if (z < 0)
				return false;
			int sz = (int)z;
			if (sz == DefaultDepth - 1)
				sz = DefaultDepth - 2;
			else if (sz >= DefaultDepth)
				return true;
			return IsSolid((int)x, (int)y, sz);
		}

		bool GameMap::ClipWorld(int x, int y, int z) const {
			if (x < 0 || x >= DefaultWidth || y < 0 || y >= DefaultHeight || z < 0)
				return 0;
			int sz = (int)z;
			if (sz == DefaultDepth - 1)
				sz = DefaultDepth - 2;
			else if (sz >= DefaultDepth - 1)
				return 1;
			else if (sz < 0)
				return 0;
			return IsSolid((int)x, (int)y, sz);
		}

		bool GameMap::ClipBox(float x, float y, float z) const {
			SPAssert(!std::isnan(x));
			SPAssert(!std::isnan(y));
			SPAssert(!std::isnan(z));
			return ClipBox((int)floorf(x), (int)floorf(y), (int)floorf(z));
		}

		bool GameMap::ClipWorld(float x, float y, float z) const {
			SPAssert(!std::isnan(x));
			SPAssert(!std::isnan(y));
			SPAssert(!std::isnan(z));
			return ClipWorld((int)floorf(x), (int)floorf(y), (int)floorf(z));
		}

		bool GameMap::CastRay(spades::Vector3 v0, spades::Vector3 v1, float length,
		                      spades::IntVector3& vOut) const {
			SPADES_MARK_FUNCTION_DEBUG();

			SPAssert(!v0.IsNaN());
			SPAssert(!v1.IsNaN());
			SPAssert(!std::isnan(length));

			v1 = v0 + v1 * length;

			Vector3 f, g;
			IntVector3 a, c, d, p, i;
			long cnt = 0;

			a = v0.Floor();
			c = v1.Floor();

			if (c.x < a.x) {
				d.x = -1;
				f.x = v0.x - a.x;
				g.x = (v0.x - v1.x) * 1024;
				cnt += a.x - c.x;
			} else if (c.x != a.x) {
				d.x = 1;
				f.x = a.x + 1 - v0.x;
				g.x = (v1.x - v0.x) * 1024;
				cnt += c.x - a.x;
			} else {
				d.x = 0;
				f.x = g.x = 0.0F;
			}
			if (c.y < a.y) {
				d.y = -1;
				f.y = v0.y - a.y;
				g.y = (v0.y - v1.y) * 1024;
				cnt += a.y - c.y;
			} else if (c.y != a.y) {
				d.y = 1;
				f.y = a.y + 1 - v0.y;
				g.y = (v1.y - v0.y) * 1024;
				cnt += c.y - a.y;
			} else {
				d.y = 0;
				f.y = g.y = 0.0F;
			}
			if (c.z < a.z) {
				d.z = -1;
				f.z = v0.z - a.z;
				g.z = (v0.z - v1.z) * 1024;
				cnt += a.z - c.z;
			} else if (c.z != a.z) {
				d.z = 1;
				f.z = a.z + 1 - v0.z;
				g.z = (v1.z - v0.z) * 1024;
				cnt += c.z - a.z;
			} else {
				d.z = 0;
				f.z = g.z = 0.0F;
			}

			Vector3 pp =
			  MakeVector3(f.x * g.z - f.z * g.x, f.y * g.z - f.z * g.y, f.y * g.x - f.x * g.y);
			p = pp.Floor();
			i = g.Floor();

			if (cnt > (long)length)
				cnt = (long)length;

			while (cnt > 0) {
				if (((p.x | p.y) >= 0) && (a.z != c.z)) {
					a.z += d.z;
					p.x -= i.x;
					p.y -= i.y;
				} else if ((p.z >= 0) && (a.x != c.x)) {
					a.x += d.x;
					p.x += i.z;
					p.z -= i.y;
				} else {
					a.y += d.y;
					p.y += i.z;
					p.z += i.x;
				}

				if (IsSolidWrapped(a.x, a.y, a.z)) {
					vOut = a;
					return true;
				}
				cnt--;
			}

			return false;
		}

		GameMap::RayCastResult GameMap::CastRay2(spades::Vector3 v0, spades::Vector3 dir,
		                                         int maxSteps) const {
			SPADES_MARK_FUNCTION_DEBUG();
			GameMap::RayCastResult result;

			SPAssert(!v0.IsNaN());
			SPAssert(!dir.IsNaN());

			dir = dir.Normalize();

			spades::IntVector3 iv = v0.Floor();
			if (IsSolidWrapped(iv.x, iv.y, iv.z)) {
				result.hit = true;
				result.startSolid = true;
				result.hitPos = v0;
				result.hitBlock = iv;
				result.normal = MakeIntVector3(0, 0, 0);
				return result;
			}

			spades::Vector3 fv;
			fv.x = (dir.x > 0.0F) ? (float)(iv.x + 1) - v0.x : v0.x - (float)iv.x;
			fv.y = (dir.y > 0.0F) ? (float)(iv.y + 1) - v0.y : v0.y - (float)iv.y;
			fv.z = (dir.z > 0.0F) ? (float)(iv.z + 1) - v0.z : v0.z - (float)iv.z;

			float invX = (dir.x != 0.0F) ? 1.0F / fabsf(dir.x) : dir.x;
			float invY = (dir.y != 0.0F) ? 1.0F / fabsf(dir.y) : dir.y;
			float invZ = (dir.z != 0.0F) ? 1.0F / fabsf(dir.z) : dir.z;

			for (int i = 0; i < maxSteps; i++) {
				IntVector3 nextBlock;
				int hasNextBlock = 0;
				float nextBlockTime = 0.0F;

				if (invX != 0.0F) {
					nextBlock = iv;
					if (dir.x > 0.0F)
						nextBlock.x++;
					else
						nextBlock.x--;
					nextBlockTime = fv.x * invX;
					hasNextBlock = 1;
				}
				if (invY != 0.0F) {
					float t = fv.y * invY;
					if (!hasNextBlock || t < nextBlockTime) {
						nextBlock = iv;
						if (dir.y > 0.0F)
							nextBlock.y++;
						else
							nextBlock.y--;
						nextBlockTime = t;
						hasNextBlock = 2;
					}
				}
				if (invZ != 0.0F) {
					float t = fv.z * invZ;
					if (!hasNextBlock || t < nextBlockTime) {
						nextBlock = iv;
						if (dir.z > 0.0F)
							nextBlock.z++;
						else
							nextBlock.z--;
						nextBlockTime = t;
						hasNextBlock = 3;
					}
				}

				SPAssert(hasNextBlock != 0);  // must hit a plane
				SPAssert(hasNextBlock == 1 || // x-plane
				         hasNextBlock == 2 || // y-plane
				         hasNextBlock == 3);  // z-plane

				fv.x = (hasNextBlock == 1) ? 1.0F : fv.x - fabsf(dir.x) * nextBlockTime;
				fv.y = (hasNextBlock == 2) ? 1.0F : fv.y - fabsf(dir.y) * nextBlockTime;
				fv.z = (hasNextBlock == 3) ? 1.0F : fv.z - fabsf(dir.z) * nextBlockTime;

				result.hitBlock = nextBlock;
				result.normal = iv - nextBlock;

				if (IsSolidWrapped(nextBlock.x, nextBlock.y, nextBlock.z)) { // hit
					Vector3 hitPos;
					hitPos.x = (dir.x > 0.0F) ? (float)(nextBlock.x + 1) - fv.x : (float)nextBlock.x + fv.x;
					hitPos.y = (dir.y > 0.0F) ? (float)(nextBlock.y + 1) - fv.y : (float)nextBlock.y + fv.y;
					hitPos.z = (dir.z > 0.0F) ? (float)(nextBlock.z + 1) - fv.z : (float)nextBlock.z + fv.z;

					result.hit = true;
					result.startSolid = false;
					result.hitPos = hitPos;
					return result;
				} else {
					iv = nextBlock;
				}
			}

			result.hit = false;
			result.startSolid = false;
			result.hitPos = v0;
			return result;
		}

		GameMap* GameMap::Load(spades::IStream* stream, std::function<void(int)> onProgress) {
			SPADES_MARK_FUNCTION();

			RandomAccessAdaptor view{*stream};

			size_t pos = 0;

			auto map = Handle<GameMap>::New();

			if (onProgress)
				onProgress(0);

			for (int y = 0; y < DefaultHeight; y++) {
				for (int x = 0; x < DefaultWidth; x++) {
					map->solidMap[x][y] = 0xFFFFFFFFFFFFFFFFULL;

					int z = 0;
					for (;;) {
						// Read a block ahead in attempt to minimize the number of calls to
						// `IStream::Read`
						view.Prefetch(pos + DefaultWidth);

						int number_4byte_chunks = view.Read<int8_t>(pos);
						int top_color_start = view.Read<int8_t>(pos + 1);
						int top_color_end = view.Read<int8_t>(pos + 2); // inclusive

						for (int i = z; i < top_color_start; i++)
							map->Set(x, y, i, false, 0, true);

						size_t colorOffset = pos + 4;
						for (z = top_color_start; z <= top_color_end; z++) {
							uint32_t col = swapColorMap(view.Read<uint32_t>(colorOffset));
							map->Set(x, y, z, true, col, true);
							colorOffset += 4;
						}

						if (top_color_end == DefaultDepth - 2)
							map->Set(x, y, DefaultDepth - 1, true, map->GetColor(x, y, DefaultDepth - 2), true);

						int len_bottom = top_color_end - top_color_start + 1;

						// check for end of data marker
						if (number_4byte_chunks == 0) {
							// infer ACTUAL number of 4-byte chunks from the length of the color data
							pos += 4 * (len_bottom + 1);
							break;
						}

						// infer the number of bottom colors in next span from chunk length
						int len_top = (number_4byte_chunks - 1) - len_bottom;

						// now skip the v pointer past the data to the beginning of the next span
						pos += (int)view.Read<int8_t>(pos) * 4;

						int bottom_color_end = view.Read<int8_t>(pos + 3); // aka air start
						int bottom_color_start = bottom_color_end - len_top;

						for (z = bottom_color_start; z < bottom_color_end; z++) {
							uint32_t col = swapColorMap(view.Read<uint32_t>(colorOffset));
							map->Set(x, y, z, true, col, true);
							colorOffset += 4;
						}

						if (bottom_color_end == DefaultDepth - 1)
							map->Set(x, y, DefaultDepth - 1, true, map->GetColor(x, y, DefaultDepth - 2), true);
					}

					if (onProgress)
						onProgress(x + y * DefaultHeight + 1);
				}
			}

			return std::move(map).Unmanage();
		}
	} // namespace client
} // namespace spades