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

#include "ColorPicker.h"

#include <algorithm>
#include <Gui/OverlayPaint.h>
#include <Client/IRenderer.h>
#include <Client/Fonts.h>
#include <Core/Math.h>

namespace spades {
	namespace gui {
		void ColorPicker::SetPresets(const std::vector<uint32_t>& p, int cols) {
			presets = p;
			presetColumns = std::max(1, cols);
		}

		void ColorPicker::SetColor(uint32_t rgb) {
			RGBToHSV(rgb);
			SyncColor();
		}

		void ColorPicker::Open() {
			open = true;
			eyedropperMode = false;
		}

		void ColorPicker::Close() {
			open = false;
			eyedropperMode = false;
			dragMode = 0;
		}

		void ColorPicker::UpdateLayout(float screenWidth, float screenHeight, float topClearance) {
			float bottomClear = 44.0F;
			presSwatch = (svSize + 6.0F + hueW) / float(presetColumns);
			float contentW = svSize + 6.0F + hueW;
			prevH = 22.0F;
			float headerH = 18.0F;
			pkW = 8.0F * 2.0F + contentW;
			pkH = 8.0F * 2.0F + headerH + svSize + 6.0F + prevH + 6.0F + 2.0F * presSwatch;
			pkX = screenWidth - 16.0F - pkW;
			pkY = screenHeight - bottomClear - pkH;
			closeS = 13.0F;
			closeX = pkX + pkW - 8.0F - closeS;
			closeY = pkY + 5.0F;
			svX = pkX + 8.0F;
			svY = pkY + 8.0F + headerH;
			hueX = svX + svSize + 6.0F;
			hueY = svY;
			prevY = svY + svSize + 6.0F;
			eyeS = prevH;
			prevX = svX;
			prevW = contentW - eyeS - 6.0F;
			eyeX = prevX + prevW + 6.0F;
			eyeY = prevY;
			presX = svX;
			presY = prevY + prevH + 6.0F;
		}

		uint32_t ColorPicker::PackRGB(float r, float g, float b) const {
			int ri = std::max(0, std::min(255, int(r * 255.0F + 0.5F)));
			int gi = std::max(0, std::min(255, int(g * 255.0F + 0.5F)));
			int bi = std::max(0, std::min(255, int(b * 255.0F + 0.5F)));
			return uint32_t((bi << 16) | (gi << 8) | ri); // 0x00BBGGRR
		}

		uint32_t ColorPicker::HSVToRGB(float h, float s, float v) const {
			float i = std::floor(h * 6.0F);
			float f = h * 6.0F - i;
			float p = v * (1.0F - s);
			float q = v * (1.0F - f * s);
			float t = v * (1.0F - (1.0F - f) * s);
			int ii = int(i) % 6;
			if (ii < 0) ii += 6;
			if (ii == 0) return PackRGB(v, t, p);
			if (ii == 1) return PackRGB(q, v, p);
			if (ii == 2) return PackRGB(p, v, t);
			if (ii == 3) return PackRGB(p, q, v);
			if (ii == 4) return PackRGB(t, p, v);
			return PackRGB(v, p, q);
		}

		Vector4 ColorPicker::ColorToVec(uint32_t c) const {
			return MakeVector4(float(c & 0xFF) / 255.0F, float((c >> 8) & 0xFF) / 255.0F,
			                   float((c >> 16) & 0xFF) / 255.0F, 1.0F);
		}

		void ColorPicker::RGBToHSV(uint32_t c) {
			float r = float(c & 0xFF) / 255.0F;
			float g = float((c >> 8) & 0xFF) / 255.0F;
			float b = float((c >> 16) & 0xFF) / 255.0F;
			float mx = std::max(r, std::max(g, b));
			float mn = std::min(r, std::min(g, b));
			float d = mx - mn;
			val = mx;
			sat = (mx <= 0.0F) ? 0.0F : d / mx;
			float h = 0.0F;
			if (d > 0.0F) {
				if (mx == r) h = (g - b) / d + (g < b ? 6.0F : 0.0F);
				else if (mx == g) h = (b - r) / d + 2.0F;
				else h = (r - g) / d + 4.0F;
				h /= 6.0F;
			}
			hue = h;
		}

		void ColorPicker::SyncColor() {
			uint32_t c = HSVToRGB(hue, sat, val);
			if (OnColorChanged)
				OnColorChanged(c);
		}

