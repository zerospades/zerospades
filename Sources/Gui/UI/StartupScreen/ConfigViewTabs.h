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

#include <Gui/UI/StartupScreen/ConfigViewFramework.h>
#include <Gui/UI/Widgets/DropDownList.h>
#include <Gui/UI/Widgets/MessageBox.h>

namespace spades {
	namespace gui {
		class StartupScreenHelper;
		class StartupScreenUI;
		class StartupScreenGraphicsDisplayResolutionEditor;
		class StartupScreenAudioOpenALEditor;
		class StartupScreenLocaleEditor;

		/** The Graphics tab: renderer/resolution selection + renderer settings. */
		class StartupScreenGraphicsTab : public ui::UIElement {
			StartupScreenHelper* helper; // weak

			StartupScreenGraphicsDisplayResolutionEditor* resEdit;

			ui::CheckBox* fullscreenCheck;
			ui::RadioButton* driverOpenGL;
			ui::RadioButton* driverSoftware;

			ui::TextViewer* helpView;
			StartupScreenConfigView* configViewGL;
			StartupScreenConfigView* configViewSoftware;

			Settings::ItemHandle r_renderer;
			Settings::ItemHandle r_fullscreen;

			void HandleHelpText(const std::string& text);
			void OnDriverOpenGL(ui::UIElement&);
			void OnDriverSoftware(ui::UIElement&);
			void OnFullscreenCheck(ui::UIElement&);

		public:
			StartupScreenGraphicsTab(StartupScreenUI* ui, Vector2 size);
			void LoadConfig();
		};

		/** A `SimpleButton` with a centered drop-down arrow. */
		class StartupScreenComboBoxDropdownButton : public ui::SimpleButton {
		public:
			StartupScreenComboBoxDropdownButton(ui::UIManager* manager)
			    : ui::SimpleButton(manager) {}
			void Render() override;
		};

		/** Width x height fields plus a drop-down of the display's video modes. */
		class StartupScreenGraphicsDisplayResolutionEditor : public ui::UIElement {
			ui::Field* widthField;
			ui::Field* heightField;
			ui::Button* dropdownButton;
			Settings::ItemHandle r_videoWidth;
			Settings::ItemHandle r_videoHeight;
			StartupScreenHelper* helper; // weak

			void SaveConfig();
			void ValueEditHandler(ui::UIElement&);
			void DropdownHandler(int index);
			void ShowDropdown(ui::UIElement&);

		public:
			StartupScreenGraphicsDisplayResolutionEditor(StartupScreenUI* ui);
			void LoadConfig();
			void Render() override;
		};

		/** Maps MSAA/FXAA cvars to a single "0|2|4|fxaa" pseudo-config. */
		class StartupScreenGraphicsAntialiasConfig : public StartupScreenGenericConfig {
			StartupScreenUI* ui; // weak
			Settings::ItemHandle msaaConfig;
			Settings::ItemHandle fxaaConfig;

		public:
			StartupScreenGraphicsAntialiasConfig(StartupScreenUI* ui);
			std::string GetValue() override;
			void SetValue(const std::string& v) override;
			std::string CheckValueCapability(const std::string& v) override;
		};

		/** The Audio tab: backend selection + OpenAL settings. */
		class StartupScreenAudioTab : public ui::UIElement {
			StartupScreenHelper* helper; // weak

			ui::RadioButton* driverOpenAL;
			ui::RadioButton* driverNull;

			ui::TextViewer* helpView;
			StartupScreenConfigView* configViewOpenAL;

			StartupScreenAudioOpenALEditor* editOpenAL;

			Settings::ItemHandle s_audioDriver;
			Settings::ItemHandle s_openalDevice;

			void HandleHelpText(const std::string& text);
			void OnDriverOpenAL(ui::UIElement&);
			void OnDriverNull(ui::UIElement&);

		public:
			StartupScreenAudioTab(StartupScreenUI* ui, Vector2 size);
			void LoadConfig();
		};

		/** A left-aligned `SimpleButton` with a right-hand drop-down arrow. */
		class StartupScreenDropdownListDropdownButton : public ui::SimpleButton {
		public:
			StartupScreenDropdownListDropdownButton(ui::UIManager* manager)
			    : ui::SimpleButton(manager) {
				alignment = MakeVector2(0.0F, 0.5F);
			}
			void Render() override;
		};

		/** Drop-down selecting the OpenAL output device. */
		class StartupScreenAudioOpenALEditor : public ui::UIElement {
			StartupScreenUI* ui;         // weak
			StartupScreenHelper* helper; // weak

			ui::Button* dropdownButton;

			void ShowDropdown(ui::UIElement&);
			void DropdownHandler(int index);

		public:
			Settings::ItemHandle openal;

			StartupScreenAudioOpenALEditor(StartupScreenUI* ui);
			void LoadConfig();
		};

		/** The Generic tab: language, tools, demo recording defaults. */
		class StartupScreenGenericTab : public ui::UIElement {
			StartupScreenUI* ui;         // weak
			StartupScreenHelper* helper; // weak
			StartupScreenLocaleEditor* editLocale;
			ui::CheckBox* buttonAutoRecord;
			ui::CheckBox* buttonAutoPrune;
			ui::Field* fieldMaxDemos;

			Settings::ItemHandle cg_demoAutoRecord;
			Settings::ItemHandle cg_demoAutoPrune;
			Settings::ItemHandle cg_demoMaxFiles;

			void OnAutoRecordChanged(ui::UIElement&);
			void OnAutoPruneChanged(ui::UIElement&);
			void OnMaxDemosChanged(ui::UIElement&);
			void OnBrowseDemosFolderPressed(ui::UIElement&);
			void OnBrowseUserDirectoryPressed(ui::UIElement&);
			void OnResetSettingsPressed(ui::UIElement&);
			void OnResetSettingsConfirmed(ui::UIElement& sender);
			void ResetAllSettings();

		public:
			StartupScreenGenericTab(StartupScreenUI* ui, Vector2 size);
			void LoadConfig();
		};

		/** Drop-down selecting the UI language. */
		class StartupScreenLocaleEditor : public ui::UIElement {
			StartupScreenUI* ui;         // weak
			StartupScreenHelper* helper; // weak
			Settings::ItemHandle core_locale;

			ui::Button* dropdownButton;

			void ShowDropdown(ui::UIElement&);
			void DropdownHandler(int index);

		public:
			StartupScreenLocaleEditor(StartupScreenUI* ui);
			void LoadConfig();
		};

		/** The System Info tab: the diagnostics report + copy button. */
		class StartupScreenSystemInfoTab : public ui::UIElement {
			StartupScreenHelper* helper; // weak
			ui::TextViewer* helpView;

			void OnCopyReport(ui::UIElement&);

		public:
			StartupScreenSystemInfoTab(StartupScreenUI* ui, Vector2 size);
		};

		/** The Advanced tab: a filterable editor for every config variable. */
		class StartupScreenAdvancedTab : public ui::UIElement {
			StartupScreenHelper* helper; // weak

			ui::Field* filter;
			ui::TextViewer* helpView;
			StartupScreenConfigView* configView;

			void HandleHelpText(const std::string& text);
			void OnFilterChanged(ui::UIElement&);

		public:
			StartupScreenAdvancedTab(StartupScreenUI* ui, Vector2 size);
			void LoadConfig();
		};
	} // namespace gui
} // namespace spades
