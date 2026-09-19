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

#include "TransformGizmo.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "GizmoCanvas.h"

namespace spades {
	namespace gui {
		namespace {
			// --- Behaviour -----------------------------------------------------
			constexpr float kDefaultSizePixels = 80.0F;
			constexpr float kMinSizePixels = 16.0F;
			constexpr float kPickTolerance = 6.0F; // pixels from a handle that still grab it
			// An axis handle fades out as its on-screen length (a fraction of the
			// gizmo size) drops from full to hide: it points at the viewer and
			// could no longer be dragged meaningfully.
			constexpr float kAxisHide = 0.1F;
			constexpr float kAxisFull = 0.25F;
			// A plane square fades out likewise as the plane turns edge-on (|cos|
			// between its normal and the line of sight).
			constexpr float kPlaneHide = 0.12F;
			constexpr float kPlaneFull = 0.3F;
			// Below this |cos| between the cursor ray and a ring's axis, the ring is
			// nearly edge-on and is turned by sliding along it on screen instead.
			constexpr float kEdgeOnLimit = 0.15F;
			// Rays grazing a drag plane closer than this |cos| are ignored: the hit
			// point would shoot off towards infinity.
			constexpr float kGrazingLimit = 0.01F;
			constexpr float kMinScaleFactor = 1.0e-3F;

			// --- Drawing -------------------------------------------------------
			constexpr int kRingSegments = 64;
			constexpr int kConeSegments = 16;
			// A ring is drawn a little past its front half, so one that faces the
			// viewer closes up into a full circle.
			constexpr float kRingFrontSlack = 0.05F;
			constexpr float kStroke = 2.5F;    // pixels
			constexpr float kStrokeHot = 3.5F; // hovered or dragged
			constexpr float kThinStroke = 1.5F;
			constexpr float kGuideLength = 8.0F; // constraint guide half-length, gizmo units
			constexpr float kHotLighten = 0.5F;

			const Vector4 kAxisColor[3] = {MakeVector4(1.0F, 0.35F, 0.35F, 1.0F),
			                               MakeVector4(0.4F, 1.0F, 0.4F, 1.0F),
			                               MakeVector4(0.45F, 0.6F, 1.0F, 1.0F)};
			const Vector4 kViewColor = MakeVector4(0.92F, 0.92F, 0.92F, 1.0F);
			const Vector4 kUniformColor = MakeVector4(0.72F, 0.72F, 0.74F, 1.0F);
			const Vector4 kDisabledColor = MakeVector4(0.55F, 0.55F, 0.55F, 0.45F);
			const Vector4 kTraceColor = MakeVector4(0.8F, 0.8F, 0.8F, 0.6F);

			// --- Handle taxonomy ---------------------------------------------
			enum class Kind { Translate, Rotate, Scale };
			enum class Shape { Axis, Plane, View, Trackball, Uniform };

			struct HandleInfo {
				Kind kind;
				Shape shape;
				int axis; // Axis: the axis; Plane: its normal axis; otherwise -1
			};

			GizmoHandle Nth(GizmoHandle first, int i) { return GizmoHandle(int(first) + i); }

			bool InRange(GizmoHandle h, GizmoHandle first, int count, int& index) {
				index = int(h) - int(first);
				return index >= 0 && index < count;
			}

			HandleInfo Info(GizmoHandle h) {
				int i;
				if (InRange(h, GizmoHandle::TranslateX, 3, i)) return {Kind::Translate, Shape::Axis, i};
				if (InRange(h, GizmoHandle::TranslateYZ, 3, i)) return {Kind::Translate, Shape::Plane, i};
				if (h == GizmoHandle::TranslateView) return {Kind::Translate, Shape::View, -1};
				if (InRange(h, GizmoHandle::RotateX, 3, i)) return {Kind::Rotate, Shape::Axis, i};
				if (h == GizmoHandle::RotateView) return {Kind::Rotate, Shape::View, -1};
				if (h == GizmoHandle::RotateTrackball) return {Kind::Rotate, Shape::Trackball, -1};
				if (InRange(h, GizmoHandle::ScaleX, 3, i)) return {Kind::Scale, Shape::Axis, i};
				if (InRange(h, GizmoHandle::ScaleYZ, 3, i)) return {Kind::Scale, Shape::Plane, i};
				return {Kind::Scale, Shape::Uniform, -1}; // ScaleUniform
			}

			// --- Math ----------------------------------------------------------
			float Component(const Vector3& v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }

			void SetComponent(Vector3& v, int i, float value) {
				if (i == 0) v.x = value;
				else if (i == 1) v.y = value;
				else v.z = value;
			}

			// Smooth 0..1 ramp of `x` between `lo` and `hi`.
			float Ramp(float lo, float hi, float x) {
				float t = std::max(0.0F, std::min(1.0F, (x - lo) / (hi - lo)));
				return t * t * (3.0F - 2.0F * t);
			}

			float SnapTo(float value, float increment) {
				return increment > 0.0F ? std::round(value / increment) * increment : value;
			}

			// `v` scaled to unit length; false (and `out` untouched) if it is ~zero.
			bool Unit(const Vector3& v, Vector3& out) {
				float len = v.GetLength();
				if (len < 1.0e-6F)
					return false;
				out = v * (1.0F / len);
				return true;
			}

			// Any unit vector perpendicular to the unit vector `n`.
			Vector3 Perpendicular(const Vector3& n) {
				Vector3 other = std::fabs(n.x) < 0.9F ? MakeVector3(1.0F, 0.0F, 0.0F)
				                                      : MakeVector3(0.0F, 1.0F, 0.0F);
				return Vector3::Cross(n, other).Normalize();
			}

			// `v` with its component along the unit normal `n` removed.
			Vector3 InPlane(const Vector3& v, const Vector3& n) { return v - n * Vector3::Dot(v, n); }

			// Right-handed turn by `angle` radians about the unit vector `axis`.
			Quaternion AxisAngle(const Vector3& axis, float angle) {
				float s = std::sin(angle * 0.5F);
				return Quaternion(axis.x * s, axis.y * s, axis.z * s, std::cos(angle * 0.5F));
			}

