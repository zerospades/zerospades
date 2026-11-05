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

#include "ObjectTool.h"

#include <cmath>

#include <Gui/UI/Components/Gizmo/TransformGizmo.h>
#include <Gui/UI/Components/TransformPanel.h>

#include "ObjectModeHost.h"
#include "SubTool.h"
#include "VoxelEditContext.h"

namespace spades {
	namespace gui {
		namespace {
			// Every handle but the trackball: free rotation would answer to a
			// press anywhere inside the rings, where a click should pick.
			GizmoHandleSet ObjectHandles() {
				return GizmoHandleSet::All().Without(GizmoHandle::RotateTrackball);
			}

			// With the scale locked (IsScaleLocked), every scale handle scales
			// evenly: the factor the drag gave the axes it stretches (those
			// farthest from 1) goes to all three.
			GizmoTransform Proportional(GizmoTransform t) {
				if (!IsScaleLocked())
					return t;
				float f = t.scale.x;
				for (float c : {t.scale.y, t.scale.z}) {
					if (std::fabs(c - 1.0F) > std::fabs(f - 1.0F))
						f = c;
				}
				t.scale = MakeVector3(f, f, f);
				return t;
			}

			// Turns snap to 15 degrees, scaling to tenths; moves take the tool's
			// step.
			GizmoSnap ObjectSnap() {
				GizmoSnap snap;
				snap.rotation = 3.14159265F / 12.0F;
				snap.scale = 0.1F;
				return snap;
			}

			/**
			 * The gizmo on the picked object. The object follows a drag live and
			 * is kept on release as one step; a click off the gizmo picks
			 * whatever is under the cursor instead.
			 */
			class ObjectGizmoSubTool : public GizmoSubTool {
			public:
				ObjectGizmoSubTool() : GizmoSubTool(ObjectSnap(), ObjectHandles()) {}
				const char* Label() const override { return "Transform"; }

				void OnPointer(IVoxelEditContext& ed, const PointerInput& e) override {
					// With nothing picked there is no gizmo, and the gizmo's own
					// handling ignores a press it has nowhere to place: the press
					// picks whatever is under the cursor instead.
					GizmoPose pose;
					if (e.IsLeft() && e.IsDown() && !gizmo.IsDragging() && !CurrentPose(ed, pose)) {
						OnClickAway(ed);
						return;
					}
					GizmoSubTool::OnPointer(ed, e);
				}

			protected:
				bool CurrentPose(IVoxelEditContext& ed, GizmoPose& pose) override {
					IObjectModeHost* objects = ed.ObjectMode();
					return objects && objects->PickedObjectPose(pose);
				}

				void OnGizmoBegin(IVoxelEditContext& ed) override {
					if (IObjectModeHost* objects = ed.ObjectMode())
						objects->BeginObjectDrag();
				}

				void OnGizmoDrag(IVoxelEditContext& ed) override {
					if (IObjectModeHost* objects = ed.ObjectMode())
						objects->PreviewObjectDrag(Proportional(gizmo.Total()));
				}

				void OnGizmoEnd(IVoxelEditContext& ed, const GizmoTransform& total) override {
					if (IObjectModeHost* objects = ed.ObjectMode()) {
						objects->PreviewObjectDrag(Proportional(total));
						objects->CommitObjectDrag();
					}
				}

				void OnGizmoCancel(IVoxelEditContext& ed, const GizmoTransform&) override {
					if (IObjectModeHost* objects = ed.ObjectMode())
						objects->CancelObjectDrag();
				}

				// Off the gizmo, another object is picked; nothing gives the
				// selection up, back to Select.
				void OnClickAway(IVoxelEditContext& ed) override {
					IObjectModeHost* objects = ed.ObjectMode();
					if (objects && !objects->PickObjectAtCursor())
						ed.ReturnToSelect();
				}
			};
		} // namespace

		// An object lies anywhere, on no grid: every step is offered, the voxel
		// first.
		ObjectTool::ObjectTool()
		    : GizmoTool(std::unique_ptr<GizmoSubTool>(new ObjectGizmoSubTool()),
		                {1.0F, 0.5F, 0.1F}, 0.0F) {
			AddSnapOptions();
		}

		ITransformTarget* ObjectTool::TransformTarget(IVoxelEditContext& ed) {
			IObjectModeHost* objects = ed.ObjectMode();
			return objects ? &objects->ObjectTransform() : nullptr;
		}

		std::string ObjectTool::Hint(IVoxelEditContext& ed) {
			IObjectModeHost* objects = ed.ObjectMode();
			if (!objects || !objects->HasPickedObject())
				return "click an object to pick it  |  click nothing to select again";
			return objects->PickedObjectName() +
			       ":  drag the gizmo to move, turn or scale it  |  click another object to "
			       "pick it, nothing to select again  |  [Tab] edit its voxels";
		}
	} // namespace gui
} // namespace spades
