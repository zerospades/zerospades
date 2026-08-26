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

#include <string>

#include <Gui/UI/Framework/UIElement.h>

namespace spades {
	namespace gui {
		namespace ui {
			/** A static, non-interactive block of text with optional background. */
			class Label : public UIElement {
			public:
				std::string text;
				Vector4 backgroundColor = MakeVector4(0, 0, 0, 0);
				Vector4 textColor = MakeVector4(1, 1, 1, 1);
				Vector4 disabledTextColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.3F);
				Vector2 alignment = MakeVector2(0.0F, 0.0F);
				float textScale = 1.0F;

				Label(UIManager* manager) : UIElement(manager) {}

				void Render() override;
			};
		} // namespace ui
	} // namespace gui
} // namespace spades