			// Where the ray meets the plane through `point` with unit normal
			// `normal`; false if it runs along the plane or points away from it.
			bool CastOnPlane(const Vector3& origin, const Vector3& direction, const Vector3& point,
			                 const Vector3& normal, Vector3& hit) {
				float along = Vector3::Dot(direction, normal);
				if (std::fabs(along) < kGrazingLimit)
					return false;
				float t = Vector3::Dot(point - origin, normal) / along;
				if (t <= 0.0F)
					return false;
				hit = origin + direction * t;
				return true;
			}

			float DistanceToSegment(const Vector2& p, const Vector2& a, const Vector2& b) {
				Vector2 ab = b - a;
				float l2 = Vector2::Dot(ab, ab);
				float t = (l2 < 1.0e-6F) ? 0.0F : Vector2::Dot(p - a, ab) / l2;
				t = std::max(0.0F, std::min(1.0F, t));
				return (p - (a + ab * t)).GetLength();
			}

			// Distance from `p` to a convex polygon: zero inside.
			float DistanceToConvex(const Vector2& p, const Vector2* pts, int n) {
				bool positive = false, negative = false;
				float edge = 1.0e30F;
				for (int i = 0; i < n; i++) {
					const Vector2& a = pts[i];
					const Vector2& b = pts[(i + 1) % n];
					float cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
					positive = positive || cross > 0.0F;
					negative = negative || cross < 0.0F;
					edge = std::min(edge, DistanceToSegment(p, a, b));
				}
				return (positive && negative) ? edge : 0.0F;
			}

			Vector4 WithAlpha(const Vector4& c, float alpha) { return MakeVector4(c.x, c.y, c.z, alpha); }

			Vector4 Lighter(const Vector4& c, float amount) {
				return MakeVector4(c.x + (1.0F - c.x) * amount, c.y + (1.0F - c.y) * amount,
				                   c.z + (1.0F - c.z) * amount, c.w);
			}

			Vector4 Shaded(const Vector4& c, float factor) {
				return MakeVector4(c.x * factor, c.y * factor, c.z * factor, c.w);
			}
		} // namespace

		// --- Layout ----------------------------------------------------------------

		/**
		 * The gizmo as laid out for one view: where it is on screen, how big one
		 * gizmo unit is, which handles are turned away, and where each kind of
		 * handle sits. Distances are in gizmo units: 1 is the rotation ring radius.
		 */
		struct GizmoFrame {
			bool valid = false;

			Vector3 center;
			Vector2 centerScreen;
			Vector3 axes[3];
			Vector3 toEye;       // unit, from the centre towards the eye
			float unit = 0.0F;   // world length of one gizmo unit
			float pixels = 0.0F; // screen length of one gizmo unit
			float axisShown[3] = {0.0F, 0.0F, 0.0F};  // 0 hidden .. 1 fully shown
			float planeShown[3] = {0.0F, 0.0F, 0.0F}; // by the plane's normal axis
			float side[3] = {1.0F, 1.0F, 1.0F};       // which way each axis faces the eye

			// Radii of the circular handles.
			float ring = 1.0F;
			float viewRing = 1.15F;
			float uniformRing = 1.15F;
			float centerRadius = 0.14F; // the centre circle
			// Arrows (translation).
			float arrowStart = 0.2F;
			float arrowTip = 1.05F;
			float arrowHeadLength = 0.24F;
			float arrowHeadRadius = 0.08F;
			// Cubes (axis scale).
			float scaleBox = 0.9F;
			float scaleBoxHalf = 0.07F;
			float scaleStubStart = 0.2F; // where a cube's own line starts, when no arrow
			bool arrows = false;         // arrows shown: cubes ride on their shafts
			// Squares.
			float planeMin = 0.3F, planeMax = 0.5F;           // translation
			float scalePlaneMin = 0.58F, scalePlaneMax = 0.72F; // scaling

			Vector3 At(int axis, float along) const { return center + axes[axis] * (along * unit); }

			// Corners of the square in the plane normal to `normalAxis`, in the
			// quadrant facing the eye.
			void Square(int normalAxis, float lo, float hi, Vector3 out[4]) const {
				int j = (normalAxis + 1) % 3, k = (normalAxis + 2) % 3;
				Vector3 u = axes[j] * (side[j] * unit), v = axes[k] * (side[k] * unit);
				out[0] = center + u * lo + v * lo;
				out[1] = center + u * hi + v * lo;
				out[2] = center + u * hi + v * hi;
				out[3] = center + u * lo + v * hi;
			}
		};

		namespace {
			float Shown(GizmoHandle h, const GizmoFrame& frame) {
				HandleInfo info = Info(h);
				if (info.shape == Shape::Axis)
					return info.kind == Kind::Rotate ? 1.0F : frame.axisShown[info.axis];
				if (info.shape == Shape::Plane)
					return frame.planeShown[info.axis];
				return 1.0F;
			}

			bool ProjectAll(const GizmoView& view, const Vector3* world, Vector2* screen, int n) {
				for (int i = 0; i < n; i++)
					if (!view.Project(world[i], screen[i]))
						return false;
				return true;
			}

			// The on-screen runs of a ring of world `radius` about unit `normal`:
			// only its front half unless `whole`, split wherever it turns away.
			void RingRuns(const GizmoView& view, const GizmoFrame& frame, const Vector3& normal,
			              float radius, bool whole, std::vector<std::vector<Vector2>>& runs) {
				runs.clear();
				Vector3 e1 = Perpendicular(normal);
				Vector3 e2 = Vector3::Cross(normal, e1);
				std::vector<Vector2> run;
				for (int i = 0; i <= kRingSegments; i++) {
					float t = 2.0F * M_PI_F * float(i) / float(kRingSegments);
					Vector3 offset = (e1 * std::cos(t) + e2 * std::sin(t)) * radius;
					Vector2 s;
					bool front =
					  whole || Vector3::Dot(offset, frame.toEye) >= -kRingFrontSlack * radius;
					if (front && view.Project(frame.center + offset, s)) {
						run.push_back(s);
					} else {
						if (run.size() >= 2)
							runs.push_back(run);
						run.clear();
					}
				}
				if (run.size() >= 2)
					runs.push_back(run);
			}

