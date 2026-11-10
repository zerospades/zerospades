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

#include "SceneTransformTarget.h"

#include <Core/VoxelModel2KV6.h>

namespace spades {
	namespace gui {
		void SceneTransformTarget::SetScene(KV6Scene* s) {
			if (s != scene)
				CancelTransform();
			scene = s;
		}

		unsigned SceneTransformTarget::ShownRows() const {
			const VoxelObject* o = scene ? scene->Node(editing ? node : scene->Active()) : nullptr;
			return o && !o->model ? unsigned(kLocation | kRotation) : unsigned(kAllRows);
		}

		bool SceneTransformTarget::CurrentTransform(Transform& transform, std::string& name,
		                                            bool& onlyMoves) const {
			if (!scene)
				return false;
			const NodePath path = editing ? node : scene->Active();
			KV6Scene::LocalPlacement p;
			if (path.empty() || !scene->LocalPlacementOf(path, p))
				return false;
			transform.position = p.position;
			transform.rotation = EulerFromRotation(p.rotation);
			transform.scale = p.scale;
			const VoxelObject* o = scene->Node(path);
			name = o->name.empty() ? std::string("(unnamed)") : o->name;
			onlyMoves = scene->KeepsTurnAndScale(path);
			return true;
		}

		void SceneTransformTarget::PreviewTransform(const Transform& transform) {
			if (!scene)
				return;
			if (!editing) {
				node = scene->Active();
				if (node.empty())
					return;
				editing = true;
			}
			scene->PreviewPlacement(node, KV6Scene::LocalPlacement{transform.position,
			                                                      RotationFromEuler(transform.rotation),
			                                                      transform.scale});
		}

		void SceneTransformTarget::CommitTransform() {
			if (scene && editing)
				scene->CommitPlacement();
			editing = false;
		}

		void SceneTransformTarget::CancelTransform() {
			if (scene && editing)
				scene->CancelPlacement();
			editing = false;
		}
	} // namespace gui
} // namespace spades
