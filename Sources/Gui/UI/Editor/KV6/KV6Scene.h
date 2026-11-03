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
#include <vector>

#include <Core/Math.h>
#include <Core/VoxelModel2KV6.h>

namespace spades {
	namespace client {
		class IModel;
		class IRenderer;
	} // namespace client
	namespace gui {
		struct GizmoView;
		struct GizmoPose;
		struct GizmoTransform;

		/**
		 * A .2kv6 scene as the model editor's Object mode shows it: its objects
		 * in place, each with the transform it and its parents give it, as the
		 * game composes them (a voxel `i` of an object lands at its world matrix
		 * times `i`; the model's own origin plays no part).
		 *
		 * One object is picked at a time: the gizmo moves, turns and scales it,
		 * and Edit mode edits its voxels. The placements make a history of
		 * their own, apart from the voxel edits.
		 */
		class KV6Scene {
		public:
			/** Reads the scene at `path`; false (the scene untouched) when it
			 *  cannot be read or holds no object with voxels. */
			bool Load(const std::string& path);
			/** Writes the scene to `path`, through a temporary file. */
			bool Save(const std::string& path) const;
			/** A scene of one object holding `model`. */
			void StartWith(Handle<VoxelModel> model, const std::string& name);

			// --- Objects ------------------------------------------------------
			/** Objects holding voxels, the ones that can be picked. */
			int Count() const { return int(entries.size()); }
			VoxelObject& Object(int index) { return *entries[std::size_t(index)].object; }
			const VoxelObject& Object(int index) const {
				return *entries[std::size_t(index)].object;
			}
			/** Where object `index`'s voxel indices land: its parents' transforms
			 *  and its own. */
			Matrix4 WorldMatrix(int index) const;

			int Picked() const { return picked; }
			void Pick(int index) { picked = index; }
			/** The object hit first along the view's ray through `cursor`, or -1. */
			int ObjectAt(const GizmoView& view, const Vector2& cursor) const;
			/** The box object `index` fills in the world, from voxel edge to edge. */
			void Box(int index, Vector3& lo, Vector3& hi) const;
			/** The box every object fills together. */
			void Box(Vector3& lo, Vector3& hi) const;
			/** The middle of object `index` and its largest extent, in the world. */
			void Bounds(int index, Vector3& centre, float& size) const;
			/** The same for every object at once. */
			void Bounds(Vector3& centre, float& size) const;

			// --- Placing the picked object ------------------------------------
			/** Where the gizmo on the picked object sits: its pivot, turned with
			 *  it. False when nothing is picked. */
			bool PickedPose(GizmoPose& pose) const;
			void BeginDrag();
			/** Shows the picked object moved by `total` from where the drag began. */
			void PreviewDrag(const GizmoTransform& total);
			/** Keeps the drag as one step; false when it changed nothing. */
			bool CommitDrag();
			void CancelDrag();
			/** Moves object `index` so its voxels stay put in the world, its
			 *  indices having been shifted by `shift` (a resized volume). */
			void CompensateShift(int index, const Vector3& shift);

			bool CanUndo() const { return undoTop > 0; }
			bool CanRedo() const { return undoTop < int(history.size()); }
			void Undo();
			void Redo();
			/** Changes since the scene was loaded or saved. */
			bool Changed() const { return changeCount != savedChangeCount; }
			void MarkChanged() { changeCount++; }
			void MarkSaved() { savedChangeCount = changeCount; }

			// --- Drawing ------------------------------------------------------
			/** Every shown object in place, and an outline round the picked one. */
			void Render(client::IRenderer& renderer);
			/** The render model of object `index` is stale (its voxels changed). */
			void InvalidateModel(int index);

		private:
			struct Entry {
				VoxelObject* object;
				// Its parents, the root first; a parent may be moved, so the
				// transform they give is worked out when asked for.
				std::vector<const VoxelObject*> parents;
				Handle<client::IModel> renderModel;
				const VoxelModel* renderedFrom = nullptr; // what renderModel shows
			};
			struct Placement {
				Vector3 position;
				Vector4 rotation;
				Vector3 scale;
			};
			struct Step {
				int index;
				Placement before, after;
			};

			std::vector<VoxelObject> roots;
			// What the file carries besides its objects, kept as it came so a
			// save writes it back (the editor does not change it yet).
			std::vector<MetadataEntry> metadata;
			std::vector<AnimationClip> clips;
			std::vector<Entry> entries;
			int picked = -1;

			bool dragging = false;
			Placement dragStart;

			std::vector<Step> history;
			int undoTop = 0;
			long changeCount = 0, savedChangeCount = 0;

			void Index();
			static Placement PlacementOf(const VoxelObject& o);
			static void Place(VoxelObject& o, const Placement& p);
			// The rotation and the scale the picked object's parents give it.
			void ParentFrame(int index, Quaternion& rotation, Vector3& scale) const;
		};
	} // namespace gui
} // namespace spades
