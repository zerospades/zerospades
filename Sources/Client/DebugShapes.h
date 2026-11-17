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

#include <Core/Math.h>

#include "IRenderer.h"

namespace spades {
	namespace client {
		/** Outlines `obb`'s twelve edges with the renderer's debug lines,
		 *  drawn with the scene under way. */
		inline void AddDebugOBB(IRenderer& renderer, const OBB3& obb, const Vector4& color) {
			const Matrix4& m = obb.m;
			Vector3 v[2][2][2];
			for (int x = 0; x < 2; x++)
				for (int y = 0; y < 2; y++)
					for (int z = 0; z < 2; z++)
						v[x][y][z] = (m * MakeVector3(float(x), float(y), float(z))).GetXYZ();
			for (int a = 0; a < 2; a++) {
				for (int b = 0; b < 2; b++) {
					renderer.AddDebugLine(v[0][a][b], v[1][a][b], color);
					renderer.AddDebugLine(v[a][0][b], v[a][1][b], color);
					renderer.AddDebugLine(v[a][b][0], v[a][b][1], color);
				}
			}
		}
	} // namespace client
} // namespace spades
