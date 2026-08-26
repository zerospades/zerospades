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

#include <algorithm>
#include <cmath>

#include "DrawUtils.h"
#include "ListView.h"
#include "ScrollBar.h"
#include <Client/IRenderer.h>
#include <Gui/UI/Framework/UIManager.h>

namespace spades {
	namespace gui {
		namespace ui {
			ListViewBase::ListViewBase(UIManager* manager) : UIElement(manager) {
				Handle<ScrollBar> sb = Handle<ScrollBar>::New(manager);
				scrollBar = sb.GetPointerOrNull();
				scrollBar->SetBounds(AABB2());
				AddChild(scrollBar);
				isMouseInteractive = true;

				scrollBar->changed = [this](UIElement&) { OnScrolled(*scrollBar); };
				model = Handle<ListViewModel>::New();
			}

			void ListViewBase::OnScrolled(UIElement&) { Layout(); }

			int ListViewBase::GetNumVisibleRows() const {
				return static_cast<int>(std::floor(size.y / rowHeight));
			}

			int ListViewBase::GetMaxTopRowIndex() const {
				return std::max(0, model->GetNumRows() - GetNumVisibleRows());
			}

			int ListViewBase::GetTopRowIndex() const {
				int idx = static_cast<int>(std::floor(scrollBar->value + 0.5));
				return Clamp(idx, 0, GetMaxTopRowIndex());
			}

			void ListViewBase::OnResized() {
				Layout();
				UIElement::OnResized();
			}

			void ListViewBase::Layout() {
				scrollBar->maxValue = static_cast<double>(GetMaxTopRowIndex());
				scrollBar->ScrollTo(scrollBar->value); // ensures value is in range
				scrollBar->largeChange = static_cast<double>(GetNumVisibleRows());

				int numRows = model->GetNumRows();

				// load items
				int visibleStart = GetTopRowIndex();
				int visibleEnd = std::min(visibleStart + GetNumVisibleRows(), numRows);
				int loadedStart = loadedStartIndex;
				int loadedEnd = loadedStartIndex + static_cast<int>(items.size());

				if (items.empty() || visibleStart >= loadedEnd || visibleEnd <= loadedStart) {
					// full reload
					UnloadAll();
					for (int i = visibleStart; i < visibleEnd; i++) {
						items.push_back(model->CreateElement(i));
						AddChild(items.back().GetPointerOrNull());
					}
					loadedStartIndex = visibleStart;
				} else {
					while (loadedStart < visibleStart) {
						UIElement* front = items.front().GetPointerOrNull();
						RemoveChild(front);
						model->RecycleElement(*front);
						items.pop_front();
						loadedStart++;
					}
					while (loadedEnd > visibleEnd) {
						UIElement* back = items.back().GetPointerOrNull();
						RemoveChild(back);
						model->RecycleElement(*back);
						items.pop_back();
						loadedEnd--;
					}
					while (visibleStart < loadedStart) {
						loadedStart--;
						items.push_front(model->CreateElement(loadedStart));
						AddChild(items.front().GetPointerOrNull());
					}
					while (visibleEnd > loadedEnd) {
						items.push_back(model->CreateElement(loadedEnd));
						AddChild(items.back().GetPointerOrNull());
						loadedEnd++;
					}
					loadedStartIndex = loadedStart;
				}

				// relayout items
				int count = static_cast<int>(items.size());
				float y = 0.0F;
				float w = GetItemWidth();
				for (int i = 0; i < count; i++) {
					items[i]->SetBounds(AABB2(0.0F, y, w, rowHeight));
					y += rowHeight;
				}

				// move scroll bar
				scrollBar->SetBounds(AABB2(size.x - scrollBarWidth, 0.0F, scrollBarWidth, size.y));
			}

			void ListViewBase::MouseWheel(float delta) { scrollBar->ScrollBy(delta); }

			void ListViewBase::Reload() {
				UnloadAll();
				Layout();
			}

			void ListViewBase::UnloadAll() {
				int count = static_cast<int>(items.size());
				for (int i = 0; i < count; i++) {
					UIElement* elem = items[i].GetPointerOrNull();
					RemoveChild(elem);
					model->RecycleElement(*elem);
				}
				items.clear();
			}

			void ListViewBase::SetModel(ListViewModel* value) {
				UnloadAll();
				model = value;
				Layout();
			}

			void ListViewBase::ScrollToTop() { scrollBar->ScrollTo(0.0); }
			void ListViewBase::ScrollToEnd() { scrollBar->ScrollTo(scrollBar->maxValue); }
			void ListViewBase::ScrollToRow(int row) { scrollBar->ScrollTo(static_cast<double>(row)); }

			void ListView::Render() {
				client::IRenderer& r = GetManager().GetRenderer();
				Vector2 pos = GetScreenPosition();
				Vector2 sz = size;

				SetColorNP(r, MakeVector4(0.0F, 0.0F, 0.0F, 0.2F));
				r.DrawFilledRect(pos.x + 1, pos.y + 1, pos.x + sz.x - 1, pos.y + sz.y - 1);

				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.06F));
				r.DrawOutlinedRect(pos.x, pos.y, pos.x + sz.x, pos.y + sz.y);

				ListViewBase::Render();
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