			float DistanceToRuns(const Vector2& p, const std::vector<std::vector<Vector2>>& runs) {
				float best = 1.0e30F;
				for (const std::vector<Vector2>& run : runs)
					for (size_t i = 0; i + 1 < run.size(); i++)
						best = std::min(best, DistanceToSegment(p, run[i], run[i + 1]));
				return best;
			}

			// Convex hull of `points` (Andrew's monotone chain), counter-clockwise.
			std::vector<Vector2> ConvexHull(std::vector<Vector2> points) {
				if (points.size() < 3)
					return points;
				std::sort(points.begin(), points.end(), [](const Vector2& a, const Vector2& b) {
					return a.x < b.x || (a.x == b.x && a.y < b.y);
				});
				auto turn = [](const Vector2& o, const Vector2& a, const Vector2& b) {
					return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
				};
				std::vector<Vector2> hull(points.size() * 2);
				size_t k = 0;
				for (size_t i = 0; i < points.size(); i++) {
					while (k >= 2 && turn(hull[k - 2], hull[k - 1], points[i]) <= 0.0F)
						k--;
					hull[k++] = points[i];
				}
				for (size_t i = points.size() - 1, lower = k + 1; i-- > 0;) {
					while (k >= lower && turn(hull[k - 2], hull[k - 1], points[i]) <= 0.0F)
						k--;
					hull[k++] = points[i];
				}
				hull.resize(k > 1 ? k - 1 : k); // the last point repeats the first
				return hull;
			}

			// A cone from `base` to `tip`, filled as one silhouette (so a
			// translucent cone blends evenly), with the darker base cap on top when
			// it faces the viewer.
			void DrawCone(GizmoCanvas& canvas, const GizmoView& view, const Vector3& base,
			              const Vector3& tip, float radius, const Vector4& color) {
				Vector3 d;
				if (!Unit(tip - base, d))
					return;
				Vector3 e1 = Perpendicular(d), e2 = Vector3::Cross(d, e1);
				std::vector<Vector2> outline(kConeSegments + 1);
				if (!view.Project(tip, outline[kConeSegments]))
					return;
				for (int i = 0; i < kConeSegments; i++) {
					float t = 2.0F * M_PI_F * float(i) / float(kConeSegments);
					if (!view.Project(base + (e1 * std::cos(t) + e2 * std::sin(t)) * radius, outline[i]))
						return;
				}
				std::vector<Vector2> silhouette = ConvexHull(outline);
				canvas.Convex(silhouette.data(), int(silhouette.size()), color);
				if (Vector3::Dot(d, view.eye - base) < 0.0F)
					canvas.Convex(outline.data(), kConeSegments, Shaded(color, 0.7F));
			}

			// A solid cube aligned with the gizmo axes, shaded per face. Each face is
			// anti-aliased on its own, so where two meet their rims leave a faint
			// line that reads as the cube's edge.
			void DrawCube(GizmoCanvas& canvas, const GizmoView& view, const GizmoFrame& frame,
			              const Vector3& center, float half, const Vector4& color) {
				for (int a = 0; a < 3; a++) {
					for (int sign = -1; sign <= 1; sign += 2) {
						Vector3 normal = frame.axes[a] * float(sign);
						Vector3 face = center + normal * half;
						Vector3 toEye = view.eye - face;
						float facing = Vector3::Dot(normal, toEye);
						if (facing <= 0.0F)
							continue; // turned away
						Vector3 u = frame.axes[(a + 1) % 3] * half;
						Vector3 v = frame.axes[(a + 2) % 3] * half;
						Vector3 corners[4] = {face - u - v, face + u - v, face + u + v, face - u + v};
						Vector2 q[4];
						if (!ProjectAll(view, corners, q, 4))
							continue;
						float light = 0.6F + 0.4F * facing / std::max(toEye.GetLength(), 1.0e-6F);
						canvas.Convex(q, 4, Shaded(color, light));
					}
				}
			}
		} // namespace

		// --- GizmoHandleSet --------------------------------------------------------

		GizmoHandleSet GizmoHandleSet::Of(GizmoHandle handle) {
			if (handle == GizmoHandle::None)
				return GizmoHandleSet();
			return GizmoHandleSet(uint32_t(1) << int(handle));
		}

		GizmoHandleSet GizmoHandleSet::Translation() {
			GizmoHandleSet set;
			for (int i = int(GizmoHandle::TranslateX); i <= int(GizmoHandle::TranslateView); i++)
				set = set | Of(GizmoHandle(i));
			return set;
		}

		GizmoHandleSet GizmoHandleSet::Rotation() {
			GizmoHandleSet set;
			for (int i = int(GizmoHandle::RotateX); i <= int(GizmoHandle::RotateTrackball); i++)
				set = set | Of(GizmoHandle(i));
			return set;
		}

		GizmoHandleSet GizmoHandleSet::Scaling() {
			GizmoHandleSet set;
			for (int i = int(GizmoHandle::ScaleX); i <= int(GizmoHandle::ScaleUniform); i++)
				set = set | Of(GizmoHandle(i));
			return set;
		}

		GizmoHandleSet GizmoHandleSet::All() { return Translation() | Rotation() | Scaling(); }

		bool GizmoHandleSet::Contains(GizmoHandle handle) const {
			return handle != GizmoHandle::None && ((bits >> int(handle)) & 1u) != 0;
		}

		// --- GizmoTransform --------------------------------------------------------

		bool GizmoTransform::IsIdentity() const {
			return translation == MakeVector3(0.0F, 0.0F, 0.0F) &&
			       !(rotation != Quaternion(0.0F, 0.0F, 0.0F, 1.0F)) &&
			       scale == MakeVector3(1.0F, 1.0F, 1.0F);
		}

