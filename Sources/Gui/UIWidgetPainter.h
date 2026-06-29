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
		 * C++ painters for the in-app editor's chrome.
		 *
		 * Each function paints a widget to match the AngelScript `spades::ui` look
		 * (same primitives, same constants), so the editor stays visually consistent
		 * with the script UI without depending on the script framework.
		 *
		 * `pos`/`size` are the widget's screen rectangle; `enabled/hover/pressed/
		 * toggled` are its state.
		 */
		namespace widgets {
			// `textScale` defaults to 1 (the full widget size); the editor passes
			// smaller values for its compact bars while keeping the same style.
			void PaintButton(client::IRenderer& r, client::IFont& font, const Vector2& pos,
			                 const Vector2& size, const std::string& caption,
			                 const Vector2& alignment, const std::string& hotKeyText,
			                 const Vector2& hotKeyAlignment, bool enabled, bool hover, bool pressed,
			                 bool toggled, float textScale = 1.0F);
			void PaintSimpleButton(client::IRenderer& r, client::IFont& font, const Vector2& pos,
			                       const Vector2& size, const std::string& caption,
			                       const Vector2& alignment, const Vector4& textColor,
			                       const Vector4& disabledTextColor, bool enabled, bool hover,
			                       bool pressed, bool toggled, float textScale = 1.0F);
			void PaintCheckBox(client::IRenderer& r, client::IFont& font, const Vector2& pos,
			                   const Vector2& size, const std::string& caption, bool hover,
			                   bool pressed, bool toggled, float textScale = 1.0F);
			// `enabled` here is the element's own Enable flag (RadioButton reads
			// `this.Enable`, not the inherited `IsEnabled`).
			void PaintRadioButton(client::IRenderer& r, client::IFont& font, const Vector2& pos,
			                      const Vector2& size, const std::string& caption, bool enabled,
			                      bool hover, bool pressed, bool toggled, float textScale = 1.0F);
			// The text-field box chrome. The caret/selection/text are drawn by the
			// caller (the editor draws its own beam).
			void PaintField(client::IRenderer& r, const Vector2& pos, const Vector2& size,
			                bool focused, bool hover);
		} // namespace widgets
	} // namespace gui
} // namespace spades
