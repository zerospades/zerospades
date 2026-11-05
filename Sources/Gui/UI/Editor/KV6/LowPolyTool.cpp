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

#include "LowPolyTool.h"

#include <algorithm>

#include <Core/VoxelBlend.h>
#include <Core/VoxelModel.h>

namespace spades {
	namespace gui {
		namespace {
			long SolidVoxels(const VoxelModel& m) {
				long n = 0;
				for (int x = 0; x < m.GetWidth(); x++)
					for (int y = 0; y < m.GetHeight(); y++)
						for (int z = 0; z < m.GetDepth(); z++)
							n += m.IsSolid(x, y, z) ? 1 : 0;
				return n;
			}

			IntVector3 SizeOf(const VoxelModel& m) {
				return MakeIntVector3(m.GetWidth(), m.GetHeight(), m.GetDepth());
			}

			// The longest side from which three times coarser, as the game's far
			// models are, still leaves enough of it; a smaller one starts at two.
			constexpr int kCoarserFrom = 24;
		} // namespace

		LowPolyTool::LowPolyTool(client::IRenderer& renderer, client::IFont& font,
		                         client::IAudioDevice* audioDevice)
		    : panel(renderer, font, audioDevice) {}

		bool LowPolyTool::Open(KV6Scene& scene, const NodePath& path, std::string& why) {
			Handle<VoxelModel> composed = scene.Composed(path, base, why);
			if (!composed)
				return false;
			node = path;
			source = composed;
			sourceVoxels = SolidVoxels(*source);
			const VoxelObject* o = scene.Node(path);
			const int longest =
			  std::max(source->GetWidth(), std::max(source->GetHeight(), source->GetDepth()));
			panel.Open(o ? (o->name.empty() ? std::string("(unnamed)") : o->name)
			             : std::string("the whole scene"),
			           longest >= kCoarserFrom ? 3 : 2);
			Update(scene);
			return true;
		}

		void LowPolyTool::Update(KV6Scene& scene) {
			const int factor = panel.Factor();
			model = DownsampledVoxelModel(*source, factor);
			const long voxels = SolidVoxels(*model);
			panel.SetStats(SizeOf(*source), sourceVoxels, SizeOf(*model), voxels);
			// The node steps aside for what it would become.
			scene.SetPreview(node, voxels > 0 ? model : Handle<VoxelModel>(),
			                 scene.LowPolyFrame(node, base, factor));
		}

		std::string LowPolyTool::Finish(KV6Scene& scene, bool create) {
			scene.ClearPreview();
			panel.Close();
			std::string said;
			if (create) {
				std::string why;
				const int factor = panel.Factor();
				if (scene.AddLowPoly(node, model, base, factor, why))
					said = "Added " + scene.Node(scene.Active())->name + ", 1/" +
					       std::to_string(factor) + " of the resolution";
				else
					said = "Cannot add the low-poly model: " + why;
			}
			source = Handle<VoxelModel>();
			model = Handle<VoxelModel>();
			return said;
		}

		void LowPolyTool::Cancel(KV6Scene& scene) {
			if (IsOpen())
				Finish(scene, false);
		}

		bool LowPolyTool::Input(KV6Scene& scene, const std::string& key, bool down,
		                        const Vector2& cursor, std::string& said) {
			said.clear();
			if (!IsOpen())
				return false;
			LowPolyPanel::Action action = LowPolyPanel::Action::None;
			if (key == "LeftMouseButton")
				action = down ? panel.Press(cursor) : panel.Release(cursor);
			else if (key == "RightMouseButton")
				; // nothing to open meanwhile: taken all the same
			else if (down)
				action = panel.Key(key);
			if (action == LowPolyPanel::Action::None)
				return key == "LeftMouseButton" || key == "RightMouseButton";
			switch (action) {
				case LowPolyPanel::Action::Changed: Update(scene); break;
				case LowPolyPanel::Action::Create: said = Finish(scene, true); break;
				case LowPolyPanel::Action::Cancel: said = Finish(scene, false); break;
				case LowPolyPanel::Action::None: break;
			}
			return true;
		}

		void LowPolyTool::Draw(float x, float y, const Vector2& cursor) {
			panel.Layout(x, y);
			panel.Draw(cursor);
		}
	} // namespace gui
} // namespace spades
