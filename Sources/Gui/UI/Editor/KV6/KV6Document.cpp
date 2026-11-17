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

#include "KV6Document.h"

#include <Core/Debug.h>
#include <Core/VoxelModel.h>

namespace spades {
	namespace gui {
		KV6Document::KV6Document() : model(Handle<VoxelModel>::New(1, 1, 1)) {}

		void KV6Document::Assign(Handle<VoxelModel> m) {
			SPAssert(m);
			model = std::move(m);
			voxelCount = 0;
			for (int x = 0; x < model->GetWidth(); x++)
				for (int y = 0; y < model->GetHeight(); y++)
					for (int z = 0; z < model->GetDepth(); z++) {
						if (model->IsSolid(x, y, z))
							voxelCount++;
					}
			version++;
		}

		IntVector3 KV6Document::Size() const {
			return MakeIntVector3(model->GetWidth(), model->GetHeight(), model->GetDepth());
		}

		IntVector3 KV6Document::MaxSize() const {
			return MakeIntVector3(kMaxWidth, kMaxHeight, kMaxDepth);
		}

		void KV6Document::Reframe(const IntVector3& size, const IntVector3& shift) {
			Handle<VoxelModel> dst = Handle<VoxelModel>::New(size.x, size.y, size.z);
			for (int x = 0; x < model->GetWidth(); x++)
				for (int y = 0; y < model->GetHeight(); y++)
					for (int z = 0; z < model->GetDepth(); z++) {
						if (model->IsSolid(x, y, z))
							dst->SetSolid(x + shift.x, y + shift.y, z + shift.z,
							              model->GetColor(x, y, z));
					}
			// The pivot stays on the same voxels.
			dst->SetOrigin(model->GetOrigin() -
			               MakeVector3(float(shift.x), float(shift.y), float(shift.z)));
			model = dst;
			version++;
		}

		bool KV6Document::IsSolid(int x, int y, int z) const { return model->IsSolid(x, y, z); }

		std::uint32_t KV6Document::Color(int x, int y, int z) const {
			return model->GetColor(x, y, z) & 0xFFFFFF;
		}

		void KV6Document::Write(int x, int y, int z, bool solid, std::uint32_t color) {
			const bool was = model->IsSolid(x, y, z);
			if (solid)
				model->SetSolid(x, y, z, color);
			else
				model->SetAir(x, y, z);
			if (was && !solid)
				voxelCount--;
			else if (!was && solid)
				voxelCount++;
			version++;
		}

		Vector3 KV6Document::Origin() const { return model->GetOrigin(); }

		void KV6Document::SetOrigin(const Vector3& origin) {
			model->SetOrigin(origin);
			version++; // the render model bakes the origin in
		}
	} // namespace gui
} // namespace spades
