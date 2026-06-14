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
#include <cstring>
#include <vector>

#include "Debug.h"
#include "Exception.h"
#include "FileManager.h"
#include "VoxelModel.h"
#include <ScriptBindings/ScriptManager.h>

namespace spades {
	VoxelModel::VoxelModel(int w, int h, int d) {
		SPADES_MARK_FUNCTION();

		if (w < 1 || h < 1 || d < 1 || w > 4096 || h > 4096)
			SPRaise("Invalid dimension: %dx%dx%d", w, h, d);

		width = w;
		height = h;
		depth = d;
		origin = MakeVector3(0, 0, 0);

		if (d > 64)
			SPRaise("Voxel model with depth > 64 is not supported.");

		// TODO: `stmp::make_unique` doesn't support `T[]` yet
		solidBits.reset(new uint64_t[w * h]);
		colors.reset(new uint32_t[w * h * d]);

		std::fill(solidBits.get(), solidBits.get() + w * h, 0);
	}
	VoxelModel::~VoxelModel() { SPADES_MARK_FUNCTION(); }

	void VoxelModel::ThrowInvalidSpan [[noreturn]] (int x, int y) const {
		SPRaise("Span (%d, %d, :) is out of bounds of voxel model of size %dx%dx%d", x, y, width,
		        height, depth);
	}

	void VoxelModel::ThrowInvalidPoint [[noreturn]] (int x, int y, int z) const {
		SPRaise("Point (%d, %d, %d) is out of bounds of voxel model of size %dx%dx%d", x, y, z,
		        width, height, depth);
	}

	void VoxelModel::ForceMaterial(MaterialType newMaterialId) {
		int count = width * height * depth;
		for (int i = 0; i < count; ++i)
			colors[i] = (colors[i] & 0xFFFFFF) | (static_cast<uint32_t>(newMaterialId) << 24);
	}

	namespace {
		struct KV6Block {
			uint32_t color;
			uint16_t zPos;
			uint8_t visFaces, lighting;
		};

		struct KV6Header {
			uint32_t xsiz, ysiz, zsiz;
			float xpivot, ypivot, zpivot;
			uint32_t blklen;
		};
	} // namespace

