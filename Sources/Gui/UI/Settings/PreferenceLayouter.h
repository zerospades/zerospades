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

#include <Gui/UI/Settings/ConfigWidgets.h>
#include <Gui/UI/Widgets/ListView.h>

namespace spades {
	namespace client {
		class FontManager;
	}
	namespace gui {
		/** One heading entry recorded for the sidebar navigation. */
		struct HeadingNavEntry {
			std::string caption;
			int row = 0;
		};

		/** Heading navigation index for one tab; shared between panel and view. */
		class HeadingNavIndex : public RefCountedObject {
		public:
			std::vector<HeadingNavEntry> entries;
			ui::ListView* list = nullptr; // weak

			HeadingNavIndex() {}
		};

		/** `ListViewModel` returning pre-built preference rows. */
		class StandardPreferenceLayouterModel : public ui::ListViewModel {
			std::vector<Handle<ui::UIElement>> items;

		public:
			StandardPreferenceLayouterModel(std::vector<Handle<ui::UIElement>> items)
			    : items(std::move(items)) {}
			int GetNumRows() override { return static_cast<int>(items.size()); }
			Handle<ui::UIElement> CreateElement(int row) override { return items[row]; }
		};

		/**
		 * Builds the rows of a preference tab and lays them out in a virtualized
		 * `ListView`. Held alive by its panel because bound hot-key fields reference
		 * its cross-field de-duplication callback.
		 */
		class StandardPreferenceLayouter : public RefCountedObject {
			ui::UIElement* parent; // weak
			std::vector<Handle<ui::UIElement>> items;
			std::vector<ConfigHotKeyField*> hotkeyItems;
			client::FontManager* fontManager; // weak

			std::vector<HeadingNavEntry> headings;

			void OnKeyBound(ui::UIElement& sender);
			ui::UIElement* CreateItem();
			ui::UIElement* CreateBasicLabel(const std::string& caption, bool enabled = true);
			void CreatePreview(ui::UIElement* field, ui::EventHandler resetHandler,
			                   ui::EventHandler randomizeHandler);

		public:
			Handle<HeadingNavIndex> headingNav;

			float fieldX, fieldWidth, fieldHeight;
			float listX, listWidth;

			StandardPreferenceLayouter(ui::UIElement* parent, client::FontManager* fontManager);

			void AddHeading(const std::string& text);
			ConfigField* AddInputField(const std::string& caption, const std::string& configName,
			                           bool enabled = true, bool compact = false);
			ConfigSlider* AddSliderField(const std::string& caption, const std::string& configName,
			                             float minRange, float maxRange, float step,
			                             Formatter formatter, bool enabled = true);
			void AddSliderGroup(const std::string& caption, const std::vector<std::string>& labels,
			                    float minRange, float maxRange, float step, int digits,
			                    const std::vector<std::string>& prefix, bool enabled = true);
			void AddVolumeSlider(const std::string& caption, const std::string& configName);
			void AddRGBSlider(const std::string& caption, const std::vector<std::string>& labels,
			                  bool enabled = true);
			void AddControl(const std::string& caption, const std::string& configName,
			                bool enabled = true);
			void AddChoiceField(const std::string& caption, const std::string& configName,
			                    const std::vector<std::string>& labels, const std::vector<int>& values,
			                    bool enabled = true);
			void AddChoiceStringField(const std::string& caption, const std::string& configName,
			                          const std::vector<std::string>& labels,
			                          const std::vector<std::string>& values, bool enabled = true);
			void AddViewmodelPresetField(const std::string& caption,
			                             const std::vector<std::string>& labels,
			                             const std::vector<float>& xs, const std::vector<float>& ys,
			                             const std::vector<float>& zs, bool enabled = true);
			void AddToggleField(const std::string& caption, const std::string& configName,
			                    bool enabled = true);
			void AddPlusMinusField(const std::string& caption, const std::string& configName,
			                       bool enabled = true);
			void AddButtonField(const std::string& caption, const std::string& btnCaption,
			                    ui::EventHandler handler, bool enabled = true);
			void AddTargetPreview();
			void AddScopePreview();

			void FinishLayout();
		};
	} // namespace gui
} // namespace spades
