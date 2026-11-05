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

#include "KV6Scene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <functional>
#include <limits>

#include <Client/IModel.h>
#include <Client/IRenderer.h>
#include <Core/LocalFileSystem.h>
#include <Core/StdStream.h>
#include <Core/VoxelModelLoader.h>
#include <Core/VoxelModel.h>
#include <Gui/UI/Components/EditorTheme.h>
#include <Gui/UI/Components/Gizmo/GizmoView.h>
#include <Gui/UI/Components/Gizmo/TransformGizmo.h>
#include <Gui/UI/Editor/Voxel/VoxelOverlayLines.h>

namespace spades {
	namespace gui {
		namespace fs = LocalFileSystem;

		namespace {
			Matrix4 LocalMatrix(const VoxelObject& o) {
				return LocalTransform(o.position, o.rotation, o.scale);
			}

			bool StartsWith(const NodePath& path, const NodePath& prefix) {
				return path.size() >= prefix.size() &&
				       std::equal(prefix.begin(), prefix.end(), path.begin());
			}

			NodePath ParentOf(const NodePath& path) {
				return NodePath(path.begin(), path.end() - 1);
			}

			// `q` once the node at `removed` is taken out of the tree; false when
			// `q` is that node or under it, and so goes with it.
			bool AfterRemoval(NodePath& q, const NodePath& removed) {
				if (StartsWith(q, removed))
					return false;
				const std::size_t level = removed.size() - 1;
				if (q.size() > level && std::equal(removed.begin(), removed.end() - 1, q.begin()) &&
				    q[level] > removed[level])
					q[level]--;
				return true;
			}

			// `q` once a node is put in at `inserted`.
			void AfterInsertion(NodePath& q, const NodePath& inserted) {
				const std::size_t level = inserted.size() - 1;
				if (q.size() > level && std::equal(inserted.begin(), inserted.end() - 1, q.begin()) &&
				    q[level] >= inserted[level])
					q[level]++;
			}

			// Voxels of its own for `o` and everything under it.
			void CloneModels(VoxelObject& o) {
				if (o.model) {
					const VoxelModel& from = *o.model;
					Handle<VoxelModel> copy =
					  Handle<VoxelModel>::New(from.GetWidth(), from.GetHeight(), from.GetDepth());
					copy->SetOrigin(from.GetOrigin());
					for (int x = 0; x < from.GetWidth(); x++) {
						for (int y = 0; y < from.GetHeight(); y++) {
							copy->GetSolidBitsAt(x, y) = from.GetSolidBitsAt(x, y);
							for (int z = 0; z < from.GetDepth(); z++)
								copy->GetColor(x, y, z) = from.GetColor(x, y, z);
						}
					}
					o.model = std::move(copy);
				}
				for (VoxelObject& child : o.children)
					CloneModels(child);
			}

			// `name`, or else it with ".001", ".002"... (in place of such an
			// ending it has), whichever `taken` does not hold.
			std::string UniqueName(const std::string& name, const std::vector<std::string>& taken) {
				auto free = [&](const std::string& n) {
					return std::find(taken.begin(), taken.end(), n) == taken.end();
				};
				if (free(name))
					return name;
				std::string stem = name;
				const std::size_t dot = name.rfind('.');
				if (dot != std::string::npos && name.size() - dot - 1 >= 3 &&
				    std::all_of(name.begin() + long(dot) + 1, name.end(),
				                [](char c) { return c >= '0' && c <= '9'; }))
					stem = name.substr(0, dot);
				for (int n = 1;; n++) {
					char number[16];
					std::snprintf(number, sizeof(number), ".%03d", n);
					if (free(stem + number))
						return stem + number;
				}
			}

			// The tracks keying the node named `from` (by its path of names), or
			// anything under it, key it as `to`.
			void RetargetTracks(std::vector<AnimationClip>& clips, const std::string& from,
			                    const std::string& to) {
				for (AnimationClip& clip : clips) {
					for (AnimationTrack& track : clip.tracks) {
						if (track.path == from)
							track.path = to;
						else if (track.path.compare(0, from.size() + 1, from + "/") == 0)
							track.path = to + track.path.substr(from.size());
					}
				}
			}

			// What a node shows as in messages: its name, or that it has none.
			std::string Quoted(const VoxelObject& o) {
				return o.name.empty() ? std::string("an unnamed object") : "\"" + o.name + "\"";
			}

			Vector3 Point(const Matrix4& m, const Vector3& p) {
				const Vector4 v = m * p;
				return MakeVector3(v.x, v.y, v.z);
			}

			// The smallest a scale factor may shrink to: an object never folds
			// flat, which no drag could bring back.
			constexpr float kMinScale = 0.01F;
			float KeepScale(float s) {
				return std::fabs(s) < kMinScale ? (s < 0.0F ? -kMinScale : kMinScale) : s;
			}
		} // namespace

		// --- Files -------------------------------------------------------------

		bool KV6Scene::Load(const std::string& path) {
			VoxelModel2KV6::SceneFile loaded;
			std::FILE* f = fs::OpenFile(path, "rb");
			if (!f)
				return false;
			try {
				StdStream stream(f, true); // takes ownership of the FILE*
				loaded = VoxelModel2KV6::LoadScene(stream);
			} catch (const std::exception&) {
				return false;
			}
			std::vector<VoxelObject> previous = std::move(roots);
			roots = std::move(loaded.objects);
			Index();
			if (entries.empty()) {
				// Nothing in it to show or edit: the scene open stays.
				roots = std::move(previous);
				Index();
				return false;
			}
			metadata = std::move(loaded.metadata);
			clips = std::move(loaded.clips);
			Restart();
			ClearPreview();
			Pick(0);
			changeCount = savedChangeCount = 0;
			return true;
		}

		bool KV6Scene::Save(const std::string& path) const {
			const std::string tmpPath = path + ".savetmp";
			std::FILE* f = fs::OpenFile(tmpPath, "wb");
			if (!f)
				return false;
			try {
				StdStream stream(f, true); // takes ownership; closes at scope exit
				VoxelModel2KV6::SceneFile file;
				file.objects = roots;
				file.metadata = metadata;
				file.clips = clips;
				VoxelModel2KV6::SaveScene(stream, file);
			} catch (const std::exception&) {
				fs::Delete(tmpPath);
				return false;
			}
			if (!fs::Rename(tmpPath, path, true)) {
				fs::Delete(tmpPath);
				return false;
			}
			return true;
		}

		void KV6Scene::StartWith(Handle<VoxelModel> model, const std::string& name) {
			VoxelObject object;
			object.name = name;
			object.model = std::move(model);
			roots.clear();
			roots.push_back(std::move(object));
			metadata.clear();
			clips.clear();
			Index();
			Restart();
			Pick(0);
			changeCount = 1; // a scene never saved has changes to save
			savedChangeCount = 0;
		}

		void KV6Scene::Restart() {
			selection.clear();
			active.clear();
			picked = -1;
			folded.clear();
			dragging = false;
			placing = false;
			history.clear();
			undoTop = 0;
		}

