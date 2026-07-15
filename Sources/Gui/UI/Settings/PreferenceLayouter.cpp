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

#include "ConfigDesigners.h"
#include "PreferenceLayouter.h"
#include <Client/Fonts.h>
#include <Core/Strings.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/Label.h>

namespace spades {
	namespace gui {
		using ui::Button;
		using ui::EventHandler;
		using ui::Label;
		using ui::ListView;
		using ui::SimpleButton;
		using ui::UIElement;
		using ui::UIManager;

		StandardPreferenceLayouter::StandardPreferenceLayouter(UIElement* parent,
		                                                       client::FontManager* fontManager)
		    : parent(parent), fontManager(fontManager) {
			float sw = parent->GetManager().screenWidth;
			float sh = parent->GetManager().screenHeight;

			fieldX = sw - 440.0F;
			float maxFieldX = 250.0F;
			if (fieldX > maxFieldX)
				fieldX = maxFieldX;

			fieldWidth = sw - 400.0F;
			float maxFieldWidth = 320.0F;
			if (fieldWidth > maxFieldWidth)
				fieldWidth = maxFieldWidth;

			fieldHeight = sh - 8.0F;
			float maxFieldHeight = 550.0F;
			if (fieldHeight > maxFieldHeight)
				fieldHeight = maxFieldHeight;

			listX = 10.0F;
			listWidth = fieldX + fieldWidth - listX;
		}

		void StandardPreferenceLayouter::OnKeyBound(UIElement& sender) {
			// unbind already bound key
			ConfigHotKeyField* bindField = dynamic_cast<ConfigHotKeyField*>(&sender);
			if (!bindField)
				return;
			std::string key = bindField->GetBoundKey();
			for (ConfigHotKeyField* f : hotkeyItems) {
				if (f != bindField) {
					if (f->GetBoundKey() == key)
						f->SetBoundKey("");
				}
			}
		}

		UIElement* StandardPreferenceLayouter::CreateItem() {
			Handle<UIElement> elem = Handle<UIElement>::New(&parent->GetManager());
			elem->size = MakeVector2(fieldWidth, 32.0F);
			items.push_back(elem);
			return elem.GetPointerOrNull();
		}

		UIElement* StandardPreferenceLayouter::CreateBasicLabel(const std::string& caption,
		                                                        bool enabled) {
			UIElement* container = CreateItem();

			Handle<Label> label = Handle<Label>::New(&parent->GetManager());
			label->text = caption;
			label->alignment = MakeVector2(0.0F, 0.5F);
			label->SetBounds(AABB2(listX, 0.0F, listWidth, 32.0F));
			label->enable = enabled;
			container->AddChild(label.GetPointerOrNull());

			return container;
		}

		void StandardPreferenceLayouter::AddHeading(const std::string& text) {
			// record this heading for the navigation bar
			if (text.size() > 0) {
				HeadingNavEntry entry;
				entry.caption = text;
				entry.row = static_cast<int>(items.size());
				headings.push_back(entry);
			}

			UIElement* container = CreateItem();

			Handle<Label> label = Handle<Label>::New(&parent->GetManager());
			label->SetFont(&fontManager->GetHeadingFont());
			label->text = text;
			label->alignment = MakeVector2(0.5F, 0.5F);
			label->SetBounds(AABB2(listX, 0.0F, listWidth, 32.0F));
			container->AddChild(label.GetPointerOrNull());
		}

		ConfigField* StandardPreferenceLayouter::AddInputField(const std::string& caption,
		                                                       const std::string& configName,
		                                                       bool enabled, bool compact) {
			UIElement* container = CreateBasicLabel(caption, enabled);

			Handle<ConfigField> field = Handle<ConfigField>::New(&parent->GetManager(), configName);
			if (compact)
				field->SetFont(&fontManager->GetSmallFont());
			field->SetBounds(AABB2(fieldX, 2.0F, fieldWidth, compact ? 20.0F : 28.0F));
			field->enable = enabled;
			container->AddChild(field.GetPointerOrNull());

			return field.GetPointerOrNull();
		}

		ConfigSlider* StandardPreferenceLayouter::AddSliderField(const std::string& caption,
		                                                         const std::string& configName,
		                                                         float minRange, float maxRange,
		                                                         float step, Formatter formatter,
		                                                         bool enabled) {
			UIElement* container = CreateBasicLabel(caption, enabled);

			Handle<ConfigSlider> slider = Handle<ConfigSlider>::New(
			    &parent->GetManager(), configName, minRange, maxRange, step, std::move(formatter));
			slider->SetBounds(AABB2(fieldX, 4.0F, fieldWidth, 24.0F));
			slider->enable = enabled;
			container->AddChild(slider.GetPointerOrNull());

			return slider.GetPointerOrNull();
		}

