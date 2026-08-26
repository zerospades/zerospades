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

#include <deque>

#include <Gui/UI/Framework/UIElement.h>

namespace spades {
	namespace gui {
		namespace ui {
			class ScrollBar;

			/** Supplies rows to a `ListViewBase` on demand. */
			class ListViewModel : public RefCountedObject {
			protected:
				virtual ~ListViewModel() {}

			public:
				virtual int GetNumRows() { return 0; }
				virtual Handle<UIElement> CreateElement(int row) {
					(void)row;
					return Handle<UIElement>();
				}
				virtual void RecycleElement(UIElement& elem) { (void)elem; }
			};

			/** Simple virtual stack panel implementation. */
			class ListViewBase : public UIElement {
				Handle<ListViewModel> model;
				std::deque<Handle<UIElement>> items;
				int loadedStartIndex = 0;

				void OnScrolled(UIElement& sender);
				void UnloadAll();

			protected:
				ScrollBar* scrollBar; // weak; owned as a child

			public:
				float rowHeight = 24.0F;
				float scrollBarWidth = 16.0F;

				ListViewBase(UIManager* manager);

				int GetNumVisibleRows() const;
				int GetMaxTopRowIndex() const;
				int GetTopRowIndex() const;

				void OnResized() override;
				void Layout();

				float GetItemWidth() const { return size.x - scrollBarWidth; }

				void MouseWheel(float delta) override;

				void Reload();

				ListViewModel* GetModel() const { return model.GetPointerOrNull(); }
				void SetModel(ListViewModel* value);

				void ScrollToTop();
				void ScrollToEnd();
				void ScrollToRow(int row);
			};

			/** A `ListViewBase` with the standard framed background. */
			class ListView : public ListViewBase {
			public:
				ListView(UIManager* manager) : ListViewBase(manager) {}
				void Render() override;
			};
		} // namespace ui
	} // namespace gui
} // namespace spades
