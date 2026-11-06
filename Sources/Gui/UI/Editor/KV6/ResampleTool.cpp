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

#include "ResampleTool.h"

#include <algorithm>

#include <Core/VoxelModel.h>

#include "KV6Document.h"

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

			// Whether a model of `size` fits in a KV6.
			bool Fits(const IntVector3& size) {
				return size.x <= KV6Document::kMaxWidth && size.y <= KV6Document::kMaxHeight &&
				       size.z <= KV6Document::kMaxDepth;
			}
		} // namespace

		ResampleTool::ResampleTool(client::IRenderer& renderer, client::IFont& font,
		                           client::IAudioDevice* audioDevice)
		    : panel(renderer, font, audioDevice) {}

		bool ResampleTool::Open(KV6Scene& scene, const NodePath& path, std::string& why) {
			// The node as the game composes it: what every step is measured from.
			Matrix4 unused;
			Handle<VoxelModel> composed = scene.Resampled(path, VoxelResolution(), unused, why);
			if (!composed)
				return false;
			node = path;
			sourceSize = SizeOf(*composed);
			sourceVoxels = SolidVoxels(*composed);

			// Every step from 1/10 to 10/1, the even ones and the ends named;
			// a finer one too big for a KV6 cannot be chosen.
			const std::vector<VoxelResolution>& steps = VoxelResolution::Steps();
			std::vector<StepSlider::Step> slider;
			int start = 0;
			for (std::size_t i = 0; i < steps.size(); i++) {
				const VoxelResolution& r = steps[i];
				StepSlider::Step s;
				s.label = r.Label();
				const int n = std::max(r.finer, r.coarser);
				s.major = n == 1 || n == 2 || n == 5 || n == VoxelResolution::kMaxStep;
				s.usable = Fits(r.SizeOf(sourceSize));
				if (r == VoxelResolution())
					start = int(i); // 1/1: the node as it is
				slider.push_back(std::move(s));
			}
			const VoxelObject* o = scene.Node(path);
			panel.Open(o ? (o->name.empty() ? std::string("(unnamed)") : o->name)
			             : std::string("the whole scene"),
			           std::move(slider), start);
			Update(scene);
			return true;
		}

		void ResampleTool::Update(KV6Scene& scene) {
			resolution = VoxelResolution::Steps()[std::size_t(panel.Index())];
			std::string why;
			model = scene.Resampled(node, resolution, frame, why);
			if (model)
				panel.SetResult(sourceSize, sourceVoxels, SizeOf(*model), SolidVoxels(*model),
				                std::string());
			else
				panel.SetResult(sourceSize, sourceVoxels, MakeIntVector3(0, 0, 0), 0, why);
			// The node steps aside for what it would become.
			scene.SetPreview(node, model, scene.NodeFrame(node, frame));
		}

		std::string ResampleTool::Finish(KV6Scene& scene, bool create) {
			scene.ClearPreview();
			panel.Close();
			std::string said;
			if (create) {
				std::string why;
				if (scene.AddResampled(node, model, frame, resolution, why))
					said = "Added " + scene.Node(scene.Active())->name + ", at " +
					       resolution.Label() + " of the resolution";
				else
					said = "Cannot add the model: " + why;
			}
			model = Handle<VoxelModel>();
			return said;
		}

		void ResampleTool::Cancel(KV6Scene& scene) {
			if (IsOpen())
				Finish(scene, false);
		}

		std::string ResampleTool::Run(KV6Scene& scene, ResamplePanel::Action action) {
			switch (action) {
				case ResamplePanel::Action::Changed: Update(scene); break;
				case ResamplePanel::Action::Create: return Finish(scene, true);
				case ResamplePanel::Action::Cancel: return Finish(scene, false);
				case ResamplePanel::Action::None: break;
			}
			return std::string();
		}

		bool ResampleTool::Input(KV6Scene& scene, const std::string& key, bool down,
		                         const Vector2& cursor, std::string& said) {
			said.clear();
			if (!IsOpen())
				return false;
			ResamplePanel::Action action = ResamplePanel::Action::None;
			if (key == "LeftMouseButton")
				action = down ? panel.Press(cursor) : panel.Release(cursor);
			else if (key == "RightMouseButton")
				; // nothing to open meanwhile: taken all the same
			else if (down)
				action = panel.Key(key);
			if (action == ResamplePanel::Action::None)
				return key == "LeftMouseButton" || key == "RightMouseButton";
			said = Run(scene, action);
			return true;
		}

		bool ResampleTool::Move(KV6Scene& scene, const Vector2& cursor) {
			if (!IsOpen())
				return false;
			Run(scene, panel.Move(cursor));
			return true;
		}

		void ResampleTool::Draw(float x, float y, const Vector2& cursor) {
			panel.Layout(x, y);
			panel.Draw(cursor);
		}
	} // namespace gui
} // namespace spades