	void VoxelModel::HollowFill() {
		std::vector<IntVector3> stack;
		std::vector<uint8_t> flags;
		flags.resize(width * height * depth);
		std::memset(flags.data(), 0, flags.size());

		stack.reserve(width * height * depth);

		auto Flag = [&](int x, int y, int z) -> uint8_t& {
			return flags[x + width * (y + height * z)];
		};

		for (int x = 0; x < width; x++) {
			for (int y = 0; y < height; y++) {
				auto m = GetSolidBitsAt(x, y);
				for (int z = 0; z < depth; z++) {
					if (m & (1ULL << z))
						Flag(x, y, z) = 1;
				}

				if (!IsSolid(x, y, 0)) {
					stack.push_back(MakeIntVector3(x, y, 0));
					Flag(x, y, 0) = 1;
				}
				if (!IsSolid(x, y, depth - 1)) {
					stack.push_back(MakeIntVector3(x, y, depth - 1));
					Flag(x, y, depth - 1) = 1;
				}
			}

			for (int z = 1; z < depth - 1; z++) {
				if (!IsSolid(x, 0, z)) {
					stack.push_back(MakeIntVector3(x, 0, z));
					Flag(x, 0, z) = 1;
				}
				if (!IsSolid(x, height - 1, z)) {
					stack.push_back(MakeIntVector3(x, height - 1, z));
					Flag(x, height - 1, z) = 1;
				}
			}
		}

		for (int y = 1; y < height - 1; y++)
		for (int z = 1; z < depth - 1; z++) {
			if (!IsSolid(0, y, z)) {
				stack.push_back(MakeIntVector3(0, y, z));
				Flag(0, y, z) = 1;
			}
			if (!IsSolid(width - 1, y, z)) {
				stack.push_back(MakeIntVector3(width - 1, y, z));
				Flag(width - 1, y, z) = 1;
			}
		}

		while (!stack.empty()) {
			auto v = stack.back();
			stack.pop_back();

			auto Visit = [&](int x, int y, int z) {
				SPAssert(x >= 0);
				SPAssert(x < width);
				SPAssert(y >= 0);
				SPAssert(y < height);
				SPAssert(z >= 0);
				SPAssert(z < depth);
				Flag(x, y, z) = 1;
				stack.push_back(MakeIntVector3(x, y, z));
			};

			if (v.x > 0 && !Flag(v.x - 1, v.y, v.z))
				Visit(v.x - 1, v.y, v.z);
			if (v.x < width - 1 && !Flag(v.x + 1, v.y, v.z))
				Visit(v.x + 1, v.y, v.z);
			if (v.y > 0 && !Flag(v.x, v.y - 1, v.z))
				Visit(v.x, v.y - 1, v.z);
			if (v.y < height - 1 && !Flag(v.x, v.y + 1, v.z))
				Visit(v.x, v.y + 1, v.z);
			if (v.z > 0 && !Flag(v.x, v.y, v.z - 1))
				Visit(v.x, v.y, v.z - 1);
			if (v.z < depth - 1 && !Flag(v.x, v.y, v.z + 1))
				Visit(v.x, v.y, v.z + 1);
		}

		for (int z = 0, idx = 0; z < depth; z++)
		for (int y = 0; y < height; y++)
		for (int x = 0; x < width; x++, idx++) {
			if (!flags[idx]) {
				SPAssert(!IsSolid(x, y, z));
				SetSolid(x, y, z, 0xDDBEEF);
			}
		}

#ifndef NDEBUG
		for (int z = 0, idx = 0; z < depth; z++)
		for (int y = 0; y < height; y++)
		for (int x = 0; x < width; x++, idx++) {
			if (!flags[idx]) {
				SPAssert(IsSolid(x, y, z));
				SPAssert(IsSolid(x + 1, y, z));
				SPAssert(IsSolid(x - 1, y, z));
				SPAssert(IsSolid(x, y + 1, z));
				SPAssert(IsSolid(x, y - 1, z));
				SPAssert(IsSolid(x, y, z + 1));
				SPAssert(IsSolid(x, y, z - 1));
			}
		}
#endif
	}

	Handle<VoxelModel> VoxelModel::LoadKV6(IStream& stream) {
		SPADES_MARK_FUNCTION();

		if (stream.Read(4) != "Kvxl")
			SPRaise("Invalid magic");

		KV6Header header;
		if (stream.Read(&header, sizeof(header)) < sizeof(header))
			SPRaise("File truncated: failed to read header");

		std::vector<KV6Block> blkdata;
		blkdata.resize(header.blklen);

		if (stream.Read(blkdata.data(), sizeof(KV6Block) * header.blklen) <
		    sizeof(KV6Block) * header.blklen)
			SPRaise("File truncated: failed to read blocks");

		std::vector<uint32_t> xoffset;
		xoffset.resize(header.xsiz);

		if (stream.Read(xoffset.data(), sizeof(uint32_t) * header.xsiz) <
		    sizeof(uint32_t) * header.xsiz)
			SPRaise("File truncated: failed to read xoffset");

		std::vector<uint16_t> xyoffset;
		xyoffset.resize(header.xsiz * header.ysiz);

		if (stream.Read(xyoffset.data(), sizeof(uint16_t) * header.xsiz * header.ysiz) <
		    sizeof(uint16_t) * header.xsiz * header.ysiz)
			SPRaise("File truncated: failed to read xyoffset");

		// validate: zpos < depth
		for (size_t i = 0; i < blkdata.size(); i++) {
			if (blkdata[i].zPos >= header.zsiz)
				SPRaise("File corrupted: blkData[i].zPos >= header.zsiz");
		}

		// validate sum(xyoffset) = blkLen
		{
			uint64_t ttl = 0;
			for (size_t i = 0; i < xyoffset.size(); i++)
				ttl += (uint32_t)xyoffset[i];
			if (ttl != (uint64_t)blkdata.size())
				SPRaise("File corrupted: sum(xyoffset) != blkdata.size()");
		}

		auto model = Handle<VoxelModel>::New(header.xsiz, header.ysiz, header.zsiz);
		model->SetOrigin(MakeVector3(-header.xpivot, -header.ypivot, -header.zpivot));

		int pos = 0;
		for (int x = 0; x < (int)header.xsiz; x++)
			for (int y = 0; y < (int)header.ysiz; y++) {
				int spanBlocks = (int)xyoffset[x * header.ysiz + y];
				int lastZ = -1;
				while (spanBlocks--) {
					const KV6Block& b = blkdata[pos];
					if (model->IsSolid(x, y, b.zPos))
						SPRaise("Duplicate voxel (%d, %d, %d)", x, y, b.zPos);
					if (b.zPos <= lastZ)
						SPRaise("Not Z-sorted");
					lastZ = b.zPos;
					model->SetSolid(x, y, b.zPos, swapColor(b.color));
					pos++;
				}
			}

		SPAssert(pos == blkdata.size());
		model->HollowFill();
		return model;
	}

