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

#include "RenderTool.h"

#include <algorithm>
#include <cmath>

#include "VoxelEditContext.h"

namespace spades {
	namespace gui {
		namespace {
			const char* const kIsometricOption = "render.isometric";
			const char* const kDimetricOption = "render.dimetric";
			const char* const kTurnOption = "render.turn";
			const char* const kRedOption = "render.red";
			const char* const kGreenOption = "render.green";
			const char* const kBlueOption = "render.blue";
			const char* const kHexOption = "render.hex";
			const char* const kBackgroundOption = "render.background";
			const char* const kTransparentOption = "render.transparent";
			const char* const kSaveOption = "render.save";
			const char* const kViewGroup = "View";
			const char* const kBackgroundGroup = "Background";
			const char* const kImageGroup = "";

			// How far above the horizon the camera looks down from. A true
			// isometric view foreshortens the three axes alike, which puts it
			// at atan(1/sqrt 2); the one pixel art goes by draws the ground's
			// diagonals two pixels across for one down, which is 30 degrees.
			const float kIsometricElevation = 0.6154797F;
			const float kDimetricElevation = kPi / 6.0F;

			// One channel of a colour held as 0x00BBGGRR; `shift` is its place.
			int Channel(std::uint32_t color, int shift) { return int((color >> shift) & 0xFF); }
			void SetChannel(std::uint32_t& color, int shift, float value) {
				const std::uint32_t byte =
				  std::uint32_t(std::max(0, std::min(255, int(std::lround(value)))));
				color = (color & ~(std::uint32_t(0xFF) << shift)) | (byte << shift);
			}
			const int kRedShift = 0, kGreenShift = 8, kBlueShift = 16;
		} // namespace

		RenderTool::RenderTool() {
			options.AddAction(kIsometricOption, "Isometric", kViewGroup);
			options.AddAction(kDimetricOption, "2:1", kViewGroup);
			options.AddAction(kTurnOption, "Turn 90", kViewGroup);
			options.AddNumber(kRedOption, "R", kBackgroundGroup, 1.0F, 0);
			options.AddNumber(kGreenOption, "G", kBackgroundGroup, 1.0F, 0);
			options.AddNumber(kBlueOption, "B", kBackgroundGroup, 1.0F, 0);
			options.AddHex(kHexOption, "Hex", kBackgroundGroup);
			options.AddBool(kTransparentOption, "Transparent", kBackgroundGroup);
			options.AddAction(kSaveOption, "Render", kImageGroup);
			// What the picker sets while this tool is active.
			options.AddColor(kBackgroundOption);
		}

		std::string RenderTool::Hint(IVoxelEditContext&) {
			return "the view is the image: turn it as you like  |  the colour picker sets the "
			       "background  |  [Enter] render";
		}

		void RenderTool::OnKey(IVoxelEditContext& ed, const KeyInput& e) {
			if (e.IsDown() && e.key == "Enter")
				ed.SaveRender(background, transparent);
		}

		void RenderTool::UpdateOptions(IVoxelEditContext&) {
			options.SetNumber(kRedOption, float(Channel(background, kRedShift)));
			options.SetNumber(kGreenOption, float(Channel(background, kGreenShift)));
			options.SetNumber(kBlueOption, float(Channel(background, kBlueShift)));
			// Hex reads red first, the way it is written.
			options.SetNumber(kHexOption, float((Channel(background, kRedShift) << 16) |
			                                    (Channel(background, kGreenShift) << 8) |
			                                    Channel(background, kBlueShift)));
			options.SetBool(kTransparentOption, transparent);
			options.SetColor(kBackgroundOption, background);
		}

		void RenderTool::OnAction(IVoxelEditContext& ed, const std::string& id) {
			if (id == kIsometricOption) {
				ed.LookIsometric(kIsometricElevation);
				ed.SetStatus("Isometric view");
			} else if (id == kDimetricOption) {
				ed.LookIsometric(kDimetricElevation);
				ed.SetStatus("2:1 isometric view");
			} else if (id == kTurnOption) {
				ed.TurnView(1);
			} else if (id == kSaveOption) {
				ed.SaveRender(background, transparent); // reports where it went
			}
		}

		void RenderTool::OnOptionColorChanged(IVoxelEditContext&, const std::string& id,
		                                      std::uint32_t color) {
			if (id == kBackgroundOption)
				background = color & 0xFFFFFF;
		}

		void RenderTool::OnOptionToggled(IVoxelEditContext&, const std::string& id, bool value) {
			if (id == kTransparentOption)
				transparent = value;
		}

		// Shown as it is typed: the background is no part of the document, so
		// there is nothing for a value not yet settled on to undo.
		void RenderTool::OnOptionNumberChanged(IVoxelEditContext&, const std::string& id,
		                                       float value, bool) {
			if (id == kRedOption) {
				SetChannel(background, kRedShift, value);
			} else if (id == kGreenOption) {
				SetChannel(background, kGreenShift, value);
			} else if (id == kBlueOption) {
				SetChannel(background, kBlueShift, value);
			} else if (id == kHexOption) {
				const std::uint32_t rgb = std::uint32_t(std::max(0.0F, value)) & 0xFFFFFF;
				background = ((rgb >> 16) & 0xFF) | (rgb & 0xFF00) | ((rgb & 0xFF) << 16);
			}
		}
	} // namespace gui
} // namespace spades
