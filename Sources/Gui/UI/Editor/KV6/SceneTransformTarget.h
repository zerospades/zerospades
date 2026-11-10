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

#include <Gui/UI/Components/TransformPanel.h>

#include "KV6Scene.h"

namespace spades {
	namespace gui {
		/**
		 * The active node of a scene, as a TransformPanel sets it: its own
		 * position, rotation and scale in its parent's frame, as the file, the
		 * scripts (ModelScene::Transform) and --2kv6-tools have them; turns as
		 * --2kv6-tools gives them, about X, then Y, then Z.
		 *
		 * An edit stays on the node it began on, whatever is picked meanwhile.
		 */
		class SceneTransformTarget final : public ITransformTarget {
		public:
			/** The scene it works on; null for none. */
			void SetScene(KV6Scene* s);

			/** An empty has no voxels to scale: a place and a turn. */
			unsigned ShownRows() const override;
			bool CanLockScale() const override { return true; }

			bool CurrentTransform(Transform& transform, std::string& name,
			                      bool& onlyMoves) const override;
			void PreviewTransform(const Transform& transform) override;
			void CommitTransform() override;
			void CancelTransform() override;

		private:
			KV6Scene* scene = nullptr;
			bool editing = false;
			NodePath node; // what the edit began on
		};
	} // namespace gui
} // namespace spades