		void KV6Scene::Index() {
			// The render models are kept: an object keeps its own wherever it
			// went in the tree.
			std::vector<Entry> previous = std::move(entries);
			entries.clear();
			std::vector<const VoxelObject*> parents;
			NodePath path;
			std::function<void(VoxelObject&)> visit = [&](VoxelObject& o) {
				if (o.model) {
					Entry e;
					e.object = &o;
					e.path = path;
					e.parents = parents;
					for (Entry& old : previous) {
						if (old.renderedFrom == o.model.GetPointerOrNull()) {
							e.renderModel = std::move(old.renderModel);
							e.renderedFrom = old.renderedFrom;
							old.renderedFrom = nullptr;
							break;
						}
					}
					entries.push_back(std::move(e));
				}
				parents.push_back(&o);
				for (std::size_t i = 0; i < o.children.size(); i++) {
					path.push_back(int(i));
					visit(o.children[i]);
					path.pop_back();
				}
				parents.pop_back();
			};
			for (std::size_t i = 0; i < roots.size(); i++) {
				path.assign(1, int(i));
				visit(roots[i]);
			}
			shapeVersion++;
		}

		// --- The tree ----------------------------------------------------------

		const std::vector<VoxelObject>* KV6Scene::ChildrenOf(const NodePath& parent) const {
			const std::vector<VoxelObject>* level = &roots;
			for (int i : parent) {
				if (i < 0 || std::size_t(i) >= level->size())
					return nullptr;
				level = &(*level)[std::size_t(i)].children;
			}
			return level;
		}

		std::vector<VoxelObject>* KV6Scene::ChildrenOf(const NodePath& parent) {
			return const_cast<std::vector<VoxelObject>*>(
			  static_cast<const KV6Scene*>(this)->ChildrenOf(parent));
		}

		const VoxelObject* KV6Scene::Node(const NodePath& path) const {
			if (path.empty())
				return nullptr;
			const std::vector<VoxelObject>* level = ChildrenOf(ParentOf(path));
			if (!level || path.back() < 0 || std::size_t(path.back()) >= level->size())
				return nullptr;
			return &(*level)[std::size_t(path.back())];
		}

		VoxelObject* KV6Scene::MutableNode(const NodePath& path) {
			return const_cast<VoxelObject*>(static_cast<const KV6Scene*>(this)->Node(path));
		}

		int KV6Scene::ObjectIndexAt(const NodePath& path) const {
			for (std::size_t i = 0; i < entries.size(); i++) {
				if (entries[i].path == path)
					return int(i);
			}
			return -1;
		}

		bool KV6Scene::Shown(const NodePath& path) const {
			for (std::size_t n = 1; n <= path.size(); n++) {
				const VoxelObject* o = Node(NodePath(path.begin(), path.begin() + long(n)));
				if (!o || o->hidden)
					return false;
			}
			return true;
		}

		bool KV6Scene::Shown(int index) const {
			const Entry& e = entries[std::size_t(index)];
			if (e.object->hidden)
				return false;
			for (const VoxelObject* parent : e.parents) {
				if (parent->hidden)
					return false;
			}
			return true;
		}

		Matrix4 KV6Scene::NodeMatrix(const NodePath& path) const {
			Matrix4 m = Matrix4::Identity();
			for (std::size_t n = 1; n <= path.size(); n++)
				m = m * LocalMatrix(*Node(NodePath(path.begin(), path.begin() + long(n))));
			return m;
		}

		std::string KV6Scene::NamePath(const NodePath& path) const {
			std::string names;
			for (std::size_t n = 1; n <= path.size(); n++) {
				const VoxelObject& o = *Node(NodePath(path.begin(), path.begin() + long(n)));
				names += (n == 1 ? "" : "/") + o.name;
			}
			return names;
		}

		// --- Selecting ---------------------------------------------------------

		void KV6Scene::Pick(int index) {
			if (index < 0 || index >= Count())
				Select({}, {});
			else
				Select({PathOf(index)}, PathOf(index));
		}

		bool KV6Scene::IsSelected(const NodePath& path) const {
			return std::find(selection.begin(), selection.end(), path) != selection.end();
		}

		void KV6Scene::Select(std::vector<NodePath> nodes, NodePath newActive) {
			selection.clear();
			for (NodePath& p : nodes) {
				if (Node(p) && !IsSelected(p))
					selection.push_back(std::move(p));
			}
			active = IsSelected(newActive) ? std::move(newActive) : NodePath();
			SyncPicked();
		}

		void KV6Scene::SyncPicked() {
			// A drag of the picked object ends with it: whatever it was moving
			// may be elsewhere now.
			const int now = active.empty() ? -1 : ObjectIndexAt(active);
			if (now != picked)
				dragging = false;
			picked = now;
		}

		bool KV6Scene::IsFolded(const NodePath& path) const {
			return std::find(folded.begin(), folded.end(), path) != folded.end();
		}

		void KV6Scene::SetFolded(const NodePath& path, bool fold) {
			auto it = std::find(folded.begin(), folded.end(), path);
			if (fold && it == folded.end() && Node(path))
				folded.push_back(path);
			else if (!fold && it != folded.end())
				folded.erase(it);
		}

		// --- Objects -----------------------------------------------------------

		Matrix4 KV6Scene::WorldMatrix(int index) const {
			const Entry& e = entries[std::size_t(index)];
			Matrix4 m = Matrix4::Identity();
			for (const VoxelObject* parent : e.parents)
				m = m * LocalMatrix(*parent);
			return m * LocalMatrix(*e.object);
		}

		void KV6Scene::ParentFrame(int index, Quaternion& rotation, Vector3& scale) const {
			rotation = Quaternion(0.0F, 0.0F, 0.0F, 1.0F);
			scale = MakeVector3(1.0F, 1.0F, 1.0F);
			for (const VoxelObject* parent : entries[std::size_t(index)].parents) {
				rotation = rotation * Quaternion(parent->rotation);
				scale = scale * parent->scale;
			}
		}