		void StandardPreferenceLayouter::AddSliderGroup(const std::string& caption,
		                                                const std::vector<std::string>& labels,
		                                                float minRange, float maxRange, float step,
		                                                int digits,
		                                                const std::vector<std::string>& prefix,
		                                                bool enabled) {
			UIElement* container = CreateBasicLabel(caption, enabled);

			float width = (fieldWidth - (labels.size() - 1)) / labels.size();
			for (size_t i = 0; i < labels.size(); ++i) {
				Handle<ConfigSlider> slider =
				    Handle<ConfigSlider>::New(&parent->GetManager(), labels[i], minRange, maxRange,
				                              step, NumberFormatter(digits, "", prefix[i]));
				slider->SetBounds(
				    AABB2(fieldX + static_cast<float>(i) * (width + 1.0F), 4.0F, width, 24.0F));
				slider->enable = enabled;
				container->AddChild(slider.GetPointerOrNull());
			}
		}

		void StandardPreferenceLayouter::AddVolumeSlider(const std::string& caption,
		                                                 const std::string& configName) {
			AddSliderField(caption, configName, 0, 1, 0.01F, NumberFormatter(0, "%", "", 100));
		}

		void StandardPreferenceLayouter::AddRGBSlider(const std::string& caption,
		                                              const std::vector<std::string>& labels,
		                                              bool enabled) {
			AddSliderGroup(caption, labels, 0, 255, 1, 0, {"R: ", "G: ", "B: "}, enabled);
		}

		void StandardPreferenceLayouter::AddControl(const std::string& caption,
		                                            const std::string& configName, bool enabled) {
			UIElement* container = CreateBasicLabel(caption, enabled);

			Handle<ConfigHotKeyField> field =
			    Handle<ConfigHotKeyField>::New(&parent->GetManager(), configName);
			field->SetBounds(AABB2(fieldX, 2.0F, fieldWidth, 28.0F));
			field->enable = enabled;
			container->AddChild(field.GetPointerOrNull());

			field->keyBound = [this](UIElement& sender) { OnKeyBound(sender); };
			hotkeyItems.push_back(field.GetPointerOrNull());
		}

		void StandardPreferenceLayouter::AddChoiceField(const std::string& caption,
		                                                const std::string& configName,
		                                                const std::vector<std::string>& labels,
		                                                const std::vector<int>& values,
		                                                bool enabled) {
			UIElement* container = CreateBasicLabel(caption, enabled);

			float width = (fieldWidth - (labels.size() - 1)) / labels.size();
			for (size_t i = 0; i < labels.size(); ++i) {
				Handle<ConfigSimpleToggleButton> field = Handle<ConfigSimpleToggleButton>::New(
				    &parent->GetManager(), labels[i], configName, values[i]);
				field->SetBounds(
				    AABB2(fieldX + static_cast<float>(i) * (width + 1.0F), 2.0F, width, 28.0F));
				field->enable = enabled;
				container->AddChild(field.GetPointerOrNull());
			}
		}

		void StandardPreferenceLayouter::AddChoiceStringField(
		    const std::string& caption, const std::string& configName,
		    const std::vector<std::string>& labels, const std::vector<std::string>& values,
		    bool enabled) {
			UIElement* container = CreateBasicLabel(caption, enabled);

			float width = (fieldWidth - (labels.size() - 1)) / labels.size();
			for (size_t i = 0; i < labels.size(); ++i) {
				Handle<ConfigSimpleToggleStringButton> field =
				    Handle<ConfigSimpleToggleStringButton>::New(&parent->GetManager(), labels[i],
				                                                configName, values[i]);
				field->SetBounds(
				    AABB2(fieldX + static_cast<float>(i) * (width + 1.0F), 2.0F, width, 28.0F));
				field->enable = enabled;
				container->AddChild(field.GetPointerOrNull());
			}
		}

