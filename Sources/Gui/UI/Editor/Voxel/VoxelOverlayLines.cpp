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

#include "VoxelOverlayLines.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

#include <Core/VoxelModel.h>
#include <Gui/OverlayPaint.h>
#include <Gui/UI/Editor/Shell/EditorCamera.h>

#include "VoxelDocument.h"

namespace spades {
	namespace gui {
		namespace {
			// Two-tone strokes, the way editors keep selection outlines legible
			// over any image: a line reads against light voxels by its casing and
			// against dark ones by its core. Widths in window units.
			constexpr float kCoreWidth = 2.0F;
			constexpr float kCasingWidth = 4.0F;
			constexpr float kCasingAlpha = 0.8F;
			// Where voxels hide a line it is drawn thin and dim, with no casing, so
			// it shows through without being taken for a visible edge.
			constexpr float kHiddenWidth = 1.0F;
			constexpr float kHiddenAlpha = 0.35F;
			// A line shorter than this on screen is drawn thinner, down to
			// kMinWidthScale, so a distant voxel grid does not fill in with casing.
			constexpr float kFullWidthLength = 16.0F;
			constexpr float kMinWidthScale = 0.5F;
			// Whether voxels hide a line is tested every this many window units
			// along it, at least once and at most kMaxSamples times.
			constexpr float kSampleSpacing = 6.0F;
			constexpr int kMaxSamples = 256;
			// How far short of a line point its occlusion ray stops, in world units,
			// so the voxel whose edge the line follows never hides it.
			constexpr float kOcclusionSlack = 0.01F;

			float Clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

			// Cut segment a-b down to the part on the side of a plane through the eye
			// where `In` is positive. False when nothing is left.
			template <class In> bool ClipToPlane(In in, Vector3& a, Vector3& b) {
				const float ia = in(a);
				const float ib = in(b);
				if (ia < 0.0F && ib < 0.0F)
					return false;
				if (ia < 0.0F)
					a = a + (b - a) * (ia / (ia - ib));
				else if (ib < 0.0F)
					b = b + (a - b) * (ib / (ib - ia));
				return true;
			}

			// Cut segment a-b down to the part within `margin` window units of the
			// viewport, the near plane included. False when nothing is left, so a
			// line's screen length, and the work spent along it, covers what shows.
			bool ClipToView(const GizmoView& view, float margin, Vector3& a, Vector3& b) {
				const Vector3 eye = view.eye;
				if (!ClipToPlane(
				      [&](const Vector3& p) { return view.Depth(p) - EditorCamera::kNearPlane; }, a,
				      b))
					return false;
				// The sides, widened by the margin, as planes through the eye: a point
				// is inside when its offset across the view is within the frustum's
				// half-width at its depth.
				const float halfWidth =
				  view.tanHalfFovX * (1.0F + 2.0F * margin / view.viewportWidth);
				const float halfHeight =
				  view.tanHalfFovY * (1.0F + 2.0F * margin / view.viewportHeight);
				const Vector3 sides[4] = {view.right, view.right * -1.0F, view.up, view.up * -1.0F};
				for (int k = 0; k < 4; k++) {
					const Vector3& across = sides[k];
					const float half = (k < 2) ? halfWidth : halfHeight;
					if (!ClipToPlane(
					      [&](const Vector3& p) {
						      const Vector3 rel = p - eye;
						      return Vector3::Dot(rel, view.forward) * half - Vector3::Dot(rel, across);
					      },
					      a, b))
						return false;
				}
				return true;
			}

			// Whether two views would put everything in the same place on screen.
			bool SameView(const GizmoView& a, const GizmoView& b) {
				return a.eye == b.eye && a.right == b.right && a.up == b.up &&
				       a.forward == b.forward && a.tanHalfFovX == b.tanHalfFovX &&
				       a.tanHalfFovY == b.tanHalfFovY && a.viewportX == b.viewportX &&
				       a.viewportY == b.viewportY && a.viewportWidth == b.viewportWidth &&
				       a.viewportHeight == b.viewportHeight;
			}

			// Segment a-b lengthened by `reach` at both ends, so strokes meeting at a
			// corner overlap there instead of leaving a notch.
			void Lengthen(Vector2& a, Vector2& b, float reach) {
				const Vector2 d = b - a;
				const float length = d.GetLength();
				if (length <= 0.0F)
					return;
				const Vector2 step = d * (reach / length);
				a -= step;
				b += step;
			}