		int KV6Scene::ObjectAt(const GizmoView& view, const Vector2& cursor) const {
			if (!view.IsValid())
				return -1;
			Vector3 origin, dir;
			view.Ray(cursor, origin, dir);
			int best = -1;
			float bestDistance = std::numeric_limits<float>::max();
			for (int i = 0; i < Count(); i++) {
				if (!Shown(i))
					continue;
				const VoxelModel& model = *Object(i).model;
				const Matrix4 world = WorldMatrix(i);
				const Matrix4 toLocal = world.Inversed();
				// The ray in the object's voxel indices, voxel `i` centred on `i`.
				const Vector3 from = Point(toLocal, origin);
				Vector3 along = Point(toLocal, origin + dir) - from;
				const float alongLength = along.GetLength();
				if (alongLength < 1.0e-6F)
					continue;
				along = along / alongLength;
				// Where it crosses the object's volume, then voxel by voxel, a
				// quarter of one at a time.
				const Vector3 lo = MakeVector3(-0.5F, -0.5F, -0.5F);
				const Vector3 hi = MakeVector3(float(model.GetWidth()) - 0.5F,
				                               float(model.GetHeight()) - 0.5F,
				                               float(model.GetDepth()) - 0.5F);
				float enter = 0.0F, leave = std::numeric_limits<float>::max();
				bool misses = false;
				for (int a = 0; a < 3 && !misses; a++) {
					const float f = from[a], d = along[a];
					const float l = lo[a], h = hi[a];
					if (std::fabs(d) < 1.0e-6F) {
						misses = f < l || f > h;
						continue;
					}
					float t0 = (l - f) / d, t1 = (h - f) / d;
					if (t0 > t1)
						std::swap(t0, t1);
					enter = std::max(enter, t0);
					leave = std::min(leave, t1);
					misses = enter > leave;
				}
				if (misses)
					continue;
				for (float t = enter; t <= leave; t += 0.25F) {
					const Vector3 p = from + along * t;
					const int x = int(std::floor(p.x + 0.5F));
					const int y = int(std::floor(p.y + 0.5F));
					const int z = int(std::floor(p.z + 0.5F));
					if (x < 0 || y < 0 || z < 0 || x >= model.GetWidth() ||
					    y >= model.GetHeight() || z >= model.GetDepth() ||
					    !model.IsSolid(x, y, z))
						continue;
					const float distance = (Point(world, p) - origin).GetLength();
					if (distance < bestDistance) {
						bestDistance = distance;
						best = i;
					}
					break;
				}
			}
			return best;
		}

		void KV6Scene::Bounds(int index, Vector3& centre, float& size) const {
			Vector3 lo, hi;
			Box(index, lo, hi);
			centre = (lo + hi) * 0.5F;
			const Vector3 extent = hi - lo;
			size = std::max(extent.x, std::max(extent.y, extent.z));
		}

		void KV6Scene::Box(Vector3& lo, Vector3& hi) const {
			lo = hi = MakeVector3(0.0F, 0.0F, 0.0F);
			for (int i = 0; i < Count(); i++) {
				Vector3 l, h;
				Box(i, l, h);
				lo = i == 0 ? l : MakeVector3(std::min(lo.x, l.x), std::min(lo.y, l.y), std::min(lo.z, l.z));
				hi = i == 0 ? h : MakeVector3(std::max(hi.x, h.x), std::max(hi.y, h.y), std::max(hi.z, h.z));
			}
		}

		void KV6Scene::Box(int index, Vector3& lo, Vector3& hi) const {
			const VoxelModel& model = *Object(index).model;
			const Matrix4 world = WorldMatrix(index);
			lo = MakeVector3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
			                 std::numeric_limits<float>::max());
			hi = lo * -1.0F;
			for (int c = 0; c < 8; c++) {
				const Vector3 corner =
				  MakeVector3((c & 1) ? float(model.GetWidth()) - 0.5F : -0.5F,
				              (c & 2) ? float(model.GetHeight()) - 0.5F : -0.5F,
				              (c & 4) ? float(model.GetDepth()) - 0.5F : -0.5F);
				const Vector3 p = Point(world, corner);
				lo = MakeVector3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
				hi = MakeVector3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
			}
		}

		void KV6Scene::Bounds(Vector3& centre, float& size) const {
			if (entries.empty()) {
				centre = MakeVector3(0.0F, 0.0F, 0.0F);
				size = 1.0F;
				return;
			}
			Vector3 lo, hi;
			for (int i = 0; i < Count(); i++) {
				Vector3 c;
				float s;
				Bounds(i, c, s);
				const Vector3 half = MakeVector3(s, s, s) * 0.5F;
				if (i == 0) {
					lo = c - half;
					hi = c + half;
				} else {
					lo = MakeVector3(std::min(lo.x, c.x - half.x), std::min(lo.y, c.y - half.y),
					                 std::min(lo.z, c.z - half.z));
					hi = MakeVector3(std::max(hi.x, c.x + half.x), std::max(hi.y, c.y + half.y),
					                 std::max(hi.z, c.z + half.z));
				}
			}
			centre = (lo + hi) * 0.5F;
			const Vector3 extent = hi - lo;
			size = std::max(extent.x, std::max(extent.y, extent.z));
		}

		// --- Changing the tree -------------------------------------------------

		NodePath KV6Scene::Relocation::Apply(const NodePath& path) const {
			for (std::size_t i = 0; i < from.size(); i++) {
				if (StartsWith(path, from[i])) {
					NodePath moved = to[i];
					moved.insert(moved.end(), path.begin() + long(from[i].size()), path.end());
					return moved;
				}
			}
			// Taken out last first, so each path taken out still names its
			// node, then put in first first, so each path put in names where
			// its node goes: a node after one in its parent's children moves up
			// a place, then down one for one put in before it.
			NodePath q = path;
			for (std::size_t i = from.size(); i-- > 0;)
				AfterRemoval(q, from[i]);
			for (const NodePath& t : to)
				AfterInsertion(q, t);
			return q;
		}

		void KV6Scene::Relocate(const Relocation& r, const std::vector<Placement>& placements) {
			CancelPlacement();
			std::vector<VoxelObject> taken(r.from.size());
			for (std::size_t i = r.from.size(); i-- > 0;) {
				std::vector<VoxelObject>& level = *ChildrenOf(ParentOf(r.from[i]));
				const auto at = level.begin() + r.from[i].back();
				taken[i] = std::move(*at);
				level.erase(at);
			}
			for (std::size_t i = 0; i < r.to.size(); i++) {
				Place(taken[i], placements[i]);
				std::vector<VoxelObject>& level = *ChildrenOf(ParentOf(r.to[i]));
				level.insert(level.begin() + r.to[i].back(), std::move(taken[i]));
			}
			for (NodePath& p : selection)
				p = r.Apply(p);
			if (!active.empty())
				active = r.Apply(active);
			for (NodePath& p : folded)
				p = r.Apply(p);
			Index();
			SyncPicked();
		}

