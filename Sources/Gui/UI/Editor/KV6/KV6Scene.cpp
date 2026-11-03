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
#include <Core/VoxelModel.h>
#include <Gui/UI/Components/Gizmo/GizmoView.h>
#include <Gui/UI/Components/Gizmo/TransformGizmo.h>
#include <Gui/UI/Editor/Voxel/VoxelOverlayLines.h>

namespace spades {
	namespace gui {
		namespace fs = LocalFileSystem;

		namespace {
			Matrix4 LocalMatrix(const VoxelObject& o) {
				return Matrix4::Translate(o.position) * Quaternion(o.rotation).ToRotationMatrix() *
				       Matrix4::Scale(o.scale);
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
			picked = 0;
			history.clear();
			undoTop = 0;
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
			picked = 0;
			history.clear();
			undoTop = 0;
			changeCount = 1; // a scene never saved has changes to save
			savedChangeCount = 0;
		}

		void KV6Scene::Index() {
			entries.clear();
			std::vector<const VoxelObject*> parents;
			std::function<void(VoxelObject&)> visit = [&](VoxelObject& o) {
				if (o.model) {
					Entry e;
					e.object = &o;
					e.parents = parents;
					entries.push_back(std::move(e));
				}
				parents.push_back(&o);
				for (VoxelObject& child : o.children)
					visit(child);
				parents.pop_back();
			};
			for (VoxelObject& root : roots)
				visit(root);
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
				const VoxelObject& o = Object(i);
				if (o.hidden)
					continue;
				const VoxelModel& model = *o.model;
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

		// --- Placing -----------------------------------------------------------

		bool KV6Scene::PickedPose(GizmoPose& pose) const {
			if (picked < 0 || picked >= Count())
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
			history.resize(std::size_t(undoTop));
			history.push_back(Step{picked, dragStart, now});
			undoTop++;
			changeCount++;
			return true;
		}

		void KV6Scene::CancelDrag() {
			if (!dragging || picked < 0)
				return;
			Place(Object(picked), dragStart);
			dragging = false;
		}

		void KV6Scene::CompensateShift(int index, const Vector3& shift) {
			if (shift.x == 0.0F && shift.y == 0.0F && shift.z == 0.0F)
				return;
			// Voxel `i` is now at `i + shift`; the object moves back by as much,
			// so it lands where it was.
			VoxelObject& o = Object(index);
			o.position = o.position - Quaternion(o.rotation).Apply(o.scale * shift);
		}

		void KV6Scene::Undo() {
			if (!CanUndo())
				return;
			const Step& step = history[std::size_t(--undoTop)];
			Place(Object(step.index), step.before);
			picked = step.index;
			changeCount++;
		}

		void KV6Scene::Redo() {
			if (!CanRedo())
				return;
			const Step& step = history[std::size_t(undoTop++)];
			Place(Object(step.index), step.after);
			picked = step.index;
			changeCount++;
		}

		// --- Drawing -----------------------------------------------------------

		void KV6Scene::InvalidateModel(int index) {
			entries[std::size_t(index)].renderModel = Handle<client::IModel>();
		}

		void KV6Scene::Render(client::IRenderer& renderer) {
			for (int i = 0; i < Count(); i++) {
				Entry& e = entries[std::size_t(i)];
				VoxelObject& o = *e.object;
				if (o.hidden)
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

			if (picked < 0 || picked >= Count())
				return;
			// The picked object's volume, outlined where it lies.
			const VoxelModel& model = *Object(picked).model;
			const Matrix4 world = WorldMatrix(picked);
			const Vector3 a = MakeVector3(-0.5F, -0.5F, -0.5F);
			const Vector3 b = MakeVector3(float(model.GetWidth()) - 0.5F,
			                              float(model.GetHeight()) - 0.5F,
			                              float(model.GetDepth()) - 0.5F);
			const Vector4 color = MakeVector4(1.0F, 0.65F, 0.2F, 1.0F);
			ForEachBoxEdge(a, b, [&](const Vector3& p, const Vector3& q) {
				renderer.AddDebugLine(Point(world, p), Point(world, q), color);
			});
		}
	} // namespace gui
} // namespace spades
