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

#include <memory>
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
		 * Where a node of a scene's tree is: its place among the scene's top
		 * objects, then among each parent's children. Empty is the scene
		 * itself. A path names a node only until the tree changes; what the
		 * scene keeps by path (its selection, its folded nodes, its history)
		 * it keeps right across its own changes.
		 */
		using NodePath = std::vector<int>;

		/**
		 * A .2kv6 scene as the model editor's Object mode shows it: its objects
		 * in place, each with the transform it and its parents give it, as the
		 * game composes them (a voxel `i` of an object lands at its world matrix
		 * times `i`; the model's own origin plays no part).
		 *
		 * Any nodes may be selected; the active one among them is the picked
		 * object when it holds voxels: the gizmo moves, turns and scales it,
		 * and Edit mode edits its voxels. The placements, the tree's shape and
		 * what is hidden make a history of their own, apart from the voxel
		 * edits.
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

			/** The active node's object, when it holds voxels; else -1. */
			int Picked() const { return picked; }
			/** Selects object `index` alone, or nothing for -1. */
			void Pick(int index);
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

			// --- The tree -----------------------------------------------------
			/** The scene's top objects, and all under them. */
			const std::vector<VoxelObject>& Roots() const { return roots; }
			/** The node at `path`; null for the scene itself or for no node. */
			const VoxelObject* Node(const NodePath& path) const;
			/** Object `index`'s place in the tree. */
			const NodePath& PathOf(int index) const { return entries[std::size_t(index)].path; }
			/** The object at `path`, when it holds voxels; else -1. */
			int ObjectIndexAt(const NodePath& path) const;
			/** Whether the node is drawn: neither it nor any node above it hidden. */
			bool Shown(const NodePath& path) const;
			bool Shown(int index) const;
			/** Bumped whenever the tree's nodes take other paths. */
			long ShapeVersion() const { return shapeVersion; }

			// --- Selecting ----------------------------------------------------
			const std::vector<NodePath>& Selection() const { return selection; }
			/** The node the selection acts about; empty for none. */
			const NodePath& Active() const { return active; }
			bool IsSelected(const NodePath& path) const;
			/** Selects `nodes` (the scene itself left out), `active` among them. */
			void Select(std::vector<NodePath> nodes, NodePath active);

			// --- Folded nodes (how the tree is shown, kept across its changes)
			bool IsFolded(const NodePath& path) const;
			void SetFolded(const NodePath& path, bool folded);

			// --- Changing the tree (each change one step of the history) ------
			/**
			 * Why `nodes` cannot go under `parent`, before its child `position`
			 * (past the last for -1), as it stands now; empty when they can. They
			 * keep where they are in the world, so a node that would be skewed
			 * there cannot go; neither can a locked node, a node into itself or
			 * what is under it, or a node whose name its new parent already has.
			 */
			std::string WhyNotMove(const std::vector<NodePath>& nodes, const NodePath& parent,
			                       int position) const;
			/** Moves them there as WhyNotMove allows, in the order the tree has
			 *  them, and selects them. False when refused (`why` says why) or
			 *  when they are where they were already. */
			bool Move(const std::vector<NodePath>& nodes, const NodePath& parent, int position,
			          std::string& why);
			/** Hides the node (and so all under it) or shows it again; a hidden
			 *  node and all under it leave the selection. */
			void SetHidden(const NodePath& path, bool hidden);

			/**
			 * Why the node cannot be named `name`, empty when it can: a locked
			 * node keeps its name, and a name is 1 to 255 bytes, holds no '/',
			 * does not start with '#', and is none of its siblings'.
			 */
			std::string WhyNotRename(const NodePath& path, const std::string& name) const;
			/** Names the node `name`, the clips' tracks of it and of all under it
			 *  following, as one step. False when refused (`why` says why) or
			 *  when that is its name already. */
			bool Rename(const NodePath& path, const std::string& name, std::string& why);

			/**
			 * The node as one model, as `--2kv6-tools extract` makes it: its own
			 * voxels when nothing hangs under it, else it composed with all under
			 * it (hidden ones left out) in its own frame, its pivot on its origin.
			 * Null, `why` saying why, when that holds no voxel.
			 */
			Handle<VoxelModel> ModelOf(const NodePath& path, std::string& why) const;
			/** Whether the node, or anything under it, holds voxels. */
			bool HoldsVoxels(const NodePath& path) const;

			/** An object as copied: with all under it, its voxels its own, and
			 *  where it was in the world. */
			struct CopiedObject {
				VoxelObject object;
				Matrix4 world;
			};
			/** The nodes (a node under another one copied goes with it), in the
			 *  tree's order. */
			std::vector<CopiedObject> Copy(const std::vector<NodePath>& nodes) const;
			/**
			 * Copies of `objects` after `parent`'s children, where they were in
			 * the world, each named apart from its new siblings (".001" added,
			 * as needed); selects them. False (`why` says why) when one would be
			 * skewed there.
			 */
			bool Paste(const std::vector<CopiedObject>& objects, const NodePath& parent,
			           std::string& why);
			/** A new object of `model` after `parent`'s children, its pivot on
			 *  the parent's origin, named `name` made unique; selects it. */
			bool AddChild(const NodePath& parent, Handle<VoxelModel> model, const std::string& name,
			              std::string& why);
			/** Why the nodes cannot be deleted: one is, or holds, a locked node;
			 *  empty when they can. */
			std::string WhyNotDelete(const std::vector<NodePath>& nodes) const;
			/** Deletes the nodes with all under them, and the clips' tracks of
			 *  them, as WhyNotDelete allows. */
			bool Delete(const std::vector<NodePath>& nodes, std::string& why);

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
			/** Every shown object in place, and outlines round the selected ones. */
			void Render(client::IRenderer& renderer);
			/** The render model of object `index` is stale (its voxels changed). */
			void InvalidateModel(int index);

		private:
			struct Entry {
				VoxelObject* object;
				NodePath path;
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
				std::vector<TransformKeyframe> keyframes; // left out of a gizmo drag's
			};
			/**
			 * Nodes taken out of the tree at `from` and put back at `to`: `from`
			 * in the tree's order, taken out last first, and `to[i]`, where
			 * `from[i]` goes, put in first first. Its reverse is `to` to `from`.
			 */
			struct Relocation {
				std::vector<NodePath> from, to;
				Relocation Reversed() const { return Relocation{to, from}; }
				/** Where the node at `path` is once the nodes have been moved. */
				NodePath Apply(const NodePath& path) const;
			};
			struct MoveStep {
				Relocation relocation;
				// Each moved node's placement where it was and where it went,
				// which keeps it where it is in the world.
				std::vector<Placement> before, after;
				// The clips, their tracks following the nodes they key.
				std::vector<AnimationClip> clipsBefore, clipsAfter;
				std::vector<NodePath> selectionBefore, selectionAfter;
				NodePath activeBefore, activeAfter;
			};
			/** Nodes put into the tree (pasted, added) or taken out of it
			 *  (deleted): `paths` in the tree's order, where they are in it. */
			struct InsertStep {
				std::vector<NodePath> paths;
				std::vector<VoxelObject> nodes;
				std::vector<AnimationClip> clipsBefore, clipsAfter;
				std::vector<NodePath> selectionBefore, selectionAfter;
				NodePath activeBefore, activeAfter;
			};
			struct RenameStep {
				std::string before, after;
				std::vector<AnimationClip> clipsBefore, clipsAfter;
			};
			struct Step {
				enum class Kind { Place, Hide, Move, Insert, Remove, Rename } kind = Kind::Place;
				NodePath path;          // Place, Hide
				Placement before, after; // Place
				bool hidden = false;     // Hide: what it became
				std::unique_ptr<MoveStep> move;
				std::unique_ptr<InsertStep> insert; // Insert, Remove
				std::unique_ptr<RenameStep> rename; // Rename, at `path`
			};
			std::vector<VoxelObject> roots;
			// What the file carries besides its objects, kept as it came so a
			// save writes it back (the editor changes only the clips' tracks,
			// which follow the objects they key).
			std::vector<MetadataEntry> metadata;
			std::vector<AnimationClip> clips;
			std::vector<Entry> entries;
			std::vector<NodePath> selection;
			NodePath active;
			int picked = -1;
			std::vector<NodePath> folded;
			long shapeVersion = 0;

			bool dragging = false;
			Placement dragStart;

			std::vector<Step> history;
			int undoTop = 0;
			long changeCount = 0, savedChangeCount = 0;

			void Index();
			/** After the selection or the tree changed: the picked object again. */
			void SyncPicked();
			/** A fresh tree (loaded, started): nothing kept of the last one's. */
			void Restart();
			void Record(Step&& step);
			VoxelObject* MutableNode(const NodePath& path);
			std::vector<VoxelObject>* ChildrenOf(const NodePath& parent);
			const std::vector<VoxelObject>* ChildrenOf(const NodePath& parent) const;
			/** What the node at `path` and its parents do to its voxels. */
			Matrix4 NodeMatrix(const NodePath& path) const;
			/** Its name and its parents', joined by '/' as tracks and scripts
			 *  name it. */
			std::string NamePath(const NodePath& path) const;
			/** The move WhyNotMove allows, worked out in full (but the
			 *  selection after it); false with `why` when it cannot be made,
			 *  and with `why` empty when it would change nothing. */
			bool PlanMove(const std::vector<NodePath>& nodes, const NodePath& parent, int position,
			              MoveStep& plan, std::string& why) const;
			void ApplyHidden(const NodePath& path, bool hidden);
			/** Puts `nodes` at `paths` (in the tree's order), or takes the
			 *  nodes at `paths` out, keeping what the scene tracks by path. */
			void InsertNodes(const std::vector<NodePath>& paths, const std::vector<VoxelObject>& nodes);
			void RemoveNodes(const std::vector<NodePath>& paths);
			/** The nodes, those under another one left out, in the tree's order. */
			std::vector<NodePath> TopMost(const std::vector<NodePath>& nodes) const;
			/** Puts `nodes` after `parent`'s children, named apart from their
			 *  siblings, and selects them, as one step. */
			void InsertUnder(const NodePath& parent, std::vector<VoxelObject> nodes);
			/** Whether `parent` is somewhere nodes can go: the scene, or a node. */
			bool CanHold(const NodePath& parent) const;
			/** Records `step` as done: the tree as it is now is after it. */
			void RecordInsert(Step::Kind kind, std::unique_ptr<InsertStep> step);
			/** Takes `r.from` out and puts them at `r.to` with `placements`,
			 *  keeping what the scene tracks by path where it went. */
			void Relocate(const Relocation& r, const std::vector<Placement>& placements);
			static Placement PlacementOf(const VoxelObject& o, bool withKeyframes = false);
			static void Place(VoxelObject& o, const Placement& p, bool withKeyframes = false);
			// The rotation and the scale the picked object's parents give it.
			void ParentFrame(int index, Quaternion& rotation, Vector3& scale) const;
		};
	} // namespace gui
} // namespace spades