		bool KV6Scene::PlanMove(const std::vector<NodePath>& nodes, const NodePath& parent,
		                        int position, MoveStep& plan, std::string& why) const {
			why.clear();
			// The nodes that move: a node under another that moves goes with it.
			const std::vector<NodePath> from = TopMost(nodes);
			const std::vector<VoxelObject>* level = ChildrenOf(parent);
			if (from.empty() || !CanHold(parent)) {
				why = "nothing to move there";
				return false;
			}
			const std::string holder = parent.empty() ? std::string("the scene") : Quoted(*Node(parent));

			for (const NodePath& p : from) {
				const VoxelObject& o = *Node(p);
				if (o.locked) {
					why = Quoted(o) + " is held in place by its template";
					return false;
				}
				if (StartsWith(parent, p)) {
					why = "an object cannot go into itself or what is under it";
					return false;
				}
			}

			// Children are found by name: none may share one.
			std::vector<std::string> names;
			for (std::size_t i = 0; i < level->size(); i++) {
				NodePath child = parent;
				child.push_back(int(i));
				if (!std::binary_search(from.begin(), from.end(), child))
					names.push_back((*level)[i].name);
			}
			for (const NodePath& p : from) {
				const std::string& name = Node(p)->name;
				if (std::find(names.begin(), names.end(), name) != names.end()) {
					why = holder + " already holds " +
					      (name.empty() ? std::string("an unnamed object")
					                    : "an object named \"" + name + "\"");
					return false;
				}
				names.push_back(name);
			}

			// Where they land: the gap they go into, as it is once they are out.
			const int count = int(level->size());
			int gap = position < 0 || position > count ? count : position;
			NodePath parentAfter = parent;
			for (std::size_t i = from.size(); i-- > 0;) {
				if (ParentOf(from[i]) == parent && from[i].back() < gap)
					gap--;
				AfterRemoval(parentAfter, from[i]);
			}
			Relocation r;
			r.from = from;
			for (std::size_t i = 0; i < from.size(); i++) {
				NodePath t = parentAfter;
				t.push_back(gap + int(i));
				r.to.push_back(std::move(t));
			}
			if (r.from == r.to)
				return false; // where they are already

			// Each keeps where it is in the world: its placement, its keys and
			// the clips' keys of it are brought from its old parent's frame into
			// its new one's.
			const Matrix4 toParent = NodeMatrix(parent).Inversed();
			std::vector<AnimationClip> clipsAfter = clips;
			std::vector<Placement> before, after;
			for (const NodePath& p : from) {
				const VoxelObject& o = *Node(p);
				before.push_back(PlacementOf(o));
				Placement placed = before.back();
				const std::string oldName = NamePath(p);
				if (ParentOf(p) != parent) {
					const Matrix4 change = toParent * NodeMatrix(ParentOf(p));
					bool exact = DecomposeTransform(
					  change * LocalTransform(o.position, o.rotation, o.scale), placed.position,
					  placed.rotation, placed.scale);
					for (AnimationClip& clip : clipsAfter) {
						for (AnimationTrack& track : clip.tracks) {
							if (track.path != oldName)
								continue;
							for (TransformKeyframe& k : track.keyframes)
								exact = exact &&
								        DecomposeTransform(
								          change * LocalTransform(k.position, k.rotation, k.scale),
								          k.position, k.rotation, k.scale);
						}
					}
					if (!exact) {
						why = Quoted(o) + " would be skewed in " + holder +
						      ": a parent scaled unevenly cannot hold it turned as it is";
						return false;
					}
				}
				after.push_back(std::move(placed));

				// The tracks keying it, or anything under it, follow it.
				RetargetTracks(clipsAfter, oldName,
				               (parent.empty() ? std::string() : NamePath(parent) + "/") + o.name);
			}
			plan.relocation = std::move(r);
			plan.before = std::move(before);
			plan.after = std::move(after);
			plan.clipsBefore = clips;
			plan.clipsAfter = std::move(clipsAfter);
			plan.selectionBefore = selection;
			plan.activeBefore = active;
			return true;
		}

		std::string KV6Scene::WhyNotMove(const std::vector<NodePath>& nodes, const NodePath& parent,
		                                 int position) const {
			MoveStep plan;
			std::string why;
			PlanMove(nodes, parent, position, plan, why);
			return why;
		}

		bool KV6Scene::Move(const std::vector<NodePath>& nodes, const NodePath& parent, int position,
		                    std::string& why) {
			auto plan = std::unique_ptr<MoveStep>(new MoveStep());
			if (!PlanMove(nodes, parent, position, *plan, why))
				return false;
			CancelDrag();
			const Relocation& r = plan->relocation;
			Relocate(r, plan->after);
			clips = plan->clipsAfter;
			// The moved nodes stay selected, the active one among them if it
			// moved, else the first of them.
			const bool activeMoved = std::find(r.to.begin(), r.to.end(), active) != r.to.end();
			Select(r.to, activeMoved ? active : r.to.front());
			plan->selectionAfter = selection;
			plan->activeAfter = active;

			Step step;
			step.kind = Step::Kind::Move;
			step.move = std::move(plan);
			Record(std::move(step));
			return true;
		}

		std::vector<NodePath> KV6Scene::TopMost(const std::vector<NodePath>& nodes) const {
			std::vector<NodePath> top;
			for (const NodePath& p : nodes) {
				if (p.empty() || !Node(p))
					continue;
				bool carried = false;
				for (const NodePath& q : nodes)
					carried = carried || (!q.empty() && q.size() < p.size() && StartsWith(p, q));
				if (!carried)
					top.push_back(p);
			}
			std::sort(top.begin(), top.end());
			top.erase(std::unique(top.begin(), top.end()), top.end());
			return top;
		}

		bool KV6Scene::CanHold(const NodePath& parent) const {
			return parent.empty() || Node(parent) != nullptr;
		}

		void KV6Scene::InsertNodes(const std::vector<NodePath>& paths,
		                           const std::vector<VoxelObject>& nodes) {
			CancelPlacement();
			for (std::size_t i = 0; i < paths.size(); i++) {
				std::vector<VoxelObject>& level = *ChildrenOf(ParentOf(paths[i]));
				level.insert(level.begin() + paths[i].back(), nodes[i]);
			}
			auto follow = [&](NodePath& q) {
				for (const NodePath& p : paths)
					AfterInsertion(q, p);
			};
			for (NodePath& q : selection)
				follow(q);
			follow(active);
			for (NodePath& q : folded)
				follow(q);
			Index();
			SyncPicked();
		}

		void KV6Scene::RemoveNodes(const std::vector<NodePath>& paths) {
			CancelPlacement();
			for (std::size_t i = paths.size(); i-- > 0;) {
				std::vector<VoxelObject>& level = *ChildrenOf(ParentOf(paths[i]));
				level.erase(level.begin() + paths[i].back());
			}
			// What went with them is forgotten; what stays follows.
			auto follow = [&](NodePath& q) {
				for (std::size_t i = paths.size(); i-- > 0;) {
					if (!AfterRemoval(q, paths[i]))
						return false;
				}
				return true;
			};
			auto gone = [&](NodePath& q) { return !follow(q); };
			selection.erase(std::remove_if(selection.begin(), selection.end(), gone), selection.end());
			if (!active.empty() && !follow(active))
				active.clear();
			folded.erase(std::remove_if(folded.begin(), folded.end(), gone), folded.end());
			Index();
			SyncPicked();
		}

		void KV6Scene::RecordInsert(Step::Kind kind, std::unique_ptr<InsertStep> insert) {
			Step step;
			step.kind = kind;
			step.insert = std::move(insert);
			Record(std::move(step));
		}

		bool KV6Scene::HoldsVoxels(const NodePath& path) const {
			if (path.empty())
				return !entries.empty();
			std::function<bool(const VoxelObject&)> holds = [&](const VoxelObject& o) {
				if (o.model)
					return true;
				for (const VoxelObject& child : o.children)
					if (holds(child))
						return true;
				return false;
			};
			const VoxelObject* o = Node(path);
			return o && holds(*o);
		}