		// --- TransformGizmo: setup -------------------------------------------------

		TransformGizmo::TransformGizmo()
		    : visible(GizmoHandleSet::Translation()),
		      enabled(GizmoHandleSet::Translation()),
		      sizePixels(kDefaultSizePixels) {}

		void TransformGizmo::SetSize(float pixels) { sizePixels = std::max(kMinSizePixels, pixels); }

		GizmoFrame TransformGizmo::MakeFrame(const GizmoView& view) const {
			GizmoFrame f;

			// Spread the handles out as more kinds share the gizmo: rings take the
			// middle, so arrows and cubes move outside them, and cubes ride the
			// arrow shafts when both show (as Blender's combined gizmo does).
			bool arrows = visible.Intersects(GizmoHandleSet::Of(GizmoHandle::TranslateX) |
			                                 GizmoHandleSet::Of(GizmoHandle::TranslateY) |
			                                 GizmoHandleSet::Of(GizmoHandle::TranslateZ));
			bool rings = visible.Intersects(GizmoHandleSet::Of(GizmoHandle::RotateX) |
			                                GizmoHandleSet::Of(GizmoHandle::RotateY) |
			                                GizmoHandleSet::Of(GizmoHandle::RotateZ) |
			                                GizmoHandleSet::Of(GizmoHandle::RotateView));
			bool cubes = visible.Intersects(GizmoHandleSet::Of(GizmoHandle::ScaleX) |
			                                GizmoHandleSet::Of(GizmoHandle::ScaleY) |
			                                GizmoHandleSet::Of(GizmoHandle::ScaleZ));
			f.arrows = arrows;
			f.uniformRing = rings ? 1.3F : 1.15F;
			f.scaleBox = rings ? 1.2F : (arrows ? 0.62F : 0.9F);
			f.arrowTip = rings ? (cubes ? 1.6F : 1.4F) : 1.05F;
			f.scaleStubStart = rings ? 1.05F : f.arrowStart;

			if (!view.IsValid())
				return f;
			f.center = pose.position;
			if (!view.Project(f.center, f.centerScreen))
				return f; // behind the viewer
			f.pixels = sizePixels;
			f.unit = sizePixels * view.WorldPerPixel(f.center);
			if (!(f.unit > 0.0F) || !Unit(view.eye - f.center, f.toEye))
				return f;

			for (int i = 0; i < 3; i++) {
				f.axes[i] = pose.axes[i];
				Vector2 tip;
				float length = 0.0F;
				if (view.Project(f.At(i, 1.0F), tip))
					length = (tip - f.centerScreen).GetLength() / f.pixels;
				f.axisShown[i] = Ramp(kAxisHide, kAxisFull, length);
				float facing = Vector3::Dot(f.axes[i], f.toEye);
				f.planeShown[i] = Ramp(kPlaneHide, kPlaneFull, std::fabs(facing));
				f.side[i] = facing >= 0.0F ? 1.0F : -1.0F;
			}
			f.valid = true;
			return f;
		}

		bool TransformGizmo::IsUsable(GizmoHandle handle, const GizmoFrame& frame) const {
			return visible.Contains(handle) && enabled.Contains(handle) && Shown(handle, frame) > 0.0F;
		}

		Vector4 TransformGizmo::HandleColor(GizmoHandle handle, const GizmoFrame& frame) const {
			HandleInfo info = Info(handle);
			Vector4 color;
			if (!enabled.Contains(handle))
				color = kDisabledColor;
			else if (info.shape == Shape::Axis || info.shape == Shape::Plane)
				color = kAxisColor[info.axis];
			else
				color = (info.shape == Shape::Uniform) ? kUniformColor : kViewColor;
			if (enabled.Contains(handle) && IsHot(handle))
				color = Lighter(color, kHotLighten);
			color.w *= Shown(handle, frame);
			return color;
		}

		// --- TransformGizmo: picking -----------------------------------------------

		GizmoHandle TransformGizmo::HandleAt(const GizmoView& view, const Vector2& cursor) const {
			GizmoFrame f = MakeFrame(view);
			if (!f.valid)
				return GizmoHandle::None;

			// Nearest handle within the tolerance wins. Ties go to the one tested
			// first, so the order below doubles as priority: the centre, then solid
			// handles, then the thin rings.
			GizmoHandle best = GizmoHandle::None;
			float bestDistance = kPickTolerance;
			auto consider = [&](GizmoHandle h, float distance) {
				if (IsUsable(h, f) && distance < bestDistance) {
					best = h;
					bestDistance = distance;
				}
			};
			float fromCenter = (cursor - f.centerScreen).GetLength();
			Vector2 a, b;

			consider(GizmoHandle::TranslateView, std::max(0.0F, fromCenter - f.centerRadius * f.pixels));

			for (int i = 0; i < 3; i++) {
				GizmoHandle cube = Nth(GizmoHandle::ScaleX, i);
				if (view.Project(f.At(i, f.scaleBox), a))
					consider(cube, std::max(0.0F, (cursor - a).GetLength() -
					                                 f.scaleBoxHalf * 1.5F * f.pixels));
				if (!f.arrows && view.Project(f.At(i, f.scaleStubStart), a) &&
				    view.Project(f.At(i, f.scaleBox), b))
					consider(cube, DistanceToSegment(cursor, a, b));
			}

			for (int i = 0; i < 3; i++) {
				GizmoHandle arrow = Nth(GizmoHandle::TranslateX, i);
				Vector2 tip;
				if (!view.Project(f.At(i, f.arrowStart), a) ||
				    !view.Project(f.At(i, f.arrowTip - f.arrowHeadLength), b) ||
				    !view.Project(f.At(i, f.arrowTip), tip))
					continue;
				float head = DistanceToSegment(cursor, b, tip) - f.arrowHeadRadius * f.pixels;
				consider(arrow, std::min(DistanceToSegment(cursor, a, b), std::max(0.0F, head)));
			}

			for (int i = 0; i < 3; i++) {
				Vector3 corners[4];
				Vector2 q[4];
				f.Square(i, f.planeMin, f.planeMax, corners);
				if (ProjectAll(view, corners, q, 4))
					consider(Nth(GizmoHandle::TranslateYZ, i), DistanceToConvex(cursor, q, 4));
				f.Square(i, f.scalePlaneMin, f.scalePlaneMax, corners);
				if (ProjectAll(view, corners, q, 4))
					consider(Nth(GizmoHandle::ScaleYZ, i), DistanceToConvex(cursor, q, 4));
			}

			std::vector<std::vector<Vector2>> runs;
			for (int i = 0; i < 3; i++) {
				GizmoHandle ring = Nth(GizmoHandle::RotateX, i);
				if (!IsUsable(ring, f))
					continue;
				RingRuns(view, f, f.axes[i], f.ring * f.unit, false, runs);
				consider(ring, DistanceToRuns(cursor, runs));
			}
			consider(GizmoHandle::RotateView, std::fabs(fromCenter - f.viewRing * f.pixels));
			consider(GizmoHandle::ScaleUniform, std::fabs(fromCenter - f.uniformRing * f.pixels));

			// The trackball is the empty space inside the rings: only if nothing
			// else is there.
			if (best == GizmoHandle::None && IsUsable(GizmoHandle::RotateTrackball, f) &&
			    fromCenter <= f.ring * f.pixels)
				best = GizmoHandle::RotateTrackball;
			return best;
		}

