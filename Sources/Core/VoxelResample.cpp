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

#include "VoxelResample.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <limits>

#include "VoxelBlend.h"
#include "VoxelModel.h"
#include "VoxelModelLoader.h"

namespace spades {
	namespace {
		// What a KV6 holds: a column is a 64-bit mask.
		constexpr int kMaxAcross = 4096;
		constexpr int kMaxDeep = 64;

		bool ParseCount(const std::string& text, int& out) {
			if (text.empty() || text.size() > 3)
				return false;
			for (char c : text)
				if (c < '0' || c > '9')
					return false;
			out = std::atoi(text.c_str());
			return out >= 1 && out <= VoxelResolution::kMaxStep;
		}

		// An object holding voxels, where they land in the scene.
		struct Placed {
			const VoxelModel* model;
			Matrix4 world;
		};

		void Collect(const VoxelObject& o, const Matrix4& parent, std::vector<Placed>& out) {
			if (o.hidden)
				return; // out of sight, with everything under it
			const Matrix4 world = parent * LocalTransform(o.position, o.rotation, o.scale);
			if (o.model)
				out.push_back(Placed{o.model.GetPointerOrNull(), world});
			for (const VoxelObject& child : o.children)
				Collect(child, world, out);
		}

		// The box a model's voxels fill under `world`, from voxel edge to edge.
		void Bounds(const Placed& p, Vector3& lo, Vector3& hi) {
			lo = MakeVector3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
			                 std::numeric_limits<float>::max());
			hi = lo * -1.0F;
			const VoxelModel& m = *p.model;
			for (int c = 0; c < 8; c++) {
				const Vector3 corner =
				  MakeVector3((c & 1) ? float(m.GetWidth()) - 0.5F : -0.5F,
				              (c & 2) ? float(m.GetHeight()) - 0.5F : -0.5F,
				              (c & 4) ? float(m.GetDepth()) - 0.5F : -0.5F);
				const Vector3 q = (p.world * MakeVector4(corner.x, corner.y, corner.z, 1.0F)).GetXYZ();
				lo = MakeVector3(std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z));
				hi = MakeVector3(std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z));
			}
		}

		std::string Size(const IntVector3& s) {
			return std::to_string(s.x) + " x " + std::to_string(s.y) + " x " + std::to_string(s.z);
		}

		bool Holdable(const IntVector3& size, std::string& why) {
			if (size.z > kMaxDeep) {
				why = Size(size) + " is deeper than the " + std::to_string(kMaxDeep) +
				      " voxels a KV6 holds";
				return false;
			}
			if (size.x > kMaxAcross || size.y > kMaxAcross) {
				why = Size(size) + " is wider than the " + std::to_string(kMaxAcross) +
				      " voxels a KV6 holds";
				return false;
			}
			return true;
		}

		// The solid voxels' box in `m`, false when there is none.
		bool SolidBox(const VoxelModel& m, IntVector3& lo, IntVector3& hi) {
			bool any = false;
			for (int x = 0; x < m.GetWidth(); x++)
				for (int y = 0; y < m.GetHeight(); y++)
					for (int z = 0; z < m.GetDepth(); z++) {
						if (!m.IsSolid(x, y, z))
							continue;
						const IntVector3 v = MakeIntVector3(x, y, z);
						lo = any ? MakeIntVector3(std::min(lo.x, x), std::min(lo.y, y), std::min(lo.z, z))
						         : v;
						hi = any ? MakeIntVector3(std::max(hi.x, x), std::max(hi.y, y), std::max(hi.z, z))
						         : v;
						any = true;
					}
			return any;
		}

		// `n` times finer: each voxel of the result samples the object its
		// middle falls in, through that object's own transform.
		Handle<VoxelModel> Finer(const std::vector<VoxelObject>& objects, int n, Matrix4& frame,
		                         std::string& why) {
			std::vector<Placed> placed;
			for (const VoxelObject& o : objects)
				Collect(o, Matrix4::Identity(), placed);
			if (placed.empty()) {
				why = "it shows no voxels";
				return Handle<VoxelModel>();
			}
			// The result's voxel `t` spans the scene's [t/n - 0.5, (t+1)/n - 0.5)
			// on each axis, so at 1/1 it would be the scene's voxel `t`.
			const float f = float(n);
			auto first = [&](float lo) { return int(std::floor((lo + 0.5F) * f)); };
			auto last = [&](float hi) { return int(std::ceil((hi + 0.5F) * f)) - 1; };
			IntVector3 tlo = MakeIntVector3(std::numeric_limits<int>::max(),
			                                std::numeric_limits<int>::max(),
			                                std::numeric_limits<int>::max());
			IntVector3 thi = tlo * -1;
			for (const Placed& p : placed) {
				Vector3 lo, hi;
				Bounds(p, lo, hi);
				tlo = MakeIntVector3(std::min(tlo.x, first(lo.x)), std::min(tlo.y, first(lo.y)),
				                     std::min(tlo.z, first(lo.z)));
				thi = MakeIntVector3(std::max(thi.x, last(hi.x)), std::max(thi.y, last(hi.y)),
				                     std::max(thi.z, last(hi.z)));
			}
			const IntVector3 span = thi - tlo + MakeIntVector3(1, 1, 1);
			// The volumes' box: the voxels in it may come to less, trimmed below.
			if (span.z > kMaxDeep * 4 || span.x > kMaxAcross || span.y > kMaxAcross) {
				Holdable(span, why);
				return Handle<VoxelModel>();
			}
			// Sampled column by column into a box of any depth, then trimmed.
			std::vector<std::uint32_t> colors(std::size_t(span.x) * std::size_t(span.y) *
			                                  std::size_t(span.z));
			std::vector<bool> solid(colors.size(), false);
			auto at = [&](int x, int y, int z) {
				return (std::size_t(z) * std::size_t(span.y) + std::size_t(y)) * std::size_t(span.x) +
				       std::size_t(x);
			};
			for (const Placed& p : placed) {
				const VoxelModel& m = *p.model;
				const Matrix4 back = p.world.Inversed();
				Vector3 lo, hi;
				Bounds(p, lo, hi);
				for (int tz = first(lo.z); tz <= last(hi.z); tz++)
					for (int ty = first(lo.y); ty <= last(hi.y); ty++)
						for (int tx = first(lo.x); tx <= last(hi.x); tx++) {
							const Vector4 middle =
							  MakeVector4((float(tx) + 0.5F) / f - 0.5F, (float(ty) + 0.5F) / f - 0.5F,
							              (float(tz) + 0.5F) / f - 0.5F, 1.0F);
							const Vector3 s = (back * middle).GetXYZ();
							const int sx = int(std::floor(s.x + 0.5F));
							const int sy = int(std::floor(s.y + 0.5F));
							const int sz = int(std::floor(s.z + 0.5F));
							if (sx < 0 || sy < 0 || sz < 0 || sx >= m.GetWidth() ||
							    sy >= m.GetHeight() || sz >= m.GetDepth() || !m.IsSolid(sx, sy, sz))
								continue;
							const std::size_t i = at(tx - tlo.x, ty - tlo.y, tz - tlo.z);
							solid[i] = true;
							colors[i] = m.GetColor(sx, sy, sz) & 0xFFFFFF;
						}
			}
			// Trimmed to what is solid.
			IntVector3 lo = MakeIntVector3(span.x, span.y, span.z), hi = MakeIntVector3(-1, -1, -1);
			for (int z = 0; z < span.z; z++)
				for (int y = 0; y < span.y; y++)
					for (int x = 0; x < span.x; x++)
						if (solid[at(x, y, z)]) {
							lo = MakeIntVector3(std::min(lo.x, x), std::min(lo.y, y), std::min(lo.z, z));
							hi = MakeIntVector3(std::max(hi.x, x), std::max(hi.y, y), std::max(hi.z, z));
						}
			if (hi.x < 0) {
				why = "nothing of it is left";
				return Handle<VoxelModel>();
			}
			const IntVector3 size = hi - lo + MakeIntVector3(1, 1, 1);
			if (!Holdable(size, why))
				return Handle<VoxelModel>();
			Handle<VoxelModel> out = Handle<VoxelModel>::New(size.x, size.y, size.z);
			for (int z = lo.z; z <= hi.z; z++)
				for (int y = lo.y; y <= hi.y; y++)
					for (int x = lo.x; x <= hi.x; x++)
						if (solid[at(x, y, z)])
							out->SetSolid(x - lo.x, y - lo.y, z - lo.z, colors[at(x, y, z)]);
			const IntVector3 t0 = tlo + lo; // the result's voxel 0, on the fine grid
			const float s = 1.0F / f;
			frame = Matrix4::Translate(MakeVector3((float(t0.x) + 0.5F) * s - 0.5F,
			                                       (float(t0.y) + 0.5F) * s - 0.5F,
			                                       (float(t0.z) + 0.5F) * s - 0.5F)) *
			        Matrix4::Scale(s);
			return out;
		}
	} // namespace

	const std::vector<VoxelResolution>& VoxelResolution::Steps() {
		static const std::vector<VoxelResolution> steps = [] {
			std::vector<VoxelResolution> list;
			for (int n = kMaxStep; n >= 2; n--)
				list.push_back(Coarser(n));
			for (int n = 1; n <= kMaxStep; n++)
				list.push_back(Finer(n));
			return list;
		}();
		return steps;
	}

	bool VoxelResolution::Parse(const std::string& text, VoxelResolution& out) {
		const std::size_t slash = text.find('/');
		int a, b;
		if (slash == std::string::npos) {
			if (!ParseCount(text, a))
				return false;
			out = Finer(a);
			return true;
		}
		if (!ParseCount(text.substr(0, slash), a) || !ParseCount(text.substr(slash + 1), b))
			return false;
		if (a != 1 && b != 1)
			return false;
		out = VoxelResolution{a, b};
		return true;
	}

	std::string VoxelResolution::Label() const {
		return std::to_string(finer) + "/" + std::to_string(coarser);
	}

	IntVector3 VoxelResolution::SizeOf(const IntVector3& size) const {
		auto side = [&](int n) { return (n * finer + coarser - 1) / coarser; };
		return MakeIntVector3(side(size.x), side(size.y), side(size.z));
	}

	Handle<VoxelModel> ResampledScene(const std::vector<VoxelObject>& objects,
	                                  const VoxelResolution& resolution, Matrix4& frame,
	                                  std::string& why) {
		why.clear();
		Handle<VoxelModel> model;
		if (resolution.finer > 1) {
			model = Finer(objects, resolution.finer, frame, why);
		} else {
			// The scene as the game composes it, then as much coarser.
			IntVector3 low;
			try {
				model = VoxelModelLoader::MergeScene(objects, &low);
			} catch (const std::exception&) {
				why = "it shows no voxels";
				return Handle<VoxelModel>();
			}
			const int n = resolution.coarser;
			if (n > 1) {
				model = DownsampledVoxelModel(*model, n);
				IntVector3 lo, hi;
				if (!SolidBox(*model, lo, hi)) {
					why = "nothing of it is left: too coarse";
					return Handle<VoxelModel>();
				}
			}
			// Block `i` holds the scene's voxels `low + i*n` to `low + i*n + n-1`.
			const float half = (float(n) - 1.0F) * 0.5F;
			frame = Matrix4::Translate(MakeVector3(float(low.x), float(low.y), float(low.z)) +
			                           MakeVector3(half, half, half)) *
			        Matrix4::Scale(float(n));
		}
		if (!model)
			return model;
		// The pivot on the scene's origin: voxel `i` lies at `frame * i`.
		const float size = resolution.VoxelSize();
		model->SetOrigin(frame.GetOrigin() / size);
		return model;
	}
} // namespace spades
