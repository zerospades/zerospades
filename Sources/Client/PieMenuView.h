/*
 Copyright (c) 2026 Francois ND
 based on code of OpenSpades (c) yvt 2013.

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

#include <array>
#include <string>

#include <Core/Math.h>

namespace spades {
	namespace client {
		class Client;
		class IFont;
		class IRenderer;

		class PieMenuView {
		public:
			enum class Variant { World, Player };
			enum Slice { None = -1 };

			static constexpr int kSliceCount = 6;

		private:
			IRenderer& renderer;
			IFont* font;
			IFont* bigFont;

			bool open = false;
			Variant variant = Variant::World;
			int targetPlayerId = -1;
			Vector2 cursor = {0.0F, 0.0F};
			int selection = None;

			float openPhase = 0.0F;
			std::array<float, kSliceCount> highlight{};

			std::array<std::string, kSliceCount> worldLabels;
			std::array<std::string, kSliceCount> playerLabels;

			// Precomputed per-slice ray params (sin/cos of θ_c ± α).
			// Populated once in the constructor; used by scanline fill.
			struct SliceRay { float s1, c1, s2, c2; };
			std::array<SliceRay, kSliceCount> sliceRays;
			std::array<float, kSliceCount> sliceCenterAngles;

		public:
			PieMenuView(Client*, IFont* font, IFont* bigFont);
			~PieMenuView();

			void Open(Variant v, int targetPlayerId = -1);
			int Close();

			bool IsOpen() const { return open; }
			Variant GetVariant() const { return variant; }
			int GetTargetPlayerId() const { return targetPlayerId; }
			int GetSelection() const { return selection; }
			const std::string& GetSelectionLabel() const;
			const std::array<std::string, kSliceCount>& GetLabels() const {
				return (variant == Variant::Player) ? playerLabels : worldLabels;
			}

			void HandleMouseDelta(float dx, float dy);
			void Update(float dt);
			void Draw();
		};
	} // namespace client
} // namespace spades
