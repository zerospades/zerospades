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

#include <string>

#include <Core/Math.h>

namespace spades {
	namespace client {
		class IRenderer;
		class IFont;
	} // namespace client
	namespace gui {
		/**
		 * The one home of the shared `spades::ui` widget appearance.
		 *
		 * Each function paints a widget exactly as its AngelScript `Render()` used to
		 * (same primitives, same constants), so both sides stay pixel-identical:
		 *   - the in-app editor calls these directly (C++-only UI, not moddable);
		 *   - the script UI forwards to them from `Render()` (see the binding in
		 *     `ScriptBindings/UIWidgetPainterScript.cpp`), and remains overridable by
		 *     mods that rewrite the `.as` widget.
		 *
		 * `pos`/`size` are the widget's screen rectangle; `enabled/hover/pressed/
		 * toggled` are its state. Edit the look in one place, here.
		 */
		namespace widgets {
			void PaintButton(client::IRenderer& r, client::IFont& font, const Vector2& pos,
			                 const Vector2& size, const std::string& caption,
			                 const Vector2& alignment, const std::string& hotKeyText,
			                 const Vector2& hotKeyAlignment, bool enabled, bool hover, bool pressed,
			                 bool toggled);
			void PaintSimpleButton(client::IRenderer& r, client::IFont& font, const Vector2& pos,
			                       const Vector2& size, const std::string& caption,
			                       const Vector2& alignment, const Vector4& textColor,
			                       const Vector4& disabledTextColor, bool enabled, bool hover,
			                       bool pressed, bool toggled);
			void PaintCheckBox(client::IRenderer& r, client::IFont& font, const Vector2& pos,
			                   const Vector2& size, const std::string& caption, bool hover,
			                   bool pressed, bool toggled);
			// `enabled` here is the element's own Enable flag (RadioButton reads
			// `this.Enable`, not the inherited `IsEnabled`).
			void PaintRadioButton(client::IRenderer& r, client::IFont& font, const Vector2& pos,
			                      const Vector2& size, const std::string& caption, bool enabled,
			                      bool hover, bool pressed, bool toggled);
		} // namespace widgets
	} // namespace gui
} // namespace spades
