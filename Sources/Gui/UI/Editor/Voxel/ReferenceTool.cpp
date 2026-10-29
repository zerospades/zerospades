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

#include "ReferenceTool.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include <Core/LocalFileSystem.h>
#include <Gui/UI/Components/Gizmo/TransformGizmo.h>

#include "SubTool.h"
#include "VoxelEditContext.h"

namespace spades {
	namespace gui {
		namespace {
			const char* const kLoadOption = "reference.load";
			const char* const kClearOption = "reference.clear";
			const char* const kShowOption = "reference.show";
			const char* const kFlipOption = "reference.flip";
			const char* const kStraightenOption = "reference.straighten";
			const char* const kSizeOption = "reference.size";
			const char* const kCentreOption[3] = {"reference.x", "reference.y", "reference.z"};
			const char* const kOpacityOption = "reference.opacity";
			const char* const kFileOption = "reference.file";

			const char* const kAxisName[3] = {"X", "Y", "Z"};
			const char* const kAxisLetter[3] = {"x", "y", "z"};

			// The smallest an image is scaled to, in voxels across.
			constexpr float kMinSize = 0.1F;

			// Every handle but the trackball, as Object mode's: free rotation
			// would answer to a press anywhere inside the rings.
			GizmoHandleSet ImageHandles() {
				return GizmoHandleSet::All().Without(GizmoHandle::RotateTrackball);
			}

			/**
			 * The image across one axis, handled with a gizmo on it: its arrows
			 * and squares move it, its rings turn it, and its cubes scale it,
			 * keeping its shape. It follows a drag live, as nothing about it is
			 * journaled, and Escape puts it back where the drag began.
			 */
			class ImageGizmoSubTool : public GizmoSubTool {
			public:
				explicit ImageGizmoSubTool(int axis)
				    : GizmoSubTool(GizmoSnap(), ImageHandles()), axis(axis) {}
				const char* Label() const override { return kAxisName[axis]; }

			protected:
				bool CurrentPose(IVoxelEditContext& ed, GizmoPose& pose) override {
					const ReferenceImage& ref = ed.Reference(axis);
					if (!ref.Loaded() || !ref.visible)
						return false; // nothing to handle
					pose.position = ref.centre;
					ReferenceFrame(axis, ref.rotation, pose.axes);
					return true;
				}

				void OnGizmoBegin(IVoxelEditContext& ed) override { start = ed.Reference(axis); }

				void OnGizmoDrag(IVoxelEditContext& ed) override {
					if (!start)
						return;
					const GizmoTransform& total = gizmo.Total();
					ReferenceImage& ref = ed.Reference(axis);
					ref.centre = start->centre + total.translation;
					ref.rotation =
					  (total.rotation * Quaternion(start->rotation)).Normalize().v;
					// An image keeps its shape: whichever of its two sides was
					// pulled the most scales it whole.
					const float su = total.scale.x, sv = total.scale.y;
					const float factor = std::fabs(su - 1.0F) >= std::fabs(sv - 1.0F) ? su : sv;
					ref.size = std::max(kMinSize, start->size * std::fabs(factor));
				}

				void OnGizmoEnd(IVoxelEditContext& ed, const GizmoTransform&) override {
					OnGizmoDrag(ed); // already where the drag left it
					start.reset();
				}

				void OnGizmoCancel(IVoxelEditContext& ed, const GizmoTransform&) override {
					if (start) {
						ReferenceImage& ref = ed.Reference(axis);
						ref.centre = start->centre;
						ref.rotation = start->rotation;
						ref.size = start->size;
					}
					start.reset();
				}

			private:
				int axis;
				// The image as the drag began, which the whole drag applies to.
				std::optional<ReferenceImage> start;
			};
		} // namespace

		ReferenceTool::ReferenceTool() {
			for (int axis = 0; axis < 3; axis++)
				subs.emplace_back(new ImageGizmoSubTool(axis));
			options.AddAction(kLoadOption, "Load Image...");
			options.AddAction(kClearOption, "Remove");
			options.AddBool(kShowOption, "Show", "", true);
			options.AddBool(kFlipOption, "Mirror");
			options.AddAction(kStraightenOption, "Straighten");
			options.AddNumber(kSizeOption, "Scale", "Size", 1.0F, 1);
			for (int a = 0; a < 3; a++)
				options.AddNumber(kCentreOption[a], kAxisName[a], "Centre", 0.5F, 1);
			options.AddNumber(kOpacityOption, "Opacity", "Look", 0.05F, 2);
			options.AddLabel(kFileOption);
		}

		void ReferenceTool::UpdateOptions(IVoxelEditContext& ed) {
			const ReferenceImage& ref = ed.Reference(active);
			const bool loaded = ref.Loaded();
			options.SetNumber(kSizeOption, ref.size);
			for (int a = 0; a < 3; a++)
				options.SetNumber(kCentreOption[a], (&ref.centre.x)[a]);
			options.SetNumber(kOpacityOption, ref.opacity);
			options.SetBool(kShowOption, ref.visible);
			options.SetBool(kFlipOption, ref.flipped);
			for (const char* id : {kClearOption, kShowOption, kFlipOption, kStraightenOption,
			                       kSizeOption, kCentreOption[0], kCentreOption[1],
			                       kCentreOption[2], kOpacityOption})
				options.SetEnabled(id, loaded);
			const Vector4& r = ref.rotation;
			options.SetEnabled(kStraightenOption,
			                   loaded && !(r.x == 0.0F && r.y == 0.0F && r.z == 0.0F));
			options.SetLabel(kFileOption, loaded ? LocalFileSystem::GetFileName(ref.path)
			                                     : std::string("no image"));
		}

		void ReferenceTool::OnOptionToggled(IVoxelEditContext& ed, const std::string& id,
		                                    bool value) {
			ReferenceImage& ref = ed.Reference(active);
			if (id == kShowOption)
				ref.visible = value;
			else if (id == kFlipOption)
				ref.flipped = value;
		}

		void ReferenceTool::OnOptionNumberChanged(IVoxelEditContext& ed, const std::string& id,
		                                          float value, bool) {
			// Nothing here is journaled, so a value shows as soon as it is typed.
			ReferenceImage& ref = ed.Reference(active);
			if (id == kSizeOption)
				ref.size = std::max(kMinSize, value);
			else if (id == kOpacityOption)
				ref.opacity = std::max(0.0F, std::min(1.0F, value));
			for (int a = 0; a < 3; a++) {
				if (id == kCentreOption[a])
					(&ref.centre.x)[a] = value;
			}
		}

		void ReferenceTool::OnAction(IVoxelEditContext& ed, const std::string& id) {
			if (id == kLoadOption)
				ed.LoadReferenceImage(active);
			else if (id == kClearOption)
				ed.ClearReferenceImage(active);
			else if (id == kStraightenOption)
				ed.Reference(active).rotation = MakeVector4(0.0F, 0.0F, 0.0F, 1.0F);
		}

		std::string ReferenceTool::Hint(IVoxelEditContext& ed) {
			const ReferenceImage& ref = ed.Reference(active);
			if (!ref.Loaded())
				return std::string("[Load Image...] an image across ") + kAxisLetter[active] +
				       " to model from  |  [1/2/3] the image across X, Y or Z";
			if (!ref.visible)
				return "[Show] it to handle it  |  [1/2/3] the image across X, Y or Z";
			return "drag the gizmo to move, turn or scale it  |  [1/2/3] the image across X, Y "
			       "or Z  |  [Esc] cancel the drag";
		}
	} // namespace gui
} // namespace spades
