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
#include <Core/RefCountedObject.h>

#include "KV6Scene.h"
#include "LowPolyPanel.h"

namespace spades {
	class VoxelModel;
	namespace client {
		class IAudioDevice;
		class IFont;
		class IRenderer;
	} // namespace client
	namespace gui {
		/**
		 * Makes a low-poly model of a node of a scene: asks how coarse
		 * (LowPolyPanel), shows the result in the node's place, the node hidden
		 * meanwhile (KV6Scene::SetPreview), and adds it to the scene beside the
		 * node, or leaves the scene as it was.
		 *
		 * Its host routes it input first while it is open: it takes clicks and
		 * the keys it knows, and leaves the rest to the host, which lets the
		 * camera move, to look the preview over, and keeps everything else
		 * from changing the scene under it.
		 */
		class LowPolyTool {
		public:
			LowPolyTool(client::IRenderer& renderer, client::IFont& font,
			            client::IAudioDevice* audioDevice);

			/** Starts on the node at `path` (the whole scene for an empty one);
			 *  false, `why` saying why, when it holds no voxel in sight. */
			bool Open(KV6Scene& scene, const NodePath& path, std::string& why);
			bool IsOpen() const { return panel.IsOpen(); }
			/**
			 * A key or button while open, the cursor at `cursor`: true when the
			 * tool took it, `said` then holding what the user is told, if
			 * anything; false leaves it to the host.
			 */
			bool Input(KV6Scene& scene, const std::string& key, bool down, const Vector2& cursor,
			           std::string& said);
			/** Gives it up: the node is shown again, the scene as it was. */
			void Cancel(KV6Scene& scene);

			/** Draws the panel, its top left corner at (x, y). */
			void Draw(float x, float y, const Vector2& cursor);

		private:
			LowPolyPanel panel;
			NodePath node;
			Handle<VoxelModel> source; // the node composed, as KV6Scene::Composed has it
			Vector3 base = MakeVector3(0.0F, 0.0F, 0.0F);
			long sourceVoxels = 0;
			Handle<VoxelModel> model; // at the factor chosen

			/** Makes the model at the panel's factor and shows it. */
			void Update(KV6Scene& scene);
			/** Ends, adding the model to the scene when `create`. */
			std::string Finish(KV6Scene& scene, bool create);
		};
	} // namespace gui
} // namespace spades
