/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <Core/Math.h>

namespace spades {
	namespace client {
		class IRenderer;
	}
	namespace gui {
		/**
		 * Parameters describing a custom crosshair. Mirrors the `TargetParam` used by
		 * the in-game view-weapon skin so the settings preview matches gameplay.
		 */
		struct TargetParam {
			bool drawLines = false;
			bool useTStyle = false;
			Vector4 lineColor;
			float lineGap = 0.0F;
			Vector2 lineLength;
			float lineThickness = 0.0F;
			bool drawOutline = false;
			bool useRoundedStyle = false;
			Vector4 outlineColor;
			float outlineThickness = 0.0F;
			bool drawDot = false;
			Vector4 dotColor;
			float dotThickness = 0.0F;
		};

		/** Draws a custom crosshair centered at `pos`, identical to the gameplay one. */
		void DrawTarget(client::IRenderer& r, Vector2 pos, const TargetParam& param);
	} // namespace gui
} // namespace spades
