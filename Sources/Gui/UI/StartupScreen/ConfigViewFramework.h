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

#include <Core/Settings.h>
#include <Gui/UI/Settings/Formatters.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/Field.h>
#include <Gui/UI/Widgets/Label.h>
#include <Gui/UI/Widgets/ListView.h>
#include <Gui/UI/Widgets/Slider.h>
#include <Gui/UI/Widgets/TextViewer.h>

namespace spades {
	namespace gui {
		class StartupScreenUI;

		using HelpTextHandler = std::function<void(const std::string&)>;

		/** Shows help text when the mouse hovers over a watched element. */
		void WatchHelp(ui::UIElement* elm, HelpTextHandler handler, const std::string& text);
		/** Like `WatchHelp`, but chains existing handlers and recurses into children. */
		void WatchHelpDeep(ui::UIElement* elm, HelpTextHandler handler, const std::string& text);

		/** Adds a left-aligned label sized to its text; shared by editors and tabs. */
		void AddConfigLabel(ui::UIElement& parent, float x, float y, float h,
		                    const std::string& text);

		/** A single named value that a config editor reads and writes. */
		class StartupScreenGenericConfig : public RefCountedObject {
		protected:
			virtual ~StartupScreenGenericConfig() {}

		public:
			virtual std::string GetValue() = 0;
			virtual void SetValue(const std::string&) = 0;
			/** Returns an empty string when there's no problem. */
			virtual std::string CheckValueCapability(const std::string&) = 0;
		};

		/** A config item backed by a cvar, capability-checked by the helper. */
		class StartupScreenConfig : public StartupScreenGenericConfig {
			StartupScreenUI* ui; // weak
			Settings::ItemHandle cfg;
			std::string cfgName;

		public:
			StartupScreenConfig(StartupScreenUI* ui, const std::string& cfg);
			std::string GetValue() override;
			void SetValue(const std::string& v) override;
			std::string CheckValueCapability(const std::string& v) override;
		};

		/** Interface implemented by each row of a `StartupScreenConfigView`. */
		class StartupScreenConfigItemEditor {
		public:
			virtual ~StartupScreenConfigItemEditor() {}
			virtual void LoadConfig() = 0;
			virtual StartupScreenGenericConfig& GetConfig() = 0;
			virtual void SetHelpTextHandler(HelpTextHandler) = 0;
			virtual std::string GetLabel() = 0;
		};

		/** Row of radio buttons choosing one of several values. */
		class StartupScreenConfigSelectItemEditor : public ui::UIElement,
		                                            public StartupScreenConfigItemEditor {
		protected:
			StartupScreenUI* ui; // weak
			std::vector<std::string> descs;
			std::vector<std::string> values;
			Handle<StartupScreenGenericConfig> config;
			std::vector<ui::RadioButton*> buttons; // weak; owned as children

		private:
			std::string label;

		public:
			StartupScreenConfigSelectItemEditor(StartupScreenUI* ui,
			                                    Handle<StartupScreenGenericConfig> cfg,
			                                    const std::string& values,
			                                    const std::string& descs);

			std::string GetLabel() override { return label; }
			void LoadConfig() override;
			std::string CheckValueCapability(const std::string& v);
			StartupScreenGenericConfig& GetConfig() override { return *config; }
			void SetHelpTextHandler(HelpTextHandler handler) override;
			void OnResized() override;
		};

		/** A single check box bound to an on/off config value. */
		class StartupScreenConfigCheckItemEditor : public ui::UIElement,
		                                           public StartupScreenConfigItemEditor {
			std::string desc;
			std::string valueOff;
			std::string valueOn;
			Handle<StartupScreenGenericConfig> config;
			ui::CheckBox* button; // weak; owned as a child
			std::string label;

			void StateChanged(ui::UIElement&);

		public:
			StartupScreenConfigCheckItemEditor(StartupScreenUI* ui,
			                                   Handle<StartupScreenGenericConfig> cfg,
			                                   const std::string& valueOff,
			                                   const std::string& valueOn,
			                                   const std::string& label, const std::string& desc);

			std::string GetLabel() override { return label; }
			void LoadConfig() override;
			std::string CheckValueCapability(const std::string& v);
			StartupScreenGenericConfig& GetConfig() override { return *config; }
			void SetHelpTextHandler(HelpTextHandler handler) override;
			void OnResized() override;
		};

		/** A labelled slider bound to a numeric config value. */
		class StartupScreenConfigSliderItemEditor : public ui::UIElement,
		                                            public StartupScreenConfigItemEditor {
			std::string desc;
			double stepSize;
			Handle<StartupScreenGenericConfig> config;
			ui::Slider* slider;    // weak; owned as a child
			ui::Label* valueLabel; // weak; owned as a child
			Formatter formatter;
			std::string label;

			void UpdateLabel();
			void DoRounding();
			void StateChanged(ui::UIElement&);

		public:
			StartupScreenConfigSliderItemEditor(StartupScreenUI* ui,
			                                    Handle<StartupScreenGenericConfig> cfg,
			                                    double minValue, double maxValue, double stepSize,
			                                    const std::string& label, const std::string& desc,
			                                    Formatter formatter);

			std::string GetLabel() override { return label; }
			void LoadConfig() override;
			StartupScreenGenericConfig& GetConfig() override { return *config; }
			void SetHelpTextHandler(HelpTextHandler handler) override;
			void OnResized() override;
		};

