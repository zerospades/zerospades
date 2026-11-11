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

#include "SelectSubTools.h"

#include <algorithm>
#include <cmath>

#include "VoxelEditContext.h"

namespace spades {
	namespace gui {
		namespace {
			const Vector4 kHover = MakeVector4(0.3F, 0.8F, 1.0F, 0.95F);
			const Vector4 kAdding = MakeVector4(0.35F, 0.75F, 1.0F, 0.95F);
			const Vector4 kRemoving = MakeVector4(1.0F, 0.42F, 0.36F, 0.95F);
			const Vector4 kBrushIdle = MakeVector4(1.0F, 1.0F, 1.0F, 0.7F);
			// Pixels a press may move and still be a click, not a rectangle.
			constexpr float kClickReach = 4.0F;
			// What [ and ] change the brush's diameter by.
			constexpr float kSizeStep = 1.25F;

			const char* const kXRayOn = "  |  X-Ray: hidden voxels too";

			const char* const kXRayOption = "select.xray";
			const char* const kDiameterOption = "select.diameter";
			// Screen pixels one press of the size box's arrows adds. The brush
			// is measured on the screen, not in voxels, so it steps in a stride
			// that tells at a glance rather than one voxel at a time.
			constexpr float kDiameterStep = 5.0F;

			void Take(IVoxelEditContext& ed, const std::vector<IntVector3>& cells, bool removing) {
				if (cells.empty())
					return;
				if (removing)
					ed.DeselectCells(cells);
				else
					ed.SelectCells(cells);
			}

			float DistanceToSegment(const Vector2& p, const Vector2& a, const Vector2& b) {
				const Vector2 ab = b - a;
				const float lengthSq = Vector2::Dot(ab, ab);
				float t = lengthSq > 0.0F ? Vector2::Dot(p - a, ab) / lengthSq : 0.0F;
				t = std::max(0.0F, std::min(1.0F, t));
				return (p - (a + ab * t)).GetLength();
			}
		} // namespace

		void AddXRayOption(ToolOptions& options, const ScreenSelectSettings& settings) {
			options.AddBool(kXRayOption, "X-Ray", "", settings.through);
		}

		void AddBrushSizeOption(ToolOptions& options) {
			options.AddNumber(kDiameterOption, "Size", "Brush", kDiameterStep, 0);
		}

		void SyncScreenOptions(ToolOptions& options, const ScreenSelectSettings& settings) {
			// Whatever set them -- a box, a bracket key, another sub-tool -- the
			// options show the settings as they stand.
			options.SetBool(kXRayOption, settings.through);
			options.SetNumber(kDiameterOption, settings.diameter);
		}

		bool ApplyXRayOption(ScreenSelectSettings& settings, const std::string& id, bool value) {
			if (id != kXRayOption)
				return false;
			settings.through = value;
			return true;
		}

		bool ApplyBrushSizeOption(ScreenSelectSettings& settings, const std::string& id,
		                          float value) {
			if (id != kDiameterOption)
				return false;
			settings.diameter = std::max(ScreenSelectSettings::kMinDiameter,
			                             std::min(ScreenSelectSettings::kMaxDiameter, value));
			return true;
		}

		// --- Voxels, as the screen selections take them ------------------------

		int VoxelsOnScreen::TakeOnScreen(IVoxelEditContext& ed,
		                                 const std::function<bool(const Vector2&)>& inside,
		                                 bool through, bool removing) {
			const std::vector<IntVector3> cells = ed.CellsOnScreen(inside, through);
			Take(ed, cells, removing);
			return int(cells.size());
		}

		bool VoxelsOnScreen::TakeAtCursor(IVoxelEditContext& ed, bool removing) {
			ed.DoPick();
			if (!ed.HasPick())
				return false;
			Take(ed, std::vector<IntVector3>(1, ed.PickSolid()), removing);
			return true;
		}

		// --- Plane -------------------------------------------------------------

		std::string PlaneSelectSubTool::Hint(IVoxelEditContext&) {
			return "click a face: [LMB] select its flat face  |  [RMB] deselect it";
		}

		void PlaneSelectSubTool::OnPointer(IVoxelEditContext& ed, const PointerInput& e) {
			if (!e.IsDown() || !(e.IsLeft() || e.IsRight()))
				return;
			ed.DoPick();
			if (!ed.HasPick())
				return;
			// The face clicked is the one toward the empty cell the ray passed.
			const IntVector3 solid = ed.PickSolid();
			const std::vector<IntVector3> face = ed.CoplanarFaces(solid, ed.PickPlace() - solid);
			Take(ed, face, e.IsRight());
			ed.SetStatus((e.IsRight() ? "Deselected a face of " : "Selected a face of ") +
			             std::to_string(face.size()) + " voxels");
		}