			// The voxels hiding the lines, walked along a ray.
			class OcclusionTest {
			public:
				explicit OcclusionTest(const VoxelOverlayLines::Occluders& o)
				    : document(*o.document), pending(o.pending), pendingAnchor(o.pendingAnchor) {
					const IntVector3 size = document.Size();
					lo = MakeIntVector3(0, 0, 0);
					hi = size - MakeIntVector3(1, 1, 1);
					if (!pending)
						return;
					const IntVector3 last =
					  pendingAnchor + MakeIntVector3(pending->GetWidth() - 1,
					                                 pending->GetHeight() - 1,
					                                 pending->GetDepth() - 1);
					lo = MakeIntVector3(std::min(lo.x, pendingAnchor.x),
					                    std::min(lo.y, pendingAnchor.y),
					                    std::min(lo.z, pendingAnchor.z));
					hi = MakeIntVector3(std::max(hi.x, last.x), std::max(hi.y, last.y),
					                    std::max(hi.z, last.z));
				}

				// Whether a voxel lies between `eye` and `point`.
				bool Hide(const Vector3& eye, const Vector3& point) const {
					const Vector3 toPoint = point - eye;
					const float distance = toPoint.GetLength();
					float tEnd = distance - kOcclusionSlack;
					if (tEnd <= 0.0F)
						return false;
					const Vector3 dir = toPoint * (1.0F / distance);
					const float o[3] = {eye.x, eye.y, eye.z};
					const float d[3] = {dir.x, dir.y, dir.z};
					const int cellLo[3] = {lo.x, lo.y, lo.z};
					const int cellHi[3] = {hi.x, hi.y, hi.z};

					// Keep to the part of the ray inside the occluders' bounds. Voxel i
					// is centred at i, so it spans i - 0.5 .. i + 0.5.
					float tStart = 0.0F;
					for (int k = 0; k < 3; k++) {
						const float boundLo = float(cellLo[k]) - 0.5F;
						const float boundHi = float(cellHi[k]) + 0.5F;
						if (d[k] == 0.0F) {
							if (o[k] < boundLo || o[k] > boundHi)
								return false;
							continue;
						}
						float t0 = (boundLo - o[k]) / d[k];
						float t1 = (boundHi - o[k]) / d[k];
						if (t0 > t1)
							std::swap(t0, t1);
						tStart = std::max(tStart, t0);
						tEnd = std::min(tEnd, t1);
					}
					if (tStart >= tEnd)
						return false;

					// Walk the voxels the ray crosses in a space shifted by half a
					// voxel, where voxel i spans i .. i + 1.
					int cell[3], step[3];
					float tNext[3], tDelta[3];
					for (int k = 0; k < 3; k++) {
						const float at = o[k] + d[k] * tStart + 0.5F;
						cell[k] = std::max(cellLo[k], std::min(cellHi[k], int(std::floor(at))));
						step[k] = d[k] >= 0.0F ? 1 : -1;
						if (d[k] == 0.0F) {
							tNext[k] = tDelta[k] = std::numeric_limits<float>::infinity();
							continue;
						}
						const float boundary = float(cell[k]) + (step[k] > 0 ? 1.0F : 0.0F);
						tNext[k] = tStart + (boundary - at) / d[k];
						tDelta[k] = std::fabs(1.0F / d[k]);
					}
					float tCell = tStart; // where the ray enters `cell`
					// The voxel the eye is inside hides nothing: the renderer shows its
					// faces from the outside only, so the scene behind it is in view.
					bool eyeCell = tStart == 0.0F;
					while (tCell < tEnd) {
						if (!eyeCell && Solid(cell[0], cell[1], cell[2]))
							return true;
						eyeCell = false;
						const int k = (tNext[0] <= tNext[1] && tNext[0] <= tNext[2]) ? 0
						              : (tNext[1] <= tNext[2])                      ? 1
						                                                             : 2;
						tCell = tNext[k];
						tNext[k] += tDelta[k];
						cell[k] += step[k];
						if (cell[k] < cellLo[k] || cell[k] > cellHi[k])
							return false;
					}
					return false;
				}

			private:
				const IVoxelDocument& document;
				const VoxelModel* pending;
				IntVector3 pendingAnchor;
				IntVector3 lo, hi; // bounds of every occluding voxel, inclusive

				bool Solid(int x, int y, int z) const {
					if (document.IsSolid(x, y, z))
						return true;
					if (!pending)
						return false;
					const IntVector3 rel = MakeIntVector3(x, y, z) - pendingAnchor;
					return pending->IsSolid(rel.x, rel.y, rel.z); // bounds-checked
				}
			};
		} // namespace

		void VoxelOverlayLines::Emit(const Vector3& a, const Vector3& b, const Vector4& color) {
			lines.push_back({a, b, color});
		}