		void TransformGizmo::Hover(const GizmoView& view, const Vector2& cursor) {
			if (!IsDragging())
				hovered = HandleAt(view, cursor);
		}

		// --- TransformGizmo: dragging ----------------------------------------------

		bool TransformGizmo::Begin(const GizmoView& view, const Vector2& cursor) {
			if (IsDragging())
				return false;
			GizmoFrame f = MakeFrame(view);
			if (!f.valid)
				return false;
			GizmoHandle handle = HandleAt(view, cursor);
			if (handle == GizmoHandle::None)
				return false;

			active = handle;
			total = GizmoTransform();
			step = GizmoTransform();
			drag = DragState();
			for (int i = 0; i < 3; i++)
				drag.axes[i] = f.axes[i];
			drag.grabPosition = pose.position;
			drag.grabCursor = cursor;
			drag.lastCursor = cursor;

			bool grabbed = false;
			switch (Info(handle).kind) {
				case Kind::Translate: grabbed = BeginTranslate(view, f, cursor); break;
				case Kind::Rotate: grabbed = BeginRotate(view, f, cursor); break;
				case Kind::Scale: grabbed = BeginScale(view, f, cursor); break;
			}
			if (!grabbed) {
				active = GizmoHandle::None;
				return false;
			}
			hovered = handle;
			return true;
		}

		bool TransformGizmo::Drag(const GizmoView& view, const Vector2& cursor) {
			if (!IsDragging() || !view.IsValid())
				return false;
			switch (Info(active).kind) {
				case Kind::Translate: return DragTranslate(view, cursor);
				case Kind::Rotate: return DragRotate(view, cursor);
				case Kind::Scale: return DragScale(view, cursor);
			}
			return false;
		}

		GizmoTransform TransformGizmo::End() {
			if (!IsDragging())
				return GizmoTransform();
			GizmoTransform result = total;
			active = GizmoHandle::None;
			hovered = GizmoHandle::None;
			total = GizmoTransform();
			step = GizmoTransform();
			return result;
		}

		GizmoTransform TransformGizmo::Cancel() {
			if (!IsDragging())
				return GizmoTransform();
			GizmoTransform undo;
			undo.translation = -total.translation;
			undo.rotation = total.rotation.Conjugate();
			undo.scale = MakeVector3(1.0F / total.scale.x, 1.0F / total.scale.y,
			                         1.0F / total.scale.z); // never zero: see DragScale
			active = GizmoHandle::None;
			hovered = GizmoHandle::None;
			total = GizmoTransform();
			step = GizmoTransform();
			return undo;
		}

		// Translation keeps the grabbed point under the cursor: cast the cursor
		// ray on a plane through the handle, then keep the part the handle allows.
		bool TransformGizmo::BeginTranslate(const GizmoView& view, const GizmoFrame& frame,
		                                    const Vector2& cursor) {
			HandleInfo info = Info(active);
			Vector3 normal;
			if (info.shape == Shape::Axis) {
				// The plane holding the axis that faces the viewer most squarely.
				const Vector3& axis = frame.axes[info.axis];
				if (!Unit(InPlane(frame.toEye, axis), normal))
					return false;
			} else if (info.shape == Shape::Plane) {
				normal = frame.axes[info.axis];
			} else {
				normal = -view.forward; // the screen plane
			}
			Vector3 origin, direction, hit;
			view.Ray(cursor, origin, direction);
			if (!CastOnPlane(origin, direction, frame.center, normal, hit))
				return false;
			drag.planeNormal = normal;
			drag.grabOffset = hit - frame.center;
			return true;
		}

		bool TransformGizmo::DragTranslate(const GizmoView& view, const Vector2& cursor) {
			Vector3 origin, direction, hit;
			view.Ray(cursor, origin, direction);
			if (!CastOnPlane(origin, direction, pose.position, drag.planeNormal, hit))
				return false;
			Vector3 move = hit - drag.grabOffset - pose.position;
			HandleInfo info = Info(active);
			if (info.shape == Shape::Axis) {
				const Vector3& axis = drag.axes[info.axis];
				move = axis * Vector3::Dot(move, axis);
			}

			// Snap the running total, per pose axis, not each step. On a grid the
			// position reached is snapped, and the total is what gets it there,
			// but only along the axes this handle moves: an arrow must never pull
			// what it moves onto the grid sideways.
			auto moves = [&](int i) {
				if (info.shape == Shape::Axis)
					return i == info.axis;
				if (info.shape == Shape::Plane)
					return i != info.axis;
				return true; // the screen-plane handle moves along all three
			};
			Vector3 wanted = total.translation + move;
			Vector3 snapped = MakeVector3(0.0F, 0.0F, 0.0F);
			for (int i = 0; i < 3; i++) {
				const float along = Vector3::Dot(wanted, drag.axes[i]);
				const float from = (snap.translationToGrid && moves(i))
				                     ? Vector3::Dot(drag.grabPosition, drag.axes[i])
				                     : 0.0F;
				snapped += drag.axes[i] * (SnapTo(from + along, snap.translation) - from);
			}
			Vector3 delta = snapped - total.translation;
			if (delta == MakeVector3(0.0F, 0.0F, 0.0F))
				return false;
			step = GizmoTransform();
			step.translation = delta;
			total.translation = snapped;
			return true;
		}

