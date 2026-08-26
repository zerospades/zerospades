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

#include <cmath>

#include "ConfigWidgets.h"
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Core/Strings.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>

namespace spades {
	namespace gui {
		using ui::SetColorNP;

		// -- ConfigField --

		ConfigField::ConfigField(ui::UIManager* manager, const std::string& configName)
		    : ui::Field(manager), config(configName, nullptr) {
			SetText(static_cast<std::string>(config));
		}

		void ConfigField::OnChanged() {
			ui::Field::OnChanged();
			config = GetText();
		}

		// -- ConfigSlider --

		ConfigSlider::ConfigSlider(ui::UIManager* manager, const std::string& configName,
		                           float minValue, float maxValue, float stepValue,
		                           Formatter formatter)
		    : ui::Slider(manager),
		      config(configName, nullptr),
		      stepSize(stepValue),
		      formatter(std::move(formatter)) {
			this->minValue = minValue;
			this->maxValue = maxValue;
			this->value = Clamp(static_cast<float>(config), minValue, maxValue);

			// compute large change
			int steps = static_cast<int>((maxValue - minValue) / stepValue);
			steps = (steps + 9) / 10;
			this->largeChange = static_cast<double>(steps) * stepValue;

			Handle<ui::Label> l = Handle<ui::Label>::New(manager);
			label = l.GetPointerOrNull();
			label->alignment = MakeVector2(0.5F, 0.5F);
			AddChild(label);
			UpdateLabel();
		}

		void ConfigSlider::OnResized() {
			ui::Slider::OnResized();
			label->SetBounds(AABB2(0.0F, 0.0F, size.x, size.y));
		}

		void ConfigSlider::UpdateLabel() {
			label->text = formatter.Format(static_cast<float>(config));
		}

		void ConfigSlider::DoRounding() {
			float v = static_cast<float>(value - minValue);
			v = std::floor((v / stepSize) + 0.5F) * stepSize;
			v += static_cast<float>(minValue);
			if (mapper)
				v = mapper(v);
			value = v;
		}

		void ConfigSlider::OnChanged() {
			ui::Slider::OnChanged();
			DoRounding();
			config = static_cast<float>(value);
			UpdateLabel();
		}

		// -- ConfigHotKeyField --

		ConfigHotKeyField::ConfigHotKeyField(ui::UIManager* manager, const std::string& configName)
		    : ui::UIElement(manager), config(configName, nullptr) {
			isMouseInteractive = true;
		}

		std::string ConfigHotKeyField::GetBoundKey() const {
			return static_cast<std::string>(const_cast<Settings::ItemHandle&>(config));
		}

		void ConfigHotKeyField::SetBoundKey(const std::string& value) { config = value; }

		void ConfigHotKeyField::KeyDown(const std::string& key) {
			if (IsFocused()) {
				std::string k = key;
				if (k != "Escape") {
					if (k == " ") {
						k = "Space";
					} else if (k == "BackSpace" || k == "Delete") {
						k = ""; // unbind
					}
					config = k;
					if (keyBound)
						keyBound(*this);
				}
				GetManager().SetActiveElement(nullptr);
				acceptsFocus = false;
			} else {
				ui::UIElement::KeyDown(key);
			}
		}

		void ConfigHotKeyField::MouseDown(ui::MouseButton button, Vector2 clientPosition) {
			(void)clientPosition;
			if (!acceptsFocus) {
				acceptsFocus = true;
				GetManager().SetActiveElement(this);
				return;
			}
			if (IsFocused()) {
				if (button == ui::MouseButton::Left) {
					config = std::string("LeftMouseButton");
				} else if (button == ui::MouseButton::Right) {
					config = std::string("RightMouseButton");
				} else if (button == ui::MouseButton::Middle) {
					config = std::string("MiddleMouseButton");
				} else if (button == ui::MouseButton::Button4) {
					config = std::string("MouseButton4");
				} else if (button == ui::MouseButton::Button5) {
					config = std::string("MouseButton5");
				}
				if (keyBound)
					keyBound(*this);
				GetManager().SetActiveElement(nullptr);
				acceptsFocus = false;
			}
		}

		void ConfigHotKeyField::MouseEnter() { hover = true; }
		void ConfigHotKeyField::MouseLeave() { hover = false; }

		void ConfigHotKeyField::Render() {
			// render background
			client::IRenderer& r = GetManager().GetRenderer();
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			SetColorNP(r, MakeVector4(0.0F, 0.0F, 0.0F, IsFocused() ? 0.3F : 0.1F));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y, sz.x, sz.y));