	void VoxelModel::SaveKV6(IStream& stream) const {
		SPADES_MARK_FUNCTION();

		// Visibility (cull) flags for a voxel face. The loader ignores these and
		// recomputes its own meshing, but external tools rely on them, so we emit
		// the standard "face is visible when the neighbouring voxel is air" set.
		auto VisFaces = [this](int x, int y, int z) -> uint8_t {
			uint8_t f = 0;
			if (!IsSolid(x - 1, y, z)) f |= 0x01; // -x
			if (!IsSolid(x + 1, y, z)) f |= 0x02; // +x
			if (!IsSolid(x, y - 1, z)) f |= 0x04; // -y
			if (!IsSolid(x, y + 1, z)) f |= 0x08; // +y
			if (!IsSolid(x, y, z - 1)) f |= 0x10; // -z
			if (!IsSolid(x, y, z + 1)) f |= 0x20; // +z
			return f;
		};

		// Gather surface voxels grouped by (x, y) column, z-ascending. Fully
		// enclosed voxels (no visible face) are skipped: they are reconstructed
		// by `HollowFill` on load, which also keeps the `0xDDBEEF` interior
		// sentinels from leaking back into the file.
		std::vector<KV6Block> blocks;
		std::vector<uint16_t> xyoffset(width * height, 0);
		std::vector<uint32_t> xoffset(width, 0);

		for (int x = 0; x < width; x++) {
			for (int y = 0; y < height; y++) {
				uint64_t bits = GetSolidBitsAtUnchecked(x, y);
				uint16_t spanCount = 0;
				for (int z = 0; z < depth; z++) {
					if (!((bits >> z) & 1))
						continue;
					uint8_t vis = VisFaces(x, y, z);
					if (vis == 0)
						continue;

					KV6Block b;
					// `swapColor` is its own inverse for the RGB bytes; the high
					// byte is conventionally 128 in KV6 files.
					b.color = swapColor(GetColorUnchecked(x, y, z) & 0xFFFFFF) | (128u << 24);
					b.zPos = static_cast<uint16_t>(z);
					b.visFaces = vis;
					b.lighting = 0;
					blocks.push_back(b);
					spanCount++;
				}
				xyoffset[x * height + y] = spanCount;
				xoffset[x] += spanCount;
			}
		}

		KV6Header header;
		header.xsiz = static_cast<uint32_t>(width);
		header.ysiz = static_cast<uint32_t>(height);
		header.zsiz = static_cast<uint32_t>(depth);
		// The loader sets `origin = -pivot`, so invert the relationship here.
		header.xpivot = -origin.x;
		header.ypivot = -origin.y;
		header.zpivot = -origin.z;
		header.blklen = static_cast<uint32_t>(blocks.size());

		stream.Write("Kvxl", 4);
		stream.Write(&header, sizeof(header));
		if (!blocks.empty())
			stream.Write(blocks.data(), sizeof(KV6Block) * blocks.size());
		stream.Write(xoffset.data(), sizeof(uint32_t) * xoffset.size());
		stream.Write(xyoffset.data(), sizeof(uint16_t) * xyoffset.size());
		stream.Flush();
	}
} // namespace spades