		// A ring turns the grabbed point around the centre, measured on the ring's
		// plane. Seen nearly edge-on that plane is useless, so the turn follows the
		// cursor along the ring's on-screen direction instead (as Blender does).
		bool TransformGizmo::BeginRotate(const GizmoView& view, const GizmoFrame& frame,
		                                 const Vector2& cursor) {
			HandleInfo info = Info(active);
			if (info.shape == Shape::Trackball) {
				drag.ringPixels = frame.ring * frame.pixels;
				return true;
			}
			Vector3 normal = (info.shape == Shape::View) ? -view.forward : frame.axes[info.axis];
			float radius = (info.shape == Shape::View) ? frame.viewRing : frame.ring;
			drag.rotationAxis = normal;
			drag.ringPixels = radius * frame.pixels;

			Vector3 origin, direction, hit, radial;
			view.Ray(cursor, origin, direction);
			if (std::fabs(Vector3::Dot(direction, normal)) >= kEdgeOnLimit &&
			    CastOnPlane(origin, direction, frame.center, normal, hit) &&
			    Unit(InPlane(hit - frame.center, normal), radial)) {
				drag.alongScreen = false;
				drag.startDirection = radial;
				drag.lastDirection = radial;
				return true;
			}
			if (info.shape == Shape::View)
				return false; // the screen plane always faces the viewer

			// Edge-on: find the ring point nearest the cursor and the way it runs.
			Vector3 e1 = Perpendicular(normal), e2 = Vector3::Cross(normal, e1);
			float bestDistance = 1.0e30F;
			Vector3 grabbed = e1;
			for (int i = 0; i < kRingSegments; i++) {
				float t = 2.0F * M_PI_F * float(i) / float(kRingSegments);
				Vector3 dir = e1 * std::cos(t) + e2 * std::sin(t);
				Vector2 s;
				if (!view.Project(frame.center + dir * (radius * frame.unit), s))
					continue;
				float d = (s - cursor).GetLength();
				if (d < bestDistance) {
					bestDistance = d;
					grabbed = dir;
				}
			}
			if (bestDistance >= 1.0e30F)
				return false;
			Vector3 point = frame.center + grabbed * (radius * frame.unit);
			Vector3 tangent = Vector3::Cross(normal, grabbed); // where a positive turn goes
			Vector2 from, to;
			if (!view.Project(point, from) || !view.Project(point + tangent * (0.25F * frame.unit), to))
				return false;
			float length = (to - from).GetLength();
			if (length < 1.0e-3F)
				return false;
			drag.alongScreen = true;
			drag.screenTangent = (to - from) * (1.0F / length);
			drag.startDirection = grabbed;
			return true;
		}

		bool TransformGizmo::DragRotate(const GizmoView& view, const Vector2& cursor) {
			HandleInfo info = Info(active);
			if (info.shape == Shape::Trackball) {
				// Roll the front of a ball the size of the rings under the cursor:
				// the axis lies in the screen plane, square to the movement.
				Vector2 moved = cursor - drag.lastCursor;
				drag.lastCursor = cursor;
				float length = moved.GetLength();
				Vector3 toEye, axis;
				if (length < 1.0e-3F || !Unit(view.eye - pose.position, toEye))
					return false;
				Vector3 movedWorld = view.right * moved.x - view.up * moved.y;
				if (!Unit(Vector3::Cross(toEye, movedWorld), axis))
					return false;
				Quaternion turn = AxisAngle(axis, length / drag.ringPixels);
				total.rotation = (turn * total.rotation).Normalize();
				step = GizmoTransform();
				step.rotation = turn;
				return true;
			}

			const Vector3& normal = drag.rotationAxis;
			if (drag.alongScreen) {
				drag.angle =
				  Vector2::Dot(cursor - drag.grabCursor, drag.screenTangent) / drag.ringPixels;
			} else {
				Vector3 origin, direction, hit, radial;
				view.Ray(cursor, origin, direction);
				if (!CastOnPlane(origin, direction, pose.position, normal, hit) ||
				    !Unit(InPlane(hit - pose.position, normal), radial))
					return false;
				// Accumulate small signed turns, so a drag can go round more than once.
				drag.angle += std::atan2(
				  Vector3::Dot(Vector3::Cross(drag.lastDirection, radial), normal),
				  Vector3::Dot(drag.lastDirection, radial));
				drag.lastDirection = radial;
			}

			float snapped = SnapTo(drag.angle, snap.rotation);
			float delta = snapped - drag.appliedAngle;
			if (delta == 0.0F)
				return false;
			drag.appliedAngle = snapped;
			total.rotation = AxisAngle(normal, snapped);
			step = GizmoTransform();
			step.rotation = AxisAngle(normal, delta);
			return true;
		}

