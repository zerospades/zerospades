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
			/** A single tab in a `SimpleTabStrip`, toggling a linked panel's visibility. */
			class SimpleTabStripItem : public ButtonBase {
			public:
				UIElement* linkedElement; // weak; the panel this tab shows

				SimpleTabStripItem(UIManager* manager, UIElement* linkedElement);

				void MouseDown(MouseButton button, Vector2 clientPosition) override;
				void Render() override;
			};

			/** A horizontal strip of tabs, each showing one linked panel. */
			class SimpleTabStrip : public UIElement {
				float nextX = 0.0F;

				void OnItemActivated(UIElement& sender);

			public:
				EventHandler changed;

				SimpleTabStrip(UIManager* manager) : UIElement(manager) {}

				void AddItem(const std::string& title, UIElement* linkedElement);
				void Render() override;
			};
		} // namespace ui
	} // namespace gui
} // namespace spades