			if (IsFocused())
				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.2F));
			else if (hover)
				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.1F));
			else
				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.06F));
			r.DrawOutlinedRect(pos.x, pos.y, pos.x + sz.x, pos.y + sz.y);

			client::IFont* font = GetFont();
			if (!font)
				return;
			std::string text = IsFocused()
			                       ? _Tr("Preferences", "Press Key to Bind or [Escape] to Cancel...")
			                       : static_cast<std::string>(config);

			Vector4 color = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F);
			if (IsFocused()) {
				color.w = std::fabs(std::sin(GetManager().time * 2.0F));
			} else {
				acceptsFocus = false;
			}

			if (text.size() == 0) {
				text = _Tr("Preferences", "Unbound");
				color.w *= 0.3F;
			} else if (text == "LeftMouseButton") {
				text = _Tr("Preferences", "Left Mouse Button");
			} else if (text == "RightMouseButton") {
				text = _Tr("Preferences", "Right Mouse Button");
			} else if (text == "MiddleMouseButton") {
				text = _Tr("Preferences", "Middle Mouse Button");
			} else if (text == "MouseButton4") {
				text = _Tr("Preferences", "Mouse Button 4");
			} else if (text == "MouseButton5") {
				text = _Tr("Preferences", "Mouse Button 5");
			} else if (text == "Escape") {
				text = _Tr("Preferences", "Escape");
			} else if (text == "Tab") {
				text = _Tr("Preferences", "Tab");
			} else if (text == "BackSpace") {
				text = _Tr("Preferences", "Backspace");
			} else if (text == "Delete") {
				text = _Tr("Preferences", "Delete");
			} else if (text == "Enter") {
				text = _Tr("Preferences", "Enter");
			} else if (text == "Space") {
				text = _Tr("Preferences", "Space");
			} else if (text == "Control") {
				text = _Tr("Preferences", "Control");
			} else if (text == "Left Ctrl") {
				text = _Tr("Preferences", "Left Control");
			} else if (text == "Right Ctrl") {
				text = _Tr("Preferences", "Right Control");
			} else if (text == "Shift") {
				text = _Tr("Preferences", "Shift");
			} else if (text == "Left Shift") {
				text = _Tr("Preferences", "Left Shift");
			} else if (text == "Right Shift") {
				text = _Tr("Preferences", "Right Shift");
			} else if (text == "Left Alt") {
				text = _Tr("Preferences", "Left Alt");
			} else if (text == "Right Alt") {
				text = _Tr("Preferences", "Right Alt");
			} else if (text == "Up") {
				text = _Tr("Preferences", "Up Arrow");
			} else if (text == "Down") {
				text = _Tr("Preferences", "Down Arrow");
			} else if (text == "Left") {
				text = _Tr("Preferences", "Left Arrow");
			} else if (text == "Right") {
				text = _Tr("Preferences", "Right Arrow");
			} else if (text == "PageUp") {
				text = _Tr("Preferences", "Page Up");
			} else if (text == "PageDown") {
				text = _Tr("Preferences", "Page Down");
			} else if (!IsFocused()) {
				text = ToUpperCase(text);
			}

			Vector2 txtSize = font->Measure(text);
			Vector2 txtPos = pos + (sz - txtSize) * 0.5F;

			font->Draw(text, txtPos, 1.0F, color);
		}

		// -- ConfigToggleButtonBase --

		ConfigToggleButtonBase::ConfigToggleButtonBase(ui::UIManager* manager,
		                                               const std::string& caption)
		    : ui::RadioButton(manager) {
			this->caption = caption;
			toggle = true;
		}

		void ConfigToggleButtonBase::OnActivated() {
			ui::RadioButton::OnActivated();
			SetValue();
		}

		void ConfigToggleButtonBase::Render() {
			toggled = IsSelected();
			ui::RadioButton::Render();
		}

		// -- ConfigSimpleToggleButton --

		ConfigSimpleToggleButton::ConfigSimpleToggleButton(ui::UIManager* manager,
		                                                   const std::string& caption,
		                                                   const std::string& configName, int value)
		    : ConfigToggleButtonBase(manager, caption), config(configName, nullptr), value(value) {
			toggled = static_cast<int>(config) == value;
		}

		bool ConfigSimpleToggleButton::IsSelected() { return static_cast<int>(config) == value; }
		void ConfigSimpleToggleButton::SetValue() { config = value; }

		// -- ConfigSimpleToggleStringButton --

		ConfigSimpleToggleStringButton::ConfigSimpleToggleStringButton(
		    ui::UIManager* manager, const std::string& caption, const std::string& configName,
		    const std::string& value)
		    : ConfigToggleButtonBase(manager, caption), config(configName, nullptr), value(value) {
			toggled = static_cast<std::string>(config) == value;
		}

		bool ConfigSimpleToggleStringButton::IsSelected() {
			return static_cast<std::string>(config) == value;
		}
		void ConfigSimpleToggleStringButton::SetValue() { config = value; }

		// -- ViewmodelPositionPresetButton --

		ViewmodelPositionPresetButton::ViewmodelPositionPresetButton(
		    ui::UIManager* manager, const std::string& caption, const std::string& groupName,
		    float x, float y, float z)
		    : ConfigToggleButtonBase(manager, caption),
		      cfgX("cg_viewWeaponX", nullptr),
		      cfgY("cg_viewWeaponY", nullptr),
		      cfgZ("cg_viewWeaponZ", nullptr),
		      presetX(x),
		      presetY(y),
		      presetZ(z) {
			this->groupName = groupName;
		}

		bool ViewmodelPositionPresetButton::IsSelected() {
			return static_cast<float>(cfgX) == presetX && static_cast<float>(cfgY) == presetY &&
			       static_cast<float>(cfgZ) == presetZ;
		}

		void ViewmodelPositionPresetButton::SetValue() {
			cfgX = presetX;
			cfgY = presetY;
			cfgZ = presetZ;
		}
	} // namespace gui
} // namespace spades