		void VoxelOverlayLines::Update(const GizmoView& view, const Occluders& occluders) {
			// The same edge is often emitted more than once (neighbouring cell
			// outlines share theirs): it is tested and drawn once. A line's
			// direction does not matter, so each is put the same way round first.
			auto before = [](const Vector3& p, const Vector3& q) {
				return std::tie(p.x, p.y, p.z) < std::tie(q.x, q.y, q.z);
			};
			auto key = [](const Line& l) {
				return std::tie(l.a.x, l.a.y, l.a.z, l.b.x, l.b.y, l.b.z, l.color.x, l.color.y,
				                l.color.z, l.color.w);
			};
			auto same = [&](const Line& l, const Line& r) { return key(l) == key(r); };
			for (Line& l : lines) {
				if (before(l.b, l.a))
					std::swap(l.a, l.b);
			}
			const bool linesChanged =
			  lines.size() != cache.emitted.size() ||
			  !std::equal(lines.begin(), lines.end(), cache.emitted.begin(), same);
			if (linesChanged) {
				cache.emitted = lines;
				cache.lines = lines;
				std::stable_sort(cache.lines.begin(), cache.lines.end(),
				                 [&](const Line& l, const Line& r) { return key(l) < key(r); });
				cache.lines.erase(std::unique(cache.lines.begin(), cache.lines.end(), same),
				                  cache.lines.end());
			}

			const bool hasPending = occluders.pending != nullptr;
			if (cache.valid && !linesChanged && SameView(cache.view, view) &&
			    cache.documentVersion == occluders.documentVersion &&
			    cache.hasPending == hasPending && cache.pendingVersion == occluders.pendingVersion &&
			    cache.pendingAnchor == occluders.pendingAnchor)
				return; // nothing the strokes depend on has moved
			cache.view = view;
			cache.documentVersion = occluders.documentVersion;
			cache.hasPending = hasPending;
			cache.pendingVersion = occluders.pendingVersion;
			cache.pendingAnchor = occluders.pendingAnchor;
			cache.valid = true;
			cache.hidden.clear();
			cache.shown.clear();

			const OcclusionTest occlusion(occluders);
			std::vector<Vector2> ends; // screen points splitting a line into pieces
			for (const Line& l : cache.lines) {
				Vector3 a = l.a, b = l.b;
				if (!ClipToView(view, kCasingWidth, a, b))
					continue;
				Vector2 pa, pb;
				if (!view.Project(a, pa) || !view.Project(b, pb))
					continue;

				const float screenLength = (pb - pa).GetLength();
				const float widthScale =
				  Clampf(screenLength / kFullWidthLength, kMinWidthScale, 1.0F);
				const int pieces = std::max(
				  1, std::min(kMaxSamples, int(std::ceil(screenLength / kSampleSpacing))));

				// Each piece is in view or hidden as its middle is; neighbouring
				// pieces alike make one stroke.
				ends.assign(1, pa);
				bool projected = true;
				for (int p = 1; p < pieces && projected; p++) {
					Vector2 s;
					projected = view.Project(a + (b - a) * (float(p) / float(pieces)), s);
					ends.push_back(s);
				}
				if (!projected)
					continue; // cannot happen past the near plane, but never draw a guess
				ends.push_back(pb);

				Vector4 hiddenColor = l.color;
				hiddenColor.w *= kHiddenAlpha;
				int runStart = 0;
				bool runHidden = false;
				for (int p = 0; p <= pieces; p++) {
					bool pieceHidden = runHidden;
					if (p < pieces) {
						const Vector3 middle = a + (b - a) * ((float(p) + 0.5F) / float(pieces));
						pieceHidden = occlusion.Hide(view.eye, middle);
						if (p == 0)
							runHidden = pieceHidden;
					}
					if (p == pieces || pieceHidden != runHidden) {
						if (runHidden)
							cache.hidden.push_back({ends[runStart], ends[p], 1.0F, hiddenColor});
						else
							cache.shown.push_back({ends[runStart], ends[p], widthScale, l.color});
						runStart = p;
						runHidden = pieceHidden;
					}
				}
			}
		}

		void VoxelOverlayLines::Draw(client::IRenderer& renderer, const GizmoView& view,
		                             const Occluders& occluders) {
			if (!view.IsValid() || !occluders.document) {
				lines.clear();
				return;
			}
			Update(view, occluders);
			lines.clear();

			// Hidden lines first, so every line in view is over them, and all the
			// casings before any core, so no casing covers a core it meets.
			for (const Stroke& s : cache.hidden)
				OverlayStrokeLine(renderer, s.a, s.b, kHiddenWidth, s.color);
			for (const Stroke& s : cache.shown) {
				const float width = kCasingWidth * s.widthScale;
				Vector2 a = s.a, b = s.b;
				Lengthen(a, b, width * 0.5F);
				OverlayStrokeLine(renderer, a, b, width,
				                  MakeVector4(0.0F, 0.0F, 0.0F, kCasingAlpha * s.color.w));
			}
			for (const Stroke& s : cache.shown) {
				const float width = kCoreWidth * s.widthScale;
				Vector2 a = s.a, b = s.b;
				Lengthen(a, b, width * 0.5F);
				OverlayStrokeLine(renderer, a, b, width, s.color);
			}
		}
	} // namespace gui
} // namespace spades