		bool ColorPicker::InRect(const Vector2& p, float x, float y, float w, float h) const {
			return OverlayInRect(p, x, y, w, h);
		}

		void ColorPicker::UpdateSV(const Vector2& p) {
			sat = Clamp((p.x - svX) / svSize, 0.0F, 1.0F);
			val = Clamp(1.0F - (p.y - svY) / svSize, 0.0F, 1.0F);
			SyncColor();
		}

		void ColorPicker::UpdateHue(const Vector2& p) {
			hue = Clamp((p.y - hueY) / svSize, 0.0F, 1.0F);
			SyncColor();
		}

		ColorPicker::ClickResult ColorPicker::HitTest(const Vector2& p) {
			if (!open)
				return {};

			if (InRect(p, closeX, closeY, closeS, closeS))
				return {ClickType::Close, -1};
			if (InRect(p, svX, svY, svSize, svSize))
				return {ClickType::SV, -1};
			if (InRect(p, hueX, hueY, hueW, svSize))
				return {ClickType::Hue, -1};
			if (InRect(p, eyeX, eyeY, eyeS, eyeS))
				return {ClickType::Eyedropper, -1};

			// Check preset swatches
			for (size_t i = 0; i < presets.size(); i++) {
				float x = presX + float(int(i) % presetColumns) * presSwatch;
				float y = presY + float(int(i) / presetColumns) * presSwatch;
				if (InRect(p, x, y, presSwatch, presSwatch))
					return {ClickType::Preset, int(i)};
			}

			return {ClickType::None, -1};
		}

		void ColorPicker::MouseDown(const Vector2& p) {
			if (!open) return;
			auto result = HitTest(p);
			switch (result.type) {
				case ClickType::SV:
					dragMode = 1;
					UpdateSV(p);
					break;
				case ClickType::Hue:
					dragMode = 2;
					UpdateHue(p);
					break;
				case ClickType::Eyedropper:
					eyedropperMode = !eyedropperMode;
					if (OnEyedropperToggled)
						OnEyedropperToggled(eyedropperMode);
					break;
				case ClickType::Preset:
					if (result.presetIndex >= 0 && result.presetIndex < int(presets.size())) {
						SetColor(presets[result.presetIndex]);
						if (OnColorPicked)
							OnColorPicked(GetColor());
					}
					break;
				default:
					break;
			}
		}

		void ColorPicker::MouseMove(const Vector2& p) {
			if (dragMode == 1)
				UpdateSV(p);
			else if (dragMode == 2)
				UpdateHue(p);
		}

		void ColorPicker::MouseUp() { dragMode = 0; }

		uint32_t ColorPicker::GetColor() const { return HSVToRGB(hue, sat, val); }

		bool ColorPicker::IsOverPicker(const Vector2& p) const {
			return open && p.x >= pkX && p.x < pkX + pkW && p.y >= pkY && p.y < pkY + pkH;
		}

