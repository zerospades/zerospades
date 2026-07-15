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
#include <vector>

#include <Gui/UI/Settings/PreferenceLayouter.h>
#include <Gui/UI/Widgets/Button.h>

namespace spades {
	namespace client {
		class FontManager;
	}
	namespace gui {
		/** Remembers the selected tab and per-tab scroll position across openings. */
		class PreferenceViewPersistedState : public RefCountedObject {
		public:
			int selectedTabIndex = 0;
			std::vector<int> tabScrollRows;
		};

		/** Options controlling a `PreferenceView` (in-game flag + persisted state). */
		struct PreferenceViewOptions {
			bool gameActive = false;
			Handle<PreferenceViewPersistedState> persistedState; // null = don't remember
		};

		/** A left-aligned tab button in the preferences sidebar. */
		class PreferenceTabButton : public ui::Button {
		public:
			PreferenceTabButton(ui::UIManager* manager) : ui::Button(manager) {
				alignment = MakeVector2(0.0F, 0.5F);
			}
		};

		/** A sidebar sub-heading button that scrolls its tab's list to a row. */
		class HeadingNavButton : public ui::SimpleButton {
			ui::ListView* targetList = nullptr; // weak
			int targetRow = 0;

		public:
			HeadingNavButton(ui::UIManager* manager) : ui::SimpleButton(manager) {}
			void SetTarget(ui::ListView* list, int row);
			void OnActivated() override;
		};

		/** One tab: its panel view, tab button, heading nav and sidebar nav panel. */
		class PreferenceTab : public RefCountedObject {
		public:
			ui::UIElement* view;             // weak; owned by PreferenceView
			PreferenceTabButton* tabButton;  // weak; owned by PreferenceView
			Handle<HeadingNavIndex> headingNav;
			ui::UIElement* navPanel = nullptr; // weak; owned by PreferenceView

			PreferenceTab(ui::UIElement* view, PreferenceTabButton* tabButton)
			    : view(view), tabButton(tabButton) {}
		};

		/** The tabbed preferences window. */
		class PreferenceView : public ui::UIElement {
			ui::UIElement* owner; // weak
			Handle<PreferenceViewPersistedState> persistedState;

			std::vector<Handle<PreferenceTab>> tabs;

			float contentsLeft, contentsWidth;
			float contentsTop, contentsHeight;
			float contentsRight;
			float tabLeft, tabRight, tabWidth;
			float tabTop, tabRowHeight;
			float panelTop, panelBottom;

			int selectedTabIndex = 0;

			ui::ListView* GetTabList(int idx);
			void RestorePersistedState();
			void SavePersistedState();
			void AddTab(ui::UIElement* view, const std::string& caption,
			            HeadingNavIndex* nav = nullptr);
			void OnTabButtonActivated(ui::UIElement& sender);
			void UpdateTabs();
			void OnClosePressed(ui::UIElement& sender);
			void OnEditHUDRequested(ui::UIElement& sender);
			void OnHUDEditDone(ui::UIElement& sender);
			void OnClosed();

		public:
			ui::EventHandler closed;

			PreferenceView(ui::UIElement* owner, const PreferenceViewOptions& options,
			               client::FontManager* fontManager);

			void HotKey(const std::string& key) override;
			void Render() override;
			void Close();
			void Run();
		};

		// -- Option panels --

		class GameOptionsPanel : public ui::UIElement {
			Handle<StandardPreferenceLayouter> layouter;

		public:
			GameOptionsPanel(ui::UIManager* manager, const PreferenceViewOptions& options,
			                 client::FontManager* fontManager, HeadingNavIndex* nav = nullptr);
		};

		class InterfaceOptions : public ui::UIElement {
			Handle<StandardPreferenceLayouter> layouter;
			void OnEditHUDClicked(ui::UIElement& sender);

		public:
			ui::EventHandler onEditHUDRequested;

			InterfaceOptions(ui::UIManager* manager, const PreferenceViewOptions& options,
			                 client::FontManager* fontManager, HeadingNavIndex* nav = nullptr);
		};

		class RendererOptionsPanel : public ui::UIElement {
			Handle<StandardPreferenceLayouter> layouter;

		public:
			RendererOptionsPanel(ui::UIManager* manager, const PreferenceViewOptions& options,
			                     client::FontManager* fontManager, HeadingNavIndex* nav = nullptr);
		};

		class ControlOptionsPanel : public ui::UIElement {
			Handle<StandardPreferenceLayouter> layouter;

		public:
			ControlOptionsPanel(ui::UIManager* manager, const PreferenceViewOptions& options,
			                    client::FontManager* fontManager, HeadingNavIndex* nav = nullptr);
		};

		class MiscOptionsPanel : public ui::UIElement {
			Handle<StandardPreferenceLayouter> layouter;

		public:
			MiscOptionsPanel(ui::UIManager* manager, const PreferenceViewOptions& options,
			                 client::FontManager* fontManager, HeadingNavIndex* nav = nullptr);
		};
	} // namespace gui
} // namespace spades
