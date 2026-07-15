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

#include <Gui/UI/Widgets/ButtonBase.h>

namespace spades {
	namespace gui {
		namespace ui {
			/** The standard framed push button with a caption and optional hot-key text. */
			class Button : public ButtonBase {
			public:
				Vector2 alignment = MakeVector2(0.5F, 0.5F);
				std::string hotKeyText;
				Vector2 hotKeyTextAlignment = MakeVector2(1.0F, 0.5F);

				Button(UIManager* manager) : ButtonBase(manager) {}
				void Render() override;
			};

			/** A borderless, subtly-shaded button used in toolbars and tab-like rows. */
			class SimpleButton : public Button {
			public:
				Vector4 textColor = MakeVector4(1, 1, 1, 1);
				Vector4 disabledTextColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.4F);

				SimpleButton(UIManager* manager) : Button(manager) {}
				void Render() override;
			};

			/** A toggle button that is mutually exclusive within a named group. */
			class RadioButton : public Button {
			public:
				std::string groupName;

				RadioButton(UIManager* manager) : Button(manager) { toggle = true; }

				void Check();
				void OnActivated() override;
				void Render() override;
			};
		} // namespace ui
	} // namespace gui
} // namespace spades
