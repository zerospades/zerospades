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

#include "ObjectSelectTool.h"

#include <algorithm>

#include "ObjectModeHost.h"
#include "VoxelEditContext.h"

namespace spades {
	namespace gui {
		const char* const kObjectSelectToolId = "objects.select";
		const char* const kObjectTransformToolId = "objects.transform";

		namespace {
			const char* const kDiameterOption = "objects.diameter";
			const char* const kXRayOption = "objects.xray";
		} // namespace

		// --- The objects, as Rect and Brush take them ---------------------------

		int ObjectsOnScreen::TakeOnScreen(IVoxelEditContext& ed,
		                                  const std::function<bool(const Vector2&)>& inside,
		                                  bool through, bool removing) {
			IObjectModeHost* objects = ed.ObjectMode();
			return objects ? objects->SelectObjectsOnScreen(inside, through, removing) : 0;
		}

		bool ObjectsOnScreen::TakeAtCursor(IVoxelEditContext& ed, bool removing) {
			IObjectModeHost* objects = ed.ObjectMode();
			return objects && objects->SelectObjectAtCursor(removing ? IObjectModeHost::SelectHow::Remove
			                                                         : IObjectModeHost::SelectHow::Add);
		}

		void ObjectsOnScreen::Done(IVoxelEditContext& ed, bool removing) {
			IObjectModeHost* objects = ed.ObjectMode();
			if (!removing && objects && objects->HasSelectedObjects())
				ed.SwitchTool(kObjectTransformToolId); // the gizmo, at once
		}

		// --- Click ------------------------------------------------------------

		std::string ObjectClickSubTool::Hint(IVoxelEditContext&) {
			return "click an object to select it  |  [Shift/Ctrl] add or take it away  |  [RMB] "
			       "deselect it";
		}

		void ObjectClickSubTool::OnPointer(IVoxelEditContext& ed, const PointerInput& e) {
			IObjectModeHost* objects = ed.ObjectMode();
			if (!objects || !e.IsDown() || !(e.IsLeft() || e.IsRight()))
				return;
			using How = IObjectModeHost::SelectHow;
			const How how = e.IsRight() ? How::Remove : (e.shift || e.ctrl) ? How::Toggle : How::Only;
			objects->SelectObjectAtCursor(how);
			if (!e.IsRight() && objects->HasSelectedObjects())
				ed.SwitchTool(kObjectTransformToolId); // the gizmo, at once
		}

		// --- Select -----------------------------------------------------------

		ObjectSelectTool::ObjectSelectTool() {
			subs.push_back(std::unique_ptr<VoxelTool>(new ObjectClickSubTool()));
			rectIndex = int(subs.size());
			subs.push_back(std::unique_ptr<VoxelTool>(new RectSelectSubTool(objects, screen)));
			brushIndex = int(subs.size());
			subs.push_back(std::unique_ptr<VoxelTool>(new BrushSelectSubTool(objects, screen)));
			options.AddNumber(kDiameterOption, "Size", "Brush", 5.0F, 0);
			options.AddBool(kXRayOption, "X-Ray", "", screen.through);
		}

		void ObjectSelectTool::UpdateOptions(IVoxelEditContext&) {
			options.SetVisible(kDiameterOption, active == brushIndex);
			options.SetVisible(kXRayOption, active == rectIndex || active == brushIndex);
			options.SetNumber(kDiameterOption, screen.diameter);
			options.SetBool(kXRayOption, screen.through);
		}

		void ObjectSelectTool::OnOptionToggled(IVoxelEditContext&, const std::string& id,
		                                       bool value) {
			if (id == kXRayOption)
				screen.through = value;
		}

		void ObjectSelectTool::OnOptionNumberChanged(IVoxelEditContext&, const std::string& id,
		                                             float value, bool) {
			if (id == kDiameterOption)
				screen.diameter = std::max(ScreenSelectSettings::kMinDiameter,
				                           std::min(ScreenSelectSettings::kMaxDiameter, value));
		}
	} // namespace gui
} // namespace spades
