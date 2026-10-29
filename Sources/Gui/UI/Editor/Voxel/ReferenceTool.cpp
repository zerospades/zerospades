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

#include "ReferenceTool.h"

#include <algorithm>

#include <Core/LocalFileSystem.h>

#include "VoxelEditContext.h"

namespace spades {
	namespace gui {
		namespace {
			const char* const kLoadOption = "reference.load";
			const char* const kClearOption = "reference.clear";
			const char* const kShowOption = "reference.show";
			const char* const kFlipOption = "reference.flip";
			const char* const kSizeOption = "reference.size";
			const char* const kAcrossOption = "reference.across";
			const char* const kAlongOption = "reference.along";
			const char* const kDepthOption = "reference.depth";
			const char* const kOpacityOption = "reference.opacity";
			const char* const kFileOption = "reference.file";

			const char* const kAxisName[3] = {"X", "Y", "Z"};
			const char* const kAxisLetter[3] = {"x", "y", "z"};

			// One sub-tool per axis: which image the bar sets up.
			class AxisSubTool : public VoxelTool {
			public:
				explicit AxisSubTool(int axis) : axis(axis) {}
				const char* Label() const override { return kAxisName[axis]; }

			private:
				int axis;
			};

			// The smallest an image is scaled to, in voxels across.
			constexpr float kMinSize = 0.1F;
		} // namespace

		ReferenceTool::ReferenceTool() {
			for (int axis = 0; axis < 3; axis++)
				subs.emplace_back(new AxisSubTool(axis));
			options.AddAction(kLoadOption, "Load Image...");
			options.AddAction(kClearOption, "Remove");
			options.AddBool(kShowOption, "Show", "", true);
			options.AddBool(kFlipOption, "Mirror");
			options.AddNumber(kSizeOption, "Scale", "Size", 1.0F, 1);
			options.AddNumber(kAcrossOption, "", "Place", 0.5F, 1);
			options.AddNumber(kAlongOption, "", "Place", 0.5F, 1);
			options.AddNumber(kDepthOption, "", "Place", 0.5F, 1);
			options.AddNumber(kOpacityOption, "Opacity", "Look", 0.05F, 2);
			options.AddLabel(kFileOption);
		}

		void ReferenceTool::UpdateOptions(IVoxelEditContext& ed) {
			const int axis = active;
			const ReferenceImage& ref = ed.Reference(axis);
			int u, v;
			ReferenceAxes(axis, u, v);
			const bool loaded = ref.Loaded();
			// The boxes are named after the axis each one moves the image along.
			options.Find(kAcrossOption)->label = kAxisName[u];
			options.Find(kAlongOption)->label = kAxisName[v];
			options.Find(kDepthOption)->label = kAxisName[axis];
			options.SetNumber(kSizeOption, ref.size);
			options.SetNumber(kAcrossOption, ref.across);
			options.SetNumber(kAlongOption, ref.along);
			options.SetNumber(kDepthOption, ref.depth);
			options.SetNumber(kOpacityOption, ref.opacity);
			options.SetBool(kShowOption, ref.visible);
			options.SetBool(kFlipOption, ref.flipped);
			for (const char* id : {kClearOption, kShowOption, kFlipOption, kSizeOption, kAcrossOption,
			                       kAlongOption, kDepthOption, kOpacityOption})
				options.SetEnabled(id, loaded);
			options.SetLabel(kFileOption, loaded ? LocalFileSystem::GetFileName(ref.path)
			                                     : std::string("no image"));
		}

		void ReferenceTool::OnOptionToggled(IVoxelEditContext& ed, const std::string& id,
		                                    bool value) {
			ReferenceImage& ref = ed.Reference(active);
			if (id == kShowOption)
				ref.visible = value;
			else if (id == kFlipOption)
				ref.flipped = value;
		}

		void ReferenceTool::OnOptionNumberChanged(IVoxelEditContext& ed, const std::string& id,
		                                          float value, bool) {
			// Nothing here is journaled, so a value shows as soon as it is typed.
			ReferenceImage& ref = ed.Reference(active);
			if (id == kSizeOption)
				ref.size = std::max(kMinSize, value);
			else if (id == kAcrossOption)
				ref.across = value;
			else if (id == kAlongOption)
				ref.along = value;
			else if (id == kDepthOption)
				ref.depth = value;
			else if (id == kOpacityOption)
				ref.opacity = std::max(0.0F, std::min(1.0F, value));
		}

		void ReferenceTool::OnAction(IVoxelEditContext& ed, const std::string& id) {
			if (id == kLoadOption)
				ed.LoadReferenceImage(active);
			else if (id == kClearOption)
				ed.ClearReferenceImage(active);
		}

		std::string ReferenceTool::Hint(IVoxelEditContext& ed) {
			const ReferenceImage& ref = ed.Reference(active);
			if (!ref.Loaded())
				return std::string("[Load Image...] an image across ") + kAxisLetter[active] +
				       " to model from  |  [1/2/3] the image across X, Y or Z";
			return "scale, place and fade it from the bar  |  [1/2/3] the image across X, Y or Z";
		}
	} // namespace gui
} // namespace spades
