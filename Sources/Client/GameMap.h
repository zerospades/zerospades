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

#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <mutex>

#include <Core/Debug.h>
#include <Core/Math.h>

#include "IGameMapListener.h"
#include <Core/RefCountedObject.h>

namespace spades {
	class IStream;
	namespace client {
		class GameMap : public RefCountedObject {
		protected:
			~GameMap();

		public:
			// fixed for now
			enum {
				DefaultWidth = 512,
				DefaultHeight = 512,
				DefaultDepth = 64 // should be <= 64
			};
			GameMap();

			/**
			 * Construct a `GameMap` from VOXLAP5 terrain data supplied by the specified stream.
			 *
			 * @param onProgress Called whenever a new column (a set of voxels with the same X and Y
			 *                   coordinates) is loaded from the stream. The parameter indicates
			 *					 the number of columns loaded
			 *					 (up to `DefaultWidth * DefaultHeight`).
			 */
			static GameMap* Load(IStream*, std::function<void(int)> onProgress = {});

			void Save(IStream*);

			int Width() const { return DefaultWidth; }
			int Height() const { return DefaultHeight; }
			int Depth() const { return DefaultDepth; }
			int GroundDepth() const { return DefaultDepth - 2; }
			int GetTop(int x, int y) const;

			inline bool IsValidMapCoord(const int x, const int y, const int z) const {
				return x >= 0 && y >= 0 && z >= 0 && x < Width() && y < Height() && z < Depth();
			}
			inline bool IsValidBuildCoord(const IntVector3& v) const {
				return IsValidMapCoord(v.x, v.y, v.z) && v.z < GroundDepth();
			}

			inline uint64_t GetSolidMap(int x, int y) const { return solidMap[x][y]; }
			inline uint64_t GetSolidMapWrapped(int x, int y) const {
				return GetSolidMap(x & (Width() - 1), y & (Height() - 1));
			}

			inline bool IsSolid(int x, int y, int z) const {
				SPAssert(IsValidMapCoord(x, y, z));
				return ((GetSolidMap(x, y) >> (uint64_t)z) & 1ULL) != 0;
			}

			inline bool IsSolidWrapped(int x, int y, int z) const {
				if (z < 0)
					return false;
				if (z >= Depth())
					return true;
				return ((GetSolidMapWrapped(x, y) >> (uint64_t)z) & 1ULL) != 0;
			}

			inline bool HasNeighbors(int x, int y, int z) const {
				return IsSolidWrapped(x + 1, y, z) 
					|| IsSolidWrapped(x - 1, y, z) 
					|| IsSolidWrapped(x, y + 1, z) 
					|| IsSolidWrapped(x, y - 1, z) 
					|| IsSolidWrapped(x, y, z + 1) 
					|| IsSolidWrapped(x, y, z - 1);
			}
			inline bool HasNeighbors(const IntVector3& v) const {
				return HasNeighbors(v.x, v.y, v.z);
			}

			inline bool IsSurface(int x, int y, int z) const {
				if (!IsSolid(x, y, z))
					return false;
				if (z == 0)
					return true;
				if (x > 0 && !IsSolid(x - 1, y, z))
					return true;
				if (x < Width() - 1 && !IsSolid(x + 1, y, z))
					return true;
				if (y > 0 && !IsSolid(x, y - 1, z))
					return true;
				if (y < Height() - 1 && !IsSolid(x, y + 1, z))
					return true;
				if (!IsSolid(x, y, z - 1))
					return true;
				if (z < Depth() - 1 && !IsSolid(x, y, z + 1))
					return true;
				return false;
			}

			/** @return 0xHHBBGGRR where HH is health (up to 100) */
			inline uint32_t GetColor(int x, int y, int z) const {
				SPAssert(IsValidMapCoord(x, y, z));
				return colorMap[x][y][z];
			}

