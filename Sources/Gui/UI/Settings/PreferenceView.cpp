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

#include "ConfigDesigners.h"
#include "PreferenceView.h"
#include <Client/IRenderer.h>
#include <Core/Strings.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>
#include <Gui/UI/Widgets/Label.h>
#include <Gui/UI/Widgets/ListView.h>

namespace spades {
	namespace gui {
		using ui::Label;
		using ui::ListView;
		using ui::SetColorNP;
		using ui::UIElement;
		using ui::UIManager;

		// -- HeadingNavButton --

		void HeadingNavButton::SetTarget(ListView* list, int row) {
			targetList = list;
			targetRow = row;
		}

		void HeadingNavButton::OnActivated() {
			if (targetList != nullptr)
				targetList->ScrollToRow(targetRow);
		}

		// -- PreferenceView --

		PreferenceView::PreferenceView(UIElement* owner, const PreferenceViewOptions& options,
		                               client::FontManager* fontManager)
		    : UIElement(&owner->GetManager()), owner(owner), persistedState(options.persistedState) {
			SetBounds(owner->GetBounds());

			UIManager* manager = &GetManager();
			float sw = manager->screenWidth;
			float sh = manager->screenHeight;

			contentsWidth = sw - 16.0F;
			float maxContentsWidth = 800.0F;
			if (contentsWidth > maxContentsWidth)
				contentsWidth = maxContentsWidth;

			contentsHeight = sh - 8.0F;
			float maxContentsHeight = 550.0F;
			if (contentsHeight > maxContentsHeight)
				contentsHeight = maxContentsHeight;

			contentsTop = (sh - contentsHeight) * 0.5F;
			contentsLeft = (sw - contentsWidth) * 0.5F;
			contentsRight = contentsLeft + contentsWidth;

			tabWidth = 150.0F;
			tabRowHeight = 30.0F;
			tabTop = contentsTop + 2.0F;
			tabLeft = contentsLeft + 2.0F;
			tabRight = tabLeft + tabWidth;

			float panelBorderOffset = 14.0F;
			panelTop = contentsTop - panelBorderOffset;
			panelBottom = contentsTop + contentsHeight + panelBorderOffset;

			{
				Handle<Label> label = Handle<Label>::New(manager);
				label->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.9F);
				label->SetBounds(AABB2(0.0F, panelTop + 1.0F, size.x, (panelBottom - panelTop) - 1.0F));
				AddChild(label.GetPointerOrNull());
			}

			Handle<HeadingNavIndex> gameNav = Handle<HeadingNavIndex>::New();
			Handle<HeadingNavIndex> hudNav = Handle<HeadingNavIndex>::New();
			Handle<HeadingNavIndex> rendererNav = Handle<HeadingNavIndex>::New();
			Handle<HeadingNavIndex> controlsNav = Handle<HeadingNavIndex>::New();

			AddTab(Handle<GameOptionsPanel>::New(manager, options, fontManager,
			                                     gameNav.GetPointerOrNull())
			           .GetPointerOrNull(),
			       _Tr("Preferences", "Game Options"), gameNav.GetPointerOrNull());

			Handle<InterfaceOptions> interfacePanel = Handle<InterfaceOptions>::New(
			    manager, options, fontManager, hudNav.GetPointerOrNull());
			interfacePanel->onEditHUDRequested = [this](UIElement& s) { OnEditHUDRequested(s); };
			AddTab(interfacePanel.GetPointerOrNull(), _Tr("Preferences", "Interface"),
			       hudNav.GetPointerOrNull());

			AddTab(Handle<RendererOptionsPanel>::New(manager, options, fontManager,
			                                         rendererNav.GetPointerOrNull())
			           .GetPointerOrNull(),
			       _Tr("Preferences", "Graphics"), rendererNav.GetPointerOrNull());
			AddTab(Handle<ControlOptionsPanel>::New(manager, options, fontManager,
			                                        controlsNav.GetPointerOrNull())
			           .GetPointerOrNull(),
			       _Tr("Preferences", "Controls"), controlsNav.GetPointerOrNull());
			AddTab(Handle<MiscOptionsPanel>::New(manager, options, fontManager).GetPointerOrNull(),
			       _Tr("Preferences", "Misc"));

			float backButtonTop = tabTop + static_cast<float>(tabs.size()) * tabRowHeight + 5.0F;

			{
				Handle<PreferenceTabButton> button = Handle<PreferenceTabButton>::New(manager);
				button->caption = _Tr("Preferences", "Back");
				button->hotKeyText = _Tr("Client", "[Esc]");
				button->SetBounds(AABB2(tabLeft, backButtonTop, tabWidth, tabRowHeight));
				button->activated = [this](UIElement& s) { OnClosePressed(s); };
				AddChild(button.GetPointerOrNull());
			}

			// build navigation containers in the sidebar
			{
				float btnH = 24.0F;
				float btnY = backButtonTop + tabRowHeight + btnH;
				float btnGap = 2.0F;

				for (size_t i = 0; i < tabs.size(); i++) {
					PreferenceTab* tab = tabs[i].GetPointerOrNull();

					HeadingNavIndex* nav = tab->headingNav.GetPointerOrNull();
					if (nav == nullptr)
						continue;

					int numHeadings = static_cast<int>(nav->entries.size());
					if (numHeadings < 2)
						continue;

					float height = static_cast<float>(numHeadings) * (btnH + btnGap) - btnGap;

					Handle<UIElement> container = Handle<UIElement>::New(manager);
					container->SetBounds(AABB2(tabLeft, btnY, tabWidth, height));
					container->visible = false;

					for (int j = 0; j < numHeadings; j++) {
						const HeadingNavEntry& ent = nav->entries[j];
						Handle<HeadingNavButton> btn = Handle<HeadingNavButton>::New(manager);
						btn->caption = ent.caption;
						btn->SetTarget(nav->list, ent.row);
						btn->SetBounds(
						    AABB2(0.0F, static_cast<float>(j) * (btnH + btnGap), tabWidth, btnH));
						container->AddChild(btn.GetPointerOrNull());
					}

					AddChild(container.GetPointerOrNull());
					tab->navPanel = container.GetPointerOrNull();
				}
			}