		Handle<VoxelModel> KV6Scene::Composed(const NodePath& path, Vector3& base,
		                                      std::string& why) const {
			why.clear();
			base = MakeVector3(0.0F, 0.0F, 0.0F);
			std::vector<VoxelObject> objects;
			std::string what = "the scene";
			if (path.empty()) {
				objects = roots;
			} else {
				const VoxelObject* node = Node(path);
				if (!node) {
					why = "there is nothing there";
					return Handle<VoxelModel>();
				}
				what = Quoted(*node);
				if (node->children.empty()) {
					// Its own voxels, as they are: voxel `i` at `i`.
					if (!node->model)
						why = what + " holds no voxels";
					return node->model;
				}
				// In its own frame, shown whatever it is set to: what hangs under
				// it lands where it does, the hidden left out as the game leaves
				// them.
				VoxelObject object = *node;
				object.position = MakeVector3(0.0F, 0.0F, 0.0F);
				object.rotation = MakeVector4(0.0F, 0.0F, 0.0F, 1.0F);
				object.scale = MakeVector3(1.0F, 1.0F, 1.0F);
				object.hidden = false;
				objects.push_back(std::move(object));
			}
			try {
				IntVector3 low;
				Handle<VoxelModel> model = VoxelModelLoader::MergeScene(objects, &low);
				base = MakeVector3(float(low.x), float(low.y), float(low.z));
				model->SetOrigin(base);
				return model;
			} catch (const std::exception&) {
				why = what + " shows no voxels: what holds them is hidden";
				return Handle<VoxelModel>();
			}
		}

		Handle<VoxelModel> KV6Scene::ModelOf(const NodePath& path, std::string& why) const {
			Vector3 base;
			return Composed(path, base, why);
		}

		Matrix4 KV6Scene::LowPolyFrame(const NodePath& path, const Vector3& base, int factor) const {
			// Block `i` holds voxels `i * factor` to `i * factor + factor - 1`:
			// its middle is half a block less a voxel's half in from its start.
			const float f = float(factor);
			const float half = (f - 1.0F) * 0.5F;
			return NodeMatrix(path) * Matrix4::Translate(base + MakeVector3(half, half, half)) *
			       Matrix4::Scale(f);
		}

		void KV6Scene::SetPreview(const NodePath& hide, Handle<VoxelModel> model,
		                          const Matrix4& frame) {
			previewing = true;
			previewHides = hide;
			if (previewModel.GetPointerOrNull() != model.GetPointerOrNull())
				previewRender = Handle<client::IModel>();
			previewModel = std::move(model);
			previewFrame = frame;
		}

		void KV6Scene::ClearPreview() {
			previewing = false;
			previewHides.clear();
			previewModel = Handle<VoxelModel>();
			previewRender = Handle<client::IModel>();
		}

		bool KV6Scene::PreviewHides(const NodePath& path) const {
			return previewing && StartsWith(path, previewHides);
		}

		bool KV6Scene::AddLowPoly(const NodePath& path, Handle<VoxelModel> model,
		                          const Vector3& base, int factor, std::string& why) {
			why.clear();
			const VoxelObject* node = Node(path);
			if (!model || (!path.empty() && !node)) {
				why = "there is nothing to add";
				return false;
			}
			// Where it lands in the node's parent's frame: as LowPolyFrame has it
			// (the node's transform times its own block frame), then moved along
			// the parent's x past the node's own extent, so the two stand side
			// by side.
			const Matrix4 local =
			  (path.empty() ? Matrix4::Identity()
			                : LocalTransform(node->position, node->rotation, node->scale));
			const float f = float(factor);
			const float half = (f - 1.0F) * 0.5F;
			const Matrix4 placed =
			  local * Matrix4::Translate(base + MakeVector3(half, half, half)) * Matrix4::Scale(f);
			float lo = std::numeric_limits<float>::max(), hi = -lo;
			for (int c = 0; c < 8; c++) {
				const Vector3 corner =
				  MakeVector3((c & 1) ? float(model->GetWidth()) - 0.5F : -0.5F,
				              (c & 2) ? float(model->GetHeight()) - 0.5F : -0.5F,
				              (c & 4) ? float(model->GetDepth()) - 0.5F : -0.5F);
				const float x = Point(placed, corner).x;
				lo = std::min(lo, x);
				hi = std::max(hi, x);
			}
			const float width = hi - lo;
			const float gap = std::max(2.0F, width * 0.1F);
			VoxelObject o;
			o.name = (path.empty() ? std::string("Scene") : node->name) + "_LowPoly";
			if (!DecomposeTransform(Matrix4::Translate(width + gap, 0.0F, 0.0F) * placed, o.position,
			                        o.rotation, o.scale)) {
				why = (path.empty() ? std::string("the scene") : Quoted(*node)) +
				      " is placed in a way a model beside it cannot be";
				return false;
			}
			o.model = std::move(model);
			const NodePath parent = path.empty() ? NodePath() : ParentOf(path);
			InsertUnder(parent, std::vector<VoxelObject>(1, std::move(o)),
			            path.empty() ? -1 : path.back() + 1);
			return true;
		}

		std::vector<KV6Scene::CopiedObject> KV6Scene::Copy(const std::vector<NodePath>& nodes) const {
			std::vector<CopiedObject> copied;
			for (const NodePath& p : TopMost(nodes)) {
				CopiedObject c{*Node(p), NodeMatrix(p)};
				// Edits to the original after the copy never reach it.
				CloneModels(c.object);
				copied.push_back(std::move(c));
			}
			return copied;
		}

		void KV6Scene::InsertUnder(const NodePath& parent, std::vector<VoxelObject> nodes,
		                           int position) {
			CancelDrag();
			const std::vector<VoxelObject>& level = *ChildrenOf(parent);
			const int at = position < 0 || position > int(level.size()) ? int(level.size()) : position;
			std::vector<std::string> taken;
			for (const VoxelObject& o : level)
				taken.push_back(o.name);
			auto step = std::unique_ptr<InsertStep>(new InsertStep());
			for (std::size_t i = 0; i < nodes.size(); i++) {
				nodes[i].name = UniqueName(nodes[i].name, taken);
				taken.push_back(nodes[i].name);
				NodePath path = parent;
				path.push_back(at + int(i));
				step->paths.push_back(std::move(path));
			}
			step->nodes = std::move(nodes);
			step->clipsBefore = step->clipsAfter = clips;
			step->selectionBefore = selection;
			step->activeBefore = active;
			InsertNodes(step->paths, step->nodes);
			SetFolded(parent, false);
			Select(step->paths, step->paths.front());
			step->selectionAfter = selection;
			step->activeAfter = active;
			RecordInsert(Step::Kind::Insert, std::move(step));
		}