			inline uint32_t GetColorWrapped(int x, int y, int z) const {
				return colorMap[x & (Width() - 1)][y & (Height() - 1)][z & (Depth() - 1)];
			}

			inline void Set(int x, int y, int z, bool solid, uint32_t color, bool unsafe = false) {
				SPAssert(IsValidMapCoord(x, y, z));

				uint64_t mask = 1ULL << z;
				uint64_t value = GetSolidMap(x, y);

				bool changed = false;
				if ((value & mask) != (solid ? mask : 0ULL)) {
					changed = true;
					value &= ~mask;
					if (solid)
						value |= mask;
					solidMap[x][y] = value;
				}

				if (solid && color != colorMap[x][y][z]) {
					changed = true;
					colorMap[x][y][z] = color;
				}

				if (!unsafe && changed) {
					std::lock_guard<std::mutex> guard{listenersMutex};
					for (auto* l : listeners)
						l->GameMapChanged(x, y, z, this);
				}
			}

			void AddListener(IGameMapListener*);
			void RemoveListener(IGameMapListener*);

			bool ClipBox(int x, int y, int z) const;
			bool ClipWorld(int x, int y, int z) const;
			bool ClipBox(float x, float y, float z) const;
			bool ClipWorld(float x, float y, float z) const;

			// vanila compat
			bool CastRay(Vector3 v0, Vector3 v1, float length, IntVector3& vOut) const;

			// accurate and slow ray casting
			struct RayCastResult {
				bool hit;
				bool startSolid;
				Vector3 hitPos;
				IntVector3 hitBlock;
				IntVector3 normal;
			};
			RayCastResult CastRay2(Vector3 v0, Vector3 dir, int maxSteps) const;

			// adapted from VOXLAP5.C by Ken Silverman <https://advsys.net/ken/>
			// https://github.com/Ericson2314/Voxlap/blob/no-asm/source/voxlap5.cpp#L454
			uint32_t gkrand = 0;
			inline uint32_t GetColorJit(uint32_t col, uint32_t amount = 0x70707) {
				gkrand = 0x1A4E86D * gkrand + 1;
				return col ^ (gkrand & amount);
			}

			// adapted from GAME.C by Ken Silverman <https://advsys.net/ken/>
			// https://github.com/Ericson2314/Voxlap/blob/no-asm/source/game.cpp#L329
			uint32_t groundColors[9] = {
				0x506050, 0x605848, 0x705040,
				0x804838, 0x704030, 0x603828,
				0x503020, 0x402818, 0x302010
			};
			inline uint32_t GetDirtColor(int x, int y, int z) {
				const int layer = z >> 3; // vertical layer
				uint32_t i = groundColors[layer];
				uint32_t j = groundColors[layer + 1];

				// interpolate between current and next layer color
				const int frac = z & 7;
				int rb = (i & 0xFF00FF) + ((((j & 0xFF00FF) - (i & 0xFF00FF)) * frac) >> 3);
				int g = (i & 0xFF00) + ((((j & 0xFF00) - (i & 0xFF00)) * frac) >> 3);
				i = (rb & 0xFF00FF) | (g & 0xFF00);

				// add corner darkening
				int dx = abs((x & 7) - 4);
				int dy = abs((y & 7) - 4);
				int dz = abs((z & 7) - 4);
				i += 4 * ((dx << 16) + (dy << 8) + dz);

				// add subtle noise
				i += 0x10101 * static_cast<uint32_t>(SampleRandom() & 7);

				return i;
			}

			/**
			 * Returns a new GameMap with identical voxel data but no listeners.
			 */
			Handle<GameMap> Clone() const;

		private:
			struct NoInit {};
			explicit GameMap(NoInit) {}

			uint64_t solidMap[DefaultWidth][DefaultHeight];
			uint32_t colorMap[DefaultWidth][DefaultHeight][DefaultDepth];
			std::list<IGameMapListener*> listeners;
			std::mutex listenersMutex;
		};
	} // namespace client
} // namespace spades