			RestorePersistedState();
			UpdateTabs();
		}

		ListView* PreferenceView::GetTabList(int idx) {
			if (idx < 0 || idx >= static_cast<int>(tabs.size()))
				return nullptr;
			UIElement* view = tabs[idx]->view;
			if (view == nullptr)
				return nullptr;
			for (const Handle<UIElement>& child : view->GetChildren()) {
				ListView* list = dynamic_cast<ListView*>(child.GetPointerOrNull());
				if (list != nullptr)
					return list;
			}
			return nullptr;
		}

		void PreferenceView::RestorePersistedState() {
			if (!persistedState)
				return;

			if (persistedState->selectedTabIndex >= 0 &&
			    persistedState->selectedTabIndex < static_cast<int>(tabs.size()))
				selectedTabIndex = persistedState->selectedTabIndex;

			size_t limit = tabs.size();
			if (persistedState->tabScrollRows.size() < limit)
				limit = persistedState->tabScrollRows.size();
			for (size_t i = 0; i < limit; i++) {
				ListView* list = GetTabList(static_cast<int>(i));
				if (list != nullptr)
					list->ScrollToRow(persistedState->tabScrollRows[i]);
			}
		}

		void PreferenceView::SavePersistedState() {
			if (!persistedState)
				return;

			persistedState->selectedTabIndex = selectedTabIndex;
			persistedState->tabScrollRows.resize(tabs.size());
			for (size_t i = 0; i < tabs.size(); i++) {
				ListView* list = GetTabList(static_cast<int>(i));
				persistedState->tabScrollRows[i] = (list != nullptr) ? list->GetTopRowIndex() : 0;
			}
		}

		void PreferenceView::AddTab(UIElement* view, const std::string& caption,
		                            HeadingNavIndex* nav) {
			UIManager* manager = &GetManager();
			Handle<PreferenceTabButton> button = Handle<PreferenceTabButton>::New(manager);
			button->toggle = true;
			button->caption = caption;
			button->SetBounds(AABB2(tabLeft, tabTop + static_cast<float>(tabs.size()) * tabRowHeight,
			                        tabWidth, tabRowHeight));
			button->activated = [this](UIElement& s) { OnTabButtonActivated(s); };
			view->SetBounds(AABB2(tabRight, tabTop, contentsRight - tabRight, contentsHeight - 6.0F));
			view->visible = false;

			Handle<PreferenceTab> tab =
			    Handle<PreferenceTab>::New(view, button.GetPointerOrNull());
			tab->headingNav = nav;
			AddChild(view);
			AddChild(button.GetPointerOrNull());
			tabs.push_back(tab);
		}

		void PreferenceView::OnTabButtonActivated(UIElement& sender) {
			for (size_t i = 0; i < tabs.size(); i++) {
				if (static_cast<UIElement*>(tabs[i]->tabButton) == &sender) {
					selectedTabIndex = static_cast<int>(i);
					UpdateTabs();
				}
			}
		}

		void PreferenceView::UpdateTabs() {
			for (size_t i = 0; i < tabs.size(); i++) {
				bool selected = selectedTabIndex == static_cast<int>(i);
				tabs[i]->tabButton->toggled = selected;
				tabs[i]->view->visible = selected;
				if (tabs[i]->navPanel != nullptr)
					tabs[i]->navPanel->visible = selected;
			}
		}

		void PreferenceView::OnClosePressed(UIElement&) { Close(); }

		void PreferenceView::OnEditHUDRequested(UIElement&) {
			visible = false;
			Handle<ConfigHUDEdit> overlay = Handle<ConfigHUDEdit>::New(&GetManager());
			overlay->OnDone = [this](UIElement& s) { OnHUDEditDone(s); };
			GetParent()->AddChild(overlay.GetPointerOrNull());
		}

		void PreferenceView::OnHUDEditDone(UIElement& sender) {
			sender.SetParent(nullptr);
			visible = true;
		}

		void PreferenceView::OnClosed() {
			if (closed)
				closed(*this);
		}

		void PreferenceView::HotKey(const std::string& key) {
			if (key == "Escape") {
				Close();
			} else {
				UIElement::HotKey(key);
			}
		}

		void PreferenceView::Render() {
			client::IRenderer& r = GetManager().GetRenderer();
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.07F));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y + panelTop, sz.x, 1.0F));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y + panelBottom, sz.x, 1.0F));

			UIElement::Render();
		}

		void PreferenceView::Close() {
			// Both Esc and the Back button route through Close(), so this is the
			// single save point for the persisted position.
			SavePersistedState();
			owner->enable = true;
			SetParent(nullptr);
			OnClosed();
		}

		void PreferenceView::Run() {
			owner->enable = false;
			owner->GetParent()->AddChild(this);
		}
	} // namespace gui
} // namespace spades