		// Scaling compares how far the cursor is from the centre now with how far
		// it was at the grab: along the axis, within the plane, or on screen.
		bool TransformGizmo::BeginScale(const GizmoView& view, const GizmoFrame& frame,
		                                const Vector2& cursor) {
			HandleInfo info = Info(active);
			if (info.shape == Shape::Uniform) {
				drag.grabDistance = (cursor - frame.centerScreen).GetLength();
				return drag.grabDistance >= 1.0F;
			}
			Vector3 normal;
			if (info.shape == Shape::Axis) {
				if (!Unit(InPlane(frame.toEye, frame.axes[info.axis]), normal))
					return false;
			} else {
				normal = frame.axes[info.axis];
			}
			Vector3 origin, direction, hit;
			view.Ray(cursor, origin, direction);
			if (!CastOnPlane(origin, direction, frame.center, normal, hit))
				return false;
			Vector3 offset = hit - frame.center;
			float minimum = 0.05F * frame.unit; // too near the centre to measure from
			drag.planeNormal = normal;
			if (info.shape == Shape::Axis) {
				drag.grabDistance = Vector3::Dot(offset, frame.axes[info.axis]);
				return std::fabs(drag.grabDistance) >= minimum;
			}
			drag.grabDistance = offset.GetLength();
			if (drag.grabDistance < minimum)
				return false;
			drag.grabDirection = offset * (1.0F / drag.grabDistance);
			return true;
		}

		bool TransformGizmo::DragScale(const GizmoView& view, const Vector2& cursor) {
			HandleInfo info = Info(active);
			float ratio;
			if (info.shape == Shape::Uniform) {
				Vector2 center;
				if (!view.Project(pose.position, center))
					return false;
				ratio = (cursor - center).GetLength() / drag.grabDistance;
			} else {
				Vector3 origin, direction, hit;
				view.Ray(cursor, origin, direction);
				if (!CastOnPlane(origin, direction, pose.position, drag.planeNormal, hit))
					return false;
				Vector3 offset = hit - pose.position;
				Vector3 along =
				  (info.shape == Shape::Axis) ? drag.axes[info.axis] : drag.grabDirection;
				ratio = Vector3::Dot(offset, along) / drag.grabDistance;
			}

			float factor = 1.0F + SnapTo(ratio - 1.0F, snap.scale);
			if (std::fabs(factor) < kMinScaleFactor) // never collapse: Cancel divides by it
				factor = factor < 0.0F ? -kMinScaleFactor : kMinScaleFactor;

			Vector3 scaled = MakeVector3(1.0F, 1.0F, 1.0F);
			if (info.shape == Shape::Axis) {
				SetComponent(scaled, info.axis, factor);
			} else if (info.shape == Shape::Plane) {
				SetComponent(scaled, (info.axis + 1) % 3, factor);
				SetComponent(scaled, (info.axis + 2) % 3, factor);
			} else {
				scaled = MakeVector3(factor, factor, factor);
			}
			if (scaled == total.scale)
				return false;
			step = GizmoTransform();
			step.scale = scaled / total.scale;
			total.scale = scaled;
			return true;
		}

		// --- TransformGizmo: drawing -----------------------------------------------

		void TransformGizmo::Draw(GizmoCanvas& canvas, const GizmoView& view) const {
			GizmoFrame f = MakeFrame(view);
			if (!f.valid)
				return;
			if (IsDragging())
				DrawDragging(canvas, view, f);
			else
				DrawIdle(canvas, view, f);
		}

		void TransformGizmo::DrawIdle(GizmoCanvas& canvas, const GizmoView& view,
		                              const GizmoFrame& f) const {
			// Back to front: the trackball tint and the big rings, the axis rings,
			// the squares, the axis handles from the farthest in, the centre last.
			DrawHandle(canvas, view, f, GizmoHandle::RotateTrackball, 1.0F);
			DrawHandle(canvas, view, f, GizmoHandle::ScaleUniform, 1.0F);
			DrawHandle(canvas, view, f, GizmoHandle::RotateView, 1.0F);
			for (int i = 0; i < 3; i++)
				DrawHandle(canvas, view, f, Nth(GizmoHandle::RotateX, i), 1.0F);
			for (int i = 0; i < 3; i++) {
				DrawHandle(canvas, view, f, Nth(GizmoHandle::TranslateYZ, i), 1.0F);
				DrawHandle(canvas, view, f, Nth(GizmoHandle::ScaleYZ, i), 1.0F);
			}
			int order[3] = {0, 1, 2};
			std::sort(order, order + 3, [&](int a, int b) {
				return view.Depth(f.At(a, 1.0F)) > view.Depth(f.At(b, 1.0F));
			});
			for (int i : order) {
				DrawHandle(canvas, view, f, Nth(GizmoHandle::TranslateX, i), 1.0F);
				DrawHandle(canvas, view, f, Nth(GizmoHandle::ScaleX, i), 1.0F);
			}
			DrawHandle(canvas, view, f, GizmoHandle::TranslateView, 1.0F);
		}

