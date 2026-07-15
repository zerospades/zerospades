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

#include <Core/Settings.h>
#include <Gui/UI/Settings/Formatters.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/Field.h>
#include <Gui/UI/Widgets/Label.h>
#include <Gui/UI/Widgets/Slider.h>

namespace spades {
	namespace gui {
		/** A text field bound to a string config item. */
		class ConfigField : public ui::Field {
			Settings::ItemHandle config;

		public:
			ConfigField(ui::UIManager* manager, const std::string& configName);
			void OnChanged() override;
		};

		/** A slider bound to a numeric config item, with a formatted value label. */
		class ConfigSlider : public ui::Slider {
			Settings::ItemHandle config;
			float stepSize;
			ui::Label* label; // weak; owned as a child
			Formatter formatter;

			void UpdateLabel();
			void DoRounding();

		public:
			ValueMapper mapper; // optional

			ConfigSlider(ui::UIManager* manager, const std::string& configName, float minValue,
			             float maxValue, float stepValue, Formatter formatter);

			void OnResized() override;
			void OnChanged() override;
		};

		/** A field that captures the next key/mouse press and binds it to a config item. */
		class ConfigHotKeyField : public ui::UIElement {
			Settings::ItemHandle config;
			bool hover = false;

		public:
			ui::EventHandler keyBound;

			ConfigHotKeyField(ui::UIManager* manager, const std::string& configName);

			std::string GetBoundKey() const;
			void SetBoundKey(const std::string& value);

			void KeyDown(const std::string& key) override;
			void MouseDown(ui::MouseButton button, Vector2 clientPosition) override;
			void MouseEnter() override;
			void MouseLeave() override;
			void Render() override;
		};

		/** Base for radio-style toggle buttons that reflect and set a config item. */
		class ConfigToggleButtonBase : public ui::RadioButton {
		public:
			ConfigToggleButtonBase(ui::UIManager* manager, const std::string& caption);

			virtual bool IsSelected() { return false; }
			virtual void SetValue() {}

			void OnActivated() override;
			void Render() override;
		};

		/** Toggle button selecting an integer value for a config item. */
		class ConfigSimpleToggleButton : public ConfigToggleButtonBase {
			Settings::ItemHandle config;
			int value;

		public:
			ConfigSimpleToggleButton(ui::UIManager* manager, const std::string& caption,
			                         const std::string& configName, int value);
			bool IsSelected() override;
			void SetValue() override;
		};

		/** Toggle button selecting a string value for a config item. */
		class ConfigSimpleToggleStringButton : public ConfigToggleButtonBase {
			Settings::ItemHandle config;
			std::string value;

		public:
			ConfigSimpleToggleStringButton(ui::UIManager* manager, const std::string& caption,
			                               const std::string& configName, const std::string& value);
			bool IsSelected() override;
			void SetValue() override;
		};

		/** Toggle button applying a preset view-model X/Y/Z position. */
		class ViewmodelPositionPresetButton : public ConfigToggleButtonBase {
			Settings::ItemHandle cfgX;
			Settings::ItemHandle cfgY;
			Settings::ItemHandle cfgZ;
			float presetX, presetY, presetZ;

		public:
			ViewmodelPositionPresetButton(ui::UIManager* manager, const std::string& caption,
			                              const std::string& groupName, float x, float y, float z);
			bool IsSelected() override;
			void SetValue() override;
		};
	} // namespace gui
} // namespace spades