		void PlaneSelectSubTool::DrawScene(IVoxelEditContext& ed) {
			ed.DoPick();
			if (!ed.HasPick())
				return;
			// The voxel, and the face whose plane a click would take.
			const IntVector3 v = ed.PickSolid();
			const IntVector3 n = ed.PickPlace() - v;
			ed.DrawCellOutline(v.x, v.y, v.z, kHover);
			const Vector3 middle = MakeVector3(float(v.x), float(v.y), float(v.z)) +
			                       MakeVector3(float(n.x), float(n.y), float(n.z)) * 0.5F;
			const Vector3 u = n.x != 0 ? MakeVector3(0, 0.5F, 0) : MakeVector3(0.5F, 0, 0);
			const Vector3 w = n.z != 0 ? MakeVector3(0, 0.5F, 0) : MakeVector3(0, 0, 0.5F);
			const Vector3 corners[4] = {middle - u - w, middle + u - w, middle + u + w, middle - u + w};
			for (int i = 0; i < 4; i++)
				ed.DrawLine3D(corners[i], corners[(i + 1) % 4], kAdding);
			ed.DrawLine3D(corners[0], corners[2], kAdding);
			ed.DrawLine3D(corners[1], corners[3], kAdding);
		}

		// --- Rect --------------------------------------------------------------

		std::string RectSelectSubTool::Hint(IVoxelEditContext&) {
			std::string hint = "drag a rectangle: [LMB] select what it takes in  |  [RMB] deselect it";
			return settings.through ? hint + kXRayOn : hint;
		}

		std::string RectSelectSubTool::EscapeLabel(IVoxelEditContext&) {
			return dragging ? "cancel the rectangle" : std::string();
		}

		AABB2 RectSelectSubTool::Rect() const {
			const float x0 = std::min(start.x, end.x), y0 = std::min(start.y, end.y);
			return AABB2(x0, y0, std::fabs(end.x - start.x), std::fabs(end.y - start.y));
		}

		void RectSelectSubTool::OnPointer(IVoxelEditContext& ed, const PointerInput& e) {
			if (e.IsDown() && (e.IsLeft() || e.IsRight())) {
				dragging = true;
				removing = e.IsRight();
				start = end = e.pos;
				return;
			}
			if (!dragging)
				return;
			if (e.IsDrag()) {
				end = e.pos;
				return;
			}
			if (!e.IsUp() || e.IsRight() != removing)
				return;
			dragging = false;
			end = e.pos;
			const AABB2 rect = Rect();
			if (rect.GetWidth() < kClickReach && rect.GetHeight() < kClickReach) {
				// A click: what is under it.
				target.TakeAtCursor(ed, removing);
				target.Done(ed, removing);
				return;
			}
			const int taken = target.TakeOnScreen(
			  ed,
			  [&](const Vector2& p) {
				  return p.x >= rect.GetMinX() && p.x <= rect.GetMaxX() && p.y >= rect.GetMinY() &&
				         p.y <= rect.GetMaxY();
			  },
			  settings.through, removing);
			ed.SetStatus((removing ? "Deselected " : "Selected ") + std::to_string(taken) + " " +
			             target.Noun());
			target.Done(ed, removing);
		}

		void RectSelectSubTool::DrawOverlay(IVoxelEditContext& ed) {
			if (dragging)
				ed.DrawScreenRect(Rect(), removing ? kRemoving : kAdding);
		}

		// --- Brush -------------------------------------------------------------

		std::string BrushSelectSubTool::Hint(IVoxelEditContext&) {
			std::string hint = "[LMB] paint a selection  |  [RMB] erase it  |  [ and ] size (" +
			                   std::to_string(int(std::lround(settings.diameter))) + " px)";
			return settings.through ? hint + kXRayOn : hint;
		}

		void BrushSelectSubTool::Sweep(IVoxelEditContext& ed, const Vector2& from, const Vector2& to) {
			const float radius = settings.diameter * 0.5F;
			// Between two events the pointer may have moved far: everything the
			// brush passed over on the way is taken, not only where it stopped.
			target.TakeOnScreen(
			  ed, [&](const Vector2& p) { return DistanceToSegment(p, from, to) <= radius; },
			  settings.through, removing);
		}

		void BrushSelectSubTool::OnPointer(IVoxelEditContext& ed, const PointerInput& e) {
			if (e.IsDown() && (e.IsLeft() || e.IsRight())) {
				painting = true;
				removing = e.IsRight();
				last = e.pos;
				Sweep(ed, e.pos, e.pos);
				return;
			}
			if (!painting)
				return;
			if (e.IsDrag()) {
				Sweep(ed, last, e.pos);
				last = e.pos;
			} else if (e.IsUp() && e.IsRight() == removing) {
				painting = false;
				target.Done(ed, removing);
			}
		}

		void BrushSelectSubTool::OnKey(IVoxelEditContext& ed, const KeyInput& e) {
			if (!e.IsDown() || (e.key != "[" && e.key != "]"))
				return;
			const float d = e.key == "]" ? settings.diameter * kSizeStep : settings.diameter / kSizeStep;
			settings.diameter = std::max(ScreenSelectSettings::kMinDiameter,
			                             std::min(ScreenSelectSettings::kMaxDiameter, d));
			ed.SetStatus("Brush " + std::to_string(int(std::lround(settings.diameter))) + " px");
		}

		void BrushSelectSubTool::DrawOverlay(IVoxelEditContext& ed) {
			const Vector4 color = !painting ? kBrushIdle : removing ? kRemoving : kAdding;
			ed.DrawScreenCircle(ed.CursorPos(), settings.diameter * 0.5F, color);
		}
	} // namespace gui
} // namespace spades
