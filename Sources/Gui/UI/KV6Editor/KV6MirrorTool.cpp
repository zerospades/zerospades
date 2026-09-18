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

#include "KV6MirrorTool.h"
#include "KV6EditorContext.h"
#include "KV6SubTool.h"

#include <cstdio>

namespace spades {
	namespace gui {
		namespace {
			const char* const kAxisOption[3] = {"mirror.x", "mirror.y", "mirror.z"};
			const char* const kResetOption = "mirror.reset";
			const char* const kReadoutOption = "mirror.readout";
		} // namespace

		MirrorTool::MirrorTool() {
			subs.push_back(std::unique_ptr<EditorTool>(new MirrorGizmoSubTool()));

			options.AddBool(kAxisOption[0], "X", "Mirror");
			options.AddBool(kAxisOption[1], "Y", "Mirror");
			options.AddBool(kAxisOption[2], "Z", "Mirror");
			options.AddAction(kResetOption, "Reset to Pivot");
			options.AddLabel(kReadoutOption);
		}

		void MirrorTool::OnActivate(IEditorContext& ed) {
			// The editor owns the axis state and the toggles only show it, so bring
			// them up to date in case it changed while another tool was active.
			for (int a = 0; a < 3; a++)
				options.SetBool(kAxisOption[a], ed.MirrorEnabled(a));
			ContainerTool::OnActivate(ed);
		}

		void MirrorTool::OnOptionToggled(IEditorContext& ed, const std::string& id, bool value) {
			for (int a = 0; a < 3; a++) {
				if (id == kAxisOption[a]) {
					ed.SetMirrorEnabled(a, value);
					return;
				}
			}
		}

		void MirrorTool::OnAction(IEditorContext& ed, const std::string& id) {
			if (id == kResetOption) {
				ed.ResetMirrorPlane();
				ed.SetStatus("Mirror planes reset to the pivot");
			}
		}

		void MirrorTool::DrawScene(IEditorContext& ed) {
			Vector3 p = ed.MirrorPlane();
			char buf[80];
			std::snprintf(buf, sizeof(buf), "Plane  %.1f, %.1f, %.1f", p.x, p.y, p.z);
			options.SetLabel(kReadoutOption, buf);

			ContainerTool::DrawScene(ed);
		}
	} // namespace gui
} // namespace spades