		bool KV6Scene::Paste(const std::vector<CopiedObject>& objects, const NodePath& parent,
		                     std::string& why) {
			why.clear();
			if (objects.empty() || !CanHold(parent)) {
				why = "nothing to paste there";
				return false;
			}
			const std::string holder = parent.empty() ? std::string("the scene") : Quoted(*Node(parent));
			// Each lands where it was in the world: its placement is brought
			// into the new parent's frame.
			const Matrix4 toParent = NodeMatrix(parent).Inversed();
			std::vector<VoxelObject> nodes;
			for (const CopiedObject& c : objects) {
				VoxelObject o = c.object;
				CloneModels(o); // each paste its own voxels
				if (!DecomposeTransform(toParent * c.world, o.position, o.rotation, o.scale)) {
					why = Quoted(o) + " would be skewed in " + holder +
					      ": a parent scaled unevenly cannot hold it turned as it is";
					return false;
				}
				nodes.push_back(std::move(o));
			}
			InsertUnder(parent, std::move(nodes));
			return true;
		}

		bool KV6Scene::AddChild(const NodePath& parent, Handle<VoxelModel> model,
		                        const std::string& name, std::string& why) {
			why.clear();
			if (!model || !CanHold(parent)) {
				why = "nothing to add there";
				return false;
			}
			VoxelObject o;
			o.name = name;
			// Voxel `i` lands at the position plus `i`; the pivot, voxel
			// `-origin`, lands on the parent's origin.
			o.position = model->GetOrigin();
			o.model = std::move(model);
			InsertUnder(parent, std::vector<VoxelObject>(1, std::move(o)));
			return true;
		}

		std::string KV6Scene::WhyNotDelete(const std::vector<NodePath>& nodes) const {
			const std::vector<NodePath> top = TopMost(nodes);
			if (top.empty())
				return "nothing to delete";
			for (const NodePath& p : top) {
				const VoxelObject& o = *Node(p);
				const VoxelObject* locked = nullptr;
				std::function<void(const VoxelObject&)> find = [&](const VoxelObject& n) {
					if (!locked && n.locked)
						locked = &n;
					for (const VoxelObject& child : n.children)
						find(child);
				};
				find(o);
				if (locked == &o)
					return Quoted(o) + " is held in place by its template";
				if (locked)
					return Quoted(o) + " holds " + Quoted(*locked) +
					       ", which its template holds in place";
			}
			return std::string();
		}

		bool KV6Scene::Delete(const std::vector<NodePath>& nodes, std::string& why) {
			why = WhyNotDelete(nodes);
			if (!why.empty())
				return false;
			CancelDrag();
			auto step = std::unique_ptr<InsertStep>(new InsertStep());
			step->paths = TopMost(nodes);
			step->clipsBefore = step->clipsAfter = clips;
			// The clips' tracks of what goes, go with it.
			for (const NodePath& p : step->paths) {
				step->nodes.push_back(*Node(p));
				const std::string name = NamePath(p);
				for (AnimationClip& clip : step->clipsAfter) {
					clip.tracks.erase(
					  std::remove_if(clip.tracks.begin(), clip.tracks.end(),
					                 [&](const AnimationTrack& t) {
						                 return t.path == name ||
						                        t.path.compare(0, name.size() + 1, name + "/") == 0;
					                 }),
					  clip.tracks.end());
				}
			}
			step->selectionBefore = selection;
			step->activeBefore = active;
			RemoveNodes(step->paths);
			clips = step->clipsAfter;
			step->selectionAfter = selection;
			step->activeAfter = active;
			RecordInsert(Step::Kind::Remove, std::move(step));
			return true;
		}

		std::string KV6Scene::WhyNotRename(const NodePath& path, const std::string& name) const {
			const VoxelObject* o = Node(path);
			if (!o)
				return "there is nothing to rename";
			if (name == o->name)
				return std::string();
			if (o->locked)
				return Quoted(*o) + " keeps the name its template gave it";
			if (name.empty())
				return "a name cannot be empty";
			if (name.size() > 255)
				return "a name is at most 255 bytes long";
			if (name.find('/') != std::string::npos)
				return "a name cannot hold '/', which separates the names of a path";
			if (name[0] == '#')
				return "a name cannot start with '#'";
			const std::vector<VoxelObject>& siblings = *ChildrenOf(ParentOf(path));
			for (std::size_t i = 0; i < siblings.size(); i++) {
				if (int(i) != path.back() && siblings[i].name == name) {
					const NodePath parent = ParentOf(path);
					return (parent.empty() ? std::string("the scene") : Quoted(*Node(parent))) +
					       " already holds an object named \"" + name + "\"";
				}
			}
			return std::string();
		}

		bool KV6Scene::Rename(const NodePath& path, const std::string& name, std::string& why) {
			why = WhyNotRename(path, name);
			VoxelObject* o = MutableNode(path);
			if (!why.empty() || !o || o->name == name)
				return false;
			auto step = std::unique_ptr<RenameStep>(new RenameStep());
			step->before = o->name;
			step->after = name;
			step->clipsBefore = step->clipsAfter = clips;
			const std::string parent = path.size() > 1 ? NamePath(ParentOf(path)) + "/" : std::string();
			RetargetTracks(step->clipsAfter, NamePath(path), parent + name);
			o->name = name;
			clips = step->clipsAfter;
			Step s;
			s.kind = Step::Kind::Rename;
			s.path = path;
			s.rename = std::move(step);
			Record(std::move(s));
			return true;
		}

		void KV6Scene::ApplyHidden(const NodePath& path, bool hidden) {
			MutableNode(path)->hidden = hidden;
			if (!hidden)
				return;
			selection.erase(std::remove_if(selection.begin(), selection.end(),
			                               [&](const NodePath& p) { return StartsWith(p, path); }),
			                selection.end());
			if (!active.empty() && StartsWith(active, path))
				active.clear();
			SyncPicked();
		}

		void KV6Scene::SetHidden(const NodePath& path, bool hidden) {
			VoxelObject* o = MutableNode(path);
			if (!o || o->hidden == hidden)
				return;
			CancelDrag();
			ApplyHidden(path, hidden);
			Step step;
			step.kind = Step::Kind::Hide;
			step.path = path;
			step.hidden = hidden;
			Record(std::move(step));
		}

		// --- Placing -----------------------------------------------------------

		bool KV6Scene::PickedPose(GizmoPose& pose) const {
			// No gizmo on what cannot be seen.
			if (picked < 0 || picked >= Count() || !Shown(picked))
				return false;
			const VoxelObject& o = Object(picked);
			const Matrix4 world = WorldMatrix(picked);
			// On the model's pivot, which is where it turns and scales about.
			pose.position = Point(world, o.model->GetOrigin() * -1.0F);
			for (int a = 0; a < 3; a++) {
				const Vector3 axis = world.GetAxis(a);
				const float length = axis.GetLength();
				if (length < 1.0e-6F)
					return false;
				pose.axes[a] = axis / length;
			}
			return true;
		}

		KV6Scene::Placement KV6Scene::PlacementOf(const VoxelObject& o) {
			return Placement{o.position, o.rotation, o.scale};
		}

