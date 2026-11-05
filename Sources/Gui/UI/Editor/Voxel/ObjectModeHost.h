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

#include <string>

#include <Core/Math.h>

namespace spades {
	namespace gui {
		struct GizmoPose;
		class ITransformTarget;
		struct GizmoTransform;

		/**
		 * What Object mode works through, offered by a host whose document is
		 * one object of a scene: the scene's objects are picked and placed here,
		 * and Edit mode edits the voxels of the picked one, which is then the
		 * editor's document.
		 *
		 * The scene keeps its own history of placements, apart from the voxel
		 * edits of Edit mode: undo in Object mode steps through it.
		 */
		class IObjectModeHost {
		public:
			virtual ~IObjectModeHost() {}

			/** The box the scene's objects fill together, in the world. */
			virtual void SceneBox(Vector3& lo, Vector3& hi) const = 0;

			/** Picks the object under the cursor, or nothing; false for nothing. */
			virtual bool PickObjectAtCursor() = 0;
			/** Whether an object is picked: Edit mode then edits it. */
			virtual bool HasPickedObject() const = 0;
			/** The picked object's name, for the hint line. */
			virtual std::string PickedObjectName() const = 0;

			/**
			 * Where the gizmo on the picked object sits: its pivot, turned with
			 * it. False when nothing is picked.
			 */
			virtual bool PickedObjectPose(GizmoPose& pose) const = 0;
			/**
			 * A drag of the picked object: it is shown moved by `total` from where
			 * the drag began, then kept as one step of the scene's history, or put
			 * back.
			 */
			virtual void BeginObjectDrag() = 0;
			virtual void PreviewObjectDrag(const GizmoTransform& total) = 0;
			virtual void CommitObjectDrag() = 0;
			virtual void CancelObjectDrag() = 0;

			virtual bool CanUndoObjects() const = 0;
			virtual bool CanRedoObjects() const = 0;
			virtual void UndoObjects() = 0;
			virtual void RedoObjects() = 0;

			/**
			 * The editor entered Edit mode (`entering`) or left it for Object
			 * mode. The editor's document is the picked object's voxels while it
			 * is in Edit mode; leaving, the host takes them back into the object.
			 */
			virtual void EditModeChanged(bool entering) = 0;

			/** The picked node's position, rotation and scale, as the transform
			 *  panel sets them by number while Object mode's tool is active. */
			virtual ITransformTarget& ObjectTransform() = 0;
		};
	} // namespace gui
} // namespace spades
