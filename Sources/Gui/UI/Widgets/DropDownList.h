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

#include <functional>
#include <string>
#include <vector>

#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/ListView.h>

namespace spades {
	namespace gui {
		namespace ui {
			/** Invoked with the chosen row index, or `-1` if the list was dismissed. */
			using DropDownListHandler = std::function<void(int)>;

			class DropDownView;

			/** A single selectable row in a drop-down popup. */
			class DropDownViewItem : public Button {
			public:
				int index = 0;
				DropDownViewItem(UIManager* manager) : Button(manager) {}
				void Render() override;
			};

			/** `ListViewModel` backing a drop-down popup with a list of strings. */
			class DropDownViewModel : public ListViewModel {
				UIManager* manager; // weak
				std::vector<std::string> list;

				void ItemClicked(UIElement& sender);

			public:
				DropDownListHandler handler;

				DropDownViewModel(UIManager* manager, std::vector<std::string> list);
				int GetNumRows() override { return static_cast<int>(list.size()); }
				Handle<UIElement> CreateElement(int row) override;
			};

			/** Full-screen dimmed backdrop that dismisses the popup when clicked. */
			class DropDownBackground : public UIElement {
			public:
				DropDownView* view;           // weak; owned as a child
				UIElement* oldRoot = nullptr; // weak

				DropDownBackground(UIManager* manager, DropDownView* view);
				void Destroy();
				void MouseDown(MouseButton button, Vector2 clientPosition) override;
				void Render() override;
			};

			/** The list body of a drop-down popup. */
			class DropDownView : public ListView {
				void ItemActivated(int index);

			public:
				DropDownListHandler handler;
				DropDownBackground* bg = nullptr; // weak; our parent

				DropDownView(UIManager* manager, DropDownListHandler handler,
				             std::vector<std::string> items);
				void Destroy();
			};

			void ShowDropDownList(UIManager* manager, float x, float y, float width,
			                      std::vector<std::string> items, DropDownListHandler handler);
			void ShowDropDownList(UIElement& e, std::vector<std::string> items,
			                      DropDownListHandler handler);
		} // namespace ui
	} // namespace gui
} // namespace spades
