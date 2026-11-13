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

// What the parts of VoxelEditor's implementation share: it is one class in
// several source files, by concern, as the client's own is (Client.cpp,
// Client_Input.cpp, Client_Draw.cpp, ...). Not for anything else to include.

#include "VoxelEditor.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <unordered_map>
#include <tuple>
#include <utility>

#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IModel.h>
#include <Client/IRenderer.h>
#include <Core/Bitmap.h>
#include <Core/Debug.h>
#include <Core/LocalFileSystem.h>
#include <Core/StdStream.h>
#include <Core/Settings.h>
#include <Core/VoxelModel.h>
#include <Gui/OverlayPaint.h>
#include <Gui/UI/Components/BarLayout.h>
#include <Gui/UI/Components/ColorPicker.h>
#include <Gui/UI/Components/MenuBar.h>
#include <Gui/UI/Components/Gizmo/GizmoCanvas.h>
#include <Gui/UI/Components/Gizmo/TransformGizmo.h>
#include <Gui/UI/Components/OptionBar.h>
#include <Gui/UI/Components/SoftwareCursor.h>
#include <Gui/UI/Components/Toolbar.h>
#include <Gui/UI/Components/TransformPanel.h>
#include <Gui/UI/Widgets/TextEntry.h>
#include <Gui/UI/Components/UIOverlayHost.h>
#include <Gui/UI/Editor/Shell/EditorKeys.h>
#include <Gui/UI/Editor/Shell/EditorUI.h>

#include "SubTool.h"
#include "VoxelDocument.h"
#include "VoxelEditorHost.h"

namespace spades {
	namespace gui {
		namespace voxeleditor {
			// `v` in whole half-voxel steps, to the nearest step with ties going up.
			// A mirror plane `k` half steps from the origin maps voxel i to k - i,
			// and the plane snaps to the same count, so snapping never changes
			// which voxels a plane pairs up.
			inline int HalfSteps(float v) { return int(std::floor(v * 2.0F + 0.5F)); }

			// Top UI bands (full width): the host's title ribbon, the toolbar and
			// the option bar (the active tool's sub-tools). The viewport starts
			// below them.
			using namespace barlayout;
			const float kBarsH = kRibbonH + kToolbarH + kSubBarH;

			// Parts of a hint or help line; a line only ever breaks between them.
			const std::string kHintSeparator = "  |  ";

			// Splits `text` at its separators into lines no wider than `maxWidth`.
			// A part wider than that on its own gets a line to itself.
			inline std::vector<std::string> WrapParts(client::IFont& font, const std::string& text,
			                                   float maxWidth) {
				std::vector<std::string> lines;
				std::string line;
				std::size_t start = 0;
				while (true) {
					const std::size_t end = text.find(kHintSeparator, start);
					const std::string part = text.substr(start, end - start);
					const std::string joined = line.empty() ? part : line + kHintSeparator + part;
					if (!line.empty() && font.Measure(joined).x > maxWidth) {
						lines.push_back(line);
						line = part;
					} else {
						line = joined;
					}
					if (end == std::string::npos)
						break;
					start = end + kHintSeparator.size();
				}
				if (!line.empty())
					lines.push_back(line);
				return lines;
			}

			// The six voxels sharing a face with a voxel, as offsets.
			const IntVector3 kFaceNeighbours[6] = {
			  MakeIntVector3(1, 0, 0),  MakeIntVector3(-1, 0, 0), MakeIntVector3(0, 1, 0),
			  MakeIntVector3(0, -1, 0), MakeIntVector3(0, 0, 1),  MakeIntVector3(0, 0, -1)};

			// The smallest box holding `cells`; false when there are none.
			inline bool BoundsOf(const std::vector<IntVector3>& cells, IntVector3& lo, IntVector3& hi) {
				if (cells.empty())
					return false;
				lo = hi = cells.front();
				for (const IntVector3& c : cells) {
					lo = MakeIntVector3(std::min(lo.x, c.x), std::min(lo.y, c.y), std::min(lo.z, c.z));
					hi = MakeIntVector3(std::max(hi.x, c.x), std::max(hi.y, c.y), std::max(hi.z, c.z));
				}
				return true;
			}
		} // namespace voxeleditor
		using namespace voxeleditor;
	} // namespace gui
} // namespace spades