		void KV6Scene::Place(VoxelObject& o, const Placement& p) {
			o.position = p.position;
			o.rotation = p.rotation;
			o.scale = p.scale;
		}

		void KV6Scene::BeginDrag() {
			if (picked < 0)
				return;
			dragStart = PlacementOf(Object(picked));
			dragging = true;
		}

		void KV6Scene::PreviewDrag(const GizmoTransform& total) {
			if (!dragging || picked < 0)
				return;
			VoxelObject& o = Object(picked);
			// The gizmo reports world changes about the pivot: they are brought
			// into the parent's frame, and the object placed so its pivot moves by
			// the translation alone.
			Quaternion parentRotation;
			Vector3 parentScale;
			ParentFrame(picked, parentRotation, parentScale);
			const Quaternion toParent = parentRotation.Conjugate();
			const Vector3 pivot = o.model->GetOrigin() * -1.0F; // in voxel indices

			const Quaternion startRotation(dragStart.rotation);
			const Vector3 startPivot =
			  dragStart.position + startRotation.Apply(dragStart.scale * pivot);
			Vector3 move = toParent.Apply(total.translation);
			move = MakeVector3(move.x / KeepScale(parentScale.x), move.y / KeepScale(parentScale.y),
			                   move.z / KeepScale(parentScale.z));
			const Quaternion turn = toParent * total.rotation * parentRotation;

			const Quaternion rotation = (turn * startRotation).Normalize();
			const Vector3 scale = MakeVector3(KeepScale(dragStart.scale.x * total.scale.x),
			                                  KeepScale(dragStart.scale.y * total.scale.y),
			                                  KeepScale(dragStart.scale.z * total.scale.z));
			o.rotation = rotation.v;
			o.scale = scale;
			o.position = startPivot + move - rotation.Apply(scale * pivot);
		}

		bool KV6Scene::CommitDrag() {
			if (!dragging || picked < 0)
				return false;
			dragging = false;
			const Placement now = PlacementOf(Object(picked));
			if (now.position == dragStart.position && now.rotation == dragStart.rotation &&
			    now.scale == dragStart.scale)
				return false;
			Step step;
			step.kind = Step::Kind::Place;
			step.path = PathOf(picked);
			step.before = dragStart;
			step.after = now;
			Record(std::move(step));
			return true;
		}

		void KV6Scene::Record(Step&& step) {
			history.resize(std::size_t(undoTop));
			history.push_back(std::move(step));
			undoTop++;
			changeCount++;
		}

		void KV6Scene::CancelDrag() {
			if (!dragging || picked < 0)
				return;
			Place(Object(picked), dragStart);
			dragging = false;
		}

		bool KV6Scene::LocalPlacementOf(const NodePath& path, LocalPlacement& out) const {
			const VoxelObject* o = Node(path);
			if (!o)
				return false;
			out = LocalPlacement{o->position, o->rotation, o->scale};
			return true;
		}

		bool KV6Scene::KeepsTurnAndScale(const NodePath& path) const {
			const VoxelObject* o = Node(path);
			return o && o->locked;
		}

		void KV6Scene::PreviewPlacement(const NodePath& path, const LocalPlacement& p) {
			VoxelObject* o = MutableNode(path);
			if (!o)
				return;
			if (!placing || placingPath != path) {
				CancelPlacement();
				CancelDrag();
				placing = true;
				placingPath = path;
				placingStart = PlacementOf(*o);
			}
			o->position = p.position;
			if (!o->locked) {
				o->rotation = Quaternion(p.rotation).Normalize().v;
				o->scale = MakeVector3(KeepScale(p.scale.x), KeepScale(p.scale.y), KeepScale(p.scale.z));
			}
		}

		bool KV6Scene::CommitPlacement() {
			if (!placing)
				return false;
			placing = false;
			VoxelObject* o = MutableNode(placingPath);
			if (!o)
				return false;
			const Placement now = PlacementOf(*o);
			if (now.position == placingStart.position && now.rotation == placingStart.rotation &&
			    now.scale == placingStart.scale)
				return false;
			Step step;
			step.kind = Step::Kind::Place;
			step.path = placingPath;
			step.before = placingStart;
			step.after = now;
			Record(std::move(step));
			return true;
		}

		void KV6Scene::CancelPlacement() {
			if (!placing)
				return;
			placing = false;
			if (VoxelObject* o = MutableNode(placingPath))
				Place(*o, placingStart);
		}

		void KV6Scene::CompensateShift(int index, const Vector3& shift) {
			if (shift.x == 0.0F && shift.y == 0.0F && shift.z == 0.0F)
				return;
			// Voxel `i` is now at `i + shift`; the object moves back by as much,
			// so it lands where it was.
			VoxelObject& o = Object(index);
			o.position = o.position - Quaternion(o.rotation).Apply(o.scale * shift);
		}

		std::string KV6Scene::StepLabel(const Step& step, bool done) const {
			auto named = [&](const NodePath& path) {
				const VoxelObject* o = Node(path);
				return o ? Quoted(*o) : std::string("an object");
			};
			auto objects = [&](const std::vector<NodePath>& paths) {
				return paths.size() == 1 ? named(paths.front())
				                         : std::to_string(paths.size()) + " objects";
			};
			switch (step.kind) {
				case Step::Kind::Place: return "placing " + named(step.path);
				case Step::Kind::Hide:
					return (step.hidden ? "hiding " : "showing ") + named(step.path);
				case Step::Kind::Move:
					return "moving " + objects(done ? step.move->relocation.to
					                                : step.move->relocation.from);
				case Step::Kind::Insert:
				case Step::Kind::Remove: {
					const InsertStep& m = *step.insert;
					// Named as they were kept: they are not in the tree either way.
					const std::string what =
					  m.nodes.size() == 1 ? Quoted(m.nodes.front())
					                      : std::to_string(m.nodes.size()) + " objects";
					return (step.kind == Step::Kind::Insert ? "adding " : "deleting ") + what;
				}
				case Step::Kind::Rename:
					return "renaming \"" + step.rename->before + "\" to \"" + step.rename->after + "\"";
			}
			return std::string();
		}

		std::string KV6Scene::UndoLabel() const {
			return CanUndo() ? StepLabel(history[std::size_t(undoTop - 1)], true) : std::string();
		}

		std::string KV6Scene::RedoLabel() const {
			return CanRedo() ? StepLabel(history[std::size_t(undoTop)], false) : std::string();
		}

		void KV6Scene::Undo() {
			if (!CanUndo())
				return;
			CancelDrag();
			CancelPlacement();
			const Step& step = history[std::size_t(--undoTop)];
			switch (step.kind) {
				case Step::Kind::Place:
					Place(*MutableNode(step.path), step.before);
					Select({step.path}, step.path);
					break;
				case Step::Kind::Hide: MutableNode(step.path)->hidden = !step.hidden; break;
				case Step::Kind::Move: {
					const MoveStep& m = *step.move;
					Relocate(m.relocation.Reversed(), m.before);
					clips = m.clipsBefore;
					Select(m.selectionBefore, m.activeBefore);
					break;
				}
				case Step::Kind::Insert:
				case Step::Kind::Remove: {
					const InsertStep& m = *step.insert;
					if (step.kind == Step::Kind::Insert)
						RemoveNodes(m.paths);
					else
						InsertNodes(m.paths, m.nodes);
					clips = m.clipsBefore;
					Select(m.selectionBefore, m.activeBefore);
					break;
				}
				case Step::Kind::Rename:
					MutableNode(step.path)->name = step.rename->before;
					clips = step.rename->clipsBefore;
					Select({step.path}, step.path);
					break;
			}
			changeCount++;
		}

