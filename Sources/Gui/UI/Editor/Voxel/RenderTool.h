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

#include "VoxelTool.h"

namespace spades {
	namespace gui {
		// Makes an image of the model. While it is active the view is the
		// image's: the model alone, from wherever the camera stands, with none
		// of what the editor draws to work by (the ground, the axes, the
		// selection). Render saves it, on the background chosen or on none.
		//
		// The background is set any of three ways, which all show the one
		// colour: on the colour picker, whose swatch on the toolbar is the
		// background's while this tool is active, as red, green and blue, or
		// as the hex a web page would write it in.
		//
		// The camera stays the user's to turn; the bar only offers the views
		// an image of a voxel model is usually wanted from: a true isometric
		// one, the 2:1 one pixel art calls isometric, and the same from the
		// next corner round.
		class RenderTool : public VoxelTool {
		public:
			RenderTool();
			const char* Label() const override { return "Render"; }
			std::string Hint(IVoxelEditContext&) override;
			void OnKey(IVoxelEditContext&, const KeyInput&) override;

			ToolOptions* Options() override { return &options; }
			void UpdateOptions(IVoxelEditContext& ed) override;
			void OnAction(IVoxelEditContext& ed, const std::string& id) override;
			void OnOptionToggled(IVoxelEditContext& ed, const std::string& id, bool value) override;
			void OnOptionNumberChanged(IVoxelEditContext& ed, const std::string& id, float value,
			                           bool committed) override;

			void OnOptionColorChanged(IVoxelEditContext& ed, const std::string& id,
			                          std::uint32_t color) override;

		private:
			ToolOptions options;
			// 0x00BBGGRR, as the picker holds a colour: the editor's own dark
			// grey until another is chosen.
			std::uint32_t background = 0x1F1A1A;
			bool transparent = false;
		};
	} // namespace gui
} // namespace spades
