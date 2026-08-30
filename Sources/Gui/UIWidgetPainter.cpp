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

#include "UIWidgetPainter.h"

#include <Client/IFont.h>
#include <Client/IImage.h>
#include <Client/IRenderer.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace gui {
		namespace widgets {
			namespace {
				// Matches the script `Renderer.ColorNP` setter: a non-premultiplied
				// colour, premultiplied here and handed to SetColorAlphaPremultiplied.
				void ColorNP(client::IRenderer& r, const Vector4& c) {
					r.SetColorAlphaPremultiplied(MakeVector4(c.x * c.w, c.y * c.w, c.z * c.w, c.w));
				}
			} // namespace

			void PaintButton(client::IRenderer& r, client::IFont& font, const Vector2& posIn,
			                 const Vector2& sizeIn, const std::string& caption,
			                 const Vector2& alignment, const std::string& hotKeyText,
			                 const Vector2& hotKeyAlignment, bool enabled, bool hover, bool pressed,
			                 bool toggled, float textScale) {
				Vector2 pos = posIn, size = sizeIn;
				Vector4 color = MakeVector4(0.2F, 0.2F, 0.2F, 0.5F);
				if (enabled) {
					if (toggled || (pressed && hover))
						color = MakeVector4(0.7F, 0.7F, 0.7F, 0.9F);
					else if (hover)
						color = MakeVector4(0.4F, 0.4F, 0.4F, 0.7F);
				} else {
					color.w *= 0.5F;
				}

				ColorNP(r, color);
				r.DrawFilledRect(pos.x + 1, pos.y + 1, pos.x + size.x - 1, pos.y + size.y - 1);

				ColorNP(r, MakeVector4(0.0F, 0.0F, 0.0F, color.w));
				r.DrawOutlinedRect(pos.x + 1, pos.y + 1, pos.x + size.x - 1, pos.y + size.y - 1);

				pos += MakeVector2(8.0F, 8.0F);
				size -= MakeVector2(16.0F, 16.0F);

				Vector2 txtSize = font.Measure(caption) * textScale;
				Vector2 txtPos = pos + (size - txtSize) * alignment;
				font.DrawShadow(caption, txtPos, textScale,
				                MakeVector4(1.0F, 1.0F, 1.0F, enabled ? 1.0F : 0.5F),
				                MakeVector4(0.0F, 0.0F, 0.0F, enabled ? 0.4F : 0.1F));

				txtSize = font.Measure(hotKeyText) * textScale;
				txtPos = pos + (size - txtSize) * hotKeyAlignment;
				font.DrawShadow(hotKeyText, txtPos, textScale,
				                MakeVector4(1.0F, 1.0F, 1.0F, enabled ? 0.6F : 0.3F),
				                MakeVector4(0.0F, 0.0F, 0.0F, enabled ? 0.1F : 0.05F));
			}

			void PaintSimpleButton(client::IRenderer& r, client::IFont& font, const Vector2& posIn,
			                       const Vector2& sizeIn, const std::string& caption,
			                       const Vector2& alignment, const Vector4& textColor,
			                       const Vector4& disabledTextColor, bool enabled, bool hover,
			                       bool pressed, bool toggled, float textScale) {
				Vector2 pos = posIn, size = sizeIn;
				Vector4 color = enabled ? textColor : disabledTextColor;

				if (toggled || (pressed && hover))
					ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.2F));
				else if (hover)
					ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.12F));
				else
					ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.07F * color.w));
				r.DrawFilledRect(pos.x, pos.y, pos.x + size.x, pos.y + size.y);

				if (toggled || (pressed && hover))
					ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.1F));
				else if (hover)
					ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.07F));
				else
					ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.03F * color.w));
				r.DrawOutlinedRect(pos.x, pos.y, pos.x + size.x, pos.y + size.y);

				pos += MakeVector2(4.0F, 4.0F);
				size -= MakeVector2(8.0F, 8.0F);

				Vector2 txtSize = font.Measure(caption) * textScale;
				Vector2 txtPos = pos + (size - txtSize) * alignment;
				font.DrawShadow(caption, txtPos, textScale, color, MakeVector4(0, 0, 0, 0.4F * color.w));
			}

			void PaintCheckBox(client::IRenderer& r, client::IFont& font, const Vector2& posIn,
			                   const Vector2& sizeIn, const std::string& caption, bool hover,
			                   bool pressed, bool toggled, float textScale) {
				Vector2 pos = posIn, size = sizeIn;
				Handle<client::IImage> img = r.RegisterImage("Gfx/UI/CheckBox.png");

				if (pressed && hover)
					ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.2F));
				else if (hover)
					ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.12F));
				else
					ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.0F));
				r.DrawFilledRect(pos.x, pos.y, pos.x + size.x, pos.y + size.y);

				Vector2 txtSize = font.Measure(caption) * textScale;
				font.DrawShadow(caption,
				                pos + (size - txtSize) * MakeVector2(0.0F, 0.5F) +
				                  MakeVector2(16.0F, 0.0F),
				                textScale, MakeVector4(1, 1, 1, 1), MakeVector4(0, 0, 0, 0.2F));

				ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, toggled ? 0.9F : 0.6F));
				r.DrawImage(*img, AABB2(pos.x, pos.y + (size.y - 16.0F) * 0.5F, 16.0F, 16.0F),
				            AABB2(toggled ? 16.0F : 0.0F, 0.0F, 16.0F, 16.0F));
			}

			void PaintRadioButton(client::IRenderer& r, client::IFont& font, const Vector2& posIn,
			                      const Vector2& sizeIn, const std::string& caption, bool enabled,
			                      bool hover, bool pressed, bool toggled, float textScale) {
				Vector2 pos = posIn, size = sizeIn;

				if (!enabled)
					ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.07F));
				if (toggled || (pressed && hover))
					ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.2F));
				else if (hover)
					ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.12F));
				else
					ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.07F));
				r.DrawFilledRect(pos.x, pos.y, pos.x + size.x, pos.y + size.y);

				if (!enabled)
					ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.03F));
				if (toggled || (pressed && hover))
					ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.06F));
				else if (hover)
					ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.04F));
				else
					ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.02F));
				r.DrawOutlinedRect(pos.x, pos.y, pos.x + size.x, pos.y + size.y);

				Vector2 txtSize = font.Measure(caption) * textScale;
				font.DrawShadow(caption, pos + (size - txtSize) * 0.5F + MakeVector2(8.0F, 0.0F),
				                textScale, MakeVector4(1, 1, 1, (toggled && enabled) ? 1.0F : 0.4F),
				                MakeVector4(0, 0, 0, 0.4F));

				if (toggled) {
					ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, (toggled && enabled) ? 0.6F : 0.3F));
					float dy = pos.y + (size.y - 8.0F) * 0.5F;
					r.DrawFilledRect(pos.x + 4.0F, dy, pos.x + 4.0F + 8.0F, dy + 8.0F);
				}
			}

			void PaintField(client::IRenderer& r, const Vector2& pos, const Vector2& size,
			                bool focused, bool hover) {
				ColorNP(r, MakeVector4(0.0F, 0.0F, 0.0F, focused ? 0.3F : 0.1F));
				r.DrawFilledRect(pos.x, pos.y, pos.x + size.x, pos.y + size.y);

				if (focused)
					ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.2F));
				else if (hover)
					ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.1F));
				else
					ColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.06F));
				r.DrawOutlinedRect(pos.x, pos.y, pos.x + size.x, pos.y + size.y);
			}
		} // namespace widgets
	} // namespace gui
} // namespace spades