		void ColorPicker::Draw(client::IRenderer& renderer, client::FontManager& fontManager,
		                       const Vector2& cursorPos) {
			if (!open)
				return;

			client::IFont& hf = fontManager.GetSmallGuiFont();

			// Semi-transparent black background panel
			OverlayColorNP(renderer, MakeVector4(0.0F, 0.0F, 0.0F, 0.55F));
			OverlayFillRect(renderer, pkX, pkY, pkW, pkH);

			// Header: "Colour" + close button
			hf.Draw("Colour", MakeVector2(pkX + 8.0F, pkY + 4.0F), 0.75F,
			        MakeVector4(0.85F, 0.85F, 0.85F, 1.0F));
			OverlayColorNP(renderer, MakeVector4(0.35F, 0.18F, 0.18F, 1.0F));
			OverlayFillRect(renderer, closeX, closeY, closeS, closeS);
			OverlayStrokeRect(renderer, closeX, closeY, closeS, closeS, 1.0F,
			                 MakeVector4(0.7F, 0.5F, 0.5F, 0.9F));

			// Close button X icon
			auto DrawLine2D = [&](const Vector2& a, const Vector2& b, float w, const Vector4& col) {
				Vector2 d = b - a;
				float len = d.GetLength();
				if (len < 0.001F) return;
				Vector2 n = MakeVector2(-d.y, d.x) * (w * 0.5F / len);
				OverlayColorNP(renderer, col);
				renderer.DrawImage((client::IImage*)NULL, a + n, b + n, a - n, AABB2(0, 0, 1, 1));
			};
			DrawLine2D(MakeVector2(closeX + 3.0F, closeY + 3.0F),
			          MakeVector2(closeX + closeS - 3.0F, closeY + closeS - 3.0F), 1.5F,
			          MakeVector4(1, 1, 1, 0.9F));
			DrawLine2D(MakeVector2(closeX + closeS - 3.0F, closeY + 3.0F),
			          MakeVector2(closeX + 3.0F, closeY + closeS - 3.0F), 1.5F,
			          MakeVector4(1, 1, 1, 0.9F));

			// SV square (24x24 grid)
			int cells = 24;
			float cw = svSize / float(cells);
			for (int yi = 0; yi < cells; yi++) {
				for (int xi = 0; xi < cells; xi++) {
					float s = (float(xi) + 0.5F) / float(cells);
					float v = 1.0F - (float(yi) + 0.5F) / float(cells);
					OverlayColorNP(renderer, ColorToVec(HSVToRGB(hue, s, v)));
					OverlayFillRect(renderer, svX + float(xi) * cw, svY + float(yi) * cw,
					               cw + 0.5F, cw + 0.5F);
				}
			}

			// SV crosshair indicator
			float mx = svX + sat * svSize;
			float my = svY + (1.0F - val) * svSize;
			OverlayColorNP(renderer, MakeVector4(1.0F, 1.0F, 1.0F, 0.9F));
			OverlayFillRect(renderer, mx - 4.0F, my - 1.0F, 8.0F, 2.0F);
			OverlayFillRect(renderer, mx - 1.0F, my - 4.0F, 2.0F, 8.0F);

			// Hue bar (24 rows)
			int hcells = 24;
			float hh = svSize / float(hcells);
			for (int i = 0; i < hcells; i++) {
				OverlayColorNP(renderer, ColorToVec(HSVToRGB((float(i) + 0.5F) / float(hcells), 1.0F, 1.0F)));
				OverlayFillRect(renderer, hueX, hueY + float(i) * hh, hueW, hh + 0.5F);
			}

			// Hue indicator
			float hy = hueY + hue * svSize;
			OverlayColorNP(renderer, MakeVector4(1.0F, 1.0F, 1.0F, 0.9F));
			OverlayFillRect(renderer, hueX - 2.0F, hy - 1.5F, hueW + 4.0F, 3.0F);

			// Current color preview
			OverlayColorNP(renderer, ColorToVec(GetColor()));
			OverlayFillRect(renderer, prevX, prevY, prevW, prevH);

			// Eyedropper button
			OverlayColorNP(renderer, eyedropperMode ? MakeVector4(0.18F, 0.45F, 0.24F, 1.0F)
			                                        : MakeVector4(0.18F, 0.18F, 0.20F, 1.0F));
			OverlayFillRect(renderer, eyeX, eyeY, eyeS, eyeS);
			DrawLine2D(MakeVector2(eyeX + 5.0F, eyeY + eyeS - 5.0F),
			          MakeVector2(eyeX + eyeS - 5.0F, eyeY + 5.0F), 2.5F,
			          MakeVector4(1.0F, 1.0F, 1.0F, 0.9F));
			OverlayColorNP(renderer, ColorToVec(GetColor()));
			OverlayFillRect(renderer, eyeX + 4.0F, eyeY + eyeS - 8.0F, 4.0F, 4.0F);
			OverlayStrokeRect(renderer, eyeX, eyeY, eyeS, eyeS,
			                 eyedropperMode ? 2.0F : 1.0F,
			                 eyedropperMode ? MakeVector4(0.5F, 1.0F, 0.6F, 1.0F)
			                                : MakeVector4(0.5F, 0.5F, 0.5F, 0.7F));

			// Preset swatches
			float pad = 2.0F;
			for (size_t i = 0; i < presets.size(); i++) {
				float x = presX + float(int(i) % presetColumns) * presSwatch;
				float y = presY + float(int(i) / presetColumns) * presSwatch;
				OverlayColorNP(renderer, ColorToVec(presets[i]));
				OverlayFillRect(renderer, x, y, presSwatch - pad, presSwatch - pad);
			}
		}
	} // namespace gui
} // namespace spades
