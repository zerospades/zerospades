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
#include <Core/VoxelResample.h>

#include "KV6Scene.h"
#include "ResamplePanel.h"

namespace spades {
	class VoxelModel;
	namespace client {
		class IAudioDevice;
		class IFont;
		class IRenderer;
	} // namespace client
	namespace gui {
		/**
		 * Makes a KV6 of a node of a scene at another resolution, from ten
		 * times coarser (a far model) to ten times finer (a part the scene
		 * scales down, at the detail it was modelled at): asks which
		 * (ResamplePanel), shows the result in the node's place, the node
		 * hidden meanwhile (KV6Scene::SetPreview), and adds it to the scene
		 * beside the node, or leaves the scene as it was.
		 *
		 * Its host routes it input first while it is open: it takes clicks,
		 * the pointer while its slider is held, and the keys it knows, and
		 * leaves the rest to the host, which lets the camera move, to look the
		 * preview over, and keeps everything else from changing the scene.
		 */
		class ResampleTool {
		public:
			ResampleTool(client::IRenderer& renderer, client::IFont& font,
			             client::IAudioDevice* audioDevice);

			/** Starts on the node at `path` (the whole scene for an empty one);
			 *  false, `why` saying why, when it holds no voxel in sight. */
			bool Open(KV6Scene& scene, const NodePath& path, std::string& why);
			bool IsOpen() const { return panel.IsOpen(); }
			/** Moves the open tool to `resolution`; false when it is not offered. */
			bool Choose(KV6Scene& scene, const VoxelResolution& resolution);
			/**
			 * A key or button while open, the cursor at `cursor`: true when the
			 * tool took it, `said` then holding what the user is told, if
			 * anything; false leaves it to the host.
			 */
			bool Input(KV6Scene& scene, const std::string& key, bool down, const Vector2& cursor,
			           std::string& said);
			/** The pointer moved to `cursor`; true when it was the tool's (its
			 *  slider held). */
			bool Move(KV6Scene& scene, const Vector2& cursor);
			/** Gives it up: the node is shown again, the scene as it was. */
			void Cancel(KV6Scene& scene);

			/** Draws the panel, its top left corner at (x, y). */
			void Draw(float x, float y, const Vector2& cursor);

		private:
			ResamplePanel panel;
			NodePath node;
			IntVector3 sourceSize = MakeIntVector3(0, 0, 0); // the node at 1/1
			long sourceVoxels = 0;
			VoxelResolution resolution;
			Handle<VoxelModel> model; // at the resolution chosen
			Matrix4 frame = Matrix4::Identity();

			/** Makes the model at the panel's resolution and shows it. */
			void Update(KV6Scene& scene);
			std::string Run(KV6Scene& scene, ResamplePanel::Action action);
			/** Ends, adding the model to the scene when `create`. */
			std::string Finish(KV6Scene& scene, bool create);
		};
	} // namespace gui
} // namespace spades
