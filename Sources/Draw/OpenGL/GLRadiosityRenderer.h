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

#include <atomic>
#include <mutex>
#include <cstdint>
#include <vector>

#include "IGLDevice.h"
#include <Core/Debug.h>
#include <Core/Math.h>

namespace spades {
	namespace client {
		class GameMap;
	}
	namespace draw {
		class GLRenderer;
		class IGLDevice;
		class GLSettings;
		class GLRadiosityRenderer {

			typedef uint32_t VoxelType;

			class UpdateDispatch;
			enum { ChunkSize = 16, ChunkSizeBits = 4, Envelope = 6 };
			GLRenderer &renderer;
			IGLDevice &device;
			GLSettings &settings;
			client::GameMap *map;

			struct Chunk {
				int cx, cy, cz;
				VoxelType dataFlat[ChunkSize][ChunkSize][ChunkSize];
				VoxelType dataX[ChunkSize][ChunkSize][ChunkSize];
				VoxelType dataY[ChunkSize][ChunkSize][ChunkSize];
				VoxelType dataZ[ChunkSize][ChunkSize][ChunkSize];
				// What changed since it was last computed: written by the map's edits
				// on the render thread and taken by the worker, under `dirtyMutex`.
				bool dirty = true;
				int dirtyMinX = 0, dirtyMaxX = ChunkSize - 1;
				int dirtyMinY = 0, dirtyMaxY = ChunkSize - 1;
				int dirtyMinZ = 0, dirtyMaxZ = ChunkSize - 1;

				// Who has the data: the render thread, to upload it, while false; the
				// worker, to compute it again, once the upload is done.
				std::atomic<bool> transferDone{true};
			};

			IGLDevice::UInteger textureFlat;
			IGLDevice::UInteger textureX;
			IGLDevice::UInteger textureY;
			IGLDevice::UInteger textureZ;

			int w, h, d;
			int chunkW, chunkH, chunkD;

			std::vector<Chunk> chunks;

			/** Guards every chunk's dirty state. */
			std::mutex dirtyMutex;

			/** The part of a chunk to compute again. */
			struct DirtyRange {
				int minX, maxX, minY, maxY, minZ, maxZ;
			};

			/** Takes what of a chunk changed for the worker to compute it again, if
			 * anything did and its last result is uploaded, and marks it clean: a
			 * change made meanwhile marks it dirty again. */
			bool TakeDirtyRange(Chunk&, DirtyRange&);

			inline Chunk &GetChunk(int cx, int cy, int cz) {
				SPAssert(cx >= 0);
				SPAssert(cx < chunkW);
				SPAssert(cy >= 0);
				SPAssert(cy < chunkH);
				SPAssert(cz >= 0);
				SPAssert(cz < chunkD);
				return chunks[(cx + cy * chunkW) * chunkD + cz];
			}

			inline Chunk &GetChunkWrapped(int cx, int cy, int cz) {
				// FIXME: support for non-POT dimensions?
				return GetChunk(cx & (chunkW - 1), cy & (chunkH - 1), cz);
			}

			void Invalidate(int minX, int minY, int minZ, int maxX, int maxY, int maxZ);

			void UpdateChunk(int cx, int cy, int cz);
			/** Computes some dirty chunks again, nearest `eye` first. */
			void UpdateDirtyChunks(const Vector3& eye);
			int GetNumDirtyChunks();

			uint32_t EncodeValue(Vector3 vec);
			float CompressDynamicRange(float v);

			UpdateDispatch *dispatch;

		public:
			struct Result {
				Vector3 base, x, y, z;
			};

			GLRadiosityRenderer(GLRenderer &renderer, client::GameMap *map);
			~GLRadiosityRenderer();

			Result Evaluate(IntVector3);

			void GameMapChanged(int x, int y, int z, client::GameMap *);

			void Update();

			IGLDevice::UInteger GetTextureFlat() { return textureFlat; }
			IGLDevice::UInteger GetTextureX() { return textureX; }
			IGLDevice::UInteger GetTextureY() { return textureY; }
			IGLDevice::UInteger GetTextureZ() { return textureZ; }
		};
	} // namespace draw
} // namespace spades