		void StandardPreferenceLayouter::AddViewmodelPresetField(
		    const std::string& caption, const std::vector<std::string>& labels,
		    const std::vector<float>& xs, const std::vector<float>& ys,
		    const std::vector<float>& zs, bool enabled) {
			UIElement* container = CreateBasicLabel(caption, enabled);

			float width = (fieldWidth - (labels.size() - 1)) / labels.size();
			for (size_t i = 0; i < labels.size(); ++i) {
				Handle<ViewmodelPositionPresetButton> button =
				    Handle<ViewmodelPositionPresetButton>::New(&parent->GetManager(), labels[i],
				                                               caption, xs[i], ys[i], zs[i]);
				button->SetBounds(
				    AABB2(fieldX + static_cast<float>(i) * (width + 1.0F), 2.0F, width, 28.0F));
				button->enable = enabled;
				container->AddChild(button.GetPointerOrNull());
			}
		}

		void StandardPreferenceLayouter::AddToggleField(const std::string& caption,
		                                                const std::string& configName, bool enabled) {
			AddChoiceField(caption, configName,
			               {_Tr("Preferences", "ON"), _Tr("Preferences", "OFF")}, {1, 0}, enabled);
		}

		void StandardPreferenceLayouter::AddPlusMinusField(const std::string& caption,
		                                                   const std::string& configName,
		                                                   bool enabled) {
			AddChoiceField(caption, configName,
			               {_Tr("Preferences", "ON"), _Tr("Preferences", "REVERSED"),
			                _Tr("Preferences", "OFF")},
			               {1, -1, 0}, enabled);
		}

		void StandardPreferenceLayouter::AddButtonField(const std::string& caption,
		                                                const std::string& btnCaption,
		                                                EventHandler handler, bool enabled) {
			UIElement* container = CreateBasicLabel(caption);

			Handle<SimpleButton> button = Handle<SimpleButton>::New(&parent->GetManager());
			button->caption = btnCaption;
			button->SetBounds(AABB2(fieldX, 4.0F, fieldWidth, 24.0F));
			button->enable = enabled;
			button->activated = std::move(handler);
			container->AddChild(button.GetPointerOrNull());
		}

		void StandardPreferenceLayouter::CreatePreview(UIElement* field, EventHandler resetHandler,
		                                               EventHandler randomizeHandler) {
			UIElement* container = CreateItem();

			field->SetBounds(AABB2(listX, 0.0F, listWidth, 64.0F));
			container->AddChild(field);

			Handle<SimpleButton> resetButton = Handle<SimpleButton>::New(&parent->GetManager());
			resetButton->caption = _Tr("Preferences", "Reset");
			resetButton->SetBounds(AABB2(10.0F + 2.0F, 2.0F, 50.0F, 20.0F));
			resetButton->activated = std::move(resetHandler);
			container->AddChild(resetButton.GetPointerOrNull());

			Handle<SimpleButton> randomizeButton = Handle<SimpleButton>::New(&parent->GetManager());
			randomizeButton->caption = _Tr("Preferences", "Randomize");
			randomizeButton->SetBounds(AABB2(fieldX + fieldWidth - 80.0F - 2.0F, 2.0F, 80.0F, 20.0F));
			randomizeButton->activated = std::move(randomizeHandler);
			container->AddChild(randomizeButton.GetPointerOrNull());
		}

		void StandardPreferenceLayouter::AddTargetPreview() {
			Handle<ConfigTarget> field = Handle<ConfigTarget>::New(&parent->GetManager());
			ConfigTarget* f = field.GetPointerOrNull();
			CreatePreview(f, [f](UIElement& s) { f->OnResetPressed(s); },
			              [f](UIElement& s) { f->OnRandomizePressed(s); });
		}

		void StandardPreferenceLayouter::AddScopePreview() {
			Handle<ConfigScope> field = Handle<ConfigScope>::New(&parent->GetManager());
			ConfigScope* f = field.GetPointerOrNull();
			CreatePreview(f, [f](UIElement& s) { f->OnResetPressed(s); },
			              [f](UIElement& s) { f->OnRandomizePressed(s); });
		}

		void StandardPreferenceLayouter::FinishLayout() {
			Handle<ListView> list = Handle<ListView>::New(&parent->GetManager());
			Handle<StandardPreferenceLayouterModel> model =
			    Handle<StandardPreferenceLayouterModel>::New(items);
			list->SetModel(model.GetPointerOrNull());
			list->rowHeight = 32.0F;
			list->SetBounds(AABB2(listX, 0.0F, listWidth + 30.0F, fieldHeight - 6.0F));
			parent->AddChild(list.GetPointerOrNull());

			// pass heading data to PreferenceView if it set a receiver
			if (headingNav && headings.size() >= 2) {
				headingNav->entries = headings;
				headingNav->list = list.GetPointerOrNull();
			}
		}
	} // namespace gui
} // namespace spades