		/** A labelled free-form text field bound to a config value. */
		class StartupScreenConfigFieldItemEditor : public ui::UIElement,
		                                           public StartupScreenConfigItemEditor {
			std::string desc;
			Handle<StartupScreenGenericConfig> config;
			ui::Field* field; // weak; owned as a child
			std::string label;

			void StateChanged(ui::UIElement&);

		public:
			StartupScreenConfigFieldItemEditor(StartupScreenUI* ui,
			                                   Handle<StartupScreenGenericConfig> cfg,
			                                   const std::string& label, const std::string& desc);

			std::string GetLabel() override { return label; }
			void LoadConfig() override;
			StartupScreenGenericConfig& GetConfig() override { return *config; }
			void SetHelpTextHandler(HelpTextHandler handler) override;
			void OnResized() override;
		};

		class StartupScreenComplexConfigPreset;

		/** Maps multiple configs to {"0", "1", ...} or "custom". */
		class StartupScreenComplexConfig : public StartupScreenGenericConfig {
		public:
			// Editors are UI elements owned by the enclosing dialog; strong refs here
			// keep them alive between dialog openings.
			std::vector<Handle<ui::UIElement>> editorElements;
			std::vector<StartupScreenConfigItemEditor*> editors; // parallel to editorElements
			std::vector<Handle<StartupScreenComplexConfigPreset>> presets;

			void AddEditor(Handle<ui::UIElement> editorElement);
			void AddPreset(Handle<StartupScreenComplexConfigPreset> preset);

			std::string GetValue() override;
			void SetValue(const std::string& v) override;
			std::string CheckValueCapability(const std::string& v) override;

			// used for StartupScreenConfigSelectItemEditor's ctor param
			std::string GetValuesString();
			std::string GetDescriptionsString();
		};

		class StartupScreenComplexConfigPreset : public RefCountedObject {
		public:
			std::string name;
			std::vector<std::string> values;
			StartupScreenComplexConfigPreset(const std::string& name, const std::string& values);
		};

		/** Select editor whose last option ("Custom") opens a detail dialog. */
		class StartupScreenConfigComplexItemEditor : public StartupScreenConfigSelectItemEditor {
			std::string dlgTitle;

			void RunDialog();

		public:
			StartupScreenConfigComplexItemEditor(StartupScreenUI* ui,
			                                     Handle<StartupScreenComplexConfig> cfg,
			                                     const std::string& label,
			                                     const std::string& desc);
		};

		/** `ListViewModel` of config rows with label-based filtering. */
		class StartupScreenConfigViewModel : public ui::ListViewModel {
		public:
			std::vector<Handle<ui::UIElement>> items;
			std::vector<Handle<ui::UIElement>> filtered;
			bool useFiltered = false;

			void Filter(const std::string& text);
			int GetNumRows() override {
				return static_cast<int>(useFiltered ? filtered.size() : items.size());
			}
			Handle<ui::UIElement> CreateElement(int row) override {
				return useFiltered ? filtered[row] : items[row];
			}
		};

		/** A virtualized list of config rows. */
		class StartupScreenConfigView : public ui::ListViewBase {
			Handle<StartupScreenConfigViewModel> vmodel;

		public:
			StartupScreenConfigView(ui::UIManager* manager);
			void Finalize() { SetModel(vmodel.GetPointerOrNull()); }
			void AddRow(Handle<ui::UIElement> elm) { vmodel->items.push_back(std::move(elm)); }

			void Filter(const std::string& text);

			void SetHelpTextHandler(HelpTextHandler handler);
			void LoadConfig();
		};

		/** Modal dialog editing the individual settings of a complex config. */
		class StartupScreenComplexConfigDialog : public ui::UIElement {
			Handle<StartupScreenComplexConfig> config;
			float contentsTop;
			float contentsHeight;

			ui::TextViewer* helpView;           // weak; owned as a child
			StartupScreenConfigView* configView; // weak; owned as a child

			ui::UIElement* oldRoot = nullptr; // weak

			void CloseActivated(ui::UIElement&);
			void HandleHelpText(const std::string& s);

		public:
			std::string title;
			ui::EventHandler dialogDone;

			StartupScreenComplexConfigDialog(ui::UIManager* manager,
			                                 Handle<StartupScreenComplexConfig> config);

			void RunDialog();
			void Render() override;
		};
	} // namespace gui
} // namespace spades