		void KV6Scene::Redo() {
			if (!CanRedo())
				return;
			CancelDrag();
			CancelPlacement();
			const Step& step = history[std::size_t(undoTop++)];
			switch (step.kind) {
				case Step::Kind::Place:
					Place(*MutableNode(step.path), step.after);
					Select({step.path}, step.path);
					break;
				case Step::Kind::Hide: ApplyHidden(step.path, step.hidden); break;
				case Step::Kind::Move: {
					const MoveStep& m = *step.move;
					Relocate(m.relocation, m.after);
					clips = m.clipsAfter;
					Select(m.selectionAfter, m.activeAfter);
					break;
				}
				case Step::Kind::Insert:
				case Step::Kind::Remove: {
					const InsertStep& m = *step.insert;
					if (step.kind == Step::Kind::Insert)
						InsertNodes(m.paths, m.nodes);
					else
						RemoveNodes(m.paths);
					clips = m.clipsAfter;
					Select(m.selectionAfter, m.activeAfter);
					break;
				}
				case Step::Kind::Rename:
					MutableNode(step.path)->name = step.rename->after;
					clips = step.rename->clipsAfter;
					Select({step.path}, step.path);
					break;
			}
			changeCount++;
		}

		// --- Drawing -----------------------------------------------------------

		void KV6Scene::InvalidateModel(int index) {
			Entry& e = entries[std::size_t(index)];
			e.renderModel = Handle<client::IModel>();
			e.surfaceOf = nullptr; // its voxels changed
		}

		const std::vector<IntVector3>& KV6Scene::SurfaceOf(int index) const {
			const Entry& e = entries[std::size_t(index)];
			const VoxelModel& m = *e.object->model;
			if (e.surfaceOf == &m)
				return e.surface;
			e.surface.clear();
			e.surfaceOf = &m;
			for (int x = 0; x < m.GetWidth(); x++)
				for (int y = 0; y < m.GetHeight(); y++)
					for (int z = 0; z < m.GetDepth(); z++) {
						if (!m.IsSolid(x, y, z))
							continue;
						if (!m.IsSolid(x - 1, y, z) || !m.IsSolid(x + 1, y, z) ||
						    !m.IsSolid(x, y - 1, z) || !m.IsSolid(x, y + 1, z) ||
						    !m.IsSolid(x, y, z - 1) || !m.IsSolid(x, y, z + 1))
							e.surface.push_back(MakeIntVector3(x, y, z));
					}
			return e.surface;
		}

		std::vector<int> KV6Scene::ObjectsOnScreen(const GizmoView& view,
		                                           const std::function<bool(const Vector2&)>& inside,
		                                           bool through) const {
			// Enough voxels of an object asked whether the view sees them to tell
			// one in sight from one hidden, without asking them all.
			constexpr std::size_t kSamples = 24;
			std::vector<int> found;
			if (!view.IsValid())
				return found;
			for (int i = 0; i < Count(); i++) {
				if (!Shown(i))
					continue;
				const Matrix4 world = WorldMatrix(i);
				std::vector<Vector2> there;
				for (const IntVector3& v : SurfaceOf(i)) {
					Vector2 screen;
					if (view.Project(Point(world, MakeVector3(float(v.x), float(v.y), float(v.z))), screen) &&
					    inside(screen))
						there.push_back(screen);
				}
				if (there.empty())
					continue;
				if (through) {
					found.push_back(i);
					continue;
				}
				const std::size_t step = std::max<std::size_t>(1, there.size() / kSamples);
				for (std::size_t k = 0; k < there.size(); k += step) {
					if (ObjectAt(view, there[k]) == i) {
						found.push_back(i);
						break;
					}
				}
			}
			return found;
		}

		void KV6Scene::Render(client::IRenderer& renderer) {
			if (previewing && previewModel) {
				if (!previewRender)
					previewRender = renderer.CreateModel(*previewModel);
				client::ModelRenderParam param;
				// The renderer adds the model's origin; the frame leaves it out.
				param.matrix = previewFrame * Matrix4::Translate(previewModel->GetOrigin() * -1.0F);
				renderer.RenderModel(*previewRender, param);
				// Its volume, outlined apart from a selection's.
				const Vector3 a = MakeVector3(-0.5F, -0.5F, -0.5F);
				const Vector3 b = MakeVector3(float(previewModel->GetWidth()) - 0.5F,
				                              float(previewModel->GetHeight()) - 0.5F,
				                              float(previewModel->GetDepth()) - 0.5F);
				const Vector4 color = EditorTheme::DropTarget();
				ForEachBoxEdge(a, b, [&](const Vector3& p, const Vector3& q) {
					renderer.AddDebugLine(Point(previewFrame, p), Point(previewFrame, q), color);
				});
			}

			for (int i = 0; i < Count(); i++) {
				Entry& e = entries[std::size_t(i)];
				VoxelObject& o = *e.object;
				if (!Shown(i) || PreviewHides(e.path))
					continue;
				if (!e.renderModel || e.renderedFrom != o.model.GetPointerOrNull()) {
					e.renderModel = renderer.CreateModel(*o.model);
					e.renderedFrom = o.model.GetPointerOrNull();
				}
				client::ModelRenderParam param;
				// The renderer adds the model's origin; the scene leaves it out.
				param.matrix = WorldMatrix(i) * Matrix4::Translate(o.model->GetOrigin() * -1.0F);
				renderer.RenderModel(*e.renderModel, param);
			}

			// The selected objects' volumes, outlined where they lie: the active
			// one brighter, as its row is in the tree.
			for (const NodePath& path : selection) {
				const int i = ObjectIndexAt(path);
				if (i < 0 || !Shown(i) || PreviewHides(path))
					continue;
				const VoxelModel& model = *Object(i).model;
				const Matrix4 world = WorldMatrix(i);
				const Vector3 a = MakeVector3(-0.5F, -0.5F, -0.5F);
				const Vector3 b = MakeVector3(float(model.GetWidth()) - 0.5F,
				                              float(model.GetHeight()) - 0.5F,
				                              float(model.GetDepth()) - 0.5F);
				const Vector4 color = i == picked ? EditorTheme::Active() : EditorTheme::Selected();
				ForEachBoxEdge(a, b, [&](const Vector3& p, const Vector3& q) {
					renderer.AddDebugLine(Point(world, p), Point(world, q), color);
				});
			}
		}
	} // namespace gui
} // namespace spades