		// While dragging, only the grabbed handle shows, with what it is doing:
		// the constraint line and the path travelled, the angle swept, or the
		// size it started from.
		void TransformGizmo::DrawDragging(GizmoCanvas& canvas, const GizmoView& view,
		                                  const GizmoFrame& f) const {
			HandleInfo info = Info(active);
			Vector4 color = HandleColor(active, f);
			Vector2 a, b;

			if (info.kind == Kind::Translate) {
				Vector3 start = pose.position - total.translation;
				int guides[2] = {-1, -1};
				if (info.shape == Shape::Axis) {
					guides[0] = info.axis;
				} else if (info.shape == Shape::Plane) {
					guides[0] = (info.axis + 1) % 3;
					guides[1] = (info.axis + 2) % 3;
				}
				for (int g : guides) {
					if (g < 0)
						continue;
					Vector3 reach = drag.axes[g] * (kGuideLength * f.unit);
					if (view.Project(start - reach, a) && view.Project(start + reach, b))
						canvas.Line(a, b, 1.0F, WithAlpha(kAxisColor[g], 0.55F));
				}
				if (view.Project(start, a)) {
					canvas.Line(a, f.centerScreen, kThinStroke, kTraceColor);
					canvas.Circle(a, 4.0F, kThinStroke, kTraceColor);
				}
				DrawHandle(canvas, view, f, active, 1.0F);
			} else if (info.kind == Kind::Rotate) {
				DrawHandle(canvas, view, f, active, 1.0F);
				if (info.shape != Shape::Trackball) {
					// The swept wedge, from where the grab was to where it is now.
					float radius = ((info.shape == Shape::View) ? f.viewRing : f.ring) * f.unit;
					const Vector3& n = drag.rotationAxis;
					Vector3 v0 = drag.startDirection, v1 = Vector3::Cross(n, v0);
					float angle = std::max(-2.0F * M_PI_F, std::min(2.0F * M_PI_F, drag.appliedAngle));
					int segments = std::max(2, int(std::fabs(angle) / (2.0F * M_PI_F) * kRingSegments));
					Vector2 prev;
					bool havePrev = false;
					for (int i = 0; i <= segments; i++) {
						float t = angle * float(i) / float(segments);
						Vector2 s;
						if (!view.Project(pose.position + (v0 * std::cos(t) + v1 * std::sin(t)) * radius, s)) {
							havePrev = false;
							continue;
						}
						if (havePrev)
							canvas.Triangle(f.centerScreen, prev, s, WithAlpha(color, 0.25F));
						if (i == 0 || i == segments)
							canvas.Line(f.centerScreen, s, kThinStroke, color);
						prev = s;
						havePrev = true;
					}
				}
			} else {
				// Scale: a ghost of the handle's starting size under the live one.
				float stretch = total.scale.x;
				if (info.shape == Shape::Axis)
					stretch = Component(total.scale, info.axis);
				else if (info.shape == Shape::Plane)
					stretch = Component(total.scale, (info.axis + 1) % 3);
				Vector4 ghost = WithAlpha(kTraceColor, 0.35F);
				if (info.shape == Shape::Uniform) {
					canvas.Circle(f.centerScreen, f.uniformRing * f.pixels, kThinStroke, ghost);
				} else if (info.shape == Shape::Axis) {
					if (view.Project(f.At(info.axis, f.scaleBox), a))
						canvas.Disc(a, f.scaleBoxHalf * f.pixels, ghost);
				} else {
					Vector3 corners[4];
					Vector2 q[4];
					f.Square(info.axis, f.scalePlaneMin, f.scalePlaneMax, corners);
					if (ProjectAll(view, corners, q, 4))
						canvas.Polyline(std::vector<Vector2>(q, q + 4), kThinStroke, ghost, true);
				}
				DrawHandle(canvas, view, f, active, stretch);
			}
			canvas.Disc(f.centerScreen, 2.5F, WithAlpha(kViewColor, 0.8F));
		}

		void TransformGizmo::DrawHandle(GizmoCanvas& canvas, const GizmoView& view,
		                                const GizmoFrame& f, GizmoHandle handle,
		                                float stretch) const {
			if (!visible.Contains(handle) || Shown(handle, f) <= 0.0F)
				return;
			HandleInfo info = Info(handle);
			Vector4 color = HandleColor(handle, f);
			bool hot = enabled.Contains(handle) && IsHot(handle);
			float width = hot ? kStrokeHot : kStroke;
			Vector2 a, b;

			switch (info.kind) {
				case Kind::Translate:
					if (info.shape == Shape::Axis) {
						Vector3 headBase = f.At(info.axis, f.arrowTip - f.arrowHeadLength);
						if (view.Project(f.At(info.axis, f.arrowStart), a) &&
						    view.Project(headBase, b))
							canvas.Line(a, b, width, color);
						DrawCone(canvas, view, headBase, f.At(info.axis, f.arrowTip),
						         f.arrowHeadRadius * f.unit * (hot ? 1.2F : 1.0F), color);
					} else if (info.shape == Shape::Plane) {
						Vector3 corners[4];
						Vector2 q[4];
						f.Square(info.axis, f.planeMin, f.planeMax, corners);
						if (!ProjectAll(view, corners, q, 4))
							break;
						canvas.Convex(q, 4, WithAlpha(color, color.w * (hot ? 0.6F : 0.35F)));
						canvas.Polyline(std::vector<Vector2>(q, q + 4), kThinStroke, color, true);
					} else {
						float radius = f.centerRadius * f.pixels;
						if (hot)
							canvas.Disc(f.centerScreen, radius, WithAlpha(color, 0.25F));
						canvas.Circle(f.centerScreen, radius, width, color);
					}
					break;

				case Kind::Rotate:
					if (info.shape == Shape::Axis) {
						std::vector<std::vector<Vector2>> runs;
						// The grabbed ring shows whole, so the turn can be followed round.
						RingRuns(view, f, f.axes[info.axis], f.ring * f.unit, handle == active,
						         runs);
						for (const std::vector<Vector2>& run : runs)
							canvas.Polyline(run, width, color, false);
					} else if (info.shape == Shape::View) {
						canvas.Circle(f.centerScreen, f.viewRing * f.pixels, width, color);
					} else if (hot) {
						// The trackball has no outline of its own; lit only when in reach.
						canvas.Disc(f.centerScreen, f.ring * f.pixels, WithAlpha(color, 0.08F));
					}
					break;

				case Kind::Scale:
					if (info.shape == Shape::Axis) {
						float along = f.scaleBox * stretch;
						// Without an arrow to ride on, a cube brings its own line.
						float from = f.arrows ? f.scaleBox : f.scaleStubStart;
						if (from != along && view.Project(f.At(info.axis, from), a) &&
						    view.Project(f.At(info.axis, along), b))
							canvas.Line(a, b, f.arrows ? kThinStroke : width, color);
						DrawCube(canvas, view, f, f.At(info.axis, along),
						         f.scaleBoxHalf * f.unit * (hot ? 1.2F : 1.0F), color);
					} else if (info.shape == Shape::Plane) {
						Vector3 corners[4];
						Vector2 q[4];
						f.Square(info.axis, f.scalePlaneMin * stretch, f.scalePlaneMax * stretch,
						         corners);
						if (!ProjectAll(view, corners, q, 4))
							break;
						canvas.Convex(q, 4, WithAlpha(color, color.w * (hot ? 0.3F : 0.12F)));
						canvas.Polyline(std::vector<Vector2>(q, q + 4), width, color, true);
					} else {
						canvas.Circle(f.centerScreen, f.uniformRing * f.pixels * stretch,
						              width + 0.5F, color);
					}
					break;
			}
		}
	} // namespace gui
} // namespace spades
