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

#include <cstdint>
#include <functional>
#include <vector>

#include <Core/Math.h>

namespace spades {
	namespace client {
		class IRenderer;
		class FontManager;
	} // namespace client
	namespace gui {
		/**
		 * HSV colour picker popup: SV grid + hue bar + eyedropper, then preset
		 * swatches (if any are set) and the colours used most recently, and a
		 * close button. Positioned at bottom-right of the screen.
		 *
		 * The host says when a colour is used (AddRecentColor), since only it
		 * knows: the recent row then holds the last colours actually applied,
		 * newest first, one click away.
		 *
		 * The eyedropper button arms a one-shot pick that only the host can carry
		 * out, since only it knows what is under the cursor. The picker holds the
		 * armed state, so there is exactly one copy of it: the host reads it with
		 * GetEyedropperMode and disarms it once it has picked. Opening or closing
		 * the picker disarms it too.
		 */
		class ColorPicker {
		public:
			enum class ClickType { None, SV, Hue, Close, Eyedropper, Preset, Recent };

			struct ClickResult {
				ClickType type = ClickType::None;
				int index = -1; // the swatch, for Preset and Recent
			};

			ColorPicker() = default;

			/** Swatches to offer, `columns` per row. */
			void SetPresets(const std::vector<uint32_t>& presets, int columns = 8);
			/** Shows `rgb`. Only the user's changes are reported (OnColorChanged). */
			void SetColor(uint32_t rgb);
			/** How many recently used colours the picker keeps, in one row. */
			static constexpr int kRecentSlots = 10;
			/**
			 * `rgb` was just used: it goes first in the recent row, moving up if it
			 * was there already. The oldest drops off past kRecentSlots.
			 */
			void AddRecentColor(uint32_t rgb);
			void Open();
			void Close();
			bool IsOpen() const { return open; }

			void UpdateLayout(float screenWidth, float screenHeight, float topClearance);

			ClickResult HitTest(const Vector2& p);
			/** A press on the panel, on whichever control is under it (if any). */
			void MouseDown(const Vector2& p);
			void MouseMove(const Vector2& p);
			void MouseUp();

			uint32_t GetColor() const;
			bool GetEyedropperMode() const { return eyedropperMode; }
			void SetEyedropperMode(bool armed) { eyedropperMode = armed; }
			bool IsOverPicker(const Vector2& p) const;
			/** The panel's screen rectangle as of the last UpdateLayout. */
			AABB2 GetBounds() const { return AABB2(pkX, pkY, pkW, pkH); }

			void Draw(client::IRenderer& renderer, client::FontManager& fontManager,
			         const Vector2& cursorPos);

			// Callbacks (optional)
			std::function<void(uint32_t)> OnColorChanged;
			std::function<void(uint32_t)> OnColorPicked;

		private:
			// HSV state
			float hue = 0.0F;
			float sat = 0.0F;
			float val = 0.78F;

			// Open/closed state
			bool open = false;
			bool eyedropperMode = false;

			// Drag tracking
			int dragMode = 0; // 0 none, 1 SV square, 2 hue bar

			// Swatches: presets, and recently used colours (newest first)
			std::vector<uint32_t> presets;
			int presetColumns = 8;
			std::vector<uint32_t> recent;

			// A grid of square swatches, `columns` to a row, from (x, y).
			struct SwatchGrid {
				float x = 0.0F, y = 0.0F, size = 0.0F;
				int columns = 1;
				Vector2 Cell(int i) const;
				// The swatch under `p` among the first `count`, or -1.
				int At(const Vector2& p, int count) const;
			};
			SwatchGrid presetGrid, recentGrid;
			float recentLabelY = 0.0F;

			// Picker panel geometry
			float pkX = 0.0F, pkY = 0.0F, pkW = 0.0F, pkH = 0.0F;
			float svX = 0.0F, svY = 0.0F;
			float svSize = 150.0F;
			float hueX = 0.0F, hueY = 0.0F;
			float hueW = 16.0F;
			float prevX = 0.0F, prevY = 0.0F;
			float prevW = 0.0F, prevH = 0.0F;
			float eyeX = 0.0F, eyeY = 0.0F;
			float eyeS = 0.0F;
			float closeX = 0.0F, closeY = 0.0F;
			float closeS = 0.0F;

			// Color space conversions
			uint32_t PackRGB(float r, float g, float b) const;
			uint32_t HSVToRGB(float h, float s, float v) const;
			Vector4 ColorToVec(uint32_t c) const;
			void RGBToHSV(uint32_t c);
			void SyncColor();

			// Helpers
			bool InRect(const Vector2& p, float x, float y, float w, float h) const;
			void UpdateSV(const Vector2& p);
			void UpdateHue(const Vector2& p);
			// Takes `rgb` as the user's choice: reported like any change they make.
			void ChooseColor(uint32_t rgb);
			// `colors` in `grid`; with `slots` above their count, the free cells
			// are drawn empty, so a row that is filling up reads as one.
			void DrawSwatches(client::IRenderer& renderer, const SwatchGrid& grid,
			                  const std::vector<uint32_t>& colors, int slots) const;
		};
	} // namespace gui
} // namespace spades
