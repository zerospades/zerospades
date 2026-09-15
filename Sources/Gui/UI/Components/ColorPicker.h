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
		 * HSV colour picker popup: SV grid + hue bar + eyedropper + presets + close button.
		 * Positioned at bottom-right of the screen; can be opened/closed.
		 */
		class ColorPicker {
		public:
			enum class ClickType { None, SV, Hue, Close, Eyedropper, Preset };

			struct ClickResult {
				ClickType type = ClickType::None;
				int presetIndex = -1;
			};

			ColorPicker() = default;

			void SetPresets(const std::vector<uint32_t>& presets, int presetColumns = 8);
			void SetColor(uint32_t rgb);
			void Open();
			void Close();
			bool IsOpen() const { return open; }

			void UpdateLayout(float screenWidth, float screenHeight, float topClearance);

			ClickResult HitTest(const Vector2& p);
			void MouseDown(const Vector2& p);
			void MouseMove(const Vector2& p);
			void MouseUp();

			uint32_t GetColor() const;
			bool GetEyedropperMode() const { return eyedropperMode; }
			bool IsOverPicker(const Vector2& p) const;

			void Draw(client::IRenderer& renderer, client::FontManager& fontManager,
			         const Vector2& cursorPos);

			// Callbacks (optional)
			std::function<void(uint32_t)> OnColorChanged;
			std::function<void(uint32_t)> OnColorPicked;
			std::function<void(bool)> OnEyedropperToggled;

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

			// Presets
			std::vector<uint32_t> presets;
			int presetColumns = 8;

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
			float presX = 0.0F, presY = 0.0F;
			float presSwatch = 0.0F;
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
		};
	} // namespace gui
} // namespace spades
