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

#include <Core/RefCountedObject.h>
#include <Gui/UI/Editor/Voxel/VoxelDocument.h>

namespace spades {
	class VoxelModel;

	namespace gui {
		/**
		 * A KV6 voxel model as a VoxelEditor's document. The volume grows to hold
		 * what is placed past it and trims to the voxels after a removal, up to
		 * the size a KV6 can hold, and it always keeps at least one voxel.
		 */
		class KV6Document final : public IVoxelDocument {
		public:
			/** The largest volume a model may grow to. A KV6 column is a 64-bit
			 *  mask, so no model can be deeper than 64 voxels. */
			static constexpr int kMaxWidth = 4096;
			static constexpr int kMaxHeight = 4096;
			static constexpr int kMaxDepth = 64;

			KV6Document();

			/** Replaces the model; the document takes it over. */
			void Assign(Handle<VoxelModel> model);
			/** The model itself, to render and save. Voxels are written through
			 *  Write, which keeps the voxel count. */
			VoxelModel& Model() { return *model; }
			const VoxelModel& Model() const { return *model; }
			int VoxelCount() const { return voxelCount; }

			// IVoxelDocument
			std::string Noun() const override { return "model"; }
			IntVector3 Size() const override;
			bool Resizable() const override { return true; }
			IntVector3 MaxSize() const override;
			void Reframe(const IntVector3& size, const IntVector3& shift) override;
			bool IsSolid(int x, int y, int z) const override;
			std::uint32_t Color(int x, int y, int z) const override;
			void Write(int x, int y, int z, bool solid, std::uint32_t color) override;
			int RemovableVoxels() const override { return voxelCount - 1; }
			std::string RemovalLimitMessage() const override {
				return "A model keeps at least one voxel";
			}
			Vector3 Origin() const override;
			void SetOrigin(const Vector3& origin) override;
			std::uint64_t Version() const override { return version; }

		private:
			Handle<VoxelModel> model;
			int voxelCount = 0;
			std::uint64_t version = 0;
		};
	} // namespace gui
} // namespace spades
